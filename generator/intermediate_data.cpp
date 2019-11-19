#include "generator/intermediate_data.hpp"

#include "platform/platform.hpp"

#include <atomic>
#include <new>
#include <set>
#include <string>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

#include <sys/mman.h>

#include "base/assert.hpp"
#include "base/checked_cast.hpp"
#include "base/logging.hpp"

#include "defines.hpp"

using namespace std;

namespace generator
{
namespace cache
{
namespace
{
size_t const kFlushCount = 10'000'000;
double const kValueOrder = 1e7;
string const kShortExtension = ".short";

// An estimation.
// OSM had around 4.1 billion nodes on 2017-11-08,
// see https://wiki.openstreetmap.org/wiki/Stats
size_t const kMaxNodesInOSM = size_t{1} << 33;

void ToLatLon(double lat, double lon, LatLon & ll)
{
  int64_t const lat64 = lat * kValueOrder;
  int64_t const lon64 = lon * kValueOrder;

  CHECK(lat64 >= numeric_limits<int32_t>::min() && lat64 <= numeric_limits<int32_t>::max(),
        ("Latitude is out of 32bit boundary:", lat64));
  CHECK(lon64 >= numeric_limits<int32_t>::min() && lon64 <= numeric_limits<int32_t>::max(),
        ("Longitude is out of 32bit boundary:", lon64));
  ll.m_lat = static_cast<int32_t>(lat64);
  ll.m_lon = static_cast<int32_t>(lon64);
}

bool FromLatLon(LatLon const & ll, double & lat, double & lon)
{
  // Assume that a valid coordinate is not (0, 0).
  if (ll.m_lat != 0.0 || ll.m_lon != 0.0)
  {
    lat = static_cast<double>(ll.m_lat) / kValueOrder;
    lon = static_cast<double>(ll.m_lon) / kValueOrder;
    return true;
  }
  lat = 0.0;
  lon = 0.0;
  return false;
}

template <class Index, class Container>
void AddToIndex(Index & index, Key relationId, Container const & values)
{
  for (auto const & v : values)
    index.Add(v.first, relationId);
}

// RawFilePointStorageMmapReader -------------------------------------------------------------------
class RawFilePointStorageMmapReader : public PointStorageReaderInterface
{
public:
  explicit RawFilePointStorageMmapReader(string const & name) :
    m_mmapReader(name)
  {}

  // PointStorageReaderInterface overrides:
  bool GetPoint(uint64_t id, double & lat, double & lon) const override
  {
    LatLon ll;
    m_mmapReader.Read(id * sizeof(ll), &ll, sizeof(ll));

    bool ret = FromLatLon(ll, lat, lon);
    if (!ret)
      LOG(LERROR, ("Node with id =", id, "not found!"));
    return ret;
  }

private:
  MmapReader m_mmapReader;
};

// RawFilePointStorageWriter -----------------------------------------------------------------------
class RawFilePointStorageWriter : public PointStorageWriterInterface
{
public:
  explicit RawFilePointStorageWriter(string const & name) :
    m_fileWriter(name)
  {}

  // PointStorageWriterInterface overrides:
  void AddPoint(uint64_t id, double lat, double lon) override
  {
    LatLon ll;
    ToLatLon(lat, lon, ll);

    m_fileWriter.Seek(id * sizeof(ll));
    m_fileWriter.Write(&ll, sizeof(ll));

    ++m_numProcessedPoints;
  }
  void AddPoints(Nodes const & nodes, bool /* concurrent */) override
  {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    for (auto const & node : nodes)
      AddPoint(node.first, node.second.m_lat, node.second.m_lon);
  }

  uint64_t GetNumProcessedPoints() const override { return m_numProcessedPoints; }

private:
  FileWriter m_fileWriter;
  std::mutex m_updateMutex;
  uint64_t m_numProcessedPoints = 0;
};

// RawMemPointStorageReader ------------------------------------------------------------------------
class RawMemPointStorageReader : public PointStorageReaderInterface
{
public:
  explicit RawMemPointStorageReader(string const & name)
  {
    m_fileMap.open(name);
    if (!m_fileMap.is_open())
      MYTHROW(Writer::OpenException, ("Failed to open", name));

    // Try aggressively (MADV_WILLNEED) and asynchronously read ahead the node file.
    auto readaheadTask = std::thread([data = m_fileMap.data(), size = m_fileMap.size()] {
      ::madvise(const_cast<char*>(data), size, MADV_WILLNEED);
    });
    readaheadTask.detach();
  }

  // PointStorageReaderInterface overrides:
  bool GetPoint(uint64_t id, double & lat, double & lon) const override
  {
    auto const * data = reinterpret_cast<LatLon const *>(m_fileMap.data());
    LatLon const & ll = data[id];
    bool ret = FromLatLon(ll, lat, lon);
    if (!ret)
      LOG(LERROR, ("Node with id =", id, "not found!"));
    return ret;
  }

private:
  boost::iostreams::mapped_file_source m_fileMap;
};

// RawMemPointStorageWriter ------------------------------------------------------------------------
class RawMemPointStorageWriter : public PointStorageWriterInterface
{
public:
  explicit RawMemPointStorageWriter(string const & name)
  {
    auto fileParams = boost::iostreams::mapped_file_params{name};
    fileParams.flags = boost::iostreams::mapped_file_sink::readwrite;
    fileParams.new_file_size = kMaxNodesInOSM * sizeof(LatLon);
    m_fileMap.open(fileParams);
    if (!m_fileMap.is_open())
      MYTHROW(Writer::OpenException, ("Failed to open", name));

    // File (mapping pages) are updated sequentially by ascending node's ids.
    // Advice to flush dirty pages after update sequentially for consecutive writing to disk.
    // See https://stackoverflow.com/questions/5902629/mmap-msync-and-linux-process-termination.
    ::madvise(const_cast<char*>(m_fileMap.data()), m_fileMap.size(), MADV_SEQUENTIAL);
  }

  ~RawMemPointStorageWriter()
  {
    // Mark dirty pages to be flushed asynchronously, no wait system buffers flushing to disk.
    // Subsequent read() will retrive updated data from flushed/unflushed cache or from a disk.
    ::msync(m_fileMap.data(), m_fileMap.size(), MS_ASYNC);
  }

  // PointStorageWriterInterface overrides:
  void AddPoint(uint64_t id, double lat, double lon) override
  {
    CHECK_LESS(id, m_fileMap.size() / sizeof(LatLon),
               ("Found node with id", id, "which is bigger than the allocated cache size"));

    auto & ll = reinterpret_cast<LatLon*>(m_fileMap.data())[id];
    ToLatLon(lat, lon, ll);

    m_numProcessedPoints.fetch_add(1, std::memory_order_relaxed);
  }
  void AddPoints(Nodes const & nodes, bool /* concurrent */) override
  {
    if (nodes.empty())
      return;
    // Check only last point (bigest id in nodes).
    CHECK_LESS(nodes.back().first, m_fileMap.size() / sizeof(LatLon),
               ("Found node with id", nodes.back().first,
                "which is bigger than the allocated cache size"));

    auto const data = reinterpret_cast<LatLon*>(m_fileMap.data());
    for (auto const & node : nodes)
    {
      LatLon & ll = data[node.first];
      ToLatLon(node.second.m_lat, node.second.m_lon, ll);
    }

    m_numProcessedPoints.fetch_add(nodes.size(), std::memory_order_relaxed);
  }
  uint64_t GetNumProcessedPoints() const override { return m_numProcessedPoints; }

private:
  boost::iostreams::mapped_file_sink m_fileMap;
  std::atomic<uint64_t> m_numProcessedPoints{0};
};

// MapFilePointStorageReader -----------------------------------------------------------------------
class MapFilePointStorageReader : public PointStorageReaderInterface
{
public:
  explicit MapFilePointStorageReader(string const & name)
  {
    LOG(LINFO, ("Nodes reading is started"));

    auto filename = name + kShortExtension;
    auto fileStream = std::ifstream{filename, std::ios::binary};
    if (!fileStream.is_open())
      MYTHROW(Writer::OpenException, ("Failed to open", filename));

    LatLonPos llp;
    while (fileStream.good() && fileStream.read(reinterpret_cast<char*>(&llp), sizeof(llp)))
    {
      LatLon ll;
      ll.m_lat = llp.m_lat;
      ll.m_lon = llp.m_lon;
      m_map.emplace(llp.m_pos, ll);
    }

    LOG(LINFO, ("Nodes reading is finished"));
  }

  // PointStorageReaderInterface overrides:
  bool GetPoint(uint64_t id, double & lat, double & lon) const override
  {
    auto const i = m_map.find(id);
    if (i == m_map.cend())
      return false;
    bool ret = FromLatLon(i->second, lat, lon);
    if (!ret)
    {
      LOG(LERROR, ("Inconsistent MapFilePointStorageReader. Node with id =", id,
                   "must exist but was not found"));
    }
    return ret;
  }

private:
  unordered_map<uint64_t, LatLon> m_map;
};

// MapFilePointStorageWriter -----------------------------------------------------------------------
class MapFilePointStorageWriter : public PointStorageWriterInterface
{
public:
  explicit MapFilePointStorageWriter(string const & name) :
    m_fileWriter(name + kShortExtension)
  {
  }

  // PointStorageWriterInterface overrides:
  void AddPoint(uint64_t id, double lat, double lon) override
  {
    LatLon ll;
    ToLatLon(lat, lon, ll);

    LatLonPos llp;
    llp.m_pos = id;
    llp.m_lat = ll.m_lat;
    llp.m_lon = ll.m_lon;

    m_fileWriter.Write(&llp, sizeof(llp));

    ++m_numProcessedPoints;
  }
  void AddPoints(Nodes const & nodes, bool /* concurrent */) override
  {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    for (auto const & node : nodes)
      AddPoint(node.first, node.second.m_lat, node.second.m_lon);
  }
  uint64_t GetNumProcessedPoints() const override { return m_numProcessedPoints; }

private:
  FileWriter m_fileWriter;
  std::mutex m_updateMutex;
  uint64_t m_numProcessedPoints = 0;
};
}  // namespace

// IndexFileReader ---------------------------------------------------------------------------------
IndexFileReader::IndexFileReader(string const & name)
{
  size_t const fileSize = boost::filesystem::file_size(name);
  if (fileSize == 0)
    return;

  auto fileStream = std::ifstream{};
  fileStream.exceptions(std::ifstream::failbit);
  fileStream.open(name, std::ios::binary);

  LOG_SHORT(LINFO, ("Offsets reading is started for file", name));
  CHECK_EQUAL(0, fileSize % sizeof(Element), ("Damaged file."));

  try
  {
    m_elements.resize(base::checked_cast<size_t>(fileSize / sizeof(Element)));
  }
  catch (bad_alloc const &)
  {
    LOG(LCRITICAL, ("Insufficient memory for required offset map"));
  }

  fileStream.read(reinterpret_cast<char*>(&m_elements[0]), base::checked_cast<size_t>(fileSize));

  sort(m_elements.begin(), m_elements.end(), ElementComparator());

  LOG_SHORT(LINFO, ("Offsets reading is finished"));
}

bool IndexFileReader::GetValueByKey(Key key, Value & value) const
{
  auto it = lower_bound(m_elements.begin(), m_elements.end(), key, ElementComparator());
  if (it != m_elements.end() && it->first == key)
  {
    value = it->second;
    return true;
  }
  return false;
}

// IndexFileWriter ---------------------------------------------------------------------------------
IndexFileWriter::IndexFileWriter(string const & name) :
  m_fileWriter(name.c_str())
{
  m_elements.reserve(kFlushCount);
}

void IndexFileWriter::WriteAll()
{
  if (m_elements.empty())
    return;

  m_fileWriter.Write(&m_elements[0], m_elements.size() * sizeof(Element));
  m_elements.clear();
}

void IndexFileWriter::Add(Key k, Value const & v)
{
  if (m_elements.size() > kFlushCount)
    WriteAll();

  m_elements.emplace_back(k, v);
}

// OSMElementCacheReader ---------------------------------------------------------------------------
OSMElementCacheReader::OSMElementCacheReader(string const & name)
  : m_offsetsReader(name + OFFSET_EXT)
  , m_name(name)
{
  if (!Platform::IsFileExistsByFullPath(name) || !boost::filesystem::file_size(name))
    return;

  m_fileMap.open(name);
  if (!m_fileMap.is_open())
    MYTHROW(Writer::OpenException, ("Failed to open", name));

  // Try aggressively (MADV_WILLNEED) and asynchronously read ahead.
  auto readaheadTask = std::thread([data = m_fileMap.data(), size = m_fileMap.size()] {
    ::madvise(const_cast<char*>(data), size, MADV_WILLNEED);
  });
  readaheadTask.detach();
}

// OSMElementCacheWriter ---------------------------------------------------------------------------
OSMElementCacheWriter::OSMElementCacheWriter(string const & name)
  : m_fileWriter(name, FileWriter::OP_WRITE_TRUNCATE, 10 * 1024 * 1024 /* bufferSize */)
  , m_currOffset{m_fileWriter.Pos()}
  , m_offsets(name + OFFSET_EXT)
  , m_name(name)
{
}

void OSMElementCacheWriter::SaveOffsets() { m_offsets.WriteAll(); }

// IntermediateDataReader
IntermediateDataReader::IntermediateDataReader(feature::GenerateInfo const & info)
  : m_nodes{CreatePointStorageReader(info.m_nodeStorageType,
                                     info.GetIntermediateFileName(NODES_FILE))}
  , m_ways(info.GetIntermediateFileName(WAYS_FILE))
  , m_relations(info.GetIntermediateFileName(RELATIONS_FILE))
  , m_nodeToRelations(info.GetIntermediateFileName(NODES_FILE, ID2REL_EXT))
  , m_wayToRelations(info.GetIntermediateFileName(WAYS_FILE, ID2REL_EXT))
{}

// IntermediateDataWriter
IntermediateDataWriter::IntermediateDataWriter(PointStorageWriterInterface & nodes,
                                               feature::GenerateInfo const & info)
  : m_nodes(nodes)
  , m_ways(info.GetIntermediateFileName(WAYS_FILE))
  , m_relations(info.GetIntermediateFileName(RELATIONS_FILE))
  , m_nodeToRelations(info.GetIntermediateFileName(NODES_FILE, ID2REL_EXT))
  , m_wayToRelations(info.GetIntermediateFileName(WAYS_FILE, ID2REL_EXT))
{}

void IntermediateDataWriter::AddNode(Key id, double lat, double lon)
{
  m_nodes.AddPoint(id, lat, lon);
}

void IntermediateDataWriter::AddNodes(Nodes const & nodes, bool concurrent)
{
  m_nodes.AddPoints(nodes, concurrent);
}

void IntermediateDataWriter::AddWay(Key id, WayElement const & e)
{
  m_ways.Write(id, e);
}

void IntermediateDataWriter::AddWays(Ways const & ways, bool concurrent)
{
  m_ways.Write(ways, concurrent);
}

void IntermediateDataWriter::AddRelations(Relations const & relations, bool concurrent)
{
  m_relations.Write(relations, concurrent);

  {
    std::lock_guard<std::mutex> lock{m_nodeToRelationsUpdateMutex};
    for (auto const & relation : relations)
      AddToIndex(m_nodeToRelations, relation.first, relation.second.nodes);
  }

  {
    std::lock_guard<std::mutex> lock{m_wayToRelationsUpdateMutex};
    for (auto const & relation : relations)
      AddToIndex(m_wayToRelations, relation.first, relation.second.ways);
  }
}

void IntermediateDataWriter::AddRelation(Key id, RelationElement const & e)
{
  static set<string> const types = {"multipolygon", "route", "boundary",
                                    "associatedStreet", "building", "restriction"};
  string const & relationType = e.GetType();
  if (!types.count(relationType))
    return;

  m_relations.Write(id, e);
  AddToIndex(m_nodeToRelations, id, e.nodes);
  AddToIndex(m_wayToRelations, id, e.ways);
}

void IntermediateDataWriter::SaveIndex()
{
  m_ways.SaveOffsets();
  m_relations.SaveOffsets();

  m_nodeToRelations.WriteAll();
  m_wayToRelations.WriteAll();
}

// Functions
unique_ptr<PointStorageReaderInterface>
CreatePointStorageReader(feature::GenerateInfo::NodeStorageType type, string const & name)
{
  switch (type)
  {
  case feature::GenerateInfo::NodeStorageType::File:
    return make_unique<RawFilePointStorageMmapReader>(name);
  case feature::GenerateInfo::NodeStorageType::Index:
    return make_unique<MapFilePointStorageReader>(name);
  case feature::GenerateInfo::NodeStorageType::Memory:
    return make_unique<RawMemPointStorageReader>(name);
  }
  UNREACHABLE();
}

unique_ptr<PointStorageWriterInterface>
CreatePointStorageWriter(feature::GenerateInfo::NodeStorageType type, string const & name)
{
  switch (type)
  {
  case feature::GenerateInfo::NodeStorageType::File:
    return make_unique<RawFilePointStorageWriter>(name);
  case feature::GenerateInfo::NodeStorageType::Index:
    return make_unique<MapFilePointStorageWriter>(name);
  case feature::GenerateInfo::NodeStorageType::Memory:
    return make_unique<RawMemPointStorageWriter>(name);
  }
  UNREACHABLE();
}

IntermediateData::IntermediateData(feature::GenerateInfo const & info)
  : m_info(info)
  , m_reader{make_shared<IntermediateDataReader>(info)}
{ }

shared_ptr<IntermediateDataReader> const & IntermediateData::GetCache() const
{
  return m_reader;
}
}  // namespace cache
}  // namespace generator

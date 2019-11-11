#include "generator/osm_source.hpp"

#include "generator/intermediate_data.hpp"
#include "generator/intermediate_elements.hpp"
#include "generator/osm_element.hpp"
#include "generator/towns_dumper.hpp"
#include "generator/translator_factory.hpp"

#include "platform/platform.hpp"

#include "geometry/mercator.hpp"

#include "base/assert.hpp"
#include "base/stl_helpers.hpp"
#include "base/file_name_utils.hpp"

#include <fstream>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/iostreams/stream.hpp>

#include <sys/mman.h>

#include "defines.hpp"

using namespace std;

namespace generator
{
// SourceReader ------------------------------------------------------------------------------------
SourceReader::SourceReader() : m_file(unique_ptr<istream, Deleter>(&cin, Deleter(false)))
{
  LOG_SHORT(LINFO, ("Reading OSM data from stdin"));
}

SourceReader::SourceReader(string const & filename)
  : m_file(unique_ptr<istream, Deleter>(new ifstream(filename), Deleter()))
{
  CHECK(static_cast<ifstream *>(m_file.get())->is_open(), ("Can't open file:", filename));
  LOG_SHORT(LINFO, ("Reading OSM data from", filename));
}

SourceReader::SourceReader(std::istream & stream)
  : m_file(unique_ptr<istream, Deleter>(&stream, Deleter(false)))
{ }

uint64_t SourceReader::Read(char * buffer, uint64_t bufferSize)
{
  m_file->read(buffer, bufferSize);
  return m_file->gcount();
}

// Functions ---------------------------------------------------------------------------------------

void BuildIntermediateNode(OsmElement && element, NodeElement & node)
{
  auto position = MercatorBounds::FromLatLon(element.m_lat, element.m_lon);
  node = {element.m_id, position.y, position.x};
}

bool BuildIntermediateWay(OsmElement && element, WayElement & way)
{
  way.m_wayOsmId = element.m_id;
  way.nodes = std::move(element.Nodes());
  return way.IsValid();
}

bool BuildIntermediateRelation(OsmElement && element, RelationElement & relation)
{
  for (auto & member : element.Members())
  {
    switch (member.m_type) {
    case OsmElement::EntityType::Node:
      relation.nodes.emplace_back(member.m_ref, std::move(member.m_role));
      break;
    case OsmElement::EntityType::Way:
      relation.ways.emplace_back(member.m_ref, std::move(member.m_role));
      break;
    case OsmElement::EntityType::Relation:
      // we just ignore type == "relation"
      break;
    default:
      break;
    }
  }

  for (auto & tag : element.Tags())
    relation.tags.emplace(std::move(tag.m_key), std::move(tag.m_value));

  return relation.IsValid();
}

void AddElementToCache(cache::IntermediateDataWriter & cache, OsmElement && element)
{
  switch (element.m_type)
  {
  case OsmElement::EntityType::Node:
  {
    NodeElement node;
    BuildIntermediateNode(std::move(element), node);
    cache.AddNode(node.m_nodeOsmId, node.m_lat, node.m_lon);
    break;
  }
  case OsmElement::EntityType::Way:
  {
    // Store way.
    auto const id = element.m_id;
    WayElement way(id);
    if (BuildIntermediateWay(std::move(element), way))
      cache.AddWay(id, way);
    break;
  }
  case OsmElement::EntityType::Relation:
  {
    // store relation
    auto const id = element.m_id;
    RelationElement relation;
    if (BuildIntermediateRelation(std::move(element), relation))
      cache.AddRelation(id, relation);
    break;
  }
  default:
    break;
  }
}

void BuildIntermediateDataFromXML(SourceReader & stream, cache::IntermediateDataWriter & cache,
                                  TownsDumper & towns)
{
  ProcessorOsmElementsFromXml processorOsmElementsFromXml(stream);
  OsmElement element;
  while (processorOsmElementsFromXml.TryRead(element))
  {
    towns.CheckElement(element, false /* concurrent */);
    AddElementToCache(cache, std::move(element));
  }
}

void BuildIntermediateDataFromXML(
    std::string const & filename, cache::IntermediateDataWriter & cache, TownsDumper & towns)
{
  SourceReader reader = filename.empty() ? SourceReader() : SourceReader(filename);
  BuildIntermediateDataFromXML(reader, cache, towns);
}

void ProcessOsmElementsFromXML(SourceReader & stream, function<void(OsmElement &&)> processor)
{
  ProcessorOsmElementsFromXml processorOsmElementsFromXml(stream);
  OsmElement element;
  while (processorOsmElementsFromXml.TryRead(element))
    processor(std::move(element));
}

void BuildIntermediateData(std::vector<OsmElement> && elements,
                           cache::IntermediateDataWriter & cache, TownsDumper & towns,
                           bool concurrent)
{
  if (elements.empty())
    return;

  auto const firstElementType = elements.front().m_type;
  auto nodes = cache::IntermediateDataWriter::Nodes{};
  if (firstElementType == OsmElement::EntityType::Node)
    nodes.reserve(elements.size());

  auto ways = cache::IntermediateDataWriter::Ways{};
  if (firstElementType == OsmElement::EntityType::Way)
    ways.reserve(elements.size());

  auto relations = cache::IntermediateDataWriter::Relations{};
  if (firstElementType == OsmElement::EntityType::Relation)
    relations.reserve(elements.size());

  for (auto & osmElement : elements)
  {
    towns.CheckElement(osmElement, concurrent);

    auto const id = osmElement.m_id;
    switch (osmElement.m_type)
    {
    case OsmElement::EntityType::Node:
    {
      nodes.emplace_back(id, NodeElement{});
      BuildIntermediateNode(std::move(osmElement), nodes.back().second);
      break;
    }
    case OsmElement::EntityType::Way:
    {
      WayElement way{id};
      if (BuildIntermediateWay(std::move(osmElement), way))
        ways.emplace_back(id, std::move(way));
      break;
    }
    case OsmElement::EntityType::Relation:
    {
      RelationElement relation;
      if (BuildIntermediateRelation(std::move(osmElement), relation))
        relations.emplace_back(id, std::move(relation));
      break;
    }
    default:
      break;
    }
  }

  if (!nodes.empty())
    cache.AddNodes(std::move(nodes), concurrent);
  if (!ways.empty())
    cache.AddWays(std::move(ways), concurrent);
  if (!relations.empty())
    cache.AddRelations(std::move(relations), concurrent);
}

void BuildIntermediateDataFromO5M(
    ProcessorOsmElementsFromO5M & o5mReader, cache::IntermediateDataWriter & cache,
    TownsDumper & towns, bool concurrent)
{
  std::vector<OsmElement> elements(o5mReader.ChunkSize());
  size_t elementsCount = 0;
  while (o5mReader.TryRead(elements[elementsCount]))
  {
    ++elementsCount;
    if (elementsCount < o5mReader.ChunkSize())
      continue;

    BuildIntermediateData(std::move(elements), cache, towns, concurrent);
    elements.resize(o5mReader.ChunkSize());  // restore capacity after std::move(elements)
    elementsCount = 0;
  }

  elements.resize(elementsCount);
  BuildIntermediateData(std::move(elements), cache, towns, concurrent);
}

void BuildIntermediateDataFromO5M(
    std::string const & filename, cache::IntermediateDataWriter & cache, TownsDumper & towns,
    unsigned int threadsCount)
{
  if (filename.empty())
  {
    // Read form stdin.
    auto && reader = SourceReader{};
    auto && o5mReader = ProcessorOsmElementsFromO5M(reader);
    return BuildIntermediateDataFromO5M(o5mReader, cache, towns, false /* concurrent */);
  }

  LOG_SHORT(LINFO, ("Reading OSM data from", filename));

  auto sourceMap = boost::iostreams::mapped_file_source{filename};
  if (!sourceMap.is_open())
    MYTHROW(Writer::OpenException, ("Failed to open", filename));
  ::madvise(const_cast<char*>(sourceMap.data()), sourceMap.size(), MADV_SEQUENTIAL);

  constexpr size_t chunkSize = 10'000;
  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < std::max(threadsCount, 1u); ++i)
  {
    threads.emplace_back([&sourceMap, &cache, &towns, threadsCount, i] {
      namespace io = boost::iostreams;
      auto && sourceArray = io::array_source{sourceMap.data(), sourceMap.size()};
      auto && stream = io::stream<io::array_source>{sourceArray, std::ios::binary};
      auto && reader = SourceReader(stream);
      auto && o5mReader = ProcessorOsmElementsFromO5M(reader, threadsCount, i, chunkSize);
      BuildIntermediateDataFromO5M(o5mReader, cache, towns, threadsCount > 1);
    });
  }

  for (auto & thread : threads)
    thread.join();
}

void ProcessOsmElementsFromO5M(SourceReader & stream, function<void(OsmElement &&)> processor)
{
  ProcessorOsmElementsFromO5M processorOsmElementsFromO5M(stream);

  OsmElement element;
  while (processorOsmElementsFromO5M.TryRead(element))
    processor(std::move(element));
}

ProcessorOsmElementsFromO5M::ProcessorOsmElementsFromO5M(
    SourceReader & stream, size_t taskCount, size_t taskId, size_t chunkSize)
  : m_stream(stream)
  , m_dataset([&](uint8_t * buffer, size_t size) {
      return m_stream.Read(reinterpret_cast<char *>(buffer), size);
  }, 1024 * 1024)
  , m_taskCount{taskCount}
  , m_taskId{taskId}
  , m_chunkSize{chunkSize}
  , m_pos(m_dataset.begin())
{
}

bool ProcessorOsmElementsFromO5M::TryRead(OsmElement & element)
{
  while (m_pos != m_dataset.end())
  {
    auto const chunkId = m_elementCounter / m_chunkSize;
    auto const chunkTaskId = chunkId % m_taskCount;
    if (chunkTaskId == m_taskId)
      return Read(element);

    ++m_pos;
    ++m_elementCounter;
  }

  return false;
}

bool ProcessorOsmElementsFromO5M::Read(OsmElement & element)
{
  using Type = osm::O5MSource::EntityType;

  element.Clear();

  auto const translate = [](Type t) -> OsmElement::EntityType {
    switch (t)
    {
    case Type::Node: return OsmElement::EntityType::Node;
    case Type::Way: return OsmElement::EntityType::Way;
    case Type::Relation: return OsmElement::EntityType::Relation;
    default: return OsmElement::EntityType::Unknown;
    }
  };

  // Be careful, we could call Nodes(), Members(), Tags() from O5MSource::Entity
  // only once (!). Because these functions read data from file simultaneously with
  // iterating in loop. Furthermore, into Tags() method calls Nodes.Skip() and Members.Skip(),
  // thus first call of Nodes (Members) after Tags() will not return any results.
  // So don not reorder the "for" loops (!).
  auto const & entity = *m_pos;
  element.m_id = entity.id;
  switch (entity.type)
  {
  case Type::Node:
  {
    element.m_type = OsmElement::EntityType::Node;
    element.m_lat = entity.lat;
    element.m_lon = entity.lon;
    break;
  }
  case Type::Way:
  {
    element.m_type = OsmElement::EntityType::Way;
    for (uint64_t nd : entity.Nodes())
      element.AddNd(nd);
    break;
  }
  case Type::Relation:
  {
    element.m_type = OsmElement::EntityType::Relation;
    for (auto const & member : entity.Members())
      element.AddMember(member.ref, translate(member.type), member.role);
    break;
  }
  default: break;
  }

  for (auto const & tag : entity.Tags())
    element.AddTag(tag.key, tag.value);

  ++m_pos;
  ++m_elementCounter;
  return true;
}

ProcessorOsmElementsFromXml::ProcessorOsmElementsFromXml(SourceReader & stream)
  : m_xmlSource([&, this](auto * element) { m_queue.emplace(*element); })
  , m_parser(stream, m_xmlSource)
{
}

bool ProcessorOsmElementsFromXml::TryReadFromQueue(OsmElement & element)
{
  if (m_queue.empty())
    return false;

  element = m_queue.front();
  m_queue.pop();
  return true;
}

bool ProcessorOsmElementsFromXml::TryRead(OsmElement & element)
{
  do {
    if (TryReadFromQueue(element))
      return true;
  } while (m_parser.Read());

  return TryReadFromQueue(element);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Generate functions implementations.
///////////////////////////////////////////////////////////////////////////////////////////////////

bool GenerateIntermediateData(feature::GenerateInfo const & info)
{
  auto nodes = cache::CreatePointStorageWriter(info.m_nodeStorageType,
                                               info.GetIntermediateFileName(NODES_FILE));
  cache::IntermediateDataWriter cache(*nodes, info);
  TownsDumper towns;

  LOG(LINFO, ("Data source:", info.m_osmFileName));

  switch (info.m_osmFileType)
  {
  case feature::GenerateInfo::OsmSourceType::XML:
    BuildIntermediateDataFromXML(info.m_osmFileName, cache, towns);
    break;
  case feature::GenerateInfo::OsmSourceType::O5M:
    BuildIntermediateDataFromO5M(info.m_osmFileName, cache, towns, info.m_threadsCount);
    break;
  }

  cache.SaveIndex();
  towns.Dump(info.GetIntermediateFileName(TOWNS_FILE));
  LOG(LINFO, ("Added points count =", nodes->GetNumProcessedPoints()));
  return true;
}
}  // namespace generator

#include "generator/covering_index_generator.hpp"

#include "generator/data_version.hpp"
#include "generator/geo_objects/geo_objects_filter.hpp"
#include "generator/geometry_holder.hpp"
#include "generator/streets/streets_filter.hpp"
#include "generator/utils.hpp"

#include "indexer/covered_object.hpp"
#include "indexer/covering_index_builder.hpp"
#include "indexer/data_header.hpp"
#include "indexer/scales.hpp"

#include "coding/file_container.hpp"
#include "coding/internal/file_data.hpp"
#include "coding/geometry_coding.hpp"

#include "geometry/convex_hull.hpp"

#include "platform/platform.hpp"

#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/string_utils.hpp"
#include "base/thread_pool_computational.hpp"
#include "base/timer.hpp"

#include "defines.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

using namespace feature;
using namespace std;

namespace generator
{
class CoveredObjectBuilder
{
public:
  CoveredObjectBuilder(base::thread_pool::computational::ThreadPool & threadPool)
    : m_threadPool(threadPool)
  {
    m_header.SetGeometryCodingParams(serial::GeometryCodingParams());
    m_header.SetScales({scales::GetUpperScale()});
  }

  boost::optional<indexer::CoveredObject const &> operator()(FeatureBuilder & fb)
  {
    auto && geometryHolder = MakeGeometryHolder(fb);
    if (!geometryHolder)
      return boost::none;
    auto & data = geometryHolder->GetBuffer();

    auto const encodedId = fb.GetMostGenericOsmId().GetEncodedId();
    m_coveredObject.SetId(encodedId);

    switch (fb.GetGeomType())
    {
    case GeomType::Point:
    {
      buffer_vector<m2::PointD, 32> points{fb.GetKeyPoint()};
      m_coveredObject.SetPoints(std::move(points));
      break;
    }
    case GeomType::Line:
    {
      buffer_vector<m2::PointD, 32> points{data.m_innerPts.begin(), data.m_innerPts.end()};
      m_coveredObject.SetPoints(std::move(points));
      break;
    }
    case GeomType::Area:
    {
      CHECK_GREATER_OR_EQUAL(data.m_innerTrg.size(), 3, ());

      m_pointsBuffer.clear();
      m_pointsBuffer.append(data.m_innerTrg.begin(), data.m_innerTrg.end());

      buffer_vector<m2::PointD, 32> triangles;
      serial::StripToTriangles(m_pointsBuffer.size(), m_pointsBuffer, triangles);
      m_coveredObject.SetTriangles(std::move(triangles));
      break;
    }
    default:
      UNREACHABLE();
    };

    return {m_coveredObject};
  }

private:
  boost::optional<GeometryHolder> MakeGeometryHolder(FeatureBuilder & fb)
  {
    // Do not limit inner triangles number to save all geometry without additional sections.
    GeometryHolder holder{
        fb, m_header, std::numeric_limits<uint32_t>::max() /* maxTrianglesNumber */};

    // Simplify and serialize geometry.
    vector<m2::PointD> points;
    m2::SquaredDistanceFromSegmentToPoint<m2::PointD> distFn;
    SimplifyPoints(distFn, scales::GetUpperScale(), holder.GetSourcePoints(), points);

    if (points.empty())
      return boost::none;

    if (fb.IsLine())
      holder.AddPoints(points, 0);

    // For areas we save outer geometry only.
    if (fb.IsArea() && holder.NeedProcessTriangles())
    {
      // At this point we don't need last point equal to first.
      points.pop_back();
      auto const & polys = fb.GetGeometry();
      if (polys.size() != 1)
      {
        points.clear();
        for (auto const & poly : polys)
          points.insert(points.end(), poly.begin(), poly.end());
      }

      if (points.size() > 2)
      {
        if (!holder.TryToMakeStrip(points, m_threadPool))
        {
          m2::ConvexHull hull(points, 1e-16);
          vector<m2::PointD> hullPoints = hull.Points();
          holder.SetInner();
          auto const id = fb.GetMostGenericOsmId();
          if (!holder.TryToMakeStrip(hullPoints))
          {
            LOG(LWARNING, ("Error while building tringles for object with OSM Id:",
                           id.GetSerialId(), "Type:", id.GetType(), "points:", points,
                           "hull:", hull.Points()));
            return boost::none;
          }
        }
      }

      if (holder.NeedProcessTriangles())
        return boost::none;
    }

    return {std::move(holder)};
  }

  DataHeader m_header;
  indexer::CoveredObject m_coveredObject;
  buffer_vector<m2::PointD, 32> m_pointsBuffer;
  base::thread_pool::computational::ThreadPool & m_threadPool;
};

template <typename FeatureFilter, typename IndexBuilder>
void CoverFeatures(
    std::string const & featuresFile, FeatureFilter && featureFilter,
    IndexBuilder && indexBuilder, unsigned int threadsCount, uint64_t chunkFeaturesCount,
    base::thread_pool::computational::ThreadPool & threadPool,
    covering::ObjectsCovering & objectsCovering)
{
  std::list<covering::ObjectsCovering> coveringsParts{};
  auto makeProcessor = [&] {
    coveringsParts.emplace_back();
    auto & covering = coveringsParts.back();

    CoveredObjectBuilder localityObjectBuilder{threadPool};
    auto processor = [featureFilter, &indexBuilder, &covering, localityObjectBuilder]
                     (FeatureBuilder & fb, uint64_t /* currPos */) mutable
    {
      if (!featureFilter(fb))
        return;

      if (auto && localityObject = localityObjectBuilder(fb))
        indexBuilder.Cover(*localityObject, covering);
    };

    return processor;
  };

  LOG(LINFO, ("Geometry cover features..."));
  feature::ProcessParallelFromDatRawFormat(
      threadsCount, chunkFeaturesCount, featuresFile, makeProcessor);
  LOG(LINFO, ("Finish features geometry covering from", featuresFile));

  LOG(LINFO, ("Merge geometry coverings..."));
  while (!coveringsParts.empty())
  {
    auto const & part = coveringsParts.back();
    objectsCovering.insert(objectsCovering.end(), part.begin(), part.end());
    coveringsParts.pop_back();
  }
  LOG(LINFO, ("Finish merging of geometry coverings of features from ", featuresFile));
}

namespace
{
bool ParseNodes(string nodesFile, set<uint64_t> & nodeIds)
{
  if (nodesFile.empty())
    return true;

  ifstream stream(nodesFile);
  if (!stream)
  {
    LOG(LERROR, ("Could not open", nodesFile));
    return false;
  }

  string line;
  size_t lineNumber = 1;
  while (getline(stream, line))
  {
    strings::SimpleTokenizer iter(line, " ");
    uint64_t nodeId;
    if (!iter || !strings::to_uint64(*iter, nodeId))
    {
      LOG(LERROR, ("Error while parsing node id at line", lineNumber, "Line contents:", line));
      return false;
    }

    nodeIds.insert(nodeId);
    ++lineNumber;
  }
  return true;
}
}  // namespace

bool GenerateRegionsIndex(std::string const & outPath, std::string const & featuresFile,
                          unsigned int threadsCount)
{
  base::thread_pool::computational::ThreadPool threadPool{threadsCount};
  indexer::RegionsIndexBuilder indexBuilder{threadPool};

  auto const featuresFilter = [](FeatureBuilder & fb) { return fb.IsArea(); };
  covering::ObjectsCovering objectsCovering;
  CoverFeatures(featuresFile, featuresFilter, indexBuilder, threadsCount,
                1 /* chunkFeaturesCount */, threadPool, objectsCovering);

  LOG(LINFO, ("Build locality index..."));
  if (!indexBuilder.BuildCoveringIndex(std::move(objectsCovering), outPath))
    return false;
  LOG(LINFO, ("Finish locality index building", outPath));
  return true;
}

bool GenerateGeoObjectsIndex(
    std::string const & outPath, std::string const & geoObjectsFeaturesFile,
    unsigned int threadsCount,
    boost::optional<std::string> const & nodesFile,
    boost::optional<std::string> const & streetsFeaturesFile)
{
  base::thread_pool::computational::ThreadPool threadPool{threadsCount};
  covering::ObjectsCovering objectsCovering;
  indexer::GeoObjectsIndexBuilder indexBuilder{threadPool};

  set<uint64_t> nodeIds;
  if (nodesFile && !ParseNodes(*nodesFile, nodeIds))
    return false;

  auto const geoObjectsFilter = [&nodeIds](FeatureBuilder & fb) {
    using generator::geo_objects::GeoObjectsFilter;

    if (GeoObjectsFilter::IsBuilding(fb) || GeoObjectsFilter::HasHouse(fb))
      return true;

    if (GeoObjectsFilter::IsPoi(fb))
      return nodeIds.count(fb.GetMostGenericOsmId().GetEncodedId()) != 0;

    return false;
  };

  CoverFeatures(geoObjectsFeaturesFile, geoObjectsFilter, indexBuilder, threadsCount,
                10 /* chunkFeaturesCount */, threadPool, objectsCovering);

  if (streetsFeaturesFile)
  {
    auto const streetsFilter = [](FeatureBuilder & fb) {
      using generator::streets::StreetsFilter;
      return StreetsFilter::IsStreet(fb);
    };

    CoverFeatures(*streetsFeaturesFile, streetsFilter, indexBuilder, threadsCount,
                  1 /* chunkFeaturesCount */, threadPool, objectsCovering);
  }

  LOG(LINFO, ("Build objects index..."));
  if (!indexBuilder.BuildCoveringIndex(std::move(objectsCovering), outPath))
    return false;
  LOG(LINFO, ("Finish objects index building", outPath));
  return true;
}

// BordersCollector --------------------------------------------------------------------------------
class BordersCollector
{
public:
  explicit BordersCollector(string const & filename)
    : m_writer(filename, FileWriter::OP_WRITE_EXISTING)
    , m_bordersWriter{m_writer.GetWriter(BORDERS_FILE_TAG)}
  {
  }

  void Collect(FeatureBuilder & fb)
  {
    if (fb.IsArea())
    {
      m_buffer.clear();
      fb.SerializeBorderForIntermediate(serial::GeometryCodingParams(), m_buffer);
      WriteFeatureData(m_buffer);
    }
  }

private:
  void WriteFeatureData(std::vector<char> const & bytes)
  {
    size_t const sz = bytes.size();
    CHECK(sz != 0, ("Empty feature not allowed here!"));

    WriteVarUint(*m_bordersWriter, sz);
    m_bordersWriter->Write(&bytes[0], sz);
  }

  FilesContainerW m_writer;
  std::unique_ptr<FileContainerWriter> m_bordersWriter;
  FeatureBuilder::Buffer m_buffer;

  DISALLOW_COPY_AND_MOVE(BordersCollector);
};

bool GenerateBorders(std::string const & outPath, string const & featuresFile)
{
  BordersCollector bordersCollector(outPath);

  ForEachFromDatRawFormat(featuresFile, [&](FeatureBuilder & fb, uint64_t /* currPos */) {
    bordersCollector.Collect(fb);
  });

  return true;
}

//--------------------------------------------------------------------------------------------------
void WriteDataVersionSection(std::string const & outPath, std::string const & dataVersionJson)
{
  FilesContainerW writer{outPath, FileWriter::OP_WRITE_EXISTING};
  writer.Write(dataVersionJson.c_str(), dataVersionJson.size(), DataVersion::kFileTag);
}
}  // namespace generator

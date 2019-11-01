#include "generator/locality_sorter.hpp"

#include "generator/geo_objects/geo_objects_filter.hpp"
#include "generator/geometry_holder.hpp"
#include "generator/streets/streets_filter.hpp"
#include "generator/utils.hpp"

#include "indexer/data_header.hpp"
#include "indexer/scales.hpp"

#include "coding/file_container.hpp"
#include "coding/internal/file_data.hpp"

#include "geometry/convex_hull.hpp"

#include "platform/platform.hpp"

#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/string_utils.hpp"
#include "base/timer.hpp"

#include "defines.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <set>
#include <vector>

using namespace feature;
using namespace std;

namespace
{
class BordersCollector : public FeaturesCollector
{
public:
  explicit BordersCollector(string const & filename)
    : FeaturesCollector(filename + EXTENSION_TMP), m_writer(filename, FileWriter::OP_WRITE_EXISTING)
  {
  }

  // FeaturesCollector overrides:
  uint32_t Collect(FeatureBuilder & fb) override
  {
    if (fb.IsArea())
    {
      FeatureBuilder::Buffer buffer;
      fb.SerializeBorderForIntermediate(serial::GeometryCodingParams(), buffer);
      WriteFeatureBase(buffer, fb);
    }
    return 0;
  }

  void Finish() override
  {
    Flush();

    m_writer.Write(m_datFile.GetName(), BORDERS_FILE_TAG);
    m_writer.Finish();
  }

private:
  FilesContainerW m_writer;
  DataHeader m_header;

  DISALLOW_COPY_AND_MOVE(BordersCollector);
};

class LocalityCollector : public FeaturesCollector
{
public:
  LocalityCollector(string const & filename, DataHeader const & header, uint32_t versionDate)
    : FeaturesCollector(filename + EXTENSION_TMP)
    , m_writer(filename)
    , m_header(header)
    , m_versionDate(versionDate)
  {
  }

  // FeaturesCollector overrides:
  void Finish() override
  {
    {
      auto w = m_writer.GetWriter(VERSION_FILE_TAG);
      version::WriteVersion(*w, m_versionDate);
    }

    m_header.SetBounds(m_bounds);
    {
      auto w = m_writer.GetWriter(HEADER_FILE_TAG);
      m_header.Save(*w);
    }

    Flush();

    m_writer.Write(m_datFile.GetName(), LOCALITY_DATA_FILE_TAG);
    m_writer.Finish();
  }

  uint32_t Collect(FeatureBuilder & fb) override
  {
    // Do not limit inner triangles number to save all geometry without additional sections.
    GeometryHolder holder(fb, m_header, numeric_limits<uint32_t>::max() /* maxTrianglesNumber */);

    // Simplify and serialize geometry.
    vector<m2::PointD> points;
    m2::SquaredDistanceFromSegmentToPoint<m2::PointD> distFn;

    SimplifyPoints(distFn, scales::GetUpperScale(), holder.GetSourcePoints(), points);

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
        if (!holder.TryToMakeStrip(points))
        {
          m2::ConvexHull hull(points, 1e-16);
          vector<m2::PointD> hullPoints = hull.Points();
          holder.SetInner();
          auto const id = fb.GetMostGenericOsmId();
          if (!holder.TryToMakeStrip(hullPoints))
          {
            LOG(LWARNING, ("Error while building tringles for object with OSM Id:", id.GetSerialId(),
                           "Type:", id.GetType(), "points:", points, "hull:", hull.Points()));
            return 0;
          }
        }
      }
    }

    auto & buffer = holder.GetBuffer();
    if (fb.PreSerializeAndRemoveUselessNamesForMwm(buffer))
    {
      fb.SerializeLocalityObject(serial::GeometryCodingParams(), buffer);
      WriteFeatureBase(buffer.m_buffer, fb);
    }
    return 0;
  }

private:
  FilesContainerW m_writer;
  DataHeader m_header;
  uint32_t m_versionDate;

  DISALLOW_COPY_AND_MOVE(LocalityCollector);
};

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

using NeedSerialize = function<bool(FeatureBuilder & fb1)>;
bool GenerateLocalityDataImpl(FeaturesCollector & collector,
                              CalculateMidPoints::MinDrawableScalePolicy const & minDrawableScalePolicy,
                              NeedSerialize const & needSerialize,
                              string const & featuresFile, string const & /*dataFile*/)
{
  // Transform features from raw format to LocalityObject format.
  try
  {
    LOG(LINFO, ("Processing", featuresFile));

    CalculateMidPoints midPoints{minDrawableScalePolicy};
    ForEachFromDatRawFormat(featuresFile, midPoints);

    // Sort features by their middle point.
    midPoints.Sort();

    FileReader reader(featuresFile);
    for (auto const & point : midPoints.GetVector())
    {
      ReaderSource<FileReader> src(reader);
      src.Skip(point.second);

      FeatureBuilder f;
      ReadFromSourceRawFormat(src, f);
      // Emit object.
      if (needSerialize(f))
        collector.Collect(f);
    }

    collector.Finish();
  }
  catch (RootException const & ex)
  {
    LOG(LCRITICAL, ("Locality data writing error:", ex.Msg()));
    return false;
  }

  return true;
}
}  // namespace

namespace feature
{
bool GenerateGeoObjectsData(string const & featuresFile,
                            NeedSerialize const & needSerialize,
                            string const & dataFile)
{
  DataHeader header;
  header.SetGeometryCodingParams(serial::GeometryCodingParams());
  header.SetScales({scales::GetUpperScale()});

  LocalityCollector localityCollector(dataFile, header,
                                      static_cast<uint32_t>(base::SecondsSinceEpoch()));
  return GenerateLocalityDataImpl(
      localityCollector,
      static_cast<int (*)(TypesHolder const & types, m2::RectD limitRect)>(GetMinDrawableScale),
      needSerialize, featuresFile, dataFile);
}

bool IsGeoObjectAccepted(FeatureBuilder & fb, bool allowStreet, set<uint64_t> const & includedPois)
{
  using generator::geo_objects::GeoObjectsFilter;
  using generator::streets::StreetsFilter;

  if (GeoObjectsFilter::IsBuilding(fb) || GeoObjectsFilter::HasHouse(fb))
    return true;

  if (allowStreet && StreetsFilter::IsStreet(fb))
    return true;

  if (GeoObjectsFilter::IsPoi(fb))
    return 0 != includedPois.count(fb.GetMostGenericOsmId().GetEncodedId());

  return false;
}

bool GenerateGeoObjectsAndStreetsData(string const & geoObjectsFeaturesFile,
                                      string const & streetFeaturesFile,
                                      boost::optional<string> const & nodesFile,
                                      string const & dataFile)
{
  auto featuresDirectory = base::GetDirectory(geoObjectsFeaturesFile);
  auto featuresFile =
      base::JoinPath(featuresDirectory,
                     std::string{"geo_objects_and_streets"} + DATA_FILE_EXTENSION_TMP);
  SCOPE_GUARD(featuresFileGuard, std::bind(Platform::RemoveFileIfExists, featuresFile));

  base::AppendFileToFile(geoObjectsFeaturesFile, featuresFile);
  base::AppendFileToFile(streetFeaturesFile, featuresFile);

  set<uint64_t> nodeIds;
  if (nodesFile && !ParseNodes(*nodesFile, nodeIds))
    return false;

  auto const needSerialize = [&nodeIds](FeatureBuilder & fb) {
    return IsGeoObjectAccepted(fb, true /* allowStreet */, nodeIds);
  };

  return GenerateGeoObjectsData(featuresFile, needSerialize, dataFile);
}

bool GenerateGeoObjectsData(string const & geoObjectsFeaturesFile,
                            boost::optional<string> const & nodesFile,
                            string const & dataFile)
{
  set<uint64_t> nodeIds;
  if (nodesFile && !ParseNodes(*nodesFile, nodeIds))
    return false;

  auto const needSerialize = [&nodeIds](FeatureBuilder & fb) {
    return IsGeoObjectAccepted(fb, false /* allowStreet */, nodeIds);
  };

  return GenerateGeoObjectsData(geoObjectsFeaturesFile, needSerialize, dataFile);
}

bool GenerateRegionsData(string const & featuresFile, string const & dataFile)
{
  DataHeader header;
  header.SetGeometryCodingParams(serial::GeometryCodingParams());
  header.SetScales({scales::GetUpperScale()});

  LocalityCollector regionsCollector(dataFile, header,
                                     static_cast<uint32_t>(base::SecondsSinceEpoch()));
  auto const needSerialize = [](FeatureBuilder const & fb) { return fb.IsArea(); };
  return GenerateLocalityDataImpl(regionsCollector, GetMinDrawableScaleGeometryOnly,
                                  needSerialize, featuresFile, dataFile);
}

bool GenerateBorders(string const & featuresFile, string const & dataFile)
{
  BordersCollector bordersCollector(dataFile);
  auto const needSerialize = [](FeatureBuilder const & fb) { return fb.IsArea(); };
  return GenerateLocalityDataImpl(bordersCollector, GetMinDrawableScaleGeometryOnly,
                                  needSerialize, featuresFile, dataFile);
}
}  // namespace feature

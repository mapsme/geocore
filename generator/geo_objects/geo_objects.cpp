#include "generator/covering_index_generator.hpp"
#include "generator/data_version.hpp"
#include "generator/feature_builder.hpp"
#include "generator/feature_generator.hpp"
#include "generator/key_value_concurrent_writer.hpp"
#include "generator/key_value_storage.hpp"

#include "generator/geo_objects/geo_objects.hpp"
#include "generator/geo_objects/geo_objects_filter.hpp"
#include "generator/geo_objects/geo_objects_maintainer.hpp"

#include "generator/regions/region_base.hpp"

#include "indexer/classificator.hpp"
#include "indexer/covering_index.hpp"

#include "coding/files_merger.hpp"
#include "coding/internal/file_data.hpp"
#include "coding/mmap_reader.hpp"

#include "geometry/mercator.hpp"

#include "base/geo_object_id.hpp"

#include "3party/jansson/myjansson.hpp"

#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

using namespace feature;

namespace generator
{
namespace geo_objects
{
// BufferedCuncurrentUnorderedMapUpdater -----------------------------------------------------------
template <typename Key, typename Value>
class BufferedCuncurrentUnorderedMapUpdater
{
public:
  static constexpr size_t kValuesBufferSize{10'000};
  // Max size for try-lock flushing into target.
  static constexpr size_t kValuesBufferSizeMax{100'000};

  BufferedCuncurrentUnorderedMapUpdater(std::unordered_map<Key, Value> & map,
                                        std::mutex & mapMutex)
    : m_map{map}
    , m_mapMutex{mapMutex}
  { }
  BufferedCuncurrentUnorderedMapUpdater(BufferedCuncurrentUnorderedMapUpdater &&) = default;
  BufferedCuncurrentUnorderedMapUpdater & operator=(
      BufferedCuncurrentUnorderedMapUpdater &&) = default;
  ~BufferedCuncurrentUnorderedMapUpdater()
  {
    if (!m_valuesBuffer.empty())
      FlushBuffer(true);
  }

  template <typename... Args>
  void Emplace(Args &&... args)
  {
    m_valuesBuffer.emplace_back(std::forward<Args>(args)...);

    if (m_valuesBuffer.size() >= kValuesBufferSize)
      FlushBuffer(m_valuesBuffer.size() >= kValuesBufferSizeMax);
  }

private:
  using MapValue = typename std::unordered_map<Key, Value>::value_type;

  void FlushBuffer(bool force)
  {
    auto && lock =
        std::unique_lock<std::mutex>{m_mapMutex, std::defer_lock};
    if (force)
      lock.lock();
    else
      lock.try_lock();

    if (!lock)
      return;

    for (auto & value : m_valuesBuffer)
      m_map.insert(std::move(value));
    lock.unlock();

    m_valuesBuffer.clear();
  }

  std::unordered_map<Key, Value> & m_map;
  std::mutex & m_mapMutex;
  std::vector<MapValue> m_valuesBuffer;
};

// BuildingsAndHousesGenerator ---------------------------------------------------------------------
class BuildingsAndHousesGenerator
{
public:
  BuildingsAndHousesGenerator(BuildingsAndHousesGenerator const &) = delete;
  BuildingsAndHousesGenerator & operator=(BuildingsAndHousesGenerator const &) = delete;

  BuildingsAndHousesGenerator(
      std::string const & geoObjectKeyValuePath, GeoObjectMaintainer & geoObjectMaintainer,
      RegionInfoLocater const & regionInfoLocater)
    : m_geoObjectKeyValuePath{geoObjectKeyValuePath}
    , m_geoObjectMaintainer{geoObjectMaintainer}
    , m_regionInfoLocater{regionInfoLocater}
  {
  }

  void GenerateBuildingsAndHouses(
      std::string const & geoObjectsTmpMwmPath, unsigned int threadsCount)
  {
    GeoId2GeoData geoId2GeoData;
    uint64_t const fileSize = boost::filesystem::file_size(geoObjectsTmpMwmPath);
    geoId2GeoData.reserve(std::min(uint64_t{500'000'000}, fileSize / 10));

    std::mutex geoId2GeoDataMutex;

    feature::ProcessParallelFromDatRawFormat(threadsCount, geoObjectsTmpMwmPath, [&] {
      return Processor{*this, m_geoObjectKeyValuePath, geoId2GeoData, geoId2GeoDataMutex};
    });

    m_geoObjectMaintainer.SetGeoData(std::move(geoId2GeoData));
  }

private:
  using GeoObjectData = GeoObjectMaintainer::GeoObjectData;
  using GeoId2GeoData = GeoObjectMaintainer::GeoId2GeoData;

  class Processor
  {
  public:
    Processor(BuildingsAndHousesGenerator & generator,
              std::string const & geoObjectKeyValuePath,
              GeoId2GeoData & geoId2GeoData, std::mutex & geoId2GeoDataMutex)
      : m_generator{generator}
      , m_kvWriter{geoObjectKeyValuePath}
      , m_geoDataCache{geoId2GeoData, geoId2GeoDataMutex}
    {
    }

    void operator()(FeatureBuilder & fb, uint64_t /* currPos */)
    {
      if (!GeoObjectsFilter::IsBuilding(fb) && !GeoObjectsFilter::HasHouse(fb))
        return;

      auto regionKeyValue = m_generator.m_regionInfoLocater(fb.GetKeyPoint());
      if (!regionKeyValue)
        return;

      WriteIntoKv(fb, *regionKeyValue);
      CacheGeoData(fb, *regionKeyValue);
    }

  private:
    void WriteIntoKv(FeatureBuilder & fb, KeyValue const & regionKeyValue)
    {
      auto const id = fb.GetMostGenericOsmId();
      auto jsonValue = AddAddress(fb.GetParams().GetStreet(), fb.GetParams().house.Get(),
                                  fb.GetKeyPoint(), fb.GetMultilangName(), regionKeyValue);

      m_kvWriter.Write(id, JsonValue{std::move(jsonValue)});
    }

    void CacheGeoData(FeatureBuilder & fb, KeyValue const & regionKeyValue)
    {
      auto const id = fb.GetMostGenericOsmId();
      auto geoData = GeoObjectData{fb.GetParams().GetStreet(), fb.GetParams().house.Get(),
                                   base::GeoObjectId(regionKeyValue.first)};
      m_geoDataCache.Emplace(id, std::move(geoData));
    }

    BuildingsAndHousesGenerator & m_generator;
    KeyValueConcurrentWriter m_kvWriter;
    BufferedCuncurrentUnorderedMapUpdater<base::GeoObjectId, GeoObjectData> m_geoDataCache;
  };

  std::string m_geoObjectKeyValuePath;
  GeoObjectMaintainer & m_geoObjectMaintainer;
  RegionInfoLocater const & m_regionInfoLocater;
};

void AddBuildingsAndThingsWithHousesThenEnrichAllWithRegionAddresses(
    std::string const & geoObjectKeyValuePath,  GeoObjectMaintainer & geoObjectMaintainer,
    std::string const & pathInGeoObjectsTmpMwm, RegionInfoLocater const & regionInfoLocater,
    bool /*verbose*/, unsigned int threadsCount)
{
  auto && generator =
      BuildingsAndHousesGenerator{geoObjectKeyValuePath, geoObjectMaintainer, regionInfoLocater};
  generator.GenerateBuildingsAndHouses(pathInGeoObjectsTmpMwm, threadsCount);
  LOG(LINFO, ("Added", geoObjectMaintainer.Size(), "geo objects with addresses."));
}

// NullBuildingsAddressing -------------------------------------------------------------------------
class NullBuildingsAddressing
{
public:
  NullBuildingsAddressing(std::string const & geoObjectsTmpMwmPath,
                          GeoObjectMaintainer & geoObjectMaintainer,
                          unsigned int threadsCount)
    : m_geoObjectsTmpMwmPath{geoObjectsTmpMwmPath}
    , m_geoObjectMaintainer{geoObjectMaintainer}
    , m_threadsCount{threadsCount}
  {
  }

  void AddAddresses()
  {
    FillBuildingsInfo();
    FillBuildingsGeometries();
    TransferGeometryToAddressPoints();
  }

  NullBuildingsInfo & GetNullBuildingsInfo() { return m_buildingsInfo; }

private:
  using BuildingsGeometries =
      std::unordered_map<base::GeoObjectId, FeatureBuilder::Geometry>;

  class BuildingsInfoFiller
  {
  public:
    BuildingsInfoFiller(NullBuildingsAddressing & addressing)
      : m_goObjectsView{addressing.m_geoObjectMaintainer.CreateView()}
      , m_addressPoints2Buildings{addressing.m_buildingsInfo.m_addressPoints2Buildings,
                                  addressing.m_addressPoints2buildingsMutex}
      , m_buildings2AddressPoints{addressing.m_buildingsInfo.m_buildings2AddressPoints,
                                  addressing.m_buildings2addressPointsMutex}
    { }

    void operator()(FeatureBuilder & fb, uint64_t /* currPos */)
    {
      if (!GeoObjectsFilter::HasHouse(fb) || !fb.IsPoint())
        return;

      // search for ids of Buildinds not stored with geoObjectsMantainer
      // they are nullBuildings
      auto const buildingId =
          m_goObjectsView.SearchIdOfFirstMatchedObject(fb.GetKeyPoint(), [&](base::GeoObjectId id) {
            auto const & geoData = m_goObjectsView.GetGeoData(id);
            return geoData && geoData->m_house.empty();
          });

      if (!buildingId)
        return;

      auto const id = fb.GetMostGenericOsmId();
      m_addressPoints2Buildings.Emplace(id, *buildingId);
      m_buildings2AddressPoints.Emplace(*buildingId, id);
    }

  private:
    using Updater =
        BufferedCuncurrentUnorderedMapUpdater<base::GeoObjectId, base::GeoObjectId>;

    GeoObjectMaintainer::GeoObjectsView m_goObjectsView;
    Updater m_addressPoints2Buildings;
    Updater m_buildings2AddressPoints;
  };

  class BuildingsGeometriesFiller
  {
  public:
    BuildingsGeometriesFiller(NullBuildingsAddressing & addressing)
      : m_buildingsInfo{addressing.m_buildingsInfo}
      , m_buildingsGeometries{addressing.m_buildingsGeometries,
                              addressing.m_buildingsGeometriesMutex}
    { }

    void operator()(FeatureBuilder & fb, uint64_t /* currPos */)
    {
      if (fb.GetParams().GetGeomType() != GeomType::Area)
        return;

      auto const id = fb.GetMostGenericOsmId();
      auto & buildings2AddressPoints = m_buildingsInfo.m_buildings2AddressPoints;
      if (buildings2AddressPoints.find(id) == buildings2AddressPoints.end())
        return;

      m_buildingsGeometries.Emplace(id, fb.GetGeometry());
    }

  private:
    using Updater =
        BufferedCuncurrentUnorderedMapUpdater<base::GeoObjectId, FeatureBuilder::Geometry>;

    NullBuildingsInfo const & m_buildingsInfo;
    Updater m_buildingsGeometries;
  };

  class GeometryTransfer
  {
  public:
    GeometryTransfer(NullBuildingsAddressing & addressing, FilesMerger & tmpMwmMerger,
                     std::atomic_size_t & pointsEnrichedStat)
      : m_buildingsInfo{addressing.m_buildingsInfo}
      , m_buildingsGeometries{addressing.m_buildingsGeometries}
      , m_collector{std::make_unique<FeaturesCollector>(GetPlatform().TmpPathForFile())}
      , m_pointsEnrichedStat{pointsEnrichedStat}
    {
      tmpMwmMerger.DeferMergeAndDelete(m_collector->GetFilePath());
    }

    void operator()(FeatureBuilder & fb, uint64_t /* currPos */)
    {
      auto const id = fb.GetMostGenericOsmId();
      auto const & addressPoints2Buildings = m_buildingsInfo.m_addressPoints2Buildings;
      auto point2BuildingIt = addressPoints2Buildings.find(id);
      if (point2BuildingIt != addressPoints2Buildings.end())
        AddGeometryToAddressPoint(fb, point2BuildingIt->second);

      auto const & buildings2AddressPoints = m_buildingsInfo.m_buildings2AddressPoints;
      if (buildings2AddressPoints.find(id) != buildings2AddressPoints.end())
        return;

      m_collector->Collect(fb);
    }

  private:
    void AddGeometryToAddressPoint(FeatureBuilder & fb, base::GeoObjectId nullBuildingId)
    {
      auto geometryIt = m_buildingsGeometries.find(nullBuildingId);
      if (geometryIt == m_buildingsGeometries.end())
      {
        LOG(LINFO, (nullBuildingId, "is a null building with strange geometry"));
        return;
      }

      auto const & geometry = geometryIt->second;

      // ResetGeometry does not reset center but SetCenter changes geometry type to Point and
      // adds center to bounding rect
      fb.SetCenter({});
      // ResetGeometry clears bounding rect
      fb.ResetGeometry();
      fb.GetParams().SetGeomType(GeomType::Area);

      for (std::vector<m2::PointD> poly : geometry)
        fb.AddPolygon(poly);

      fb.PreSerialize();

      ++m_pointsEnrichedStat;
    }

    NullBuildingsInfo const & m_buildingsInfo;
    BuildingsGeometries const & m_buildingsGeometries;
    std::unique_ptr<FeaturesCollector> m_collector;
    std::atomic_size_t & m_pointsEnrichedStat;
  };

  void FillBuildingsInfo()
  {
    feature::ProcessParallelFromDatRawFormat(m_threadsCount, m_geoObjectsTmpMwmPath, [&] {
      return BuildingsInfoFiller{*this};
    });

    LOG(LINFO, ("Found", m_buildingsInfo.m_addressPoints2Buildings.size(),
                "address points with outer building geometry"));
    LOG(LINFO, ("Found", m_buildingsInfo.m_buildings2AddressPoints.size(),
                "helpful addressless buildings"));
  }

  void FillBuildingsGeometries()
  {
    feature::ProcessParallelFromDatRawFormat(m_threadsCount, m_geoObjectsTmpMwmPath, [&] {
      return BuildingsGeometriesFiller{*this};
    });

    LOG(LINFO, ("Cached ", m_buildingsGeometries.size(), "buildings geometries"));
  }

  void TransferGeometryToAddressPoints()
  {
    auto const repackedTmpMwm = GetPlatform().TmpPathForFile();
    auto tmpMwmMerger = FilesMerger(repackedTmpMwm);

    std::atomic_size_t pointsEnrichedStat{0};
    feature::ProcessParallelFromDatRawFormat(m_threadsCount, m_geoObjectsTmpMwmPath, [&] {
      return GeometryTransfer{*this, tmpMwmMerger, pointsEnrichedStat};
    });

    tmpMwmMerger.Merge();
    CHECK(base::RenameFileX(repackedTmpMwm, m_geoObjectsTmpMwmPath), ());

    LOG(LINFO, (pointsEnrichedStat, "address points were enriched with outer building geomery"));
  }

  std::string m_geoObjectsTmpMwmPath;
  GeoObjectMaintainer & m_geoObjectMaintainer;
  unsigned int m_threadsCount;

  NullBuildingsInfo m_buildingsInfo;
  std::mutex m_addressPoints2buildingsMutex;
  std::mutex m_buildings2addressPointsMutex;

  BuildingsGeometries m_buildingsGeometries;
  std::mutex m_buildingsGeometriesMutex;
};

NullBuildingsInfo EnrichPointsWithOuterBuildingGeometry(GeoObjectMaintainer & geoObjectMaintainer,
                                                        std::string const & pathInGeoObjectsTmpMwm,
                                                        unsigned int threadsCount)
{
  auto && addressing =
      NullBuildingsAddressing{pathInGeoObjectsTmpMwm, geoObjectMaintainer, threadsCount};
  addressing.AddAddresses();
  return addressing.GetNullBuildingsInfo();
}

// PoisAddressEnricher -----------------------------------------------------------------------------
class PoisAddressEnricher
{
public:
  PoisAddressEnricher(std::string geoObjectKeyValuePath,
                      std::string const & geoObjectsTmpMwmPath,
                      std::string const & localityIndexedPoiIdsPath,
                      GeoObjectMaintainer & geoObjectMaintainer,
                      NullBuildingsInfo const & buildingsInfo,
                      unsigned int threadsCount)
    : m_geoObjectKeyValuePath{geoObjectKeyValuePath}
    , m_geoObjectsTmpMwmPath{geoObjectsTmpMwmPath}
    , m_localityIndexedPoiIdsPath{localityIndexedPoiIdsPath}
    , m_geoObjectMaintainer{geoObjectMaintainer}
    , m_buildingsInfo{buildingsInfo}
    , m_threadsCount{threadsCount}
  {
  }

  void AddAddresses()
  {
    auto poiIdsFilesMerger = FilesMerger(m_localityIndexedPoiIdsPath);
    auto && poisAddressEnrichedStat = std::atomic_size_t{0};
    feature::ProcessParallelFromDatRawFormat(m_threadsCount, m_geoObjectsTmpMwmPath, [&] {
      return Processor{*this, poiIdsFilesMerger, poisAddressEnrichedStat};
    });
    poiIdsFilesMerger.Merge();

    LOG(LINFO, ("Added", poisAddressEnrichedStat, "POIs enriched with address."));
  }

private:
  class Processor
  {
  public:
    Processor(PoisAddressEnricher & enricher, FilesMerger & poiIdsFilesMerger,
              std::atomic_size_t & poisAddressEnrichedStat)
      : m_goObjectsView{enricher.m_geoObjectMaintainer.CreateView()}
      , m_buildingsInfo{enricher.m_buildingsInfo}
      , m_kvWriter{enricher.m_geoObjectKeyValuePath}
      , m_poisAddressEnrichedStat{poisAddressEnrichedStat}
    {
      auto const poiIdsPath = GetPlatform().TmpPathForFile();
      m_localityIndexedPoiIds.open(poiIdsPath);
      if (!m_localityIndexedPoiIds.is_open())
        MYTHROW(Writer::OpenException, ("Failed to open", poiIdsPath));

      poiIdsFilesMerger.DeferMergeAndDelete(poiIdsPath);
    }

    void operator()(FeatureBuilder & fb, uint64_t /* currPos */)
    {
      if (!GeoObjectsFilter::IsPoi(fb))
        return;
      if (GeoObjectsFilter::IsBuilding(fb) || GeoObjectsFilter::HasHouse(fb))
        return;

      // No name and coordinates here, we will take it from fb in MakeJsonValueWithNameFromFeature
      auto house = FindHouse(fb);
      if (!house)
        return;

      WriteIntoKv(fb, house);

      auto const id = fb.GetMostGenericOsmId();
      m_localityIndexedPoiIds << id << "\n";
    }

  private:
    JsonValue FindHouse(FeatureBuilder const & fb)
    {
      base::JSONPtr house =
          m_goObjectsView.GetFullGeoObject(
              fb.GetKeyPoint(), [](GeoObjectMaintainer::GeoObjectData const & data)
          {
            return !data.m_house.empty();
          });
      if (house)
        return JsonValue{std::move(house)};

      auto && potentialIds = m_goObjectsView.SearchObjectsInIndex(fb.GetKeyPoint());

      auto const & buildings2AddressPoints = m_buildingsInfo.m_buildings2AddressPoints;
      for (base::GeoObjectId id : potentialIds)
      {
        auto const it = buildings2AddressPoints.find(id);
        if (it != buildings2AddressPoints.end())
          return JsonValue{m_goObjectsView.GetFullGeoObjectWithoutNameAndCoordinates(it->second)};
      }

      return JsonValue{};
    }

    JsonValue MakeJsonValueWithNameFromFeature(FeatureBuilder const & fb, JsonValue const & json)
    {
      auto jsonWithAddress = json.MakeDeepCopyJson();

      auto properties = json_object_get(jsonWithAddress.get(), "properties");
      Localizator localizator(*properties);
      localizator.SetLocale("name", Localizator::EasyObjectWithTranslation(fb.GetMultilangName()));

      UpdateCoordinates(fb.GetKeyPoint(), jsonWithAddress);
      return JsonValue{std::move(jsonWithAddress)};
    }

    void WriteIntoKv(FeatureBuilder & fb, JsonValue const & house)
    {
      auto const id = fb.GetMostGenericOsmId();
      auto jsonValue = MakeJsonValueWithNameFromFeature(fb, house);

      m_kvWriter.Write(id, JsonValue{std::move(jsonValue)});
      ++m_poisAddressEnrichedStat;
    }

    GeoObjectMaintainer::GeoObjectsView m_goObjectsView;
    NullBuildingsInfo const & m_buildingsInfo;
    KeyValueConcurrentWriter m_kvWriter;
    std::ofstream m_localityIndexedPoiIds;
    std::atomic_size_t & m_poisAddressEnrichedStat;
  };

  std::string m_geoObjectKeyValuePath;
  std::string m_geoObjectsTmpMwmPath;
  std::string m_localityIndexedPoiIdsPath;
  GeoObjectMaintainer & m_geoObjectMaintainer;
  NullBuildingsInfo const & m_buildingsInfo;
  unsigned int m_threadsCount;
};

void AddPoisEnrichedWithHouseAddresses(GeoObjectMaintainer & geoObjectMaintainer,
                                       NullBuildingsInfo const & buildingsInfo,
                                       std::string const & geoObjectKeyValuePath,
                                       std::string const & pathInGeoObjectsTmpMwm,
                                       std::string const & localityIndexedPoiIdsPath,
                                       bool /*verbose*/, unsigned int threadsCount)
{
  auto && poisAddressEnricher =
      PoisAddressEnricher{geoObjectKeyValuePath, pathInGeoObjectsTmpMwm, localityIndexedPoiIdsPath,
                          geoObjectMaintainer, buildingsInfo, threadsCount};
  poisAddressEnricher.AddAddresses();
}

//--------------------------------------------------------------------------------------------------
bool JsonHasBuilding(JsonValue const & json)
{
  auto && address =
      base::GetJSONObligatoryFieldByPath(json, "properties", "locales", "default", "address");

  auto && building = base::GetJSONOptionalField(address, "building");
  return building && !base::JSONIsNull(building);
}

boost::optional<indexer::GeoObjectsIndex<IndexReader>> MakeTempGeoObjectsIndex(
    std::string const & pathToGeoObjectsTmpMwm, unsigned int threadsCount)
{
  auto const indexFile = GetPlatform().TmpPathForFile();
  SCOPE_GUARD(removeIndexFile, std::bind(Platform::RemoveFileIfExists, std::cref(indexFile)));
  if (!GenerateGeoObjectsIndex(indexFile, pathToGeoObjectsTmpMwm, threadsCount))
  {
    LOG(LCRITICAL, ("Error generating geo objects index."));
    return {};
  }

  return indexer::ReadIndex<indexer::GeoObjectsIndexBox<IndexReader>, MmapReader>(indexFile);
}
}  // namespace geo_objects
}  // namespace generator

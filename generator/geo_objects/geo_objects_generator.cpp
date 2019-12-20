#include "generator/geo_objects/geo_objects_generator.hpp"

#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/timer.hpp"

#include <future>

namespace
{
template <class Activist>
auto Measure(std::string activity, Activist && activist)
{
  LOG(LINFO, ("Start", activity));
  auto timer = base::Timer();
  SCOPE_GUARD(_, [&]() { LOG(LINFO, ("Finish", activity, timer.ElapsedSeconds(), "seconds.")); });

  return activist();
}
}  // namespace

namespace generator
{
namespace geo_objects
{

GeoObjectsGenerator::GeoObjectsGenerator(
    RegionInfoLocater && regionInfoLocater,
    GeoObjectMaintainer::RegionIdGetter && regionIdGetter, std::string pathInGeoObjectsTmpMwm,
    std::string pathOutIdsWithoutAddress, std::string pathOutGeoObjectsKv, bool verbose,
    unsigned int threadsCount)
  : m_pathInGeoObjectsTmpMwm(std::move(pathInGeoObjectsTmpMwm))
  , m_pathOutPoiIdsToAddToCoveringIndex(std::move(pathOutIdsWithoutAddress))
  , m_pathOutGeoObjectsKv(std::move(pathOutGeoObjectsKv))
  , m_verbose(verbose)
  , m_threadsCount(threadsCount)
  , m_geoObjectMaintainer{std::move(regionIdGetter)}
  , m_regionInfoLocater{std::move(regionInfoLocater)}
{
}

bool GeoObjectsGenerator::GenerateGeoObjects()
{
  return Measure("generating geo objects", [&]() { return GenerateGeoObjectsPrivate(); });
}

bool GeoObjectsGenerator::GenerateGeoObjectsPrivate()
{
  // Index buidling requires a lot of memory (~140GB).
  // Build index when there is a lot of memory,
  // before AddBuildingsAndThingsWithHousesThenEnrichAllWithRegionAddresses().
  auto geoObjectIndex = MakeTempGeoObjectsIndex(m_pathInGeoObjectsTmpMwm, m_threadsCount);
  if (!geoObjectIndex)
    return false;
  LOG(LINFO, ("Index was built."));
  m_geoObjectMaintainer.SetIndex(std::move(*geoObjectIndex));

  AddBuildingsAndThingsWithHousesThenEnrichAllWithRegionAddresses(
      m_pathOutGeoObjectsKv, m_geoObjectMaintainer, m_pathInGeoObjectsTmpMwm, m_regionInfoLocater,
      m_verbose, m_threadsCount);
  LOG(LINFO, ("Geo objects with addresses were built."));

  LOG(LINFO, ("Enrich address points with outer null building geometry."));
  NullBuildingsInfo const & buildingInfo = EnrichPointsWithOuterBuildingGeometry(
      m_geoObjectMaintainer, m_pathInGeoObjectsTmpMwm, m_threadsCount);

  std::ofstream streamPoiIdsToAddToCoveringIndex(m_pathOutPoiIdsToAddToCoveringIndex);

  AddPoisEnrichedWithHouseAddresses(
      m_geoObjectMaintainer, buildingInfo, m_pathOutGeoObjectsKv, m_pathInGeoObjectsTmpMwm,
      streamPoiIdsToAddToCoveringIndex, m_verbose, m_threadsCount);

  FilterAddresslessThanGaveTheirGeometryToInnerPoints(m_pathInGeoObjectsTmpMwm, buildingInfo,
                                                      m_threadsCount);
  LOG(LINFO, ("Addressless buildings with geometry we used for inner points were filtered"));

  LOG(LINFO, ("Geo objects without addresses were built."));
  LOG(LINFO, ("Geo objects key-value storage saved to", m_pathOutGeoObjectsKv));
  LOG(LINFO, ("Ids of POIs without addresses saved to", m_pathOutPoiIdsToAddToCoveringIndex));
  return true;
}

bool GenerateGeoObjects(std::string const & regionsIndex, std::string const & regionsKeyValue,
                        std::string const & geoObjectsFeatures,
                        std::string const & nodesListToIndex, std::string const & geoObjectKeyValue,
                        bool verbose, unsigned int threadsCount)

{
  auto regionInfoGetter = regions::RegionInfoGetter(regionsIndex, regionsKeyValue);
  LOG(LINFO, ("Size of regions key-value storage:", regionInfoGetter.GetStorage().Size()));

  auto findDeepest = [&regionInfoGetter](auto && point) {
    return regionInfoGetter.FindDeepest(point);
  };
  auto keyValueFind = [&regionInfoGetter](auto && id) {
    return regionInfoGetter.GetStorage().Find(id.GetEncodedId());
  };

  geo_objects::GeoObjectsGenerator geoObjectsGenerator{std::move(findDeepest),
                                                       std::move(keyValueFind),
                                                       geoObjectsFeatures,
                                                       nodesListToIndex,
                                                       geoObjectKeyValue,
                                                       verbose,
                                                       threadsCount};

  return geoObjectsGenerator.GenerateGeoObjects();
}
}  // namespace geo_objects
}  // namespace generator

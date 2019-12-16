#pragma once

#include "generator/key_value_storage.hpp"

#include "generator/geo_objects/geo_objects_maintainer.hpp"

#include "geometry/meter.hpp"
#include "geometry/point2d.hpp"

#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/timer.hpp"

#include "platform/platform.hpp"

#include <string>

#include <boost/optional.hpp>

namespace generator
{
namespace geo_objects
{

using IndexReader = ReaderPtr<Reader>;
using RegionInfoLocater = std::function<boost::optional<KeyValue>(m2::PointD const & pathPoint)>;

boost::optional<indexer::GeoObjectsIndex<IndexReader>> MakeTempGeoObjectsIndex(
    std::string const & pathToGeoObjectsTmpMwm, unsigned int threadsCount);

bool JsonHasBuilding(JsonValue const & json);

void AddBuildingsAndThingsWithHousesThenEnrichAllWithRegionAddresses(
    std::string const & geoObjectKeyValuePath, GeoObjectMaintainer & geoObjectMaintainer,
    std::string const & pathInGeoObjectsTmpMwm, RegionInfoLocater const & regionInfoLocater,
    bool verbose, unsigned int threadsCount);

struct NullBuildingsInfo
{
  std::unordered_map<base::GeoObjectId, base::GeoObjectId> m_addressPoints2Buildings;
  // Quite possible to have many points for one building. We want to use
  // their addresses for POIs according to buildings and have no idea how to distinguish between
  // them, so take one random
  std::unordered_map<base::GeoObjectId, base::GeoObjectId> m_buildings2AddressPoints;
};

NullBuildingsInfo EnrichPointsWithOuterBuildingGeometry(
    GeoObjectMaintainer & geoObjectMaintainer, std::string const & pathInGeoObjectsTmpMwm,
    unsigned int threadsCount);

void AddPoisEnrichedWithHouseAddresses(GeoObjectMaintainer & geoObjectMaintainer,
                                       NullBuildingsInfo const & buildingsInfo,
                                       std::string const & geoObjectKeyValuePath,
                                       std::string const & pathInGeoObjectsTmpMwm,
                                       std::ostream & streamPoiIdsToAddToCoveringIndex,
                                       bool verbose, unsigned int threadsCount);
}  // namespace geo_objects
}  // namespace generator

#pragma once

#include "generator/key_value_storage.hpp"

#include "generator/regions/region_info_getter.hpp"

#include "generator/feature_builder.hpp"

#include "indexer/covering_index.hpp"

#include "coding/reader.hpp"

#include "geometry/point2d.hpp"

#include "base/geo_object_id.hpp"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <boost/optional.hpp>

#include "3party/jansson/myjansson.hpp"

namespace generator
{
namespace geo_objects
{
void UpdateCoordinates(m2::PointD const & point, base::JSONPtr & json);
base::JSONPtr AddAddress(std::string const & street, std::string const & house, m2::PointD point,
                         StringUtf8Multilang const & name, KeyValue const & regionKeyValue);

class GeoObjectMaintainer
{
public:
  using RegionIdGetter = std::function<std::shared_ptr<JsonValue>(base::GeoObjectId id)>;

  struct GeoObjectData
  {
    std::string m_street;
    std::string m_house;
    base::GeoObjectId m_regionId;
  };

  using GeoId2GeoData = std::unordered_map<base::GeoObjectId, GeoObjectData>;
  using GeoIndex = indexer::GeoObjectsIndex<ReaderPtr<Reader>>;

  class GeoObjectsView
  {
  public:
    GeoObjectsView(GeoIndex const & geoIndex, GeoId2GeoData const & geoId2GeoData,
                   RegionIdGetter const & regionIdGetter)
      : m_geoIndex(geoIndex)
      , m_geoId2GeoData(geoId2GeoData)
      , m_regionIdGetter(regionIdGetter)
    {
    }
    boost::optional<base::GeoObjectId> SearchIdOfFirstMatchedObject(
        m2::PointD const & point, std::function<bool(base::GeoObjectId)> && pred) const;

    boost::optional<GeoObjectData> GetGeoData(base::GeoObjectId id) const;

    std::vector<base::GeoObjectId> SearchObjectsInIndex(m2::PointD const & point) const
    {
      return SearchGeoObjectIdsByPoint(m_geoIndex, point);
    }

    base::JSONPtr GetFullGeoObjectWithoutNameAndCoordinates(base::GeoObjectId id) const;

    base::JSONPtr GetFullGeoObject(
        m2::PointD point,
        std::function<bool(GeoObjectMaintainer::GeoObjectData const &)> && pred) const;

    static std::vector<base::GeoObjectId> SearchGeoObjectIdsByPoint(GeoIndex const & index,
                                                                    m2::PointD point);

  private:
    GeoIndex const & m_geoIndex;
    GeoId2GeoData const & m_geoId2GeoData;
    RegionIdGetter const & m_regionIdGetter;
  };

  GeoObjectMaintainer(RegionIdGetter && regionIdGetter);

  void SetIndex(GeoIndex && index) { m_index = std::move(index); }

  void SetGeoData(GeoId2GeoData && geoId2GeoData) { m_geoId2GeoData = std::move(geoId2GeoData); }

  size_t Size() const { return m_geoId2GeoData.size(); }

  GeoObjectsView CreateView()
  {
    return GeoObjectsView(m_index, m_geoId2GeoData, m_regionIdGetter);
  }

private:
  GeoIndex m_index;
  RegionIdGetter m_regionIdGetter;
  GeoId2GeoData m_geoId2GeoData;
};
}  // namespace geo_objects
}  // namespace generator

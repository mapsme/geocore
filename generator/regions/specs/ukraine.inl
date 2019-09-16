#include "generator/regions/collector_region_info.hpp"
#include "generator/regions/country_specifier.hpp"
#include "generator/regions/country_specifier_builder.hpp"
#include "generator/regions/region.hpp"
#include "generator/regions/region_base.hpp"

#include "geometry/mercator.hpp"

#include "base/logging.hpp"

#include <string>
#include <vector>

#include <boost/geometry.hpp>

namespace generator
{
namespace regions
{
namespace specs
{
class UkraineSpecifier final : public CountrySpecifier
{
public:
  static std::vector<std::string> GetCountryNames() { return {"Ukraine"}; }

  void RectifyBoundary(std::vector<Region> & outers, std::vector<Region> const & planet) override
  {
    FixAdministrativeRegion1(outers, planet);
    FixAdministrativeRegion2(outers, planet);
  }

private:
  // CountrySpecifier overrides:
  PlaceLevel GetSpecificCountryLevel(Region const & region) const override
  {
    AdminLevel adminLevel = region.GetAdminLevel();
    switch (adminLevel)
    {
    case AdminLevel::Four: return PlaceLevel::Region;    // Oblasts
    case AdminLevel::Six: return PlaceLevel::Subregion;  // районы в областях
    case AdminLevel::Seven: return PlaceLevel::Sublocality;  // Административные районы в городах
    default: break;
    }

    return PlaceLevel::Unknown;
  }

  void FixAdministrativeRegion1(std::vector<Region> & outers, std::vector<Region> const & planet)
  {
    auto const labelPoint = ms::LatLon({45.1890034, 34.7401104});
    auto region = FindCorrectingAdministrativeRegion(planet, {"Республика Крым", "Крым"},
                                                     labelPoint);
    if (!region)
    {
      LOG(LWARNING, ("Failed to fix region1 for Ukraine"));
      return;
    }

    ExcludeRegionArea(outers, *region);
  }

  void FixAdministrativeRegion2(std::vector<Region> & outers, std::vector<Region> const & planet)
  {
    auto const labelPoint = ms::LatLon({44.5547288, 33.4720239});
    auto region = FindCorrectingAdministrativeRegion(planet, {"Севастополь"}, labelPoint);
    if (!region)
    {
      LOG(LWARNING, ("Failed to fix region2 for Ukraine"));
      return;
    }

    ExcludeRegionArea(outers, *region);
  }

  boost::optional<Region> FindCorrectingAdministrativeRegion(std::vector<Region> const & planet,
      std::vector<std::string> const & multiName, ms::LatLon coveredPoint)
  {
    auto const checkPoint = MercatorBounds::FromLatLon(coveredPoint);
    for (auto const & region : planet)
    {
      if (region.GetAdminLevel() == AdminLevel::Unknown)
        continue;

      auto && isoCode = region.GetIsoCode();
      if (!isoCode || *isoCode != "RU")
        continue;

      if (std::count(multiName.begin(), multiName.end(), region.GetName()) &&
          region.Contains({checkPoint.x, checkPoint.y}))
      {
        return {region};
      }
    }

    return {};
  }
};

REGISTER_COUNTRY_SPECIFIER(UkraineSpecifier);
}  // namespace specs
}  // namespace regions
}  // namespace generator

#pragma once

#include "generator/regions/country_specifier.hpp"
#include "generator/regions/level_region.hpp"
#include "generator/regions/node.hpp"
#include "generator/regions/region.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/optional.hpp>

namespace generator
{
namespace regions
{
// This class is needed to build a hierarchy of regions. We can have several nodes for a region
// with the same name, represented by a multi-polygon (several polygons).
class RegionsBuilder
{
public:
  using Regions = std::vector<Region>;
  using StringsList = std::vector<std::string>;
  using CountryFn = std::function<void(std::string const &, Node::PtrList const &)>;

  explicit RegionsBuilder(Regions && regions, PlacePointsMap && placePointsMap,
                          unsigned int threadsCount = 1);

  Regions const & GetCountriesOuters() const;
  StringsList GetCountryInternationalNames() const;
  void ForEachCountry(CountryFn fn);

  static void InsertIntoSubtree(Node::Ptr & subtree, LevelRegion && region,
                                CountrySpecifier const & countrySpecifier);
  // Return: 0 - no relation, 1 - |l| contains |r|, -1 - |r| contains |l|.
  static int CompareAffiliation(LevelRegion const & l, LevelRegion const & r,
                                CountrySpecifier const & countrySpecifier);
  static bool IsAreaLessRely(Region const & l, Region const & r);

private:
  static constexpr double kAreaRelativeErrorPercent = 0.1;

  void MoveLabelPlacePoints(PlacePointsMap & placePointsMap, Regions & regions);
  Regions FormRegionsInAreaOrder(Regions && regions);
  Regions ExtractCountriesOuters(Regions & regions);
  Node::PtrList BuildCountry(std::string const & countryName) const;
  boost::optional<std::string> FindCountryCode(Regions const & outers) const;
  static std::string const & GetCountryCode(std::string const & isoCode);
  Node::PtrList BuildCountryRegionTrees(Regions const & outers,
                                        boost::optional<std::string> const & countryCode,
                                        CountrySpecifier const & countrySpecifier) const;
  Node::Ptr BuildCountryRegionTree(Region const & outer,
                                   boost::optional<std::string> const & countryCode,
                                   CountrySpecifier const & countrySpecifier) const;
  std::vector<Node::Ptr> MakeCountryNodesInAreaOrder(
      Region const & countryOuter, Regions const & regionsInAreaOrder,
      boost::optional<std::string> const & countryCode,
      CountrySpecifier const & countrySpecifier) const;
  Node::Ptr ChooseParent(std::vector<Node::Ptr> const & nodesInAreaOrder,
                         std::vector<Node::Ptr>::const_reverse_iterator forItem,
                         CountrySpecifier const & countrySpecifier) const;
  std::vector<Node::Ptr>::const_reverse_iterator FindAreaLowerBoundRely(
      std::vector<Node::Ptr> const & nodesInAreaOrder,
      std::vector<Node::Ptr>::const_reverse_iterator forItem) const;
  static void InsertIntoSubtree(Node::Ptr & subtree, Node::Ptr && newNode,
                                CountrySpecifier const & countrySpecifier);

  Regions m_countriesOuters;
  Regions m_regionsInAreaOrder;
  PlacePointsMap m_placePointsMap;
  unsigned int m_threadsCount;
};
}  // namespace regions
}  // namespace generator

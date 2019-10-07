#include "generator/regions/regions_builder.hpp"

#include "generator/regions/admin_suburbs_marker.hpp"
#include "generator/regions/country_specifier_builder.hpp"
#include "generator/regions/place_points_integrator.hpp"

#include "base/assert.hpp"
#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"
#include "base/thread_pool_computational.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <numeric>
#include <queue>
#include <thread>
#include <unordered_set>

namespace generator
{
namespace regions
{
RegionsBuilder::RegionsBuilder(Regions && regions, PlacePointsMap && placePointsMap,
                               size_t threadsCount)
  : m_threadsCount(threadsCount)
{
  ASSERT(m_threadsCount != 0, ());

  std::erase_if(placePointsMap, [](auto const & item) {
    return strings::IsASCIINumeric(item.second.GetName());
  });

  MoveLabelPlacePoints(placePointsMap, regions);

  m_regionsInAreaOrder = FormRegionsInAreaOrder(std::move(regions));
  m_countriesOuters = ExtractCountriesOuters(m_regionsInAreaOrder);
  m_placePointsMap = std::move(placePointsMap);
}

void RegionsBuilder::MoveLabelPlacePoints(PlacePointsMap & placePointsMap, Regions & regions)
{
  for (auto & region : regions)
  {
    if (auto labelOsmId = region.GetLabelOsmId())
    {
      auto label = placePointsMap.find(*labelOsmId);
      if (label == placePointsMap.end())
        continue;

      if (label->second.GetPlaceType() == PlaceType::Country &&
          region.GetAdminLevel() != AdminLevel::Two)
      {
        continue;
      }

      region.SetLabel(label->second);
    }
  }

  for (auto & region : regions)
  {
    if (auto const & label = region.GetLabel())
      placePointsMap.erase(label->GetId());
  }
}

RegionsBuilder::Regions RegionsBuilder::FormRegionsInAreaOrder(Regions && regions)
{
  auto const cmp = [](Region const & l, Region const & r) { return l.GetArea() > r.GetArea(); };
  std::sort(std::begin(regions), std::end(regions), cmp);
  return std::move(regions);
}

RegionsBuilder::Regions RegionsBuilder::ExtractCountriesOuters(Regions & regions)
{
  Regions countriesOuters;

  auto const isCountry = [](Region const & region) {
    auto const placeType = region.GetPlaceType();
    if (placeType == PlaceType::Country)
      return true;

    auto const adminLevel = region.GetAdminLevel();
    return adminLevel == AdminLevel::Two && placeType == PlaceType::Unknown;
  };
  std::copy_if(std::begin(regions), std::end(regions), std::back_inserter(countriesOuters),
               isCountry);

  base::EraseIf(regions, isCountry);

  return countriesOuters;
}

RegionsBuilder::Regions const & RegionsBuilder::GetCountriesOuters() const
{
  return m_countriesOuters;
}

RegionsBuilder::StringsList RegionsBuilder::GetCountryInternationalNames() const
{
  StringsList result;
  std::unordered_set<std::string> set;
  for (auto const & c : GetCountriesOuters())
  {
    auto const & name = c.GetInternationalName();
    if (set.insert(name).second)
      result.emplace_back(std::move(name));
  }

  return result;
}

Node::Ptr RegionsBuilder::BuildCountryRegionTree(
    Region const & outer,
    boost::optional<std::string> const & countryCode,
    CountrySpecifier const & countrySpecifier) const
{
  auto nodes = MakeCountryNodesInAreaOrder(outer, m_regionsInAreaOrder, countryCode,
                                           countrySpecifier);

  for (auto i = std::crbegin(nodes), end = std::crend(nodes); i != end; ++i)
  {
    if (auto parent = ChooseParent(nodes, i, countrySpecifier))
    {
      (*i)->SetParent(parent);
      parent->AddChild(*i);
    }
  }

  return nodes.front();
}

std::vector<Node::Ptr> RegionsBuilder::MakeCountryNodesInAreaOrder(
    Region const & countryOuter, Regions const & regionsInAreaOrder,
    boost::optional<std::string> const & countryCode,
    CountrySpecifier const & countrySpecifier) const
{
  std::vector<Node::Ptr> nodes{
      std::make_shared<Node>(LevelRegion{PlaceLevel::Country, countryOuter})};
  for (auto const & region : regionsInAreaOrder)
  {
    if (countryOuter.ContainsRect(region))
    {
      auto && regionIsoCode = region.GetIsoCode();
      if (regionIsoCode && countryCode && GetCountryCode(*regionIsoCode) != *countryCode)
        continue;

      auto level = strings::IsASCIINumeric(region.GetName()) ? PlaceLevel::Unknown
                                                             : countrySpecifier.GetLevel(region);
      auto node = std::make_shared<Node>(LevelRegion{level, region});
      nodes.emplace_back(std::move(node));
    }
  }

  return nodes;
}

Node::Ptr RegionsBuilder::ChooseParent(std::vector<Node::Ptr> const & nodesInAreaOrder,
                                       std::vector<Node::Ptr>::const_reverse_iterator forItem,
                                       CountrySpecifier const & countrySpecifier) const
{
  auto const & node = *forItem;
  auto const & region = node->GetData();

  auto const from = FindAreaLowerBoundRely(nodesInAreaOrder, forItem);
  CHECK(from <= forItem, ());

  Node::Ptr parent;
  for (auto i = from, end = std::crend(nodesInAreaOrder); i != end; ++i)
  {
    auto const & candidate = *i;
    auto const & candidateRegion = candidate->GetData();

    if (parent)
    {
      auto const & parentRegion = parent->GetData();
      if (IsAreaLessRely(parentRegion, candidateRegion))
        break;
    }

    if (!candidateRegion.ContainsRect(region) && !candidateRegion.Contains(region.GetCenter()))
      continue;

    if (i == forItem)
      continue;

    auto const c = CompareAffiliation(candidateRegion, region, countrySpecifier);
    if (c == 1)
    {
      if (parent && 0 <= CompareAffiliation(candidateRegion, parent->GetData(), countrySpecifier))
        continue;

      parent = candidate;
    }
  }

  CHECK(!parent || -1 == CompareAffiliation(region, parent->GetData(), countrySpecifier),
        (GetRegionNotation(region), GetRegionNotation(parent->GetData())));
  return parent;
}

std::vector<Node::Ptr>::const_reverse_iterator RegionsBuilder::FindAreaLowerBoundRely(
    std::vector<Node::Ptr> const & nodesInAreaOrder,
    std::vector<Node::Ptr>::const_reverse_iterator forItem) const
{
  auto const & region = (*forItem)->GetData();

  auto areaLessRely = [](Node::Ptr const & element, Region const & region) {
    auto const & elementRegion = element->GetData();
    return IsAreaLessRely(elementRegion, region);
  };

  return std::lower_bound(std::crbegin(nodesInAreaOrder), forItem, region, areaLessRely);
}

// static
void RegionsBuilder::InsertIntoSubtree(Node::Ptr & subtree, LevelRegion && region,
                                       CountrySpecifier const & countrySpecifier)
{
  auto newNode = std::make_shared<Node>(std::move(region));
  InsertIntoSubtree(subtree, std::move(newNode), countrySpecifier);
}

// static
void RegionsBuilder::InsertIntoSubtree(Node::Ptr & subtree, Node::Ptr && newNode,
                                       CountrySpecifier const & countrySpecifier)
{
  CHECK(0 < CompareAffiliation(subtree->GetData(), newNode->GetData(), countrySpecifier), ());

  auto & children = subtree->GetChildren();
  auto childIt = children.begin();
  while (childIt != children.end())
  {
    auto & child = *childIt;
    auto const c = CompareAffiliation(child->GetData(), newNode->GetData(), countrySpecifier);
    if (c > 0)
      return InsertIntoSubtree(child, std::move(newNode), countrySpecifier);

    if (c < 0)
    {
      child->SetParent(newNode);
      newNode->AddChild(child);
      childIt = children.erase(childIt);
      continue;
    }

    ASSERT(c == 0, ());
    ++childIt;
  }

  newNode->SetParent(subtree);
  subtree->AddChild(newNode);
}

// static
int RegionsBuilder::CompareAffiliation(LevelRegion const & l, LevelRegion const & r,
                                       CountrySpecifier const & countrySpecifier)
{
  if (IsAreaLessRely(r, l) && l.Contains(r))
    return 1;
  if (IsAreaLessRely(l, r) && r.Contains(l))
    return -1;

  if (l.CalculateOverlapPercentage(r) < 50.0)
    return 0;

  auto const lArea = l.GetArea();
  auto const rArea = r.GetArea();
  if (0.5 * lArea > rArea)
  {
    ASSERT_GREATER(0.5 * lArea, 0, ());
    LOG(LDEBUG, ("Region", l.GetId(), GetRegionNotation(l), "contains partly", r.GetId(),
                 GetRegionNotation(r)));
    return 1;
  }
  if (0.5 * rArea > lArea)
  {
    ASSERT_GREATER(0.5 * rArea, 0, ());
    LOG(LDEBUG, ("Region", r.GetId(), GetRegionNotation(r), "contains partly", l.GetId(),
                 GetRegionNotation(l)));
    return -1;
  }

  return countrySpecifier.RelateByWeight(l, r);
}

// static
bool RegionsBuilder::IsAreaLessRely(Region const & l, Region const & r)
{
  constexpr auto lAreaRation = 1. + kAreaRelativeErrorPercent / 100.;
  return lAreaRation * l.GetArea() < r.GetArea();
}

void RegionsBuilder::ForEachCountry(CountryFn fn)
{
  std::vector<std::future<Node::PtrList>> buildingTasks;

  {
    base::thread_pool::computational::ThreadPool threadPool(m_threadsCount);

    for (auto const & countryName : GetCountryInternationalNames())
    {
      auto result = threadPool.Submit([this, countryName]() { return BuildCountry(countryName); });
      buildingTasks.emplace_back(std::move(result));
    }
  }

  for (auto && task : buildingTasks)
  {
    auto countryTrees = task.get();
    CHECK(!countryTrees.empty(), ());
    auto && countryName = countryTrees.front()->GetData().GetInternationalName();
    fn(countryName, countryTrees);
  }
}

Node::PtrList RegionsBuilder::BuildCountry(std::string const & countryName) const
{
  auto countrySpecifier = CountrySpecifierBuilder::GetInstance().MakeCountrySpecifier(countryName);

  Regions outers;
  auto const & countries = GetCountriesOuters();
  auto const pred = [&](Region const & country) {
    return countryName == country.GetInternationalName();
  };
  std::copy_if(std::begin(countries), std::end(countries), std::back_inserter(outers), pred);

  countrySpecifier->RectifyBoundary(outers, m_regionsInAreaOrder);

  auto countryCode = FindCountryCode(outers);
  auto countryTrees = BuildCountryRegionTrees(outers, countryCode, *countrySpecifier);

  PlacePointsIntegrator pointsIntegrator{m_placePointsMap, *countrySpecifier};
  LOG(LINFO, ("Start integrate place points for", countryName));
  pointsIntegrator.ApplyTo(countryTrees);
  LOG(LINFO, ("Finish integrate place points for", countryName));

  AdminSuburbsMarker suburbsMarker;
  LOG(LINFO, ("Start mark admin suburbs for", countryName));
  for (auto & tree : countryTrees)
    suburbsMarker.MarkSuburbs(tree);
  LOG(LINFO, ("Finish mark admin suburbs for", countryName));

  countrySpecifier->AdjustRegionsLevel(countryTrees);

  return countryTrees;
}

boost::optional<std::string> RegionsBuilder::FindCountryCode(Regions const & outers) const
{
  for (auto const & outer : outers)
  {
    if (auto isoCode = outer.GetIsoCode())
      return {GetCountryCode(*isoCode)};
  }
  return {};
}

// static
std::string const & RegionsBuilder::GetCountryCode(std::string const & isoCode)
{
  static auto iso2SovereignIso = std::unordered_map<std::string, std::string>{
    {"AX", "FI"},
    {"AS", "US"},
    {"AI", "GB"},
    {"AW", "NL"},
    {"BM", "GB"},
    {"BQ", "NL"},
    {"BV", "NO"},
    {"IO", "GB"},
    {"KY", "GB"},
    {"CX", "AU"},
    {"CC", "AU"},
    {"CK", "NZ"},
    {"CW", "NZ"},
    {"FK", "GB"},
    {"GF", "FR"},
    {"PF", "FR"},
    {"TF", "FR"},
    {"GI", "GB"},
    {"GL", "DK"},
    {"GP", "FR"},
    {"GU", "US"},
    {"HM", "AU"},
    {"HK", "CH"},
    {"MO", "CH"},
    {"MQ", "FR"},
    {"YT", "FR"},
    {"MS", "GB"},
    {"NC", "FR"},
    {"NU", "NZ"},
    {"NF", "AU"},
    {"MO", "US"},
    {"PN", "GB"},
    {"PR", "US"},
    {"RE", "FR"},
    {"BL", "FR"},
    {"SH", "GB"},
    {"MF", "FR"},
    {"PM", "FR"},
    {"SX", "NL"},
    {"GS", "GB"},
    {"SJ", "NO"},
    {"TK", "NZ"},
    {"TC", "GB"},
    {"UM", "US"},
    {"VG", "GB"},
    {"VI", "US"},
    {"WF", "FR"},
  };

  auto sovereignIso = iso2SovereignIso.find(isoCode);
  if (sovereignIso != iso2SovereignIso.end())
    return sovereignIso->second;

  return isoCode;
}

Node::PtrList RegionsBuilder::BuildCountryRegionTrees(
    Regions const & outers,
    boost::optional<std::string> const & countryCode,
    CountrySpecifier const & countrySpecifier) const
{
  Node::PtrList trees;
  for (auto const & outer : outers)
  {
    auto tree = BuildCountryRegionTree(outer, countryCode, countrySpecifier);
    trees.push_back(std::move(tree));
  }

  return trees;
}
}  // namespace regions
}  // namespace generator

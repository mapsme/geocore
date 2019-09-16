#include "generator/filter_world.hpp"

#include "indexer/categories_holder.hpp"
#include "indexer/classificator.hpp"
#include "indexer/scales.hpp"

#include <algorithm>

namespace generator
{
std::shared_ptr<FilterInterface> FilterWorld::Clone() const
{
  return std::make_shared<FilterWorld>();
}

bool FilterWorld::IsAccepted(feature::FeatureBuilder const & fb)
{
  return IsGoodScale(fb) || IsInternationalAirport(fb);
}

// static
bool FilterWorld::IsInternationalAirport(feature::FeatureBuilder const & fb)
{
  auto static const kAirport = classif().GetTypeByPath({"aeroway", "aerodrome", "international"});
  return fb.HasType(kAirport);
}

// static
bool FilterWorld::IsGoodScale(feature::FeatureBuilder const & fb)
{
  // GetMinFeatureDrawScale also checks suitable size for AREA features
  return scales::GetUpperWorldScale() >= fb.GetMinFeatureDrawScale();
}
}  // namespace generator

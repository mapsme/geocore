#include "generator/translator_region.hpp"

#include "generator/feature_maker.hpp"
#include "generator/filter_interface.hpp"
#include "generator/generate_info.hpp"
#include "generator/intermediate_data.hpp"
#include "generator/osm_element.hpp"
#include "generator/osm_element_helpers.hpp"
#include "generator/regions/collector_region_info.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <string>

using namespace feature;

namespace generator
{
// TranslatorRegion --------------------------------------------------------------------------------
TranslatorRegion::TranslatorRegion(std::shared_ptr<FeatureProcessorInterface> const & processor,
                                   std::shared_ptr<cache::IntermediateData const> const & cache,
                                   std::string const & regionsInfoPath)
  : Translator(processor, cache, std::make_shared<FeatureMakerSimple>(cache))

{
  SetFilter(std::make_shared<FilterRegions>());
  SetCollector(std::make_shared<regions::CollectorRegionInfo>(regionsInfoPath));
}

std::shared_ptr<TranslatorInterface>
TranslatorRegion::Clone() const
{
  return Translator::CloneBase<TranslatorRegion>();
}

void TranslatorRegion::Merge(TranslatorInterface const & other)
{
  other.MergeInto(*this);
}

void TranslatorRegion::MergeInto(TranslatorRegion & other) const
{
  MergeIntoBase(other);
}

// FilterRegions ----------------------------------------------------------------------------------
std::shared_ptr<FilterInterface> FilterRegions::Clone() const
{
  return std::make_shared<FilterRegions>();
}

bool FilterRegions::IsAccepted(OsmElement const & element)
{
  for (auto const & t : element.Tags())
  {
    if (t.m_key == "place" && regions::EncodePlaceType(t.m_value) != regions::PlaceType::Unknown)
      return true;
    if (t.m_key == "place:PH" && (t.m_value == "district" || t.m_value == "barangay"))
      return true;

    if (t.m_key == "boundary" && t.m_value == "administrative")
    {
      if (IsEnclaveBoundaryWay(element))
        return false;

      return true;
    }
  }

  return false;
}

bool FilterRegions::IsAccepted(FeatureBuilder const & feature)
{
  return feature.GetParams().IsValid() && !feature.IsLine();
}

bool FilterRegions::IsEnclaveBoundaryWay(OsmElement const & element) const
{
  if (!element.IsWay() || !IsGeometryClosed(element))
    return false;

  if (!element.HasTag("admin_level", "2") || !element.HasTag("boundary", "administrative"))
    return false;

  return !element.HasTag("type", "boundary") && !element.HasTag("type", "multipolygon");
}

bool FilterRegions::IsGeometryClosed(OsmElement const & element) const
{
  auto const & nodes = element.Nodes();
  return nodes.size() >= 3 && nodes.front() == nodes.back();
}
}  // namespace generator

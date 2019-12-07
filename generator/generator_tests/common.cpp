#include "generator/generator_tests/common.hpp"

#include "generator/feature_generator.hpp"
#include "generator/osm2type.hpp"

#include "indexer/classificator.hpp"
#include "indexer/classificator_loader.hpp"

#include "platform/platform.hpp"

#include "base/file_name_utils.hpp"
#include "base/string_utils.hpp"

#include <fstream>

#include "defines.hpp"

namespace generator_tests
{
OsmElement MakeOsmElement(uint64_t id, Tags const & tags, OsmElement::EntityType t)
{
  OsmElement el;
  el.m_id = id;
  el.m_type = t;
  for (auto const & t : tags)
    el.AddTag(t.first, t.second);

  return el;
}

std::string GetFileName(std::string const & filename)
{
  auto & platform = GetPlatform();
  return filename.empty() ? platform.TmpPathForFile() : platform.TmpPathForFile(filename);
}

OsmElement MakeOsmElement(OsmElementData const & elementData)
{
  OsmElement el;
  el.m_id = elementData.m_id;

  if (!el.m_members.empty())
    el.m_type = OsmElement::EntityType::Relation;
  if (elementData.m_points.size() == 1)
    el.m_type = OsmElement::EntityType::Node;
  else
    el.m_type = OsmElement::EntityType::Way;

  for (auto const & tag : elementData.m_tags)
    el.AddTag(tag.m_key, tag.m_value);
  el.m_members = elementData.m_members;

  return el;
}

feature::FeatureBuilder FeatureBuilderFromOmsElementData(OsmElementData const & elementData)
{
  auto el = MakeOsmElement(elementData);
  feature::FeatureBuilder fb;
  CHECK_GREATER_OR_EQUAL(elementData.m_points.size(), 1, ());
  if (elementData.m_points.size() == 1)
  {
    fb.SetCenter(elementData.m_points[0]);
  }
  else if (elementData.m_points.front() == elementData.m_points.back())
  {
    auto poly = elementData.m_points;
    fb.AddPolygon(poly);
    fb.SetHoles({});
    fb.SetArea();
  }
  else
  {
    fb.SetLinear();
    for (auto const & point : elementData.m_points)
      fb.AddPoint(point);
  }

  using base::GeoObjectId;
  auto osmIdType = GeoObjectId::Type{};
  switch (el.m_type)
  {
  case OsmElement::EntityType::Node:
    osmIdType = GeoObjectId::Type::ObsoleteOsmNode;
    break;
  case OsmElement::EntityType::Way:
    osmIdType = GeoObjectId::Type::ObsoleteOsmWay;
    break;
  case OsmElement::EntityType::Relation:
    osmIdType = GeoObjectId::Type::ObsoleteOsmRelation;
    break;
  default:
    UNREACHABLE();
  }
  auto osmId = GeoObjectId{osmIdType, el.m_id};
  fb.SetOsmId(osmId);

  ftype::GetNameAndType(&el, fb.GetParams(),
                        [](uint32_t type) { return classif().IsTypeValid(type); });
  return fb;
}

void WriteFeatures(std::vector<OsmElementData> const & osmElements, ScopedFile const & featuresFile)
{
  classificator::Load();

  feature::FeaturesCollector collector(featuresFile.GetFullPath());
  for (auto const & elementData : osmElements)
  {
    auto fb = FeatureBuilderFromOmsElementData(elementData);
    collector.Collect(fb);
  }
  collector.Finish();
}
}  // namespace generator_tests

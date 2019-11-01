#pragma once
#include "generator/feature_builder.hpp"
#include "generator/osm_element.hpp"

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"

#include "platform/platform_tests_support/scoped_dir.hpp"
#include "platform/platform_tests_support/scoped_file.hpp"

#include <cstdint>
#include <string>

namespace generator_tests
{
using Tags = std::vector<std::pair<std::string, std::string>>;
using platform::tests_support::ScopedDir;
using platform::tests_support::ScopedFile;

OsmElement MakeOsmElement(uint64_t id, Tags const & tags, OsmElement::EntityType t);

std::string GetFileName(std::string const & filename = std::string());

struct TagValue
{
  std::string m_key;
  std::string m_value;
};

struct Tag
{
  TagValue operator=(std::string const & value) const { return {m_name, value}; }

  std::string m_name;
};

struct OsmElementData
{
  uint64_t m_id;
  std::vector<TagValue> m_tags;
  std::vector<m2::PointD> m_points;
  std::vector<OsmElement::Member> m_members;
};

// Use cautiously, nothing means it works with your osm types.
OsmElement MakeOsmElement(OsmElementData const & elementData);
feature::FeatureBuilder FeatureBuilderFromOmsElementData(OsmElementData const & elementData);

struct RectArea : m2::RectD
{
  using m2::RectD::RectD;

  operator std::vector<m2::PointD> () const
  { return {LeftBottom(), LeftTop(), RightTop(), RightBottom(), LeftBottom()}; }
};

void WriteFeatures(std::vector<OsmElementData> const & osmElements,
                   ScopedFile const & featuresFile);
}  // namespace generator_tests

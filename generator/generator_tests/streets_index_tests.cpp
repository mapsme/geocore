#include "testing/testing.hpp"
#include "generator/generator_tests/common.hpp"

#include "generator/data_version.hpp"
#include "generator/geo_objects/geo_objects.hpp"
#include "generator/locality_index_generator.hpp"

#include "indexer/locality_index.hpp"

#include "base/assert.hpp"

using namespace generator_tests;
using namespace generator;
using namespace indexer;
using namespace base;
using namespace std::literals;

using IndexReader = generator::geo_objects::IndexReader;
using Objects = std::vector<GeoObjectId>;

GeoObjectsIndex<IndexReader> GenerateStreetsIndex(std::vector<OsmElementData> const & osmElements)
{
  ScopedFile const geoObjectsFeatures{
      "geoObjects"s + DATA_FILE_EXTENSION, ScopedFile::Mode::Create};
  ScopedFile const streetsFeatures{"streets"s + DATA_FILE_EXTENSION, ScopedFile::Mode::DoNotCreate};
  WriteFeatures(osmElements, streetsFeatures);

  ScopedFile const streetsIndex{"streets"s + LOC_IDX_FILE_EXTENSION, ScopedFile::Mode::DoNotCreate};
  bool streetsIndexGeneration =
      GenerateGeoObjectsIndex(streetsIndex.GetFullPath(), geoObjectsFeatures.GetFullPath(),
                              1 /* threadsCount */, {} /* nodesFile */,
                              streetsFeatures.GetFullPath());
  CHECK(streetsIndexGeneration, ());

  return ReadIndex<GeoObjectsIndexBox<IndexReader>, MmapReader>(streetsIndex.GetFullPath());
}

Objects GetObjectsAtPoint(
    GeoObjectsIndex<IndexReader> const & index, m2::PointD const & atPoint)
{
  auto results = Objects{};
  index.ForEachAtPoint([&] (auto const osmId) { results.push_back(osmId); }, atPoint);
  std::sort(results.begin(), results.end());
  return results;
}

UNIT_TEST(StreetsIndexTest_HighwayIndex)
{
  auto const osmElements = std::vector<OsmElementData>{
      {1, {{"name", "Arbat Street"}, {"highway", "residential"}}, {{1.001, 2.001}, {1.002, 2.001}},
       {}},
      {2, {{"name", "New Arbat Street"}, {"highway", "residential"}},
       {{1.001, 2.002}, {1.002, 2.001}}, {}}};

  auto const & streetsIndex = GenerateStreetsIndex(osmElements);

  auto const arbatEnd = GetObjectsAtPoint(streetsIndex, {1.001, 2.001});
  TEST_EQUAL(arbatEnd.size(), 1, ());
  TEST_EQUAL(arbatEnd, Objects({MakeOsmWay(1)}), ());

  auto const junction = GetObjectsAtPoint(streetsIndex, {1.002, 2.001});
  TEST_EQUAL(junction.size(), 2, ());
  TEST_EQUAL(junction, Objects({MakeOsmWay(1), MakeOsmWay(2)}), ());
}

UNIT_TEST(StreetsIndexTest_SquareIndex)
{
  auto const osmElements = std::vector<OsmElementData>{
      {1, {{"name", "New Square"}, {"place", "square"}}, RectArea{{1.000, 2.000}, {1.002, 2.002}},
       {}}};

  auto const & streetsIndex = GenerateStreetsIndex(osmElements);

  auto outside = GetObjectsAtPoint(streetsIndex, {1.000 - 0.001, 2.000 + 0.001});
  TEST(outside.empty(), ());

  auto inside = GetObjectsAtPoint(streetsIndex, {1.000 + 0.001, 2.000 + 0.001});
  TEST_EQUAL(inside.size(), 1, ());
  TEST_EQUAL(inside, Objects({MakeOsmWay(1)}), ());
}

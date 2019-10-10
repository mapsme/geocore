#include "testing/testing.hpp"

#include "generator/feature_builder.hpp"
#include "generator/generator_tests/common.hpp"
#include "generator/key_value_storage.hpp"
#include "generator/streets/streets_builder.hpp"

#include "geometry/point2d.hpp"

#include "3party/jansson/myjansson.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace generator_tests;
using namespace generator;
using namespace generator::streets;
using namespace base;

std::shared_ptr<JsonValue> RussiaGetter(uint64_t id)
{
  auto const russiaId = base::MakeOsmNode(424314830).GetEncodedId();
  std::shared_ptr<JsonValue> russiaValue = std::make_shared<JsonValue>(LoadFromString(
      R"#({
            "type": "Feature",
            "properties": {
              "locales": {
                "default": {
                  "address": {
                    "country": "Russia"
                  },
                  "name": "Russia"
                }
              },
              "rank": 1,
              "code": "RU"
            }
          })#"));
  return id == russiaId ? russiaValue : std::shared_ptr<JsonValue>{};
}

StreetsBuilder::RegionFinder RussiaFinder()
{
  auto finder = [](auto && /* point */, auto && selector) -> boost::optional<KeyValue> {
    static auto const russiaId = base::MakeOsmNode(424314830).GetEncodedId();
    static auto const russiaValue = RussiaGetter(russiaId);
    CHECK(russiaValue, ());
    if (selector({russiaId, russiaValue}))
      return std::make_pair(russiaId, russiaValue);
    return {};
  };
  return finder;
}

UNIT_TEST(StreetsBuilderTest_AggreatedStreetsInKv)
{
  auto const osmElements = std::vector<OsmElementData>{
      {1, {{"name", "Arbat Street"}, {"highway", "residential"}}, {{1.001, 2.001}, {1.002, 2.001}},
       {}},
      {2, {{"name", "Arbat Street"}, {"highway", "residential"}}, {{1.002, 2.001}, {1.002, 2.002}},
       {}},
      {3, {{"name", "New Arbat Street"}, {"highway", "residential"}},
       {{1.001, 2.002}, {1.002, 2.002}}, {}}};
  ScopedFile const streetsFeatures{"streets.mwm", ScopedFile::Mode::DoNotCreate};
  WriteFeatures(osmElements, streetsFeatures);

  StreetsBuilder streetsBuilder{RussiaFinder()};
  streetsBuilder.AssembleStreets(streetsFeatures.GetFullPath());
  ScopedFile const streetsJsonlFile{"streets.jsonl", ScopedFile::Mode::DoNotCreate};
  std::ofstream streetsJsonlStream(streetsJsonlFile.GetFullPath());
  streetsBuilder.SaveStreetsKv(RussiaGetter, streetsJsonlStream);
  streetsJsonlStream.flush();

  KeyValueStorage streetsStorage{streetsJsonlFile.GetFullPath(), 1000 /* cacheValuesCountLimit */};
  TEST_EQUAL(streetsStorage.Size(), 2, ());
  TEST(bool(streetsStorage.Find(MakeOsmWay(1).GetEncodedId())) !=
           bool(streetsStorage.Find(MakeOsmWay(2).GetEncodedId())),
       ());
  TEST(streetsStorage.Find(MakeOsmWay(3).GetEncodedId()), ());
}

UNIT_TEST(StreetsBuilderTest_AggreatedStreetsInFeatures)
{
  auto const osmElements = std::vector<OsmElementData>{
      {1, {{"name", "Arbat Street"}, {"highway", "residential"}}, {{1.001, 2.001}, {1.002, 2.001}},
       {}},
      {2, {{"name", "Arbat Street"}, {"highway", "residential"}}, {{1.002, 2.001}, {1.002, 2.002}},
       {}},
      {3, {{"name", "New Arbat Street"}, {"highway", "residential"}},
       {{1.001, 2.002}, {1.002, 2.002}}, {}}};
  ScopedFile const streetsFeatures{"streets.mwm", ScopedFile::Mode::DoNotCreate};
  WriteFeatures(osmElements, streetsFeatures);

  StreetsBuilder streetsBuilder{RussiaFinder()};
  streetsBuilder.AssembleStreets(streetsFeatures.GetFullPath());
  streetsBuilder.RegenerateAggreatedStreetsFeatures(streetsFeatures.GetFullPath());

  std::vector<feature::FeatureBuilder> features;
  std::unordered_set<GeoObjectId> featureIds;
  feature::ForEachFromDatRawFormat(streetsFeatures.GetFullPath(), [&](auto && fb, ...) {
    features.emplace_back(fb);
    featureIds.insert(fb.GetMostGenericOsmId());
  });
  TEST_EQUAL(features.size(), 3, ());
  TEST_EQUAL(featureIds.size(), 2, ());
  TEST((featureIds.find(MakeOsmWay(1)) != featureIds.end()) != 
            (featureIds.find(MakeOsmWay(2)) != featureIds.end()),
        ());
  TEST(featureIds.find(MakeOsmWay(3)) != featureIds.end(), ());
}

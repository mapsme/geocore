#include "testing/testing.hpp"

#include "geocoder/geocoder.hpp"
#include "geocoder/hierarchy_reader.hpp"


#include "platform/platform_tests_support/scoped_file.hpp"

#include "base/geo_object_id.hpp"
#include "base/math.hpp"
#include "base/stl_helpers.hpp"

#include <algorithm>
#include <iomanip>
#include <string>
#include <vector>

using namespace platform::tests_support;
using namespace std;

namespace
{
using Id = base::GeoObjectId;

double const kCertaintyEps = 1e-3;
string const kRegionsData = R"#(
C00000000004B279 {"type": "Feature", "geometry": {"type": "Point", "coordinates": [-80.1142033187951, 21.55511095]}, "properties": {"kind": "country", "locales": {"default": {"name": "Cuba", "address": {"country": "Cuba"}}}, "rank": 2}}
C0000000001C4CA7 {"type": "Feature", "geometry": {"type": "Point", "coordinates": [-78.7260117405499, 21.74300205]}, "properties": {"kind": "province", "locales": {"default": {"name": "Ciego de Ávila", "address": {"region": "Ciego de Ávila", "country": "Cuba"}}}, "rank": 4}}
C00000000059D6B5 {"type": "Feature", "geometry": {"type": "Point", "coordinates": [-78.9263054493181, 22.08185765]}, "properties": {"kind": "district", "locales": {"default": {"name": "Florencia", "address": {"subregion": "Florencia", "region": "Ciego de Ávila", "country": "Cuba"}}}, "rank": 6}}
)#";
}  // namespace

namespace geocoder
{
void TestGeocoder(Geocoder & geocoder, string const & query, vector<Result> && expected)
{
  vector<Result> actual;
  geocoder.ProcessQuery(query, actual);
  TEST_EQUAL(actual.size(), expected.size(), (query, actual, expected));
  sort(actual.begin(), actual.end(), base::LessBy(&Result::m_osmId));
  sort(expected.begin(), expected.end(), base::LessBy(&Result::m_osmId));
  for (size_t i = 0; i < std::min(actual.size(), expected.size()); ++i)
  {
    TEST(actual[i].m_certainty >= 0.0 && actual[i].m_certainty <= 1.0,
         (query, actual[i].m_certainty));
    TEST_EQUAL(actual[i].m_osmId, expected[i].m_osmId, (query));
    TEST_NEAR(actual[i].m_certainty, expected[i].m_certainty, kCertaintyEps,
         (query, actual[i].m_certainty, expected[i].m_certainty));
  }
}

UNIT_TEST(Geocoder_Smoke)
{
  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kRegionsData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  base::GeoObjectId const florenciaId(0xc00000000059d6b5);
  base::GeoObjectId const cubaId(0xc00000000004b279);

  TestGeocoder(geocoder, "florencia", {{florenciaId, 1.0}});
  TestGeocoder(geocoder, "cuba florencia", {{florenciaId, 1.0}, {cubaId, 0.713776}});
  TestGeocoder(geocoder, "florencia somewhere in cuba", {{cubaId, 0.713776}, {florenciaId, 1.0}});
}

UNIT_TEST(Geocoder_Hierarchy)
{
  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kRegionsData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());
  auto const & hierarchy = geocoder.GetHierarchy();
  auto const & dictionary = hierarchy.GetNormalizedNameDictionary();

  vector<Hierarchy::Entry> entries;
  geocoder.GetIndex().ForEachDocId({("florencia")}, [&](Index::DocId const & docId) {
    entries.emplace_back(geocoder.GetIndex().GetDoc(docId));
  });

  TEST_EQUAL(entries.size(), 1, ());
  TEST_EQUAL(entries[0].GetNormalizedMultipleNames(Type::Country, dictionary).GetMainName(), "cuba",
             ());
  TEST_EQUAL(entries[0].GetNormalizedMultipleNames(Type::Region, dictionary).GetMainName(),
             "ciego de avila", ());
  TEST_EQUAL(entries[0].GetNormalizedMultipleNames(Type::Subregion, dictionary).GetMainName(),
             "florencia", ());
}

UNIT_TEST(Geocoder_EnglishNames)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}, "en": {"address": {"locality": "Moscow"}}}}}
11 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица Новый Арбат"}}, "en": {"address": {"locality": "Moscow", "street": "New Arbat Avenue"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Moscow, New Arbat", {{Id{0x11}, 1.0}, {Id{0x10}, 0.558011}});
}

UNIT_TEST(Geocoder_OnlyBuildings)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Some Locality"}}}}}

21 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Good", "locality": "Some Locality"}}}}}
22 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "5", "street": "Good", "locality": "Some Locality"}}}}}

31 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Bad", "locality": "Some Locality"}}}}}
32 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "10", "street": "Bad", "locality": "Some Locality"}}}}}

40 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "MaybeNumbered", "locality": "Some Locality"}}}}}
41 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "MaybeNumbered-3", "locality": "Some Locality"}}}}}
42 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "3", "street": "MaybeNumbered", "locality": "Some Locality"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  base::GeoObjectId const localityId(0x10);
  base::GeoObjectId const goodStreetId(0x21);
  base::GeoObjectId const badStreetId(0x31);
  base::GeoObjectId const building5(0x22);
  base::GeoObjectId const building10(0x32);

  TestGeocoder(geocoder, "some locality", {{localityId, 1.0}});
  TestGeocoder(geocoder, "some locality good", {{goodStreetId, 1.0}, {localityId, 0.834711}});
  TestGeocoder(geocoder, "some locality bad", {{badStreetId, 1.0}, {localityId, 0.834711}});

  TestGeocoder(geocoder, "some locality good 5", {{building5, 1.0}});
  TestGeocoder(geocoder, "some locality bad 10", {{building10, 1.0}});

  // There is a building "10" on Bad Street but we should not return it.
  // Another possible resolution would be to return just "Good Street" (relaxed matching)
  // but at the time of writing the goal is to either have an exact match or no match at all.
  TestGeocoder(geocoder, "some locality good 10", {});

  // Sometimes we may still emit a non-building.
  // In this case it happens because all query tokens are used.
  base::GeoObjectId const numberedStreet(0x41);
  base::GeoObjectId const houseOnANonNumberedStreet(0x42);
  TestGeocoder(geocoder, "some locality maybenumbered 3",
               {{numberedStreet, 1.0}, {houseOnANonNumberedStreet, 0.865248}});
}

UNIT_TEST(Geocoder_MismatchedLocality)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Moscow"}}}}}
11 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Paris"}}}}}

21 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Krymskaya", "locality": "Moscow"}}}}}
22 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "2", "street": "Krymskaya", "locality": "Moscow"}}}}}

31 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Krymskaya", "locality": "Paris"}}}}}
32 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "3", "street": "Krymskaya", "locality": "Paris"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  base::GeoObjectId const building2(0x22);

  TestGeocoder(geocoder, "Moscow Krymskaya 2", {{building2, 1.0}});

  // "Krymskaya 3" looks almost like a match to "Paris-Krymskaya-3" but we should not emit it.
  TestGeocoder(geocoder, "Moscow Krymskaya 3", {});
}

//--------------------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_HouseNumberPartialMatch)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
11 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Зорге", "locality": "Москва"}}}}}
12 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "7", "street": "Зорге", "locality": "Москва"}}}}}
13 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "7 к2", "street": "Зорге", "locality": "Москва"}}}}}
14 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "7 к2 с3", "street": "Зорге", "locality": "Москва"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, Зорге 7к2", {{Id{0x13}, 1.0}, {Id{0x14}, 0.995}, {Id{0x12}, 0.975}});
  TestGeocoder(geocoder, "Москва, Зорге 7 к2", {{Id{0x13}, 1.0}, {Id{0x14}, 0.995}, {Id{0x12}, 0.975}});
  TestGeocoder(geocoder, "Москва, Зорге 7", {{Id{0x12}, 1.0}, {Id{0x13}, 0.993}, {Id{0x14}, 0.990}});
  TestGeocoder(geocoder, "Москва, Зорге 7к1", {{Id{0x12}, 0.95}});
  TestGeocoder(geocoder, "Москва, Зорге 7A", {{Id{0x12}, 0.95}});
  TestGeocoder(geocoder, "Москва, Зорге 7 A", {{Id{0x12}, 0.95}});
}

// Geocoder_Moscow* -----------------------------------------------------------------------------
UNIT_TEST(Geocoder_MoscowLocalityRank)
{
  string const kData = R"#(
10 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Москва"}}}, "rank": 2}}
11 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва", "region": "Москва"}}, "en": {"address": {"locality": "Moscow"}}}, "rank": 4}}
12 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Ленинский проспект", "locality": "Москва", "region": "Москва"}}, "en": {"address": {"locality": "Moscow"}}}}}

20 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Тверская Область"}}}, "rank": 2}}
21 {"properties": {"kind": "hamlet", "locales": {"default": {"address": {"locality": "Москва", "region": "Тверская Область"}}}, "rank": 4}}
22 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Ленинский проспект", "locality": "Москва", "region": "Тверская Область"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва", {{Id{0x11}, 1.0}, {Id{0x21}, 0.207843}, {Id{0x10}, 0.794118}});
  TestGeocoder(geocoder, "Москва, Ленинский проспект",
               {{Id{0x12}, 1.0}, {Id{0x22}, 0.556044}, {Id{0x11}, 0.56044}, {Id{0x10}, 0.445055},
                {Id{0x21}, 0.116484}});
}

// Geocoder_StreetWithNumber* ----------------------------------------------------------------------
UNIT_TEST(Geocoder_StreetWithNumberInCity)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
11 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 1905 года"}}}}}

20 {"properties": {"kind": "town", "locales": {"default": {"address": {"locality": "Краснокамск"}}}}}
28 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Краснокамск", "street": "улица 1905 года"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, улица 1905 года", {{Id{0x11}, 1.0}});
}

UNIT_TEST(Geocoder_StreetWithNumberInClassifiedCity)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
11 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 1905 года"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "город Москва, улица 1905 года", {{Id{0x11}, 1.0}});
}

UNIT_TEST(Geocoder_StreetWithNumberInAnyCity)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
11 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 1905 года"}}}}}

20 {"properties": {"kind": "town", "locales": {"default": {"address": {"locality": "Краснокамск"}}}}}
28 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Краснокамск", "street": "улица 1905 года"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "улица 1905 года", {{Id{0x11}, 1.0}, {Id{0x28}, 1.0}});
}

UNIT_TEST(Geocoder_StreetWithNumberAndWithoutStreetSynonym)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
11 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 1905 года"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, 1905 года", {{Id{0x11}, 1.0}});
}

UNIT_TEST(Geocoder_UntypedStreetWithNumberAndStreetSynonym)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
13 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "8 Марта"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, улица 8 Марта", {{Id{0x13}, 1.0}});
}

UNIT_TEST(Geocoder_StreetWithTwoNumbers)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
12 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "4-я улица 8 Марта"}}}}}

13 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 8 Марта"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, 4-я улица 8 Марта", {{Id{0x12}, 1.0}});
}

UNIT_TEST(Geocoder_BuildingOnStreetWithNumber)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
13 {"properties": {"kind": "street", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 8 Марта"}}}}}
15 {"properties": {"kind": "building", "locales": {"default": {"address": {"locality": "Москва", "street": "улица 8 Марта", "building": "4"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, улица 8 Марта, 4", {{Id{0x15}, 1.0}});
}

//--------------------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_LocalityBuilding)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Zelenograd"}}}}}
22 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "2", "locality": "Zelenograd"}}}}}
31 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Krymskaya", "locality": "Zelenograd"}}}}}
32 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "2", "street": "Krymskaya", "locality": "Zelenograd"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  base::GeoObjectId const building2(0x22);
  TestGeocoder(geocoder, "Zelenograd 2", {{building2, 1.0}});
}

//--------------------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_LocalityBuildingRankWithSuburb)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва"}}}}}
11 {"properties": {"kind": "suburb", "locales": {"default": {"address": {"suburb": "Арбат", "locality": "Москва"}}}}}
12 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "1", "suburb": "Арбат", "locality": "Москва"}}}}}
13 {"properties": {"kind": "suburb", "locales": {"default": {"address": {"suburb": "район Северный", "locality": "Москва"}}}}}
14 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "1", "suburb": "район Северный", "locality": "Москва"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Москва, Арбат 1", {{Id{0x12}, 1.0}, {Id{0x14}, 0.830645}});
}

//--------------------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_LocalityAndStreetBuildingsRank)
{
  string const kData = R"#(
10 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Zelenograd"}}}}}
22 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "2", "locality": "Zelenograd"}}}}}
31 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Krymskaya", "locality": "Zelenograd"}}}}}
32 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "2", "street": "Krymskaya", "locality": "Zelenograd"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Zelenograd, Krymskaya 2", {{Id{0x32}, 1.0}, {Id{0x22}, 0.72028}});
}

// Geocoder_Subregion* -----------------------------------------------------------------------------
UNIT_TEST(Geocoder_SubregionInLocality)
{
  string const kData = R"#(
10 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Москва"}}}, "rank": 2}}
11 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва", "region": "Москва"}}}, "rank": 4}}
12 {"properties": {"kind": "district", "locales": {"default": {"address": {"subregion": "Северный административный округ", "locality": "Москва", "region": "Москва"}}}, "rank": 3}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Северный административный округ", {{Id{0x12}, 1.0}});
  TestGeocoder(geocoder, "Москва, Северный административный округ",
               {{Id{0x12}, 1.0}, {Id{0x11}, 0.316181}, {Id{0x10}, 0.251085}});
  TestGeocoder(geocoder, "Москва", {{Id{0x11}, 1.0}, {Id{0x10}, 0.794118}});
}

// Geocoder_NumericalSuburb* ----------------------------------------------------------------------
UNIT_TEST(Geocoder_NumericalSuburbRelevance)
{
  string const kData = R"#(
10 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Metro Manila"}}}}}
11 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Caloocan", "region": "Metro Manila"}}}}}
12 {"properties": {"kind": "suburb", "locales": {"default": {"address": {"suburb": "60", "locality": "Caloocan", "region": "Metro Manila"}}}}}
20 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Белгород"}}}}}
21 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Щорса", "locality": "Белгород"}}}}}
22 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "60", "street": "Щорса", "locality": "Белгород"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Caloocan, 60", {{Id{0x12}, 1.0}});
  TestGeocoder(geocoder, "60", {});
  TestGeocoder(geocoder, "Metro Manila, 60", {{Id{0x10}, 1.0}});
  TestGeocoder(geocoder, "Белгород, Щорса, 60", {{Id{0x22}, 1.0}});
}

// Geocoder_Serialization --------------------------------------------------------------------------
UNIT_TEST(Geocoder_Serialization)
{
  string const kData = R"#(
10 {"properties": {"kind": "country", "locales": {"default": {"address": {"country": "Россия"}}, "en": {"address": {"country": "Russia"}}}, "rank": 1}}
11 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Москва", "country": "Россия"}}}, "rank": 2}}
12 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Москва", "region": "Москва", "country": "Россия"}}}, "rank": 4}}
13 {"properties": {"kind": "street", "locales": {"default": {"address": {"street": "Арбат", "locality": "Москва", "region": "Москва", "country": "Россия"}}}, "rank": 7}}
15 {"properties": {"kind": "building", "locales": {"default": {"address": {"building": "4", "street": "Арбат", "locality": "Москва", "region": "Москва", "country": "Россия"}}}, "rank": 8}}
)#";

  Geocoder geocoderFromJsonl;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoderFromJsonl.LoadFromJsonl(regionsJsonFile.GetFullPath());

  ScopedFile const regionsTokenIndexFile("regions.tokidx", ScopedFile::Mode::DoNotCreate);
  geocoderFromJsonl.SaveToBinaryIndex(regionsTokenIndexFile.GetFullPath());

  Geocoder geocoderFromTokenIndex;
  geocoderFromTokenIndex.LoadFromBinaryIndex(regionsTokenIndexFile.GetFullPath());

  for (auto const & name : {"russia", "россия", "москва", "арбат"})
  {
    vector<base::GeoObjectId> objectsFromJsonl;
    geocoderFromJsonl.GetIndex().ForEachDocId({name}, [&](Index::DocId const & docId) {
      objectsFromJsonl.emplace_back(geocoderFromJsonl.GetIndex().GetDoc(docId).m_osmId);

      geocoderFromJsonl.GetIndex().ForEachRelatedBuilding(docId, [&](Index::DocId const & docId) {
        objectsFromJsonl.emplace_back(geocoderFromJsonl.GetIndex().GetDoc(docId).m_osmId);
      });
    });

    vector<base::GeoObjectId> objectsFromTokenIndex;
    geocoderFromTokenIndex.GetIndex().ForEachDocId({name}, [&](Index::DocId const & docId) {
      objectsFromTokenIndex.emplace_back(geocoderFromTokenIndex.GetIndex().GetDoc(docId).m_osmId);

      geocoderFromTokenIndex.GetIndex().ForEachRelatedBuilding(docId, [&](Index::DocId const & docId) {
        objectsFromTokenIndex.emplace_back(geocoderFromTokenIndex.GetIndex().GetDoc(docId).m_osmId);
      });
    });

    TEST_GREATER_OR_EQUAL(objectsFromJsonl.size(), 1, ());
    TEST_EQUAL(objectsFromTokenIndex, objectsFromJsonl, ());
  }
}

//--------------------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_EmptyFileConcurrentRead)
{
  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", "");
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath(), false, 8 /* reader threads */);

  TEST_EQUAL(geocoder.GetHierarchy().GetEntries().size(), 0, ());
}

UNIT_TEST(Geocoder_BigFileConcurrentRead)
{
  int const kEntryCount = 100000;

  stringstream s;
  for (int i = 0; i < kEntryCount; ++i)
  {
    s << setw(16) << setfill('0') << hex << uppercase << i << " "
      << "{"
      << R"("type": "Feature",)"
      << R"("geometry": {"type": "Point", "coordinates": [0, 0]},)"
      << R"("properties": {"kind": "country", "locales": {"default": {)"
      << R"("name": ")" << i << R"(", "address": {"country": ")" << i << R"("}}}, "rank": 2})"
      << "}\n";
  }

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", s.str());
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath(), false, 8 /* reader threads */);

  TEST_EQUAL(geocoder.GetHierarchy().GetEntries().size(), kEntryCount, ());
}

//--------------------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_CityVsHamletRankTest)
{
  string const kData = R"#(
10 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Оренбургская область"}}}}}
11 {"properties": {"kind": "hamlet", "locales": {"default": {"address": {"locality": "Красноярск", "region": "Оренбургская область"}}}}}
20 {"properties": {"kind": "state", "locales": {"default": {"address": {"region": "Красноярский край"}}}}}
21 {"properties": {"kind": "city", "locales": {"default": {"address": {"locality": "Красноярск", "region": "Красноярский край"}}}}}
)#";

  Geocoder geocoder;
  ScopedFile const regionsJsonFile("regions.jsonl", kData);
  geocoder.LoadFromJsonl(regionsJsonFile.GetFullPath());

  TestGeocoder(geocoder, "Красноярск", {{Id{0x21}, 1.0}, {Id{0x11}, 0.2099}});
}

// Kind tests --------------------------------------------------------------------------------------
UNIT_TEST(Geocoder_KindStringConversion)
{
  TEST_EQUAL(static_cast<int>(Kind::Unknown), 0, ());
  for (auto i = 1; i < static_cast<int>(Kind::Count); ++i)
  {
    auto const kind = static_cast<Kind>(i);
    TEST_EQUAL(kind, KindFromString(ToString(kind)), ());
  }
}
}  // namespace geocoder

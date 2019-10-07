#include "testing/testing.hpp"

#include "indexer/centers_table.hpp"
#include "indexer/classificator_loader.hpp"
#include "indexer/data_header.hpp"


#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include "geometry/mercator.hpp"
#include "geometry/point2d.hpp"


#include <cstdint>
#include <utility>
#include <vector>

using namespace search;
using namespace std;

namespace
{
using TBuffer = vector<uint8_t>;

struct CentersTableTest
{
  CentersTableTest() { classificator::Load(); }
};

UNIT_CLASS_TEST(CentersTableTest, Subset)
{
  vector<pair<uint32_t, m2::PointD>> const features = {
      {1, m2::PointD(0, 0)}, {5, m2::PointD(1, 1)}, {10, m2::PointD(2, 2)}};

  serial::GeometryCodingParams codingParams;

  TBuffer buffer;
  {
    CentersTableBuilder builder;

    builder.SetGeometryCodingParams(codingParams);
    for (auto const & feature : features)
      builder.Put(feature.first, feature.second);

    MemWriter<TBuffer> writer(buffer);
    builder.Freeze(writer);
  }

  {
    MemReader reader(buffer.data(), buffer.size());
    auto table = CentersTable::Load(reader, codingParams);
    TEST(table.get(), ());

    uint32_t i = 0;
    size_t j = 0;

    while (i < 100)
    {
      ASSERT(j == features.size() || features[j].first >= i, ("Invariant violation"));

      m2::PointD actual;
      if (j != features.size() && i == features[j].first)
      {
        TEST(table->Get(i, actual), ());
        TEST_LESS_OR_EQUAL(MercatorBounds::DistanceOnEarth(actual, features[j].second), 1, ());
      }
      else
      {
        TEST(!table->Get(i, actual), ());
      }

      ++i;
      while (j != features.size() && features[j].first < i)
        ++j;
    }
  }
}
}  // namespace

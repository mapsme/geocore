//
//  intermediate_data_test.cpp
//  generator_tool
//
//  Created by Sergey Yershov on 20.08.15.
//  Copyright (c) 2015 maps.me. All rights reserved.
//

#include "testing/testing.hpp"
#include "generator/generator_tests/common.hpp"
#include "generator/generator_tests/source_data.hpp"

#include "generator/intermediate_elements.hpp"
#include "generator/osm_element.hpp"
#include "generator/osm_source.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"


#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace generator_tests;
using namespace generator;
using namespace std;
using namespace std::literals;

using OsmFormatParser = std::function<void(SourceReader &, function<void(OsmElement &&)>)>;

UNIT_TEST(Intermediate_Data_empty_way_element_save_load_test)
{
  WayElement e1(1 /* fake osm id */);

  using TBuffer = vector<uint8_t>;
  TBuffer buffer;
  MemWriter<TBuffer> w(buffer);

  e1.Write(w);

  MemReader r(buffer.data(), buffer.size());

  WayElement e2(1 /* fake osm id */);

  e2.Read(r);

  TEST_EQUAL(e2.nodes.size(), 0, ());
}

UNIT_TEST(Intermediate_Data_way_element_save_load_test)
{
  vector<uint64_t> testData = {0, 1, 2, 3, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF};

  WayElement e1(1 /* fake osm id */);

  e1.nodes = testData;

  using TBuffer = vector<uint8_t>;
  TBuffer buffer;
  MemWriter<TBuffer> w(buffer);

  e1.Write(w);

  MemReader r(buffer.data(), buffer.size());

  WayElement e2(1 /* fake osm id */);

  e2.Read(r);

  TEST_EQUAL(e2.nodes, testData, ());
}

UNIT_TEST(Intermediate_Data_relation_element_save_load_test)
{
  std::vector<RelationElement::Member> testData = {{1, "inner"},
                                                   {2, "outer"},
                                                   {3, "unknown"},
                                                   {4, "inner role"}};

  RelationElement e1;

  e1.nodes = testData;
  e1.ways = testData;

  e1.tags.emplace("key1", "value1");
  e1.tags.emplace("key2", "value2");
  e1.tags.emplace("key3", "value3");
  e1.tags.emplace("key4", "value4");

  using TBuffer = vector<uint8_t>;
  TBuffer buffer;
  MemWriter<TBuffer> w(buffer);

  e1.Write(w);

  MemReader r(buffer.data(), buffer.size());

  RelationElement e2;

  e2.nodes.emplace_back(30, "000unknown");
  e2.nodes.emplace_back(40, "000inner role");
  e2.ways.emplace_back(10, "000inner");
  e2.ways.emplace_back(20, "000outer");
  e2.tags.emplace("key1old", "value1old");
  e2.tags.emplace("key2old", "value2old");

  e2.Read(r);

  TEST_EQUAL(e2.nodes, testData, ());
  TEST_EQUAL(e2.ways, testData, ());

  TEST_EQUAL(e2.tags.size(), 4, ());
  TEST_EQUAL(e2.tags["key1"], "value1", ());
  TEST_EQUAL(e2.tags["key2"], "value2", ());
  TEST_EQUAL(e2.tags["key3"], "value3", ());
  TEST_EQUAL(e2.tags["key4"], "value4", ());

  TEST_NOT_EQUAL(e2.tags["key1old"], "value1old", ());
  TEST_NOT_EQUAL(e2.tags["key2old"], "value2old", ());
}

std::vector<OsmElement> ReadOsmElements(
    std::string const & filename, OsmElement::EntityType type, OsmFormatParser parser)
{
  std::vector<OsmElement> elements;

  auto stream = std::fstream{filename};
  SourceReader reader(stream);
  parser(reader, [&elements, type](OsmElement && e)
  {
    if (e.m_type == type)
      elements.push_back(std::move(e));
  });

  return elements;
}

UNIT_TEST(IntermediateData_WaysGenerationTest)
{
  auto const osmSamples = std::map<std::string, std::string>{
      {"xml", way_xml_data}, {"o5m", {std::begin(way_o5m_data), std::end(way_o5m_data)}}};
  auto const osmFormatParsers = std::map<std::string, OsmFormatParser>{
      {"xml", ProcessOsmElementsFromXML}, {"o5m", ProcessOsmElementsFromO5M}};

  for (auto const & sample : osmSamples)
  {
    auto const & osmFileTypeExtension = sample.first;
    auto const & osmFileData = sample.second;

    // Skip test for node storage type "mem": 64Gb required.
    for (auto const & nodeStorageType : {"raw"s, "map"s})
    {
      auto const & dataPath = ScopedDir("intermediate_data", true /* recursiveForceRemove */);
      auto genInfo = feature::GenerateInfo{};
      genInfo.m_dataPath = dataPath.GetFullPath();
      genInfo.m_targetDir = dataPath.GetFullPath();
      genInfo.m_tmpDir = dataPath.GetFullPath();
      genInfo.m_osmFileName = "planet." + osmFileTypeExtension;
      genInfo.SetOsmFileType(osmFileTypeExtension);
      genInfo.SetNodeStorageType(nodeStorageType);

      std::ofstream{genInfo.m_osmFileName} << osmFileData;
      auto generation = GenerateIntermediateData(genInfo);
      CHECK(generation, ());

      auto ways = ReadOsmElements(genInfo.m_osmFileName, OsmElement::EntityType::Way,
                                  osmFormatParsers.at(osmFileTypeExtension));
      TEST_EQUAL(ways.size(), 1, ());

      auto const & intermediateData = cache::IntermediateData{genInfo, true /* forceReload */};
      auto const & cache = intermediateData.GetCache();
      for (auto const & element : ways)
      {
        auto way = WayElement{element.m_id};
        TEST(cache->GetWay(element.m_id, way), ());
        TEST_EQUAL(way.m_wayOsmId, element.m_id, ());
        TEST_EQUAL(way.nodes.size(), element.Nodes().size(), ());
      }
    }
  }
}

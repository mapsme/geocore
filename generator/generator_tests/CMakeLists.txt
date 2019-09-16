project(generator_tests)

set(
  SRC
  coasts_test.cpp
  collector_city_area_tests.cpp
  common.cpp
  common.hpp
  feature_builder_test.cpp
  feature_merger_test.cpp
  filter_elements_tests.cpp
  geo_objects_tests.cpp
  intermediate_data_test.cpp
  merge_collectors_tests.cpp
  metadata_parser_test.cpp
  metalines_tests.cpp
  node_mixer_test.cpp
  osm2meta_test.cpp
  osm_o5m_source_test.cpp
  osm_type_test.cpp
  region_info_collector_tests.cpp
  regions_tests.cpp
  source_data.cpp
  source_data.hpp
  source_to_element_test.cpp
  street_geometry_tests.cpp
  street_regions_tracing_tests.cpp
  tag_admixer_test.cpp
  translation_test.cpp
  types_helper.hpp
)

geocore_add_test(${PROJECT_NAME} ${SRC})

geocore_link_libraries(
  ${PROJECT_NAME}
  generator_tests_support
  platform_tests_support
  generator
)

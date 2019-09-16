project(indexer_tests)

set(
  SRC
  borders_test.cpp
  bounds.hpp
  categories_test.cpp
  cell_coverer_test.cpp
  cell_id_test.cpp
  centers_table_test.cpp
  checker_test.cpp
  cities_boundaries_serdes_tests.cpp
  classificator_tests.cpp
  drules_selector_parser_test.cpp
  editable_map_object_test.cpp
  feature_metadata_test.cpp
  feature_names_test.cpp
  index_builder_test.cpp
  interval_index_test.cpp
  locality_index_test.cpp
  mwm_set_test.cpp
  postcodes_matcher_tests.cpp
  scales_test.cpp
  search_string_utils_test.cpp
  sort_and_merge_intervals_test.cpp
  string_slice_tests.cpp
  succinct_trie_test.cpp
  test_mwm_set.hpp
  test_type.cpp
  trie_test.cpp
  visibility_test.cpp
  wheelchair_tests.cpp
)

geocore_add_test(${PROJECT_NAME} ${SRC})

geocore_link_libraries(
  ${PROJECT_NAME}
  generator_tests_support
  platform_tests_support
  indexer
  coding
  base
)

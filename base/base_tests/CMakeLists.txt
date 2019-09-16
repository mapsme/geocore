project(base_tests)

add_definitions(-DGEOCORE_UNIT_TEST_DISABLE_PLATFORM_INIT)

set(
  SRC
  assert_test.cpp
  beam_tests.cpp
  bidirectional_map_tests.cpp
  bits_test.cpp
  buffer_vector_test.cpp
  cache_test.cpp
  condition_test.cpp
  control_flow_tests.cpp
  file_name_utils_tests.cpp
  geo_object_id_tests.cpp
  levenshtein_dfa_test.cpp
  logging_test.cpp
  math_test.cpp
  matrix_test.cpp
  mem_trie_test.cpp
  observer_list_test.cpp
  ref_counted_tests.cpp
  regexp_test.cpp
  scope_guard_test.cpp
  stl_helpers_tests.cpp
  string_format_test.cpp
  string_utils_test.cpp
  thread_pool_computational_tests.cpp
  thread_pool_delayed_tests.cpp
  thread_pool_tests.cpp
  thread_safe_queue_tests.cpp
  threaded_list_test.cpp
  threads_test.cpp
  timegm_test.cpp
  timer_test.cpp
  uni_string_dfa_test.cpp
  visitor_tests.cpp
)

geocore_add_test(${PROJECT_NAME} ${SRC})
geocore_link_libraries(${PROJECT_NAME} base)

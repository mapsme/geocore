project(geocoder_tests)

set(
  SRC
  geocoder_tests.cpp
  house_numbers_matcher_test.cpp
)

geocore_add_test(${PROJECT_NAME} ${SRC})

geocore_link_libraries(
  ${PROJECT_NAME}
  platform_tests_support
  geocoder
  indexer
  platform
  coding
  base
  jansson
)

project(generator_tests_support)

set(
  SRC
  test_feature.cpp
  test_feature.hpp
  test_with_classificator.cpp
  test_with_classificator.hpp
)

geocore_add_library(${PROJECT_NAME} ${SRC})

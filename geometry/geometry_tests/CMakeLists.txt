project(geometry_tests)

add_definitions(-DGEOCORE_UNIT_TEST_DISABLE_PLATFORM_INIT)

set(
  SRC
  algorithm_test.cpp
  angle_test.cpp
  anyrect_test.cpp
  bounding_box_tests.cpp
  calipers_box_tests.cpp
  cellid_test.cpp
  common_test.cpp
  convex_hull_tests.cpp
  diamond_box_tests.cpp
  distance_on_sphere_test.cpp
  equality.hpp
  intersect_test.cpp
  large_polygon.hpp
  latlon_test.cpp
  line2d_tests.cpp
  mercator_test.cpp
  parametrized_segment_tests.cpp
  point_test.cpp
  polygon_test.cpp
  rect_test.cpp
  region2d_binary_op_test.cpp
  region_tests.cpp
  robust_test.cpp
  screen_test.cpp
  segment2d_tests.cpp
  simplification_test.cpp
  test_regions.hpp
  transformations_test.cpp
  tree_test.cpp
)

geocore_add_test(${PROJECT_NAME} ${SRC})
geocore_link_libraries(${PROJECT_NAME} geometry base)

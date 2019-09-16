project(generator_tool)

set(SRC generator_tool.cpp)

geocore_add_executable(${PROJECT_NAME} ${SRC})
geocore_link_libraries(
  ${PROJECT_NAME}
  generator
  geocoder
  ${Boost_PROGRAM_OPTIONS_LIBRARY}
  ${CMAKE_DL_LIBS}
)

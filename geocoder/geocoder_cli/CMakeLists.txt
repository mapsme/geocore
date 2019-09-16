project(geocoder_cli)

set(
  SRC
  geocoder_cli.cpp
)

geocore_add_executable(${PROJECT_NAME} ${SRC})

geocore_link_libraries(
  ${PROJECT_NAME}
  ${Boost_PROGRAM_OPTIONS_LIBRARY}
  geocoder
)

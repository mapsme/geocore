project(platform_tests_support)

set(
  SRC
  scoped_dir.cpp
  scoped_dir.hpp
  scoped_file.cpp
  scoped_file.hpp
  scoped_mwm.cpp
  scoped_mwm.hpp
  writable_dir_changer.cpp
  writable_dir_changer.hpp
)

geocore_add_library(${PROJECT_NAME} ${SRC})

# Function for setting target platform:
function(geocore_set_platform_var PLATFORM_VAR pattern)
  set(${PLATFORM_VAR} FALSE PARENT_SCOPE)

  if (NOT PLATFORM)
    if (${ARGN})
      list(GET ARGN 0 default_case)
      if (${default_case})
        set(${PLATFORM_VAR} TRUE PARENT_SCOPE)
        message("Setting ${PLATFORM_VAR} to true")
      endif()
    endif()
  else()
    message("Platform: ${PLATFORM}")
    if (${PLATFORM} MATCHES ${pattern})
      set(${PLATFORM_VAR} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

# Functions for using in subdirectories
function(geocore_add_executable executable)
  add_executable(${executable} ${ARGN})
  add_dependencies(${executable} BuildVersion)
  if (USE_ASAN)
    target_link_libraries(
      ${executable}
      "-fsanitize=address"
      "-fno-omit-frame-pointer"
    )
  endif()
  if (USE_TSAN)
    target_link_libraries(
      ${executable}
      "-fsanitize=thread"
      "-fno-omit-frame-pointer"
    )
  endif()
  if (USE_PPROF)
    target_link_libraries(${executable} "-lprofiler")
  endif()
endfunction()

function(geocore_add_library library)
  add_library(${library} ${ARGN})
  add_dependencies(${library} BuildVersion)
endfunction()

function(geocore_add_test executable)
  if (NOT SKIP_TESTS)
    include(GoogleTest)
    geocore_add_executable(
      ${executable}
      ${ARGN}
      ${GEOCORE_ROOT}/testing/testingmain.cpp
     )
     geocore_link_libraries(${executable} gtest_main)
     target_include_directories(${executable} PRIVATE ${CMAKE_BINARY_DIR})
     gtest_discover_tests(${executable} TEST_PREFIX "${executable}:")
  endif()
endfunction()

function(geocore_add_test_subdirectory subdir)
  if (NOT SKIP_TESTS)
    add_subdirectory(${subdir})
  else()
    message("SKIP_TESTS: Skipping subdirectory ${subdir}")
  endif()
endfunction()

function(geocore_link_platform_deps target)
  if ("${ARGN}" MATCHES "platform")
    if (PLATFORM_MAC)
      target_link_libraries(
        ${target}
        "-framework CFNetwork"
        "-framework Foundation"
        "-framework IOKit"
        "-framework SystemConfiguration"
        "-framework Security"
      )
    endif()
  endif()
endfunction()

function(geocore_link_libraries target)
  if (TARGET ${target})
    target_link_libraries(${target} ${ARGN} ${CMAKE_THREAD_LIBS_INIT})
    geocore_link_platform_deps(${target} ${ARGN})
  else()
    message("~> Skipping linking the libraries to the target ${target} as it"
            " does not exist")
  endif()
endfunction()

function(append VAR)
  set(${VAR} ${${VAR}} ${ARGN} PARENT_SCOPE)
endfunction()

function(add_clang_compile_options)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(${ARGV})
  endif()
endfunction()

function(add_gcc_compile_options)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(${ARGV})
  endif()
endfunction()

function(add_clang_cpp_compile_options)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ARGV}>")
  endif()
endfunction()

function(add_gcc_cpp_compile_options)
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ARGV}>")
  endif()
endfunction()

function(configure_gtest)
  #download and unpack googletest at configure time
  configure_file(cmake/gtest-download.cmake.in googletest-download/CMakeLists.txt)
  configure_file(testing/path.hpp.in testing/path.hpp)
  execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
  if(result)
    message(FATAL_ERROR "CMake step for googletest failed: ${result}")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
  if(result)
    message(FATAL_ERROR "Build step for googletest failed: ${result}")
  endif()

  # Add googletest directly to our build. This defines
  # the gtest and gtest_main targets.
  add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
                   ${CMAKE_CURRENT_BINARY_DIR}/googletest-build
                   EXCLUDE_FROM_ALL)
  include_directories("${gtest_SOURCE_DIR}/include")
endfunction()

file(GLOB_RECURSE UNIT_SOURCES        CONFIGURE_DEPENDS "unit/*.cpp")
file(GLOB_RECURSE INTEGRATION_SOURCES CONFIGURE_DEPENDS "integration/*.cpp")

if(NOT UNIT_SOURCES AND NOT INTEGRATION_SOURCES)
  message(STATUS "No alignx test sources found; skipping test targets.")
  return()
endif()

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

if(UNIT_SOURCES)
  if(TARGET alignx_lib)
    add_executable(unit_tests ${UNIT_SOURCES})
    target_link_libraries(unit_tests PRIVATE alignx_lib GTest::gtest_main)
    target_compile_definitions(unit_tests PRIVATE
      TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/toy_data"
    )
    gtest_discover_tests(unit_tests)
  else()
    message(WARNING "Unit tests found but alignx_lib unavailable; skipping unit_tests.")
  endif()
endif()

if(INTEGRATION_SOURCES)
  if(TARGET alignx_lib)
    add_executable(integration_tests ${INTEGRATION_SOURCES})
    target_link_libraries(integration_tests PRIVATE alignx_lib GTest::gtest_main)
    target_compile_definitions(integration_tests PRIVATE
      TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/toy_data"
    )
    gtest_discover_tests(integration_tests)
  else()
    message(WARNING "Integration tests found but alignx_lib unavailable; skipping integration_tests.")
  endif()
endif()

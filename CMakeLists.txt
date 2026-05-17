cmake_minimum_required(VERSION 3.25)
project(alignx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG   "/Zi /RTC1")
  add_compile_options(/utf-8 /W3)
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG   "-g -fsanitize=address,undefined")
endif()

# ── Feature options ────────────────────────────────────────────────────────────
option(ALIGNX_BUILD_TESTS  "Build alignx tests"              ON)
option(ALIGNX_ENABLE_SIMD  "Enable AVX2 SIMD (Phase 2+)"    OFF)
option(ALIGNX_ENABLE_CUDA  "Enable CUDA backend (Phase 3+)"  OFF)
option(ALIGNX_ENABLE_ZSTD  "Enable zstd support for AXF1 compressed payloads" OFF)
option(ALIGNX_BUILD_PYTHON "Build Python bindings via pybind11"               OFF)

if(ALIGNX_ENABLE_CUDA)
  enable_language(CUDA)
endif()

# ── Dependencies ───────────────────────────────────────────────────────────────
find_package(ZLIB REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

# HTSlib: try vcpkg CMake config first, fall back to pkg-config
find_package(htslib CONFIG QUIET)
if(htslib_FOUND)
  set(ALIGNX_HTSLIB_TARGET htslib::htslib)
  message(STATUS "htslib found via CMake config")
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(HTSLIB QUIET IMPORTED_TARGET htslib)
  endif()
  if(HTSLIB_FOUND)
    set(ALIGNX_HTSLIB_TARGET PkgConfig::HTSLIB)
    set(htslib_FOUND TRUE)
    message(STATUS "htslib found via pkg-config")
  else()
    message(WARNING "htslib not found — BAM/CRAM I/O disabled. Install via vcpkg or system pkg.")
  endif()
endif()

if(ALIGNX_ENABLE_ZSTD)
  find_package(zstd CONFIG QUIET)
  if(zstd_FOUND)
    if(TARGET zstd::libzstd_shared)
      set(ALIGNX_ZSTD_TARGET zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static)
      set(ALIGNX_ZSTD_TARGET zstd::libzstd_static)
    elseif(TARGET zstd::libzstd)
      set(ALIGNX_ZSTD_TARGET zstd::libzstd)
    else()
      message(FATAL_ERROR "zstd found, but no supported zstd CMake target is available")
    endif()
    message(STATUS "zstd found via CMake config")
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
    endif()
    if(ZSTD_FOUND)
      set(ALIGNX_ZSTD_TARGET PkgConfig::ZSTD)
      set(zstd_FOUND TRUE)
      message(STATUS "zstd found via pkg-config")
    else()
      message(FATAL_ERROR "ALIGNX_ENABLE_ZSTD=ON requires zstd; install zstd or disable ALIGNX_ENABLE_ZSTD")
    endif()
  endif()
endif()

# ── Core library ───────────────────────────────────────────────────────────────
file(GLOB_RECURSE ALIGNX_LIB_SOURCES CONFIGURE_DEPENDS
  src/io/*.cpp
  src/index/*.cpp
  src/format/*.cpp
  src/compress/*.cpp
  src/query/*.cpp
  src/analysis/*.cpp
  src/convert/*.cpp
  src/cli/*.cpp
  src/bench/*.cpp
)

if(ALIGNX_LIB_SOURCES)
  add_library(alignx_lib STATIC ${ALIGNX_LIB_SOURCES})
  target_include_directories(alignx_lib PUBLIC src)
  target_compile_features(alignx_lib PUBLIC cxx_std_23)
  target_link_libraries(alignx_lib PUBLIC ZLIB::ZLIB)
  target_link_libraries(alignx_lib PUBLIC CLI11::CLI11)

  if(htslib_FOUND)
    target_link_libraries(alignx_lib PUBLIC ${ALIGNX_HTSLIB_TARGET})
    target_compile_definitions(alignx_lib PUBLIC ALIGNX_HAVE_HTSLIB)
  endif()

  if(ALIGNX_ENABLE_ZSTD)
    target_link_libraries(alignx_lib PUBLIC ${ALIGNX_ZSTD_TARGET})
    target_compile_definitions(alignx_lib PUBLIC ALIGNX_HAVE_ZSTD)
  endif()

  if(ALIGNX_ENABLE_SIMD)
    if(MSVC)
      target_compile_options(alignx_lib PUBLIC /arch:AVX2)
    else()
      target_compile_options(alignx_lib PUBLIC -mavx2 -mbmi2)
    endif()
    target_compile_definitions(alignx_lib PUBLIC ALIGNX_HAVE_AVX2)
  endif()
else()
  message(STATUS "No alignx library sources found; scaffold-only configure.")
endif()

# ── CLI executable ─────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
  if(TARGET alignx_lib)
    add_executable(alignx src/main.cpp)
    target_link_libraries(alignx PRIVATE alignx_lib)
    install(TARGETS alignx DESTINATION bin)
  else()
    message(WARNING "src/main.cpp exists but alignx_lib has no sources; skipping alignx executable.")
  endif()
else()
  message(STATUS "src/main.cpp not found; skipping alignx executable.")
endif()

# ── Tests ──────────────────────────────────────────────────────────────────────
if(ALIGNX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Python bindings ───────────────────────────────────────────────────────────
if(ALIGNX_BUILD_PYTHON AND TARGET alignx_lib)
  find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(_alignx src/python/bindings.cpp)
  target_link_libraries(_alignx PRIVATE alignx_lib)
  set_target_properties(_alignx PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
  )
  add_custom_command(TARGET _alignx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/src/python/alignx"
      "${CMAKE_BINARY_DIR}/python/alignx"
    COMMENT "Copying alignx Python package to build directory"
  )
  message(STATUS "Python bindings enabled (pybind11 ${pybind11_VERSION})")
endif()

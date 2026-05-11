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

  if(htslib_FOUND)
    target_link_libraries(alignx_lib PUBLIC ${ALIGNX_HTSLIB_TARGET})
    target_compile_definitions(alignx_lib PUBLIC ALIGNX_HAVE_HTSLIB)
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
    target_link_libraries(alignx PRIVATE alignx_lib CLI11::CLI11)
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

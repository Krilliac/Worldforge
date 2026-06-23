cmake_minimum_required(VERSION 3.16)
project(worldforge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# StormLib: battle-tested MPQ reader. Do NOT roll your own.
# Pinned to a real release tag; bump deliberately and re-test.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  stormlib
  GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
  GIT_TAG        v9.31
  GIT_SHALLOW    TRUE
)
# We only need read support; skip StormLib's own test/tool targets if exposed.
set(STORMLIB_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(stormlib)

# StormLib's static library target is named "storm".
add_library(wforge STATIC
  src/mpq.cpp
  src/wow_files.cpp
  src/terrain.cpp
  src/image.cpp
  src/blp.cpp
  src/m2.cpp
  src/wmo.cpp
  src/crypto.cpp
  src/srp6.cpp
  src/raster.cpp
  src/editing.cpp
  src/gizmo.cpp
  src/editor_bridge.cpp
  src/db_export.cpp
  src/debugdraw.cpp
  src/modelmesh.cpp
  src/clientfx.cpp
)
target_include_directories(wforge PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${stormlib_SOURCE_DIR}/src        # StormLib.h lives here
)
target_link_libraries(wforge PUBLIC storm)

add_executable(wforge-dump src/main.cpp)
target_link_libraries(wforge-dump PRIVATE wforge)

# ---------------------------------------------------------------------------
# Unit tests. Most modules (math, coords, terrain parsing) need no MPQ data,
# so they run fully offline in CI.
# ---------------------------------------------------------------------------
add_executable(wforge-tests
  tests/test_main.cpp
  tests/test_math.cpp
  tests/test_coords.cpp
  tests/test_terrain.cpp
  tests/test_blp.cpp
  tests/test_m2.cpp
  tests/test_wmo.cpp
  tests/test_anim.cpp
  tests/test_proto.cpp
  tests/test_raster.cpp
  tests/test_editing.cpp
  tests/test_bridge.cpp
  tests/test_debugdraw.cpp
  tests/test_modelmesh.cpp
  tests/test_clientfx.cpp
)
target_link_libraries(wforge-tests PRIVATE wforge)
target_include_directories(wforge-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

enable_testing()
add_test(NAME unit COMMAND wforge-tests)

if(MSVC)
  target_compile_options(wforge      PRIVATE /W4 /permissive-)
  target_compile_options(wforge-dump PRIVATE /W4 /permissive-)
  # WoW data files are little-endian; we decode byte-wise so this is portable,
  # but flag wide-char surprises early.
  target_compile_definitions(wforge PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(wforge      PRIVATE -Wall -Wextra)
  target_compile_options(wforge-dump PRIVATE -Wall -Wextra)
endif()

add_executable(wforge-raster-demo src/raster_demo.cpp)
target_link_libraries(wforge-raster-demo PRIVATE wforge)

add_executable(wforge-debug-demo src/debug_demo.cpp)
target_link_libraries(wforge-debug-demo PRIVATE wforge)

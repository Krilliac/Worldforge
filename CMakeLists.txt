cmake_minimum_required(VERSION 3.16)
project(worldforge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On Windows, socket/StormLib code pulls in <windows.h>, whose min/max macros
# clobber std::min/std::max and whose bulk slows compiles. Suppress both engine-
# wide so the same source builds clean on MSVC and MinGW as it does on GCC/Clang.
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

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
  src/fxbridge.cpp
  src/dbc_defs.cpp
  src/gridmap.cpp
  src/navmesh.cpp
  src/vmap.cpp
  src/rhi_software.cpp
  src/server/world_sim.cpp
  src/m2_render.cpp
  src/scene.cpp
  src/terrain_render.cpp
  src/asset_loader.cpp
  src/client_data.cpp
  src/wav.cpp
  src/audio.cpp
  src/sound_entries.cpp
  src/sound_player.cpp
  src/world_view.cpp
  src/picking.cpp
  src/bounds.cpp
  src/scene_pick.cpp
  src/wmo_render.cpp
)
target_include_directories(wforge PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${stormlib_SOURCE_DIR}/src        # StormLib.h lives here
)
target_link_libraries(wforge PUBLIC storm)
# StormLib.h (v9.31) emits an MSVC #pragma comment(lib, "StormLib<cfg>.lib") auto-
# link directive (e.g. StormLibRAD.lib) in any consumer TU. That name never matches
# the target's actual output ("StormLib.lib"), so MSVC fails with LNK1104. The header
# gates this on __STORMLIB_NO_STATIC_LINK__ (StormLib's own sources self-suppress via
# __STORMLIB_SELF__). Define it PUBLIC so mpq.cpp and every downstream target skip the
# pragma and let CMake drive the linking. (The target's INTERFACE STORMLIB_NO_AUTO_LINK
# is a no-op here -- that macro does not exist in this header version.)
target_compile_definitions(wforge PUBLIC __STORMLIB_NO_STATIC_LINK__)

add_executable(wforge-dump src/main.cpp)
target_link_libraries(wforge-dump PRIVATE wforge)

# ---------------------------------------------------------------------------
# Standalone bridge server: the runnable WorldForgeBridge analog (sockets +
# WorldSim, no mangos headers). Lets the full editor<->server round-trip run
# and be tested here, and gives the real editor a target to point at.
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
add_library(wforge-server STATIC src/server/stub_server.cpp)
target_include_directories(wforge-server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wforge-server PUBLIC wforge Threads::Threads)
if(WIN32)
  target_link_libraries(wforge-server PUBLIC ws2_32)
endif()

add_executable(wforge-stub-server tools/stub_server_main.cpp)
target_link_libraries(wforge-stub-server PRIVATE wforge-server)

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
  tests/test_fxbridge.cpp
  tests/test_integration.cpp
  tests/test_serverdata.cpp
  tests/test_rhi.cpp
  tests/test_world_sim.cpp
  tests/test_render.cpp
  tests/test_asset.cpp
  tests/test_client_data.cpp
  tests/test_audio.cpp
  tests/test_world_view.cpp
  tests/test_picking.cpp
  tests/test_bounds.cpp
  tests/test_wmo_pick.cpp
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

add_executable(wforge-terrain-demo src/terrain_demo.cpp)
target_link_libraries(wforge-terrain-demo PRIVATE wforge)

# M2 model geometry validation on real vanilla models: parse + skin a bind pose,
# report vertex/triangle/bone counts + bounds, and render the model to a PNG.
add_executable(wforge-m2dump src/m2_demo.cpp)
target_link_libraries(wforge-m2dump PRIVATE wforge)

# ---------------------------------------------------------------------------
# Editor: Dear ImGui panels. ImGui core compiles with no GPU/display (it only
# emits draw data), so the panels + a headless smoke test build everywhere.
# The runnable windowed app (GLFW + OpenGL backend) is gated separately.
# ---------------------------------------------------------------------------
option(WFORGE_EDITOR     "Build the ImGui editor panels + headless tests" ON)
option(WFORGE_EDITOR_APP "Build the runnable windowed editor (needs GLFW + OpenGL)" OFF)
option(WFORGE_RHI_GL     "Build the OpenGL 3.3 RHI backend (needs glad + a GL context)" OFF)
option(WFORGE_AUDIO      "Build the miniaudio host backend (needs an audio device)" OFF)

# Audio host backend -- the device realisation of AudioDevice, backed by
# miniaudio (single-header). OFF by default like the GL RHI: it opens a real
# audio device, while NullAudioDevice is the tested realisation here. When ON,
# FetchContent pulls miniaudio and a wforge-audio library is built; the engine
# core (NullAudioDevice, WAV codec, SoundPlayer) builds everywhere regardless.
if(WFORGE_AUDIO)
  include(FetchContent)
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21)
  FetchContent_MakeAvailable(miniaudio)
  add_library(wforge-audio STATIC src/audio_miniaudio.cpp)
  target_compile_definitions(wforge-audio PUBLIC WFORGE_AUDIO)
  target_include_directories(wforge-audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${miniaudio_SOURCE_DIR})
  target_link_libraries(wforge-audio PUBLIC wforge ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# OpenGL RHI backend -- the GPU realisation of rhi::Device. OFF by default: it
# needs a GL 3.3 loader (glad) and a current context, so it builds on a desktop,
# not in headless CI (the SoftwareDevice is the tested realisation here).
if(WFORGE_RHI_GL)
  find_package(OpenGL REQUIRED)
  add_library(wforge-rhi-gl STATIC src/rhi_gl.cpp)
  target_include_directories(wforge-rhi-gl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  # Provide a glad target (e.g. add_subdirectory(external/glad)) before enabling.
  target_link_libraries(wforge-rhi-gl PUBLIC wforge glad OpenGL::GL)
endif()

set(IMGUI_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUIZMO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imguizmo/src)

if(WFORGE_EDITOR)
  if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
    message(FATAL_ERROR "Dear ImGui submodule missing. Run: git submodule update --init --recursive")
  endif()

  # ImGui core + ImGuizmo (in-viewport transform handles). ImGuizmo only emits
  # ImGui draw commands, so it builds headlessly too.
  add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
  )
  target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUIZMO_DIR})

  add_library(wforge-editor STATIC
    src/editor/AtmospherePanel.cpp
    src/editor/DebugVisPanel.cpp
    src/editor/GizmoController.cpp
    src/editor/SoftwareImGui.cpp
    src/editor/ViewportPanel.cpp
    src/editor/BridgeClient.cpp
    src/editor/ModelViewerPanel.cpp
    src/editor/EntityInspectorPanel.cpp
  )
  target_include_directories(wforge-editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(wforge-editor PUBLIC wforge imgui)
  find_package(Threads REQUIRED)
  target_link_libraries(wforge-editor PUBLIC Threads::Threads)
  if(WIN32)
    target_link_libraries(wforge-editor PUBLIC ws2_32)
  endif()
  if(NOT MSVC)
    target_compile_options(wforge-editor PRIVATE -Wall -Wextra)
  endif()

  add_executable(wforge-editor-tests
    tests/editor/test_editor_main.cpp
    tests/editor/test_panels.cpp
    tests/editor/test_gizmo_controller.cpp
    tests/editor/test_software_imgui.cpp
    tests/editor/test_viewport.cpp
    tests/editor/test_bridge_client.cpp
    tests/editor/test_e2e.cpp
    tests/editor/test_model_viewer.cpp
    tests/editor/test_entity_inspector.cpp
    tests/editor/test_move_emitter.cpp
    tests/editor/test_placement_editor.cpp
  )
  target_link_libraries(wforge-editor-tests PRIVATE wforge-editor wforge-server)
  target_include_directories(wforge-editor-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME editor COMMAND wforge-editor-tests)

  # Headless editor: composites the CPU scene + CPU ImGui to a PNG (no GPU).
  add_executable(wforge-editor-headless src/editor/headless_main.cpp)
  target_link_libraries(wforge-editor-headless PRIVATE wforge-editor)

  # Headless model viewer (animated M2 + playback panel -> PNG).
  add_executable(wforge-model-viewer src/editor/modelviewer_main.cpp)
  target_link_libraries(wforge-model-viewer PRIVATE wforge-editor)
endif()

# Scene demo: terrain + textured instances + overlay -> PNG.
add_executable(wforge-scene-demo src/scene_demo.cpp)
target_link_libraries(wforge-scene-demo PRIVATE wforge)
if(WFORGE_EDITOR)
  target_include_directories(wforge-scene-demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# Runnable windowed editor: ImGui GLFW + OpenGL3 backends (bundled in the
# submodule). OFF by default -- needs a GL/GLFW toolchain + a display, so it is
# built on a desktop, not in headless CI.
if(WFORGE_EDITOR_APP)
  find_package(OpenGL REQUIRED)
  find_package(glfw3 QUIET)
  if(NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/glfw)   # provide via: git submodule add .../glfw external/glfw
  endif()
  add_executable(wforge-editor-app
    src/editor/app/main.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
  )
  target_include_directories(wforge-editor-app PRIVATE ${IMGUI_DIR}/backends)
  target_link_libraries(wforge-editor-app PRIVATE wforge-editor glfw OpenGL::GL)
endif()

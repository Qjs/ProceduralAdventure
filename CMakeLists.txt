cmake_minimum_required(VERSION 3.22)
project(ProceduralAdventure C)
enable_language(CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build SDL3 as a static library from submodule
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/SDL)

# Build cimgui as a static library
set(CIMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/cimgui)
set(IMGUI_DIR  ${CIMGUI_DIR}/imgui)

add_library(cimgui STATIC
    ${CIMGUI_DIR}/cimgui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(cimgui PUBLIC
    ${CIMGUI_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(cimgui PUBLIC SDL3::SDL3-static)

# Delaunator-c library
add_library(delaunator STATIC
    lib/delaunator-c/delaunator.c
)
target_include_directories(delaunator PUBLIC lib/delaunator-c)

# Box2D submodule
set(HAVE_BOX2D OFF)
if(EXISTS ${CMAKE_SOURCE_DIR}/lib/box2d/CMakeLists.txt)
    add_subdirectory(lib/box2d)
    set(HAVE_BOX2D ON)
endif()

# SDL3Starter executable
add_executable(ProceduralAdventure
    src/main.c
    src/imgui_sdl3.cpp
    src/app.c
    src/stb_impl.c
    src/mapgen/mg_points.c
    src/mapgen/mg_mesh.c
    src/mapgen/mg_island.c
    src/mapgen/mg_elevation.c
    src/mapgen/mg_raster.c
    src/mapgen/mg_map.c
    src/mapgen/mg_rivers.c
    src/game/g_game.c
    src/game/g_terrain.c
    src/game/g_unit.c
    src/game/g_render.c
    src/game/g_render_primitives.c
    src/game/g_render_entities.c
    src/game/g_render_effects.c
    src/game/g_enemy.c
    src/game/g_combat.c
    src/game/g_unit_gen.c
    src/game/g_particles.c
    src/game/g_physics.c
)

target_include_directories(ProceduralAdventure PRIVATE
    src
    lib/stb
    lib/delaunator-c
)
target_link_libraries(ProceduralAdventure PRIVATE
    SDL3::SDL3-static
    cimgui
    delaunator
    m
)

if(HAVE_BOX2D)
    target_compile_definitions(ProceduralAdventure PRIVATE HAVE_BOX2D=1)
    target_link_libraries(ProceduralAdventure PRIVATE box2d)
endif()

if(EMSCRIPTEN)
    set_target_properties(ProceduralAdventure PROPERTIES SUFFIX ".html")

    target_compile_options(ProceduralAdventure PRIVATE -fno-exceptions -fno-rtti)
    target_compile_options(cimgui PRIVATE -fno-exceptions -fno-rtti)

    target_link_options(ProceduralAdventure PRIVATE
        -sINITIAL_MEMORY=32MB
        -sALLOW_MEMORY_GROWTH=1
        -sMAXIMUM_MEMORY=256mb
        -sSTACK_SIZE=256KB
        -sENVIRONMENT=web
        -sWASM_BIGINT
        -sDISABLE_EXCEPTION_CATCHING=1
        -sEVAL_CTORS=1
        --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets
        --shell-file ${CMAKE_SOURCE_DIR}/shell.html
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(ProceduralAdventure PRIVATE -O3 -flto)
        target_link_options(ProceduralAdventure PRIVATE -O3 -flto)
    endif()
endif()

cmake_minimum_required(VERSION 3.20)

project(FrontierEngine
    VERSION 0.1.0
    DESCRIPTION "Procedural first-person outdoor sandbox engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FRONTIER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(FRONTIER_USE_SYSTEM_GLM "Use a system GLM instead of the bundled headers" OFF)
option(FRONTIER_FETCH_SDL2 "Download and build SDL2 automatically when it is not installed" OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# SDL2: prefer an installed package, then pkg-config, then FRONTIER_SDL2_ROOT.
find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FRONTIER_SDL2 QUIET sdl2)
    endif()
endif()

if(NOT TARGET SDL2::SDL2 AND FRONTIER_SDL2_ROOT)
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    find_library(FRONTIER_SDL2_LIBRARY
        NAMES SDL2 libSDL2
        PATHS "${FRONTIER_SDL2_ROOT}/lib" "${FRONTIER_SDL2_ROOT}/lib64"
        NO_DEFAULT_PATH)
    if(NOT FRONTIER_SDL2_LIBRARY)
        message(FATAL_ERROR "FRONTIER_SDL2_ROOT was set but no SDL2 library was found under ${FRONTIER_SDL2_ROOT}/lib")
    endif()
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${FRONTIER_SDL2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FRONTIER_SDL2_ROOT}/include/SDL2;${FRONTIER_SDL2_ROOT}/include")
endif()

if(NOT TARGET SDL2::SDL2 AND FRONTIER_SDL2_FOUND)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FRONTIER_SDL2_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FRONTIER_SDL2_LIBRARIES}"
        INTERFACE_LINK_DIRECTORIES "${FRONTIER_SDL2_LIBRARY_DIRS}")
endif()

# Optional: fetch and build SDL2 from source. This is the easiest route on
# Windows, where SDL2 is usually not installed system-wide.
if(NOT TARGET SDL2::SDL2 AND FRONTIER_FETCH_SDL2)
    include(FetchContent)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    # Overridable so the same build works behind a proxy, from a mirror, or
    # from a local tarball (file:///...).
    set(FRONTIER_SDL2_URL
        "https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-2.28.5"
        CACHE STRING "URL of the SDL2 source archive used by FRONTIER_FETCH_SDL2")
    set(FRONTIER_SDL2_URL_HASH
        "SHA256=9f0556e4a24ef5b267010038ad9e9948b62f236d5bcc4b22179f95ef62d84023"
        CACHE STRING "Hash of FRONTIER_SDL2_URL (set to empty to skip verification)")
    message(STATUS "SDL2 not found locally: fetching and building it from ${FRONTIER_SDL2_URL}")
    if(POLICY CMP0135)
        # Keep archive timestamps from deciding whether sources look stale.
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(SDL2
        URL "${FRONTIER_SDL2_URL}"
        URL_HASH "${FRONTIER_SDL2_URL_HASH}")
    FetchContent_MakeAvailable(SDL2)
endif()

if(NOT TARGET SDL2::SDL2 AND TARGET SDL2::SDL2-static)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_LINK_LIBRARIES SDL2::SDL2-static
        INTERFACE_INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:SDL2::SDL2-static,INTERFACE_INCLUDE_DIRECTORIES>")
endif()

if(NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR
        "SDL2 was not found.\n"
        "Install SDL2 development files (Debian/Ubuntu: sudo apt install libsdl2-dev,\n"
        "Fedora: sudo dnf install SDL2-devel, macOS: brew install sdl2) or configure with\n"
        "  -DFRONTIER_SDL2_ROOT=/path/to/sdl2-install\n"
        "or let CMake download and build SDL2 for you:\n"
        "  cmake -S . -B build -DFRONTIER_FETCH_SDL2=ON")
endif()

# ---------------------------------------------------------------------------
# Dear ImGui (vendored) + the engine's OpenGL loader backend
# ---------------------------------------------------------------------------
add_library(frontier_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
    src/ui/ImGuiGLBackend.cpp
)

target_include_directories(frontier_imgui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(frontier_imgui PUBLIC SDL2::SDL2)

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------
set(FRONTIER_SOURCES
    src/main.cpp
    src/core/Config.cpp
    src/core/Engine.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/Noise.cpp
    src/core/Time.cpp
    src/core/Window.cpp
    src/game/Player.cpp
    src/render/Framebuffer.cpp
    src/render/GeometryBuilder.cpp
    src/render/GL.cpp
    src/render/Mesh.cpp
    src/render/Renderer.cpp
    src/render/Shader.cpp
    src/render/Texture.cpp
    src/scene/Camera.cpp
    src/scene/Frustum.cpp
    src/scene/Terrain.cpp
    src/scene/TimeOfDay.cpp
    src/scene/Vegetation.cpp
    src/scene/Water.cpp
    src/ui/DebugUI.cpp
    src/world/World.cpp
)

add_executable(frontier ${FRONTIER_SOURCES})

target_include_directories(frontier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
)

if(FRONTIER_USE_SYSTEM_GLM)
    find_package(glm CONFIG REQUIRED)
    target_link_libraries(frontier PRIVATE glm::glm)
else()
    target_include_directories(frontier PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm)
endif()

target_link_libraries(frontier PRIVATE frontier_imgui SDL2::SDL2)

# SDL redefines main() to SDL_main() on Windows unless this is set. Frontier
# uses a plain console main() entry point, so keep the standard entry point
# instead of pulling in the SDL2main shim.
if(WIN32)
    target_compile_definitions(frontier PRIVATE SDL_MAIN_HANDLED)
endif()

# windows.h's min()/max() macros would break std::max/std::min and GLM. Define
# NOMINMAX (and WIN32_LEAN_AND_MEAN) for every translation unit, not just the
# ones that happen to include our math header first.
if(WIN32)
    target_compile_definitions(frontier PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(frontier_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# GL entry points are resolved through SDL_GL_GetProcAddress; on Linux the
# dynamic loader still wants libdl for SDL's own dlsym usage.
if(UNIX AND NOT APPLE)
    target_link_libraries(frontier PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()

if(MSVC)
    target_compile_options(frontier PRIVATE /W4 /permissive- /utf-8)
    if(FRONTIER_WARNINGS_AS_ERRORS)
        target_compile_options(frontier PRIVATE /WX)
    endif()
else()
    target_compile_options(frontier PRIVATE -Wall -Wextra -Wpedantic)
    if(FRONTIER_WARNINGS_AS_ERRORS)
        target_compile_options(frontier PRIVATE -Werror)
    endif()
endif()

# Keep the runtime asset layout next to the executable for one-command runs.
# This runs on every build so shader edits are picked up without relinking.
add_custom_target(frontier_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:frontier>/assets"
    DEPENDS frontier
    COMMENT "Copying shader assets next to the executable")

# On Windows the SDL2 runtime DLL must sit next to the executable. Imported
# shared targets know their own DLL location, so copy it after linking.
if(WIN32 AND TARGET SDL2::SDL2)
    get_target_property(FRONTIER_SDL2_TYPE SDL2::SDL2 TYPE)
    if(FRONTIER_SDL2_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET frontier POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:frontier>"
            COMMENT "Copying the SDL2 runtime next to the executable")
    endif()
endif()

# Optional: make the working directory of a debug launch the build directory.
set_target_properties(frontier PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:frontier>")

install(TARGETS frontier RUNTIME DESTINATION bin)
install(DIRECTORY assets DESTINATION bin)

get_target_property(FRONTIER_SDL2_INCLUDES SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Frontier Engine: SDL2 include directories: ${FRONTIER_SDL2_INCLUDES}")
message(STATUS "Frontier Engine: build type ${CMAKE_BUILD_TYPE}, C++${CMAKE_CXX_STANDARD}")

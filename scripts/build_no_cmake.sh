#!/usr/bin/env bash
# Derecho Engine - fallback build without CMake.
#
# The primary build system is CMake (see README.md). This script exists for
# minimal environments or CI images where CMake is unavailable. It expects SDL2
# development headers and libraries to be installed, or SDL2_ROOT to point at a
# local SDL2 install (with include/SDL2 and lib/).
#
# Usage:
#   ./scripts/build_no_cmake.sh            # release build into ./build
#   BUILD_TYPE=debug ./scripts/build_no_cmake.sh
#   JOBS=8 ./scripts/build_no_cmake.sh
#   SDL2_ROOT=/opt/sdl2 ./scripts/build_no_cmake.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CXX="${CXX:-g++}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
OUT="${OUT:-build}"
OBJ="$OUT/obj"

SDL_CFLAGS=""
SDL_LIBS="-lSDL2"
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
    SDL_CFLAGS="$(pkg-config --cflags sdl2)"
    SDL_LIBS="$(pkg-config --libs sdl2)"
elif [[ -n "${SDL2_ROOT:-}" ]]; then
    SDL_CFLAGS="-I${SDL2_ROOT}/include/SDL2 -I${SDL2_ROOT}/include"
    SDL_LIBS="-L${SDL2_ROOT}/lib -lSDL2 -Wl,-rpath,${SDL2_ROOT}/lib"
else
    echo "error: SDL2 not found. Install libsdl2-dev or set SDL2_ROOT." >&2
    exit 1
fi

if [[ "${BUILD_TYPE:-release}" == "debug" ]]; then
    OPT_FLAGS="-O0 -g"
else
    OPT_FLAGS="-O2 -DNDEBUG"
fi

INCLUDES=(
    -I"$ROOT/src"
    -I"$ROOT/third_party"
    -I"$ROOT/third_party/glm"
    -I"$ROOT/third_party/imgui"
    -I"$ROOT/third_party/imgui/backends"
)
# SDL2 include flags may contain spaces; split intentionally.
read -r -a SDL_CFLAGS_ARR <<<"$SDL_CFLAGS"

SOURCES=(
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
    src/ui/ImGuiGLBackend.cpp
    src/world/World.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
)

mkdir -p "$OBJ"
echo "Compiling ${#SOURCES[@]} translation units with $JOBS jobs..."
# Portable-ish parallelism without requiring GNU parallel.
pids=()
for src in "${SOURCES[@]}"; do
    obj="$OBJ/$(echo "$src" | tr '/' '_').o"
    "$CXX" -std=c++20 $OPT_FLAGS -Wall -Wextra \
        "${INCLUDES[@]}" "${SDL_CFLAGS_ARR[@]}" -c "$src" -o "$obj" &
    pids+=($!)
    while (( $(jobs -rp | wc -l) >= JOBS )); do
        wait -n || { echo "compilation failed" >&2; exit 1; }
    done
done
for pid in "${pids[@]}"; do
    wait "$pid" || { echo "compilation failed" >&2; exit 1; }
done

echo "Linking $OUT/derecho..."
"$CXX" -O2 "$OBJ"/*.o -o "$OUT/derecho" $SDL_LIBS -ldl -lpthread -lm

# Runtime assets next to the executable.
mkdir -p "$OUT/assets"
cp -r "$ROOT/assets/." "$OUT/assets/"

echo "Done: $OUT/derecho"
echo "Run: cd $OUT && ./derecho   (headless smoke test: ./derecho --headless=120)"

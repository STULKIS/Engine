// Compiles Dear ImGui's OpenGL3 backend against the engine's runtime loader
// instead of bundling a second GL loader.
//
// IMGUI_IMPL_OPENGL_LOADER_CUSTOM tells the backend not to include its private
// gl3w-based loader; the engine's own GL.h provides the entry points.
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM

#include "render/GL.h"
using namespace derecho::render::gl;

#include "imgui_impl_opengl3.cpp"

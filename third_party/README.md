# Third-party dependencies

Frontier Engine vendors only the parts needed to build reproducibly without a
package manager. Everything here is permissively licensed; the upstream license
text is retained in the corresponding directory where one was shipped.

| Directory | Project | Version | License | Source |
| --- | --- | --- | --- | --- |
| `glm/` | OpenGL Mathematics (GLM) | 0.9.9.8 | MIT (`glm/copying.txt`) | https://github.com/g-truc/glm |
| `imgui/` | Dear ImGui | 1.91.0 | MIT (`imgui/LICENSE.txt`) | https://github.com/ocornut/imgui |
| `gl/glcorearb.h` | Khronos OpenGL Registry (core profile header) | registry snapshot | Apache-2.0 / MIT (header text) | https://github.com/KhronosGroup/OpenGL-Registry |
| `KHR/khrplatform.h` | Khronos platform types | registry snapshot | Apache-2.0 / MIT (header text) | https://github.com/KhronosGroup/EGL-Registry |

Notes:

- `third_party/gl/glcorearb.h` is included for OpenGL **types and enumerants
  only**. The project never defines `GL_GLEXT_PROTOTYPES`; all entry points are
  resolved at runtime through `SDL_GL_GetProcAddress` (see `src/render/GL.*`).
  This removes the need for a system OpenGL development package.
- `third_party/imgui/backends` contains only `imgui_impl_sdl2.*`,
  `imgui_impl_opengl3.*` and `imgui_impl_opengl3_loader.h` (the latter is unused
  at build time because the engine compiles the backend against its own loader
  via `src/ui/ImGuiGLBackend.cpp`).
- SDL2 is **not** vendored; it is a build dependency (see the top-level README
  for install commands).

Updating GLM or Dear ImGui: replace the files in place, rebuild, and re-run
`scripts/validate_shaders.py` plus `./frontier --headless=120`.

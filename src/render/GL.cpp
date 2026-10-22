#include "render/GL.h"

#include "core/Log.h"

namespace frontier::render::gl {

#define FRONTIER_GL_DEFINE(type, name) type name = nullptr;
FRONTIER_GL_FUNCTIONS(FRONTIER_GL_DEFINE)
#undef FRONTIER_GL_DEFINE

bool LoadFunctions(void* (*getProcAddress)(const char*)) {
    int missing = 0;

#define FRONTIER_GL_LOAD(type, name)                                            \
    name = reinterpret_cast<type>(getProcAddress(#name));                       \
    if (!name) {                                                                \
        FR_ERROR("OpenGL entry point missing: %s", #name);                      \
        ++missing;                                                              \
    }
    FRONTIER_GL_FUNCTIONS(FRONTIER_GL_LOAD)
#undef FRONTIER_GL_LOAD

    if (missing > 0) {
        FR_ERROR("%d OpenGL 4.5 entry point(s) could not be resolved.", missing);
        return false;
    }
    FR_INFO("Resolved all OpenGL 4.5 entry points through SDL_GL_GetProcAddress.");
    return true;
}

const char* ErrorString(GLenum error) {
    switch (error) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "GL_UNKNOWN_ERROR";
    }
}

bool CheckErrors(const char* context) {
    bool clean = true;
    for (int i = 0; i < 16; ++i) {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) {
            break;
        }
        clean = false;
        FR_WARN("OpenGL error in %s: %s (0x%04X)", context, ErrorString(err), static_cast<unsigned>(err));
    }
    return clean;
}

} // namespace frontier::render::gl

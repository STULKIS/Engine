#include "core/Window.h"

#include "core/Log.h"

#include <algorithm>
#include <cstdio>

namespace frontier::core {
namespace {

void SetGLAttributes(bool vsync) {
    (void)vsync;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
}

} // namespace

Window::~Window() {
    Destroy();
}

bool Window::Create(const std::string& title, int width, int height, bool fullscreen, bool resizable, bool vsync) {
    m_title = title;
    m_width = width;
    m_height = height;
    m_fullscreen = fullscreen;
    m_resizable = resizable;
    m_vsync = vsync;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        FR_ERROR("SDL video subsystem init failed: %s", SDL_GetError());
        return false;
    }

    if (vsync) {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
        SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    }

    SetGLAttributes(vsync);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, flags);
    if (!m_window) {
        FR_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SetGLAttributes(vsync);
        // Retry with the smallest sane request before giving up.
        m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (!m_window) {
            FR_ERROR("SDL_CreateWindow retry failed: %s", SDL_GetError());
            return false;
        }
    }

    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context) {
        FR_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        FR_ERROR("Frontier Engine requires an OpenGL 4.5 core profile context.");
        const char* driver = SDL_GetCurrentVideoDriver();
        FR_ERROR("SDL video driver in use: %s", driver ? driver : "unknown");
        FR_ERROR("Things to check:");
        FR_ERROR("  1. Update the GPU driver (NVIDIA / AMD / Intel), then reboot.");
        FR_ERROR("  2. Remote desktop sessions often expose no OpenGL 4.5; run locally.");
        FR_ERROR("  3. On laptops, force the discrete GPU for this executable.");
        FR_ERROR("  4. Do not force an old GL version through driver profiles or");
        FR_ERROR("     MESA_GL_VERSION_OVERRIDE/__GL_* environment variables.");
        return false;
    }

    if (!SDL_GL_MakeCurrent(m_window, m_context)) {
        FR_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    RefreshDrawableSize();

    // Report what the driver actually handed us (it may be newer than the
    // requested 4.5 core, or a compatibility profile).
    int contextMajor = 0;
    int contextMinor = 0;
    int profileMask = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &contextMajor);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &contextMinor);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profileMask);
    FR_INFO("GL context: %d.%d %s", contextMajor, contextMinor,
            (profileMask & SDL_GL_CONTEXT_PROFILE_CORE) ? "core" : "compatibility");

    FR_INFO("Window created: %s (%dx%d, %s, vsync=%s)",
            title.c_str(), m_width, m_height,
            fullscreen ? "fullscreen" : "windowed", vsync ? "on" : "off");
    return true;
}

void Window::Destroy() {
    if (m_context) {
        SDL_GL_DeleteContext(m_context);
        m_context = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool Window::MakeCurrent() {
    if (!m_window || !m_context) {
        return false;
    }
    return SDL_GL_MakeCurrent(m_window, m_context) == 0;
}

void Window::SwapBuffers() {
    if (m_window) {
        SDL_GL_SwapWindow(m_window);
    }
}

bool Window::Recreate(int width, int height, bool fullscreen) {
    if (!m_window) {
        return false;
    }
    if (fullscreen != m_fullscreen) {
        if (SDL_SetWindowFullscreen(m_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
            FR_WARN("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
            return false;
        }
    }
    if (!fullscreen) {
        SDL_SetWindowSize(m_window, width, height);
    }
    RefreshDrawableSize();
    m_fullscreen = fullscreen;
    return true;
}

void Window::RefreshDrawableSize() {
    if (!m_window) {
        return;
    }
    int drawableWidth = 0;
    int drawableHeight = 0;
    SDL_GL_GetDrawableSize(m_window, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0 || drawableHeight <= 0) {
        SDL_GetWindowSize(m_window, &drawableWidth, &drawableHeight);
    }
    m_width = std::max(1, drawableWidth);
    m_height = std::max(1, drawableHeight);
}

void Window::SetVSync(bool enabled) {
    if (m_vsync == enabled) {
        return;
    }
    if (SDL_GL_SetSwapInterval(enabled ? 1 : 0) == 0) {
        m_vsync = enabled;
        FR_INFO("VSync %s.", enabled ? "enabled" : "disabled");
    } else {
        FR_WARN("Could not change swap interval: %s", SDL_GetError());
    }
}

bool Window::ToggleFullscreen() {
    return Recreate(m_width, m_height, !m_fullscreen);
}

std::string Window::Title() const {
    return m_title;
}

} // namespace frontier::core

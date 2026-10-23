#include "core/Window.h"

#include "core/Log.h"

#include <algorithm>
#include <cstdio>

namespace derecho::core {
namespace {

// Resolves glGetString without the engine's GL loader (which is only loaded
// after the window exists) so the context can be probed right here.
using GLGetStringProc = const unsigned char* (*)(unsigned int);
constexpr unsigned int kGLVersion = 0x1F02;

const char* ProbeGLVersion() {
    static GLGetStringProc getString = nullptr;
    static bool resolved = false;
    if (!resolved) {
        getString = reinterpret_cast<GLGetStringProc>(SDL_GL_GetProcAddress("glGetString"));
        resolved = true;
    }
    if (!getString) {
        return nullptr;
    }
    return reinterpret_cast<const char*>(getString(kGLVersion));
}

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
    int desktopWidth = 0;
    int desktopHeight = 0;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        DE_ERROR("SDL video subsystem init failed: %s", SDL_GetError());
        return false;
    }

    if (vsync) {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
        SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    }

    // A 1600x900 default window does not fit on a 1366x768 laptop panel; the
    // window ends up larger than the desktop and can be partly off-screen,
    // which reads as "it started but I see nothing". Shrink it to the usable
    // desktop area when the request does not fit.
    if (!fullscreen) {
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
            desktopWidth = usable.w;
            desktopHeight = usable.h;
            const int maxWidth = std::max(640, usable.w - 80);
            const int maxHeight = std::max(480, usable.h - 120);
            if (width > maxWidth || height > maxHeight) {
                DE_WARN("Requested window %dx%d does not fit the %dx%d desktop; using %dx%d.",
                        width, height, usable.w, usable.h, std::min(width, maxWidth),
                        std::min(height, maxHeight));
                width = std::min(width, maxWidth);
                height = std::min(height, maxHeight);
            }
        }
    }
    m_width = width;
    m_height = height;

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
        DE_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SetGLAttributes(vsync);
        // Retry with the smallest sane request before giving up.
        m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    640, 360, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (!m_window) {
            DE_ERROR("SDL_CreateWindow retry failed: %s", SDL_GetError());
            return false;
        }
    }

    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context) {
        DE_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        DE_ERROR("Derecho Engine requires an OpenGL 4.5 core profile context.");
        const char* driver = SDL_GetCurrentVideoDriver();
        DE_ERROR("SDL video driver in use: %s", driver ? driver : "unknown");
        DE_ERROR("Things to check:");
        DE_ERROR("  1. Update the GPU driver (NVIDIA / AMD / Intel), then reboot.");
        DE_ERROR("  2. Remote desktop sessions often expose no OpenGL 4.5; run locally.");
        DE_ERROR("  3. On laptops, force the discrete GPU for this executable.");
        DE_ERROR("  4. Do not force an old GL version through driver profiles or");
        DE_ERROR("     MESA_GL_VERSION_OVERRIDE/__GL_* environment variables.");
        return false;
    }

    // SDL_GL_CreateContext already makes the new context current, so asking for
    // it again is a no-op on a correct driver -- but some Windows drivers fail
    // a redundant wglMakeCurrent for an already-current context, which used to
    // abort start-up even though the context was perfectly usable. Only make it
    // current when it really is not, and never treat that as fatal on its own:
    // the context is verified by probing glGetString below.
    std::string makeCurrentError;
    if (SDL_GL_GetCurrentContext() != m_context) {
        const int result = SDL_GL_MakeCurrent(m_window, m_context);
        if (result != 0) {
            makeCurrentError = SDL_GetError() ? SDL_GetError() : "";
            DE_WARN("SDL_GL_MakeCurrent returned %d (%s); verifying the context directly.",
                    result, makeCurrentError.empty() ? "no error reported by SDL" : makeCurrentError.c_str());
        }
    } else {
        DE_DEBUG("Context already current after creation; skipping MakeCurrent.");
    }

    if (!ProbeGLVersion()) {
        // Give the driver one clean retry before giving up: creating a context
        // while another one is current is a known source of driver bugs.
        DE_WARN("No usable OpenGL context after creation; recreating it once.");
        SDL_GL_DeleteContext(m_context);
        m_context = SDL_GL_CreateContext(m_window);
        if (m_context && SDL_GL_GetCurrentContext() != m_context) {
            SDL_GL_MakeCurrent(m_window, m_context);
        }
    }

    const char* probedVersion = m_context ? ProbeGLVersion() : nullptr;
    if (!probedVersion) {
        DE_ERROR("No usable OpenGL context is current after creation.");
        if (!makeCurrentError.empty()) {
            DE_ERROR("wglMakeCurrent/SDL_GL_MakeCurrent reported: %s", makeCurrentError.c_str());
        }
        DE_ERROR("The window and context were created, but the driver will not "
                 "activate the context for rendering.");
        DE_ERROR("This is almost always a driver or session problem, not a setting:");
        DE_ERROR("  1. Install the current GPU driver from the vendor (not Windows Update).");
        DE_ERROR("  2. Remote desktop / RDP, virtual machines and Windows Sandbox often");
        DE_ERROR("     cannot activate a hardware OpenGL 4.5 context; run on the local desktop.");
        DE_ERROR("  3. On hybrid laptops, force the discrete GPU for derecho.exe.");
        DE_ERROR("  4. Close tools that hook OpenGL (overlays, capture software) and retry.");
        DE_ERROR("Run 'derecho --glinfo' afterwards to print the full GL environment.");
        return false;
    }
    DE_INFO("OpenGL context active: %s", probedVersion);

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
    DE_INFO("GL context: %d.%d %s", contextMajor, contextMinor,
            (profileMask & SDL_GL_CONTEXT_PROFILE_CORE) ? "core" : "compatibility");

    DE_INFO("Window created: %s (%dx%d, %s, vsync=%s)",
            title.c_str(), m_width, m_height,
            fullscreen ? "fullscreen" : "windowed", vsync ? "on" : "off");
    if (const char* driver = SDL_GetCurrentVideoDriver()) {
        DE_INFO("SDL video driver: %s, desktop %dx%d.", driver, desktopWidth, desktopHeight);
    }
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
            DE_WARN("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
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
        DE_INFO("VSync %s.", enabled ? "enabled" : "disabled");
    } else {
        DE_WARN("Could not change swap interval: %s", SDL_GetError());
    }
}

bool Window::ToggleFullscreen() {
    return Recreate(m_width, m_height, !m_fullscreen);
}

void Window::SetIcon(const uint8_t* rgba, int width, int height) {
    if (!m_window || !rgba || width <= 0 || height <= 0) {
        return;
    }
    // The engine's own PNG decoder produced these pixels, so no SDL_image is
    // needed just to brand the window.
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint8_t*>(rgba), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        DE_WARN("Could not create the window icon surface: %s", SDL_GetError());
        return;
    }
    SDL_SetWindowIcon(m_window, surface);
    SDL_FreeSurface(surface);
}

std::string Window::Title() const {
    return m_title;
}

} // namespace derecho::core

// Derecho Engine - SDL2 window, OpenGL 4.5 context and swap control.
#pragma once

#include <SDL.h>

#include <string>

namespace derecho::core {

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Creates the SDL window and a 4.5 core profile context. Fails cleanly when
    // the platform cannot provide one (the caller can then fall back to headless).
    bool Create(const std::string& title, int width, int height, bool fullscreen, bool resizable, bool vsync);

    void Destroy();

    // Recreates the GL context bound to the window (used on display change).
    bool MakeCurrent();
    void SwapBuffers();

    SDL_Window* Handle() const { return m_window; }
    SDL_GLContext Context() const { return m_context; }

    int Width() const { return m_width; }
    int Height() const { return m_height; }
    // Re-queries the drawable (framebuffer) size, which can differ from the
    // window size on HiDPI displays.
    void RefreshDrawableSize();
    float Aspect() const { return m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height) : 1.0f; }
    void SetSize(int width, int height) { m_width = width; m_height = height; }

    // Recreates the window at a new geometry (fullscreen toggle / resize).
    bool Recreate(int width, int height, bool fullscreen);
    bool ToggleFullscreen();
    bool IsFullscreen() const { return m_fullscreen; }
    void SetVSync(bool enabled);

    // Sets the taskbar/titlebar icon from tightly packed RGBA8 pixels.
    void SetIcon(const uint8_t* rgba, int width, int height);

    std::string Title() const;

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    std::string m_title;
    int m_width = 0;
    int m_height = 0;
    bool m_fullscreen = false;
    bool m_resizable = true;
    bool m_vsync = true;
};

} // namespace derecho::core

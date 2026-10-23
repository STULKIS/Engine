#include "core/Engine.h"

#include "core/Log.h"
#include "core/Window.h"
#include "game/Player.h"
#include "render/GL.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "scene/Camera.h"
#include "scene/Frustum.h"
#include "scene/Terrain.h"
#include "ui/DebugUI.h"
#include "world/World.h"

#include <SDL.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace frontier::core {

using namespace frontier::render::gl;

namespace {

std::string ResolveAssetDirectory(const std::string& requested) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const std::string candidates[] = {
        requested,
        "../" + requested,
        "../../" + requested,
        "../Resources/" + requested,
    };
    for (const std::string& candidate : candidates) {
        if (fs::exists(fs::path(candidate) / "shaders" / "sky.vert", ec)) {
            return candidate;
        }
    }

    // Fallback relative to the executable.
    if (char* basePath = SDL_GetBasePath()) {
        const std::string path = std::string(basePath) + requested;
        SDL_free(basePath);
        if (fs::exists(fs::path(path) / "shaders" / "sky.vert", ec)) {
            return path;
        }
    }
    return requested;
}

// Parses the "major.minor" prefix of a GL version string.
bool ParseVersion(const char* text, int& major, int& minor) {
    if (!text || !text[0]) {
        return false;
    }
    return std::sscanf(text, "%d.%d", &major, &minor) == 2;
}

// Inspects the current GL context and refuses to start on drivers that cannot
// run the engine. Logs the renderer/version so bug reports are actionable.
bool ValidateGLContext() {
    const char* versionString = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* shadingLanguage = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    const char* rendererString = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* vendorString = reinterpret_cast<const char*>(glGetString(GL_VENDOR));

    int major = 0;
    int minor = 0;
    const bool versionParsed = ParseVersion(versionString, major, minor);
    int glslMajor = 0;
    int glslMinor = 0;
    ParseVersion(shadingLanguage, glslMajor, glslMinor);

    GLint profileMask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    const bool coreProfile = (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;

    GLint maxSamples = 0;
    GLint maxTextureSize = 0;
    GLint maxColorAttachments = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);

    FR_INFO("GL version:   %s", versionString ? versionString : "unknown");
    FR_INFO("GLSL version: %s", shadingLanguage ? shadingLanguage : "unknown");
    FR_INFO("GL vendor:    %s", vendorString ? vendorString : "unknown");
    FR_INFO("GL renderer:  %s", rendererString ? rendererString : "unknown");
    FR_INFO("GL profile:   %s, max samples %d, max texture %d, colour attachments %d",
            coreProfile ? "core" : "compatibility", maxSamples, maxTextureSize, maxColorAttachments);

    const std::string rendererName = rendererString ? rendererString : "";
    if (rendererName.find("llvmpipe") != std::string::npos || rendererName.find("softpipe") != std::string::npos ||
        rendererName.find("SwiftShader") != std::string::npos) {
        FR_WARN("Software rasterizer detected; expect low frame rates. Lower --render-scale and view distance.");
    }

    if (!versionParsed) {
        FR_WARN("Could not parse the OpenGL version string; continuing anyway.");
        return true;
    }
    if (major < 4 || (major == 4 && minor < 5)) {
        FR_ERROR("Frontier Engine needs OpenGL 4.5 core; the driver only provides %d.%d.", major, minor);
        FR_ERROR("Update the GPU driver (NVIDIA/AMD/Intel) and try again.");
        FR_ERROR("On Mesa/llvmpipe you can force a higher reported version with");
        FR_ERROR("  MESA_GL_VERSION_OVERRIDE=4.5 MESA_GLSL_VERSION_OVERRIDE=450 ./frontier");
        FR_ERROR("(that only works if the driver actually implements the features).");
        return false;
    }
    if (glslMajor > 0 && (glslMajor < 4 || (glslMajor == 4 && glslMinor < 50))) {
        FR_ERROR("Frontier Engine shaders need GLSL 4.50, but the driver reports %d.%02d.", glslMajor, glslMinor);
        return false;
    }
    if (!coreProfile) {
        FR_WARN("A compatibility profile was created instead of a core profile; this usually still works.");
    }
    return true;
}

std::string TimestampString() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec);
    return buffer;
}

} // namespace

Engine::Engine() = default;

Engine::~Engine() {
    Shutdown();
}

int Engine::Run(AppConfig& config) {
    m_config = config;
    if (m_config.headless) {
        return RunHeadless(m_config);
    }

    if (!Initialize(m_config)) {
        Shutdown();
        return 1;
    }

    if (m_config.selftest) {
        const int exitCode = RunSelfTest();
        Shutdown();
        FR_INFO("Frontier Engine shut down cleanly.");
        return exitCode;
    }

    m_running = true;
    m_clock.Reset();
    m_frameStats = {};
    m_frameStats.ResetWindow();
    double fpsWindowTimer = 0.0;

    while (m_running) {
        const auto frameStart = std::chrono::high_resolution_clock::now();
        const float deltaSeconds = m_clock.Tick();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            HandleEvent(event);
        }
        m_input->NewFrame();

        Update(deltaSeconds);

        // Build the debug UI for this frame.
        m_debugUI->NewFrame();
        ui::DebugActions actions;
        m_debugUI->Draw(m_config, *m_world, *m_player, *m_renderer, m_frameStats, actions);
        ApplyActions(actions);
        if (!m_running) {
            break;
        }

        RenderFrame();
        m_debugUI->Render();
        m_window->SwapBuffers();

        const double cpuMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::high_resolution_clock::now() - frameStart)
                                 .count();
        m_frameStats.Push(deltaSeconds, cpuMs);

        fpsWindowTimer += deltaSeconds;
        if (fpsWindowTimer >= 1.0) {
            FR_INFO("Frame stats: %.1f FPS (avg), %.2f ms frame, %d draw calls, %.2f M triangles, "
                    "%d instances visible, %d culled, time %.2f h (%s).",
                    m_frameStats.smoothedFps, m_frameStats.smoothedDelta * 1000.0f,
                    m_renderer->Stats().drawCalls,
                    static_cast<double>(m_renderer->Stats().triangles) / 1'000'000.0,
                    m_renderer->Stats().visibleInstances, m_renderer->Stats().culledInstances,
                    m_world->Time().Hours(), m_world->Time().PhaseName().c_str());
            fpsWindowTimer = 0.0;
            m_frameStats.ResetWindow();
        }
    }

    Shutdown();
    FR_INFO("Frontier Engine shut down cleanly.");
    return 0;
}

bool Engine::Initialize(AppConfig& config) {
    FR_INFO("Starting Frontier Engine %s", "0.1.0");
    FR_INFO("World config: seed %d, %.0f m, %d quads, %d trees, %d grass.",
            config.world.seed, config.world.terrainSize, config.world.terrainResolution,
            config.world.treeCount, config.world.grassCount);

    config.assetsDir = ResolveAssetDirectory(config.assetsDir);
    FR_INFO("Asset directory: %s", config.assetsDir.c_str());

    m_window = std::make_unique<Window>();
    if (!m_window->Create(config.window.title, config.window.width, config.window.height,
                          config.window.fullscreen, config.window.resizable, config.window.vsync)) {
        FR_ERROR("Window/OpenGL 4.5 initialization failed.");
        FR_ERROR("This build needs an OpenGL 4.5 capable driver and a display server.");
        FR_ERROR("Try running with --headless to validate world generation without a GPU.");
        return false;
    }

    m_input = std::make_unique<Input>();
    m_input->Initialize();
    m_input->SetMouseSensitivity(config.game.mouseSensitivity);

    if (!render::gl::LoadFunctions(reinterpret_cast<void* (*)(const char*)>(SDL_GL_GetProcAddress))) {
        FR_ERROR("Failed to resolve OpenGL entry points.");
        return false;
    }
    if (!ValidateGLContext()) {
        FR_ERROR("The OpenGL context is not usable by this engine.");
        return false;
    }

    m_renderer = std::make_unique<render::Renderer>();
    if (!m_renderer->Initialize(config.render, config.assetsDir)) {
        FR_ERROR("Renderer initialization failed.");
        return false;
    }
    m_renderer->Resize(m_window->Width(), m_window->Height());

    m_world = std::make_unique<world::World>();
    if (!m_world->Generate(config.world) || !m_world->BuildRenderResources()) {
        FR_ERROR("World generation failed.");
        return false;
    }

    m_player = std::make_unique<game::Player>();
    m_player->Settings().walkSpeed = config.game.moveSpeed;
    m_player->Settings().sprintMultiplier = config.game.sprintMultiplier;
    m_player->Settings().jumpVelocity = config.game.jumpVelocity;
    m_player->Settings().gravity = config.game.gravity;
    m_player->Settings().mouseSensitivity = config.game.mouseSensitivity;
    m_player->Settings().invertY = config.game.invertY;
    m_player->Spawn(m_world->Terrain(), m_world->SpawnPoint());
    m_player->SetCaptured(true);
    m_player->Camera().SetPerspective(config.game.fieldOfView, m_window->Aspect(), 0.1f,
                                      std::max(220.0f, config.render.viewDistance * 1.1f));
    m_input->SetRelativeMouseMode(true);

    m_debugUI = std::make_unique<ui::DebugUI>();
    if (!m_debugUI->Initialize(*m_window, config.demoUI)) {
        FR_ERROR("Debug UI initialization failed.");
        return false;
    }

    // Apply a couple of GL defaults that the engine relies on.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);

    FR_INFO("Controls: WASD move | mouse look | Space jump | Shift sprint | Ctrl walk");
    FR_INFO("          F1 panel | F2 wireframe | F3 shadows | F4 foliage | F5 water");
    FR_INFO("          F6 pause time | F11 fullscreen | F12 screenshot | R respawn | Esc release mouse");
    FR_INFO("Frontier Engine initialized successfully.");
    return true;
}

void Engine::Shutdown() {
    if (m_debugUI) {
        m_debugUI->Shutdown();
        m_debugUI.reset();
    }
    if (m_input) {
        m_input->SetRelativeMouseMode(false);
        m_input->Shutdown();
        m_input.reset();
    }
    if (m_world) {
        m_world->ReleaseRenderResources();
        m_world.reset();
    }
    m_player.reset();
    if (m_renderer) {
        m_renderer->Shutdown();
        m_renderer.reset();
    }
    if (m_window) {
        m_window->Destroy();
        m_window.reset();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

void Engine::HandleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT:
            FR_INFO("Quit requested by the window manager.");
            m_running = false;
            return;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                event.window.event == SDL_WINDOWEVENT_RESIZED) {
                m_window->RefreshDrawableSize();
                if (m_renderer) {
                    m_renderer->Resize(m_window->Width(), m_window->Height());
                }
            }
            return;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT && m_input && !m_input->IsRelativeMouseMode()) {
                const bool overUI = m_debugUI && m_debugUI->WantsMouse();
                if (!overUI) {
                    m_input->SetRelativeMouseMode(true);
                    if (m_player) {
                        m_player->SetCaptured(true);
                    }
                }
            }
            return;
        case SDL_KEYDOWN:
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                if (m_input && m_input->IsRelativeMouseMode()) {
                    m_input->SetRelativeMouseMode(false);
                    if (m_player) {
                        m_player->SetCaptured(false);
                    }
                    FR_INFO("Mouse released. Click the viewport to capture it again.");
                } else {
                    m_running = false;
                }
                return;
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F11 && m_window) {
                m_window->ToggleFullscreen();
                m_window->RefreshDrawableSize();
                m_renderer->Resize(m_window->Width(), m_window->Height());
                return;
            }
            break;
        default:
            break;
    }

    if (m_input) {
        m_input->ProcessEvent(event);
    }
}

void Engine::Update(float deltaSeconds) {
    if (!m_world || !m_player || !m_window) {
        return;
    }

    // Handle gameplay hotkeys, but never while typing into the debug UI.
    if (m_input && (!m_debugUI || !m_debugUI->WantsKeyboard())) {
        if (m_input->WasActionPressed(Action::ToggleUI)) {
            m_debugUI->ToggleVisible();
        }
        if (m_input->WasActionPressed(Action::ToggleWireframe)) {
            m_config.render.wireframe = !m_config.render.wireframe;
        }
        if (m_input->WasActionPressed(Action::ToggleShadows)) {
            m_config.render.showShadows = !m_config.render.showShadows;
        }
        if (m_input->WasActionPressed(Action::ToggleFoliage)) {
            m_config.render.showFoliage = !m_config.render.showFoliage;
        }
        if (m_input->WasActionPressed(Action::ToggleWater)) {
            m_config.render.showWater = !m_config.render.showWater;
        }
        if (m_input->WasActionPressed(Action::ToggleTimePause)) {
            m_world->Time().SetPaused(!m_world->Time().Paused());
        }
        if (m_input->WasActionPressed(Action::Screenshot)) {
            m_screenshotRequested = true;
        }
        if (m_input->WasKeyPressed(SDL_SCANCODE_R)) {
            RespawnPlayer();
        }
        if (m_input->IsKeyDown(SDL_SCANCODE_LEFTBRACKET)) {
            m_world->Time().SetHours(m_world->Time().Hours() - deltaSeconds * 2.0f);
        }
        if (m_input->IsKeyDown(SDL_SCANCODE_RIGHTBRACKET)) {
            m_world->Time().SetHours(m_world->Time().Hours() + deltaSeconds * 2.0f);
        }
        if (m_input->WasKeyPressed(SDL_SCANCODE_G)) {
            m_config.world.seed += 1;
            m_world->Regenerate(m_config.world);
            RespawnPlayer();
        }
    }

    if (m_window->Handle()) {
        m_window->SetVSync(m_config.window.vsync);
    }

    SyncPlayerSettings();

    if (m_input) {
        m_player->ApplyMouseLook(*m_input);
    }
    if (m_input) {
        m_player->Update(deltaSeconds, *m_input, m_world->Terrain());
    }

    scene::Camera& camera = m_player->Camera();
    camera.SetAspect(static_cast<float>(m_window->Width()) / static_cast<float>(std::max(1, m_window->Height())));

    const scene::Frustum frustum = scene::Frustum::FromViewProjection(camera.ViewProjection());
    m_world->Update(deltaSeconds, camera, frustum, m_config.render);
}

void Engine::SyncPlayerSettings() {
    game::PlayerSettings& settings = m_player->Settings();
    settings.walkSpeed = m_config.game.moveSpeed;
    settings.sprintMultiplier = m_config.game.sprintMultiplier;
    settings.jumpVelocity = m_config.game.jumpVelocity;
    settings.gravity = m_config.game.gravity;
    settings.mouseSensitivity = m_config.game.mouseSensitivity;
    settings.invertY = m_config.game.invertY;

    if (m_input) {
        m_input->SetMouseSensitivity(m_config.game.mouseSensitivity);
    }
    if (m_player->Camera().Fov() != m_config.game.fieldOfView) {
        m_player->Camera().SetFov(m_config.game.fieldOfView);
    }
    m_player->Camera().SetPerspective(m_config.game.fieldOfView,
                                      static_cast<float>(m_window->Width()) /
                                          static_cast<float>(std::max(1, m_window->Height())),
                                      0.1f, std::max(220.0f, m_config.render.viewDistance * 1.1f));
}

void Engine::ApplyActions(ui::DebugActions& actions) {
    if (actions.respawnPlayer) {
        RespawnPlayer();
    }
    if (actions.regenerateWorld) {
        m_config.world.seed += 1;
        m_world->Regenerate(m_config.world);
        RespawnPlayer();
    }
    if (actions.takeScreenshot) {
        m_screenshotRequested = true;
    }
    if (actions.toggleFullscreen && m_window) {
        m_window->ToggleFullscreen();
        m_window->RefreshDrawableSize();
        m_renderer->Resize(m_window->Width(), m_window->Height());
    }
    if (actions.toggleMouseCapture && m_input) {
        const bool capture = !m_input->IsRelativeMouseMode();
        m_input->SetRelativeMouseMode(capture);
        m_player->SetCaptured(capture);
    }
    if (actions.saveConfig) {
        const std::string path = "frontier-saved-config.ini";
        if (SaveConfigFile(path, m_config)) {
            FR_INFO("Configuration written to %s", path.c_str());
        }
    }
    if (actions.requestQuit) {
        m_running = false;
    }
}

void Engine::RespawnPlayer() {
    if (!m_world || !m_player) {
        return;
    }
    m_player->Spawn(m_world->Terrain(), m_world->SpawnPoint());
    m_player->SetCaptured(true);
    if (m_input) {
        m_input->SetRelativeMouseMode(true);
    }
    FR_INFO("Player respawned at (%.1f, %.1f, %.1f).", m_world->SpawnPoint().x,
            m_world->SpawnPoint().y, m_world->SpawnPoint().z);
}

void Engine::RenderFrame() {
    if (!m_renderer || !m_world || !m_player) {
        return;
    }
    m_renderer->Render(m_player->Camera(), *m_world, m_config.render,
                       static_cast<float>(m_clock.TotalTime()));

    if (m_screenshotRequested) {
        m_screenshotRequested = false;
        TakeScreenshot();
    }
}

void Engine::TakeScreenshot() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("screenshots", ec);
    const std::string path = "screenshots/frontier-" + TimestampString() + ".ppm";
    if (m_renderer->SaveScreenshot(path)) {
        FR_INFO("Screenshot written: %s (PPM, view with GIMP/ffmpeg/ImageMagick)", path.c_str());
    }
}

namespace {

struct FrameReport {
    int samples = 0;
    double meanLuma = 0.0;
    double stdDev = 0.0;
    double topMean = 0.0;
    double bottomMean = 0.0;
    double blackFraction = 1.0;
};

// Summarises an RGB back-buffer read. Rows are bottom-up (OpenGL convention),
// so "top" means the upper half of the screen, where the sky usually is.
FrameReport AnalyseFrame(const unsigned char* pixels, size_t pixelBytes, int width, int height) {
    FrameReport report;
    if (!pixels || width <= 0 || height <= 0 ||
        pixelBytes < static_cast<size_t>(width) * static_cast<size_t>(height) * 3u) {
        return report;
    }
    const int step = std::max(1, width / 200);
    double sum = 0.0;
    double sumSquares = 0.0;
    double topSum = 0.0;
    double bottomSum = 0.0;
    int topCount = 0;
    int bottomCount = 0;
    int black = 0;

    for (int y = 0; y < height; y += step) {
        const bool topHalf = y >= height / 2;
        for (int x = 0; x < width; x += step) {
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3u;
            const double r = pixels[index + 0] / 255.0;
            const double g = pixels[index + 1] / 255.0;
            const double b = pixels[index + 2] / 255.0;
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            sum += luma;
            sumSquares += luma * luma;
            ++report.samples;
            if (luma < 0.02) {
                ++black;
            }
            if (topHalf) {
                topSum += luma;
                ++topCount;
            } else {
                bottomSum += luma;
                ++bottomCount;
            }
        }
    }

    if (report.samples == 0) {
        return report;
    }
    report.meanLuma = sum / report.samples;
    const double variance = std::max(0.0, sumSquares / report.samples - report.meanLuma * report.meanLuma);
    report.stdDev = std::sqrt(variance);
    report.blackFraction = static_cast<double>(black) / report.samples;
    report.topMean = topCount > 0 ? topSum / topCount : 0.0;
    report.bottomMean = bottomCount > 0 ? bottomSum / bottomCount : 0.0;
    return report;
}

} // namespace

int Engine::RunSelfTest() {
    const int frames = std::max(5, m_config.selftestFrames);
    const int width = m_window->Width();
    const int height = m_window->Height();
    FR_INFO("Self-test: rendering %d frames at %dx%d and inspecting the result.", frames, width, height);

    constexpr float kStep = 1.0f / 60.0f;
    float elapsed = 0.0f;
    const size_t pixelBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    std::unique_ptr<unsigned char[]> pixels;

    for (int frame = 0; frame < frames; ++frame) {
        elapsed += kStep;
        scene::Camera& camera = m_player->Camera();
        const scene::Frustum frustum = scene::Frustum::FromViewProjection(camera.ViewProjection());
        m_world->Update(kStep, camera, frustum, m_config.render);
        m_renderer->Render(camera, *m_world, m_config.render, elapsed);

        // Exercise the debug overlay too: it compiles ImGui's own GLSL at the
        // first draw, which is a common source of driver-specific failures.
        m_debugUI->NewFrame();
        ui::DebugActions actions; // inspected but not applied
        m_debugUI->Draw(m_config, *m_world, *m_player, *m_renderer, m_frameStats, actions);
        m_debugUI->Render();

        if (frame + 1 == frames) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories("screenshots", ec);
            m_renderer->SaveScreenshot("screenshots/selftest.ppm");

            if (pixelBytes > 0) {
                pixels.reset(new unsigned char[pixelBytes]());
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.get());
            }
        }
        m_window->SwapBuffers();
    }

    const bool glClean = render::gl::CheckErrors("self-test");
    const FrameReport report = AnalyseFrame(pixels.get(), pixelBytes, width, height);
    const render::RenderStats& stats = m_renderer->Stats();

    FR_INFO("Self-test image: mean luminance %.3f, contrast %.3f, black pixels %.1f%%, "
            "sky %.3f vs ground %.3f.",
            report.meanLuma, report.stdDev, report.blackFraction * 100.0,
            report.topMean, report.bottomMean);
    FR_INFO("Self-test totals: %d draw calls, %zu triangles, %d visible instances, %d culled.",
            stats.drawCalls, stats.triangles, stats.visibleInstances, stats.culledInstances);

    const bool sampled = report.samples > 0;
    const bool lit = report.meanLuma > 0.03 && report.meanLuma < 0.97;
    const bool varied = report.stdDev > 0.03;
    const bool notOverwhelminglyBlack = report.blackFraction < 0.90;
    const bool horizon = std::fabs(report.topMean - report.bottomMean) > 0.02;
    const bool drewGeometry = stats.drawCalls >= 3 && stats.triangles > 5000;

    bool pass = true;
    auto check = [&pass](bool ok, const char* label) {
        FR_INFO("  [%s] %s", ok ? "PASS" : "FAIL", label);
        pass = pass && ok;
    };
    check(sampled, "frame buffer readable");
    check(glClean, "no OpenGL errors");
    check(lit, "image is neither black nor blown out");
    check(varied, "image has contrast (not a flat colour)");
    check(notOverwhelminglyBlack, "most pixels are not black");
    check(horizon, "sky and ground regions differ");
    check(drewGeometry, "geometry was drawn");

    if (pass) {
        FR_INFO("Self-test PASSED: the renderer produced a varied, non-black frame.");
        FR_INFO("A copy of the frame was written to screenshots/selftest.ppm.");
        return 0;
    }

    FR_ERROR("Self-test FAILED.");
    FR_ERROR("Try --safe-mode (no MSAA/HDR/bloom), then --no-bloom, --msaa=1 or --no-hdr "
             "to isolate the feature your driver dislikes.");
    FR_ERROR("The OpenGL version/renderer and any GL errors are logged above; "
             "screenshots/selftest.ppm shows what was rendered.");
    return 3;
}

int Engine::RunHeadless(AppConfig& config) {
    FR_INFO("Headless mode: no window, no OpenGL. Validating world generation, time of day "
            "and player simulation.");
    const std::string assets = ResolveAssetDirectory(config.assetsDir);
    FR_INFO("Assets resolved to: %s", assets.c_str());

    world::World world;
    if (!world.Generate(config.world)) {
        FR_ERROR("Headless world generation failed.");
        return 2;
    }

    Input input;
    input.Initialize();
    input.SetMouseSensitivity(config.game.mouseSensitivity);

    game::Player player;
    player.Settings().walkSpeed = config.game.moveSpeed;
    player.Settings().sprintMultiplier = config.game.sprintMultiplier;
    player.Settings().jumpVelocity = config.game.jumpVelocity;
    player.Settings().gravity = config.game.gravity;
    player.Settings().invertY = config.game.invertY;
    player.Spawn(world.Terrain(), world.SpawnPoint());
    player.SetCaptured(true);
    player.Camera().SetPerspective(config.game.fieldOfView, 16.0f / 9.0f, 0.1f,
                                   std::max(220.0f, config.render.viewDistance * 1.1f));

    // Frustum sanity check: objects straight ahead are visible, objects behind
    // the camera or beyond the far plane are culled. This guards against a
    // transposed matrix silently hiding all instanced vegetation.
    scene::Camera probe;
    probe.SetPosition(vec3(0.0f));
    probe.SetRotation(0.0f, 0.0f);
    probe.SetPerspective(68.0f, 16.0f / 9.0f, 0.1f, 900.0f);
    const scene::Frustum probeFrustum = scene::Frustum::FromViewProjection(probe.ViewProjection());
    const bool frustumFront = probeFrustum.ContainsSphere(vec3(0.0f, 0.0f, -50.0f), 2.0f);
    const bool frustumBehind = probeFrustum.ContainsSphere(vec3(0.0f, 0.0f, 50.0f), 2.0f);
    const bool frustumFar = probeFrustum.ContainsSphere(vec3(0.0f, 0.0f, -2000.0f), 4.0f);

    // Validate that every shader source and include can be resolved. This
    // exercises the same loader the windowed renderer uses, without GL.
    const char* shaderFiles[] = {
        "sky.vert", "sky.frag", "terrain.vert", "terrain.frag",
        "foliage.vert", "foliage.frag", "water.vert", "water.frag",
        "shadow.vert", "shadow_instanced.vert", "shadow.frag",
        "fullscreen.vert", "bloom_threshold.frag", "bloom_blur.frag", "post.frag",
    };
    int shadersExpanded = 0;
    int shaderFailures = 0;
    for (const char* shaderFile : shaderFiles) {
        const std::string path = assets + "/shaders/" + shaderFile;
        std::string source;
        std::string error;
        if (!render::ExpandShaderIncludes(path, source, error) || source.find("void main") == std::string::npos) {
            FR_ERROR("Shader source check failed for %s: %s", path.c_str(),
                     error.empty() ? "no entry point found" : error.c_str());
            ++shaderFailures;
        } else {
            ++shadersExpanded;
        }
    }
    FR_INFO("Shader sources expanded: %d/%d.", shadersExpanded,
            shadersExpanded + shaderFailures);

    const float dt = 1.0f / 60.0f;
    const int jumpFrame = 20;
    const float startHours = world.Time().Hours();
    float totalSimulated = 0.0f;
    for (int frame = 0; frame < config.headlessFrames; ++frame) {
        input.InjectKey(SDL_SCANCODE_W, true);
        input.InjectKey(SDL_SCANCODE_LSHIFT, frame > 40);
        input.InjectKey(SDL_SCANCODE_SPACE, frame >= jumpFrame && frame < jumpFrame + 2);
        input.NewFrame();
        player.Update(dt, input, world.Terrain());

        const scene::Frustum frustum = scene::Frustum::FromViewProjection(player.Camera().ViewProjection());
        world.Update(dt, player.Camera(), frustum, config.render);
        totalSimulated += dt;

        if ((frame + 1) % 60 == 0) {
            FR_INFO("headless frame %d/%d: pos (%.1f, %.1f, %.1f), speed %.2f m/s, grounded=%d, "
                    "swimming=%d, time %.2f h, engine instances %d",
                    frame + 1, config.headlessFrames, player.Position().x, player.Position().y,
                    player.Position().z, player.HorizontalSpeed(), player.IsGrounded() ? 1 : 0,
                    player.IsSwimming() ? 1 : 0, world.Time().Hours(),
                    world.Vegetation().Stats().visibleInstances);
        }
    }

    const float walkDistance = glm::length(vec2(player.Position().x - world.SpawnPoint().x,
                                                player.Position().z - world.SpawnPoint().z));
    FR_INFO("Headless simulation complete: %.2f s simulated, %d frames, walked %.1f m, "
            "final Y %.1f m.",
            totalSimulated, config.headlessFrames, walkDistance, player.Position().y);
    const long long terrainTriangles = static_cast<long long>(world.Terrain().Resolution()) *
                                       world.Terrain().Resolution() * 2;
    // Smoke-test assertions: these catch broken generation before a GPU is ever
    // involved (the windowed path relies on the same data).
    bool ok = true;
    const bool spawnDry = world.SpawnPoint().y > world.Terrain().WaterLevel() + 0.5f;
    const bool spawnInside = std::fabs(world.SpawnPoint().x) < world.Terrain().Size() * 0.5f &&
                             std::fabs(world.SpawnPoint().z) < world.Terrain().Size() * 0.5f;
    const size_t expectedHeights = static_cast<size_t>(world.Terrain().Resolution() + 1) *
                                   static_cast<size_t>(world.Terrain().Resolution() + 1);
    const bool heightfieldComplete = world.Terrain().Heights().size() == expectedHeights;
    const bool moved = walkDistance > 0.5f;
    const bool timeAdvanced = world.Time().Hours() != startHours;

    if (!spawnDry) { FR_ERROR("Smoke test failed: spawn point is underwater."); ok = false; }
    if (!spawnInside) { FR_ERROR("Smoke test failed: spawn point is outside the map."); ok = false; }
    if (!heightfieldComplete) { FR_ERROR("Smoke test failed: heightfield sample count mismatch."); ok = false; }
    if (shaderFailures != 0) { FR_ERROR("Smoke test failed: shader assets could not be expanded."); ok = false; }
    if (!moved) { FR_ERROR("Smoke test failed: player did not move forward."); ok = false; }
    if (!timeAdvanced) { FR_ERROR("Smoke test failed: time of day did not advance."); ok = false; }
    if (!frustumFront || frustumBehind || frustumFar) {
        FR_ERROR("Smoke test failed: frustum culling is inconsistent "
                 "(front=%d behind=%d far=%d).",
                 frustumFront ? 1 : 0, frustumBehind ? 1 : 0, frustumFar ? 1 : 0);
        ok = false;
    }
    if (config.world.treeCount > 0 && world.Vegetation().Stats().totalInstances == 0) {
        FR_ERROR("Smoke test failed: no vegetation was placed.");
        ok = false;
    }

    const world::LightingState& lighting = world.Lighting();
    FR_INFO("Lighting: sun elevation %.1f deg, daylight %.2f, stars %.2f, sun intensity %.2f.",
            lighting.sun.elevationDegrees, lighting.daylight, lighting.stars, lighting.sun.intensity);
    FR_INFO("Terrain: %lld triangles (CPU grid) | vegetation instances: %d | visible candidates: %d "
            "| culled: %d",
            terrainTriangles, world.Vegetation().Stats().totalInstances,
            world.Vegetation().Stats().visibleInstances, world.Vegetation().Stats().culledInstances);
    if (!ok) {
        FR_ERROR("Headless validation failed.");
        return 3;
    }
    FR_INFO("Headless validation succeeded (spawn, heightfield, movement, time, vegetation).");
    return 0;
}

} // namespace frontier::core

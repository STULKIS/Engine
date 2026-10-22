#include "core/Engine.h"

#include "core/Log.h"
#include "core/Window.h"
#include "game/Player.h"
#include "render/GL.h"
#include "render/Renderer.h"
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
    const GLubyte* glVersion = render::gl::glGetString(GL_VERSION);
    const GLubyte* glRenderer = render::gl::glGetString(GL_RENDERER);
    const GLubyte* glVendor = render::gl::glGetString(GL_VENDOR);
    FR_INFO("OpenGL: %s", glVersion ? reinterpret_cast<const char*>(glVersion) : "unknown");
    FR_INFO("Renderer: %s (%s)",
            glRenderer ? reinterpret_cast<const char*>(glRenderer) : "unknown",
            glVendor ? reinterpret_cast<const char*>(glVendor) : "unknown");

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

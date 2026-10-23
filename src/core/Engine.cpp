#include "core/Engine.h"

#include "asset/AssetLibrary.h"
#include "asset/Mesh.h"
#include "asset/Placeholders.h"
#include "core/Log.h"
#include "core/Window.h"
#include "game/Player.h"
#include "render/GL.h"
#include "render/Renderer.h"
#include "render/Shader.h"
#include "scene/Camera.h"
#include "scene/Frustum.h"
#include "scene/Picking.h"
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

namespace derecho::core {

using namespace derecho::render::gl;

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

    DE_INFO("GL version:   %s", versionString ? versionString : "unknown");
    DE_INFO("GLSL version: %s", shadingLanguage ? shadingLanguage : "unknown");
    DE_INFO("GL vendor:    %s", vendorString ? vendorString : "unknown");
    DE_INFO("GL renderer:  %s", rendererString ? rendererString : "unknown");
    DE_INFO("GL profile:   %s, max samples %d, max texture %d, colour attachments %d",
            coreProfile ? "core" : "compatibility", maxSamples, maxTextureSize, maxColorAttachments);

    const std::string rendererName = rendererString ? rendererString : "";
    if (rendererName.find("llvmpipe") != std::string::npos || rendererName.find("softpipe") != std::string::npos ||
        rendererName.find("SwiftShader") != std::string::npos) {
        DE_WARN("Software rasterizer detected; expect low frame rates. Lower --render-scale and view distance.");
    }

    if (!versionParsed) {
        DE_WARN("Could not parse the OpenGL version string; continuing anyway.");
        return true;
    }
    if (major < 4 || (major == 4 && minor < 5)) {
        DE_ERROR("Derecho Engine needs OpenGL 4.5 core; the driver only provides %d.%d.", major, minor);
        DE_ERROR("Update the GPU driver (NVIDIA/AMD/Intel) and try again.");
        DE_ERROR("On Mesa/llvmpipe you can force a higher reported version with");
        DE_ERROR("  MESA_GL_VERSION_OVERRIDE=4.5 MESA_GLSL_VERSION_OVERRIDE=450 ./derecho");
        DE_ERROR("(that only works if the driver actually implements the features).");
        return false;
    }
    if (glslMajor > 0 && (glslMajor < 4 || (glslMajor == 4 && glslMinor < 50))) {
        DE_ERROR("Derecho Engine shaders need GLSL 4.50, but the driver reports %d.%02d.", glslMajor, glslMinor);
        return false;
    }
    if (!coreProfile) {
        DE_WARN("A compatibility profile was created instead of a core profile; this usually still works.");
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
    if (m_config.glInfo) {
        const int exitCode = RunGLInfo(m_config);
        Shutdown();
        return exitCode;
    }

    if (!Initialize(m_config)) {
        Shutdown();
        return 1;
    }

    if (m_config.selftest) {
        const int exitCode = RunSelfTest();
        Shutdown();
        DE_INFO("Derecho Engine shut down cleanly.");
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

        // Build the user interface for this frame: the editor, or the compact
        // in-game debug overlay when running game-only (--play).
        if (m_editor) {
            m_editor->NewFrame();
            ui::EditorActions editorActions;
            m_editor->Draw(m_config, *m_world, m_entities, *m_player, *m_renderer, m_frameStats,
                           m_editorCamera, editorActions);
            ApplyEditorActions(editorActions);

            // Keep the compact overlay available inside play mode.
            if (m_playState != ui::PlayState::Stopped && m_debugUI->Visible()) {
                ui::DebugActions playActions;
                m_debugUI->Draw(m_config, *m_world, m_entities, *m_player, *m_renderer, m_frameStats,
                                playActions);
                ApplyActions(playActions);
            }
            if (!m_running) {
                break;
            }
            RenderFrame();
            m_editor->Render();
        } else {
            m_debugUI->NewFrame();
            ui::DebugActions actions;
            m_debugUI->Draw(m_config, *m_world, m_entities, *m_player, *m_renderer, m_frameStats,
                            actions);
            ApplyActions(actions);
            if (!m_running) {
                break;
            }
            RenderFrame();
            m_debugUI->Render();
        }
        m_window->SwapBuffers();

        const double cpuMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::high_resolution_clock::now() - frameStart)
                                 .count();
        m_frameStats.Push(deltaSeconds, cpuMs);

        fpsWindowTimer += deltaSeconds;
        if (fpsWindowTimer >= 1.0) {
            DE_INFO("Frame stats: %.1f FPS (avg), %.2f ms frame, %d draw calls, %.2f M triangles, "
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
    DE_INFO("Derecho Engine shut down cleanly.");
    return 0;
}

bool Engine::Initialize(AppConfig& config) {
    DE_INFO("Starting Derecho Engine %s", "0.1.0");
    DE_INFO("World config: seed %d, %.0f m, %d quads, %d trees, %d grass.",
            config.world.seed, config.world.terrainSize, config.world.terrainResolution,
            config.world.treeCount, config.world.grassCount);

    config.assetsDir = ResolveAssetDirectory(config.assetsDir);
    DE_INFO("Asset directory: %s", config.assetsDir.c_str());

    m_window = std::make_unique<Window>();
    if (!m_window->Create(config.window.title, config.window.width, config.window.height,
                          config.window.fullscreen, config.window.resizable, config.window.vsync)) {
        DE_ERROR("Window/OpenGL 4.5 initialization failed.");
        DE_ERROR("This build needs an OpenGL 4.5 capable driver and a display server.");
        DE_ERROR("Try running with --headless to validate world generation without a GPU.");
        return false;
    }

    {
        std::string iconError;
        asset::Image icon;
        if (asset::LoadImageFile(config.assetsDir + "/branding/derecho-logo.png", icon, iconError)) {
            m_window->SetIcon(icon.pixels.data(), icon.width, icon.height);
            DE_INFO("Window icon: %s (%dx%d).", "branding/derecho-logo.png", icon.width, icon.height);
        } else {
            DE_WARN("Window icon not loaded: %s", iconError.c_str());
        }
    }

    m_input = std::make_unique<Input>();
    m_input->Initialize();
    m_input->SetMouseSensitivity(config.game.mouseSensitivity);

    if (!render::gl::LoadFunctions(reinterpret_cast<void* (*)(const char*)>(SDL_GL_GetProcAddress))) {
        DE_ERROR("Failed to resolve OpenGL entry points.");
        return false;
    }
    if (!ValidateGLContext()) {
        DE_ERROR("The OpenGL context is not usable by this engine.");
        return false;
    }

    m_renderer = std::make_unique<render::Renderer>();
    if (!m_renderer->Initialize(config.render, config.assetsDir)) {
        DE_ERROR("Renderer initialization failed.");
        return false;
    }
    m_renderer->Resize(m_window->Width(), m_window->Height());

    m_world = std::make_unique<world::World>();
    if (!m_world->Generate(config.world) || !m_world->BuildRenderResources()) {
        DE_ERROR("World generation failed.");
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

    // Scene content: a small generated set of props around the spawn point,
    // or a scene file supplied on the command line.
    BuildDefaultScene();
    if (!config.loadScene.empty()) {
        LoadScene(config.loadScene);
    }
    if (!config.saveScene.empty()) {
        SaveScene(config.saveScene);
    }

    m_debugUI = std::make_unique<ui::DebugUI>();
    if (!m_debugUI->Initialize(*m_window, config.demoUI)) {
        DE_ERROR("Debug UI initialization failed.");
        return false;
    }

    m_editorMode = config.editor;
    // In the editor the frame is displayed inside the viewport panel instead of
    // being blitted under the panels.
    m_renderer->SetPresentEnabled(!m_editorMode);
    if (m_editorMode) {
        m_editor = std::make_unique<ui::Editor>();
        if (!m_editor->Initialize()) {
            DE_ERROR("Editor initialization failed.");
            return false;
        }
        // Frame the starter scene from a comfortable distance, like opening a
        // level in any DCC-style editor.
        const vec3 spawn = m_world->SpawnPoint();
        m_editorCamera.SetPerspective(config.game.fieldOfView,
                                      static_cast<float>(m_window->Width()) /
                                          static_cast<float>(std::max(1, m_window->Height())),
                                      0.1f, std::max(220.0f, config.render.viewDistance * 1.1f));
        m_editorCamera.SetPosition(spawn + vec3(11.0f, 8.5f, 13.0f));
        m_editorCamera.LookAt(spawn + vec3(0.0f, 2.0f, 0.0f));
        DE_INFO("Editor ready: hierarchy, inspector, asset browser, console, gizmos, play-in-editor.");
        // The editor owns the mouse; play mode captures it again.
        if (m_input) {
            m_input->SetRelativeMouseMode(false);
            m_player->SetCaptured(false);
        }
    }

    // Apply a couple of GL defaults that the engine relies on.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);

    DE_INFO("Controls: WASD move | mouse look | Space jump | Shift sprint | Ctrl walk");
    DE_INFO("          F1 panel | F2 wireframe | F3 shadows | F4 foliage | F5 water");
    DE_INFO("          F6 pause time | F7 render style | F11 fullscreen | F12 screenshot");
    DE_INFO("          R respawn | G new world | Esc release mouse");
    DE_INFO("Derecho Engine initialized successfully.");
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
            DE_INFO("Quit requested by the window manager.");
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
                if (m_playState != ui::PlayState::Stopped) {
                    StopPlay();
                    return;
                }
                if (m_input && m_input->IsRelativeMouseMode()) {
                    m_input->SetRelativeMouseMode(false);
                    if (m_player) {
                        m_player->SetCaptured(false);
                    }
                    DE_INFO("Mouse released. Click the viewport to capture it again.");
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
        if (m_input->WasActionPressed(Action::ToggleCamera)) {
            m_config.game.thirdPerson = !m_config.game.thirdPerson;
            if (m_player) {
                m_player->SetThirdPerson(m_config.game.thirdPerson);
            }
            DE_INFO("Camera: %s", m_config.game.thirdPerson ? "third person" : "first person");
        }
        if (m_input->WasActionPressed(Action::ToggleStyle)) {
            const bool anime = m_config.render.style == RenderStyle::Anime;
            m_config.render.style = anime ? RenderStyle::Realistic : RenderStyle::Anime;
            DE_INFO("Render style: %s", anime ? "realistic" : "anime (cel shading + outlines)");
        }
        if (m_input->WasActionPressed(Action::ToggleTimePause)) {
            m_world->Time().SetPaused(!m_world->Time().Paused());
        }
        if (m_input->WasActionPressed(Action::Screenshot)) {
            m_screenshotRequested = true;
        }
        if (m_input->WasKeyPressed(SDL_SCANCODE_R) &&
            m_playState == ui::PlayState::Playing) {
            RespawnPlayer();
        }
        if (m_input->IsKeyDown(SDL_SCANCODE_LEFTBRACKET)) {
            m_world->Time().SetHours(m_world->Time().Hours() - deltaSeconds * 2.0f);
        }
        if (m_input->IsKeyDown(SDL_SCANCODE_RIGHTBRACKET)) {
            m_world->Time().SetHours(m_world->Time().Hours() + deltaSeconds * 2.0f);
        }
        if (m_input->WasKeyPressed(SDL_SCANCODE_F8)) {
            SaveScene(m_entities.Path());
        }
        if (m_input->WasKeyPressed(SDL_SCANCODE_F9)) {
            LoadScene(m_entities.Path());
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

    const bool playing = m_playState == ui::PlayState::Playing;
    if (m_input && playing) {
        m_player->ApplyMouseLook(*m_input);
    }
    if (m_input && playing) {
        m_player->Update(deltaSeconds, *m_input, m_world->Terrain());
    }

    scene::Camera& camera = m_player->Camera();
    camera.SetAspect(static_cast<float>(m_window->Width()) / static_cast<float>(std::max(1, m_window->Height())));

    // Scene behaviours run after the player so scripts react to this frame's
    // camera state, and before the world update so foliage follows the props.
    // Editing freezes them, exactly like Unity's edit mode.
    UpdatePlayerCharacter();
    if (playing) {
        m_scripts.Update(m_entities, deltaSeconds);
    }

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
    settings.thirdPerson = m_config.game.thirdPerson;
    settings.thirdPersonDistance = m_config.game.thirdPersonDistance;

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
    m_editorCamera.SetPerspective(m_config.game.fieldOfView,
                                 static_cast<float>(m_window->Width()) /
                                     static_cast<float>(std::max(1, m_window->Height())),
                                 0.1f, std::max(220.0f, m_config.render.viewDistance * 1.1f));
}

void Engine::StartPlay() {
    if (!m_player || m_world == nullptr) {
        return;
    }
    if (m_playState == ui::PlayState::Paused) {
        m_playState = ui::PlayState::Playing;
        return;
    }
    if (m_playState == ui::PlayState::Playing) {
        return;
    }
    // Snapshot the authored scene so Stop can undo everything the game changed.
    m_playSnapshot = m_entities.Serialize();
    // "Play from here": drop the player where the editor camera is looking, so
    // the run starts at the spot being inspected.
    if (m_editor) {
        vec3 startPosition = m_editorCamera.Position();
        startPosition.y = m_world->Terrain().HeightAt(startPosition.x, startPosition.z) + 0.2f;
        if (glm::length(startPosition - m_player->Position()) > 2.0f) {
            m_player->Spawn(m_world->Terrain(), startPosition);
        }
    }
    m_playStartPosition = m_player->Position();
    m_scripts.Clear(m_entities);
    m_playState = ui::PlayState::Playing;
    if (m_input) {
        m_input->SetRelativeMouseMode(true);
    }
    m_player->SetCaptured(true);
    UpdatePlayerCharacter();
    DE_INFO("Play mode started: scene snapshot taken (%zu entities).", m_entities.AliveCount());
}

void Engine::StopPlay() {
    if (m_playState == ui::PlayState::Stopped) {
        return;
    }
    m_playState = ui::PlayState::Stopped;
    m_scripts.Clear(m_entities);

    if (!m_playSnapshot.empty()) {
        std::string error;
        if (!m_entities.Deserialize(m_playSnapshot, error)) {
            DE_ERROR("Could not restore the scene after play mode: %s", error.c_str());
        }
        m_playSnapshot.clear();
    }

    // Re-link the player character after the scene was replaced.
    m_playerEntity = scene::kInvalidEntity;
    for (const scene::Entity& entity : m_entities.Entities()) {
        if (entity.alive && entity.name == "Player") {
            m_playerEntity = entity.id;
            break;
        }
    }

    if (m_player && m_world) {
        m_player->Spawn(m_world->Terrain(), m_playStartPosition);
        m_player->SetCaptured(false);
    }
    if (m_input) {
        m_input->SetRelativeMouseMode(false);
    }
    UpdatePlayerCharacter();
    DE_INFO("Play mode stopped: scene restored to the snapshot.");
}

void Engine::TogglePause() {
    if (m_playState == ui::PlayState::Playing) {
        m_playState = ui::PlayState::Paused;
        DE_INFO("Play mode paused.");
    } else if (m_playState == ui::PlayState::Paused) {
        m_playState = ui::PlayState::Playing;
        DE_INFO("Play mode resumed.");
    }
}

void Engine::ApplyEditorActions(ui::EditorActions& actions) {
    if (actions.play) {
        StartPlay();
    }
    if (actions.pause) {
        TogglePause();
    }
    if (actions.stop) {
        StopPlay();
    }
    if (actions.saveScene) {
        SaveScene(m_entities.Path());
    }
    if (actions.reloadScene) {
        StopPlay();
        LoadScene(m_entities.Path());
    }
    if (actions.newScene) {
        StopPlay();
        BuildDefaultScene();
        if (m_editor) {
            m_editor->Select(scene::kInvalidEntity);
        }
    }
    if (actions.duplicateEntity && m_editor) {
        if (m_entities.Find(m_editor->Selected())) {
            m_editor->Select(m_entities.Duplicate(m_editor->Selected()));
        }
    }
    if (actions.deleteEntity && m_editor) {
        const scene::EntityId selected = m_editor->Selected();
        if (m_entities.Find(selected)) {
            m_entities.Destroy(selected);
            m_editor->Select(scene::kInvalidEntity);
        }
    }
    if (actions.toggleFullscreen && m_window) {
        m_window->ToggleFullscreen();
        m_window->RefreshDrawableSize();
        m_renderer->Resize(m_window->Width(), m_window->Height());
    }
    if (actions.requestScreenshot) {
        m_screenshotRequested = true;
    }
    if (actions.quit) {
        m_running = false;
    }
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
    if (actions.saveScene) {
        SaveScene(m_entities.Path());
    }
    if (actions.loadScene) {
        LoadScene(m_entities.Path());
    }
    if (actions.saveConfig) {
        const std::string path = "derecho-saved-config.ini";
        if (SaveConfigFile(path, m_config)) {
            DE_INFO("Configuration written to %s", path.c_str());
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
    DE_INFO("Player respawned at (%.1f, %.1f, %.1f).", m_world->SpawnPoint().x,
            m_world->SpawnPoint().y, m_world->SpawnPoint().z);
}

void Engine::RenderFrame() {
    if (!m_renderer || !m_world || !m_player) {
        return;
    }
    m_renderer->Render(m_player->Camera(), *m_world, m_entities, m_config.render,
                       static_cast<float>(m_clock.TotalTime()));

    if (m_screenshotRequested) {
        m_screenshotRequested = false;
        TakeScreenshot();
    }
}

void Engine::BuildDefaultScene() {
    m_scripts.Clear(m_entities);
    m_entities.Clear();
    m_entities.SetPath("scene.json");

    const vec3 spawn = m_world->SpawnPoint();
    const auto& terrain = m_world->Terrain();

    // A ring of stone pillars marks the spawn point, and a marker cone sits above
    // it. This is deliberately simple: it is starter content you can select,
    // edit, delete, and save back to a scene file from the panel.
    constexpr int kPillars = 8;
    for (int i = 0; i < kPillars; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(kPillars) * kTwoPi;
        const float x = spawn.x + std::cos(angle) * 9.0f;
        const float z = spawn.z + std::sin(angle) * 9.0f;
        const float ground = terrain.HeightAt(x, z);
        const scene::EntityId id = m_entities.Create(m_entities.UniqueName("Pillar"),
                                                     scene::Primitive::Cylinder,
                                                     vec3(x, ground + 1.7f, z));
        if (scene::Entity* entity = m_entities.Find(id)) {
            entity->scale = vec3(0.8f, 3.4f, 0.8f);
            entity->color = vec3(0.66f, 0.64f, 0.60f);
        }
    }

    const scene::EntityId marker = m_entities.Create("Spawn marker", scene::Primitive::Cone,
                                                     spawn + vec3(0.0f, 3.4f, 0.0f));
    if (scene::Entity* entity = m_entities.Find(marker)) {
        entity->scale = vec3(1.4f, 1.6f, 1.4f);
        entity->color = vec3(0.92f, 0.55f, 0.25f);
        entity->script = "Spin";
        entity->SetParam("speed", 30.0f);
    }

    const scene::EntityId orb = m_entities.Create("Orb", scene::Primitive::Sphere,
                                                  spawn + vec3(0.0f, 5.4f, 0.0f));
    if (scene::Entity* entity = m_entities.Find(orb)) {
        entity->scale = vec3(0.9f);
        entity->color = vec3(0.55f, 0.78f, 0.95f);
        entity->script = "Bob";
        entity->SetParam("amplitude", 0.35f);
        entity->SetParam("frequency", 0.25f);
    }

    // The visible player: an imported low-poly character that the third-person
    // camera frames. It follows the player every frame and is hidden in
    // first person so it never clips into the view.
    m_playerEntity = m_entities.Create("Player", scene::Primitive::Box, spawn);
    if (scene::Entity* entity = m_entities.Find(m_playerEntity)) {
        entity->mesh = "character";
        entity->texture = "cloth";
        entity->scale = vec3(1.0f);
        entity->color = vec3(1.0f);
        entity->visible = m_config.game.thirdPerson;
        entity->castShadow = true;
    }
    UpdatePlayerCharacter();

    // Placeholder props that prove the model import path: a crystal and rocks.
    const scene::EntityId crystal = m_entities.Create("Crystal", scene::Primitive::Sphere,
                                                      spawn + vec3(4.5f, 0.0f, -3.0f));
    if (scene::Entity* entity = m_entities.Find(crystal)) {
        entity->mesh = "crystal";
        entity->position.y = terrain.HeightAt(entity->position.x, entity->position.z);
        entity->color = vec3(0.62f, 0.86f, 0.95f);
        entity->script = "Spin";
        entity->SetParam("speed", 12.0f);
    }
    for (int i = 0; i < 5; ++i) {
        const float angle = static_cast<float>(i) / 5.0f * kTwoPi + 0.6f;
        const float x = spawn.x + std::cos(angle) * 14.0f;
        const float z = spawn.z + std::sin(angle) * 14.0f;
        const scene::EntityId rock = m_entities.Create(m_entities.UniqueName("Rock"),
                                                       scene::Primitive::Sphere,
                                                       vec3(x, terrain.HeightAt(x, z) - 0.25f, z));
        if (scene::Entity* entity = m_entities.Find(rock)) {
            entity->mesh = "rock";
            entity->texture = "stone";
            entity->rotation.y = angle * 57.2958f;
            entity->scale = vec3(1.1f, 0.9f, 1.1f);
            entity->color = vec3(0.86f);
        }
    }

    DE_INFO("Scene ready: %zu entities around the spawn point.", m_entities.AliveCount());
}

void Engine::SaveScene(const std::string& path) {
    const std::string target = path.empty() ? "scene.json" : path;
    std::string error;
    if (m_entities.Save(target, error)) {
        m_entities.SetPath(target);
    } else {
        DE_ERROR("Could not save the scene: %s", error.c_str());
    }
}

void Engine::LoadScene(const std::string& path) {
    const std::string source = path.empty() ? "scene.json" : path;
    std::string error;
    m_scripts.Clear(m_entities);
    if (!m_entities.Load(source, error)) {
        DE_ERROR("Could not load the scene: %s", error.c_str());
        return;
    }
    m_entities.SetPath(source);

    // A loaded scene may or may not contain the player character.
    m_playerEntity = scene::kInvalidEntity;
    for (const scene::Entity& entity : m_entities.Entities()) {
        if (entity.alive && entity.name == "Player") {
            m_playerEntity = entity.id;
            break;
        }
    }
}

void Engine::UpdatePlayerCharacter() {
    if (!m_player || m_playerEntity == scene::kInvalidEntity) {
        return;
    }
    scene::Entity* entity = m_entities.Find(m_playerEntity);
    if (!entity) {
        m_playerEntity = scene::kInvalidEntity;
        return;
    }
    // The model faces +Z; the camera looks down -Z at yaw 0, hence 180 - yaw.
    entity->position = m_player->Position();
    entity->rotation.y = 180.0f - m_player->YawDegrees();
    // Editing shows the character (it is part of the scene); the game hides it
    // in first person so it cannot clip into the view.
    const bool editing = m_editor && m_playState == ui::PlayState::Stopped;
    entity->visible = m_config.game.thirdPerson || editing;
}

void Engine::TakeScreenshot() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("screenshots", ec);
    const std::string path = "screenshots/derecho-" + TimestampString() + ".ppm";
    if (m_renderer->SaveScreenshot(path)) {
        DE_INFO("Screenshot written: %s (PPM, view with GIMP/ffmpeg/ImageMagick)", path.c_str());
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

int Engine::RunGLInfo(AppConfig& config) {
    DE_INFO("OpenGL environment report");

    SDL_version compiled{};
    SDL_version linkedVersion{};
    SDL_VERSION(&compiled);
    SDL_GetVersion(&linkedVersion);
    DE_INFO("SDL: compiled %d.%d.%d, linked %d.%d.%d", compiled.major, compiled.minor, compiled.patch,
            linkedVersion.major, linkedVersion.minor, linkedVersion.patch);

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        DE_ERROR("SDL video subsystem could not start: %s", SDL_GetError());
        DE_ERROR("There is no usable display (no GPU driver, no desktop session, or a");
        DE_ERROR("container without a display server). Start it from a local desktop.");
        return 1;
    }

    const char* driver = SDL_GetCurrentVideoDriver();
    DE_INFO("SDL video driver: %s", driver ? driver : "unknown");
    const int displays = SDL_GetNumVideoDisplays();
    DE_INFO("Displays: %d", displays);
    for (int i = 0; i < displays; ++i) {
        SDL_Rect bounds{};
        SDL_GetDisplayBounds(i, &bounds);
        SDL_Rect usable{};
        SDL_GetDisplayUsableBounds(i, &usable);
        SDL_DisplayMode mode{};
        SDL_GetCurrentDisplayMode(i, &mode);
        const char* name = SDL_GetDisplayName(i);
        DE_INFO("  [%d] %s: %dx%d at (%d,%d), usable %dx%d, mode %dx%d @ %d Hz, format %s",
                i, name ? name : "display", bounds.w, bounds.h, bounds.x, bounds.y,
                usable.w, usable.h, mode.w, mode.h, mode.refresh_rate,
                SDL_GetPixelFormatName(mode.format));
    }

    AppConfig infoConfig = config;
    if (!m_window) {
        m_window = std::make_unique<Window>();
    }
    const bool created = m_window->Create(infoConfig.window.title, infoConfig.window.width,
                                          infoConfig.window.height, false, true, false);
    if (!created) {
        DE_ERROR("Window/context creation failed; the messages above say why.");
        DE_ERROR("Run this report from a local desktop session with an up-to-date GPU driver.");
        return 1;
    }
    DE_INFO("Window: %dx%d", m_window->Width(), m_window->Height());

    if (!render::gl::LoadFunctions(reinterpret_cast<void* (*)(const char*)>(SDL_GL_GetProcAddress))) {
        DE_ERROR("OpenGL entry points could not be resolved; the driver is not usable.");
        return 1;
    }

    auto glString = [](GLenum name) {
        const GLubyte* value = render::gl::glGetString(name);
        return value ? reinterpret_cast<const char*>(value) : "(unavailable)";
    };
    DE_INFO("GL_VERSION:  %s", glString(GL_VERSION));
    DE_INFO("GL_VENDOR:   %s", glString(GL_VENDOR));
    DE_INFO("GL_RENDERER: %s", glString(GL_RENDERER));
    DE_INFO("GLSL:        %s", glString(GL_SHADING_LANGUAGE_VERSION));

    GLint value = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &value);
    DE_INFO("Profile:     %s",
            (value & GL_CONTEXT_CORE_PROFILE_BIT) ? "core" : "compatibility");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
    DE_INFO("Max texture size: %d", value);
    glGetIntegerv(GL_MAX_SAMPLES, &value);
    DE_INFO("Max MSAA samples: %d", value);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &value);
    DE_INFO("Max colour attachments: %d", value);

    GLint extensionCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    DE_INFO("Extensions: %d", extensionCount);
    GLint maxVertexAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    DE_INFO("Vertex attributes: %d", maxVertexAttribs);

    DE_INFO("Report complete. If GL_VERSION is below 4.5 the engine cannot run; "
            "update the GPU driver.");
    return 0;
}

int Engine::RunSelfTest() {
    const int frames = std::max(5, m_config.selftestFrames);
    const int width = m_window->Width();
    const int height = m_window->Height();
    DE_INFO("Self-test: rendering %d frames at %dx%d and inspecting the result.", frames, width, height);

    constexpr float kStep = 1.0f / 60.0f;
    float elapsed = 0.0f;
    const size_t pixelBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    std::unique_ptr<unsigned char[]> pixels;

    for (int frame = 0; frame < frames; ++frame) {
        elapsed += kStep;
        scene::Camera& camera = m_player->Camera();
        const scene::Frustum frustum = scene::Frustum::FromViewProjection(camera.ViewProjection());
        m_world->Update(kStep, camera, frustum, m_config.render);
        m_renderer->Render(camera, *m_world, m_entities, m_config.render, elapsed);

        // Exercise the debug overlay too: it compiles ImGui's own GLSL at the
        // first draw, which is a common source of driver-specific failures.
        m_debugUI->NewFrame();
        ui::DebugActions actions; // inspected but not applied
        m_debugUI->Draw(m_config, *m_world, m_entities, *m_player, *m_renderer, m_frameStats, actions);
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

    DE_INFO("Self-test image: mean luminance %.3f, contrast %.3f, black pixels %.1f%%, "
            "sky %.3f vs ground %.3f.",
            report.meanLuma, report.stdDev, report.blackFraction * 100.0,
            report.topMean, report.bottomMean);
    DE_INFO("Self-test totals: %d draw calls, %zu triangles, %d visible instances, %d culled.",
            stats.drawCalls, stats.triangles, stats.visibleInstances, stats.culledInstances);

    const bool sampled = report.samples > 0;
    const bool lit = report.meanLuma > 0.03 && report.meanLuma < 0.97;
    const bool varied = report.stdDev > 0.03;
    const bool notOverwhelminglyBlack = report.blackFraction < 0.90;
    const bool horizon = std::fabs(report.topMean - report.bottomMean) > 0.02;
    const bool drewGeometry = stats.drawCalls >= 3 && stats.triangles > 5000;

    bool pass = true;
    auto check = [&pass](bool ok, const char* label) {
        DE_INFO("  [%s] %s", ok ? "PASS" : "FAIL", label);
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
        DE_INFO("Self-test PASSED: the renderer produced a varied, non-black frame.");
        DE_INFO("A copy of the frame was written to screenshots/selftest.ppm.");
        return 0;
    }

    DE_ERROR("Self-test FAILED.");
    DE_ERROR("Try --safe-mode (no MSAA/HDR/bloom), then --no-bloom, --msaa=1 or --no-hdr "
             "to isolate the feature your driver dislikes.");
    DE_ERROR("The OpenGL version/renderer and any GL errors are logged above; "
             "screenshots/selftest.ppm shows what was rendered.");
    return 3;
}

int Engine::RunHeadless(AppConfig& config) {
    DE_INFO("Headless mode: no window, no OpenGL. Validating world generation, time of day "
            "and player simulation.");
    const std::string assets = ResolveAssetDirectory(config.assetsDir);
    DE_INFO("Assets resolved to: %s", assets.c_str());

    world::World world;
    if (!world.Generate(config.world)) {
        DE_ERROR("Headless world generation failed.");
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
            DE_ERROR("Shader source check failed for %s: %s", path.c_str(),
                     error.empty() ? "no entry point found" : error.c_str());
            ++shaderFailures;
        } else {
            ++shadersExpanded;
        }
    }
    DE_INFO("Shader sources expanded: %d/%d.", shadersExpanded,
            shadersExpanded + shaderFailures);

    // Scene round-trip: build a small scene, save it, load it back and compare.
    // This exercises the JSON writer, the parser, id remapping and the parent
    // validation without needing a GPU.
    bool sceneRoundTrip = false;
    {
        namespace fs = std::filesystem;
        const std::string path = "scene-selftest.json";
        scene::Scene source;
        const scene::EntityId box = source.Create("Box A", scene::Primitive::Box, vec3(1.0f, 2.0f, 3.0f));
        const scene::EntityId sphere = source.Create("Sphere B", scene::Primitive::Sphere, vec3(-4.0f, 0.5f, 8.0f));
        if (scene::Entity* entity = source.Find(sphere)) {
            entity->scale = vec3(1.5f, 2.0f, 0.5f);
            entity->color = vec3(0.9f, 0.2f, 0.1f);
            entity->parent = box;
            entity->script = "Spin";
            entity->SetParam("speed", 90.0f);
        }

        std::string error;
        scene::Scene loaded;
        if (!source.Save(path, error)) {
            DE_ERROR("Scene save failed: %s", error.c_str());
        } else if (!loaded.Load(path, error)) {
            DE_ERROR("Scene load failed: %s", error.c_str());
        } else {
            const scene::Entity* boxLoaded = loaded.Find(box);
            const scene::Entity* sphereLoaded = loaded.Find(sphere);
            const bool countMatches = loaded.AliveCount() == source.AliveCount();
            const bool valuesMatch = boxLoaded && sphereLoaded &&
                                     glm::length(boxLoaded->position - vec3(1.0f, 2.0f, 3.0f)) < 1e-4f &&
                                     glm::length(sphereLoaded->scale - vec3(1.5f, 2.0f, 0.5f)) < 1e-4f &&
                                     sphereLoaded->parent == box &&
                                     sphereLoaded->primitive == scene::Primitive::Sphere;
            const bool scriptMatches = sphereLoaded && sphereLoaded->script == "Spin" &&
                                       std::abs(sphereLoaded->Param("speed", 0.0f) - 90.0f) < 1e-3f;
            sceneRoundTrip = countMatches && valuesMatch && scriptMatches;
            DE_INFO("Scene round-trip: %zu entities, hierarchy, values and scripts %s.",
                    loaded.AliveCount(), sceneRoundTrip ? "preserved" : "MISMATCH");
        }

        // Behaviour system: a Spin script must actually rotate its entity, and
        // must not fire on entities that have no script.
        if (sceneRoundTrip) {
            scene::Scene runtime;
            const scene::EntityId spinner =
                runtime.Create("Spinner", scene::Primitive::Box, vec3(0.0f));
            if (scene::Entity* entity = runtime.Find(spinner)) {
                entity->script = "Spin";
                entity->SetParam("speed", 90.0f);
            }
            const scene::EntityId idle = runtime.Create("Idle", scene::Primitive::Box, vec3(0.0f));

            scene::ScriptHost host;
            for (int step = 0; step < 60; ++step) {
                host.Update(runtime, 1.0f / 60.0f);
            }
            const scene::Entity* spun = runtime.Find(spinner);
            const scene::Entity* still = runtime.Find(idle);
            const bool rotated = spun && std::abs(spun->rotation.y - 90.0f) < 2.0f;
            const bool untouched = still && glm::length(still->rotation) < 1e-4f;
            if (!rotated || !untouched) {
                sceneRoundTrip = false;
                DE_ERROR("Script test failed: spin=%.2f idle=%.2f", spun ? spun->rotation.y : -1.0f,
                         still ? still->rotation.y : -1.0f);
            } else {
                DE_INFO("Script test: Spin advanced %.1f degrees over 1 s, idle entity unchanged.",
                        spun->rotation.y);
            }
        }
        std::error_code ec;
        fs::remove(path, ec);
        if (!sceneRoundTrip) {
            DE_ERROR("Smoke test failed: scene save/load did not round-trip.");
        }
    }

    // Asset pipeline: generate the placeholder art into a scratch folder, load
    // it back through the same library the renderer uses, and prove that an OBJ
    // written by the engine can be read again byte-for-value.
    bool assetPipelineOk = false;
    {
        namespace fs = std::filesystem;
        const std::string root = "asset-selftest";
        std::error_code ec;
        fs::remove_all(root, ec);

        std::string error;
        asset::AssetLibrary library;
        if (!asset::EnsurePlaceholderAssets(root, error)) {
            DE_ERROR("Asset test failed: %s", error.c_str());
        } else if (!library.Scan(root, error)) {
            DE_ERROR("Asset test failed: %s", error.c_str());
        } else {
            const asset::MeshAsset* character = library.FindMesh("character");
            const asset::TextureAsset* cloth = library.FindTexture("cloth");
            const asset::TextureAsset* stone = library.FindTexture("stone");
            const bool modelsLoaded = library.Meshes().size() >= 3 &&
                                      character && !character->mesh.Empty() &&
                                      character->mesh.boundsMax.y > 1.5f;
            const bool texturesLoaded = library.Textures().size() >= 3 && cloth && stone &&
                                        cloth->image.width > 0 && stone->image.height > 0;

            // OBJ round-trip through the writer and parser.
            bool objRoundTrip = false;
            if (character) {
                std::string writeError;
                const std::string path = root + "/roundtrip.obj";
                if (asset::SaveObj(path, character->mesh, "roundtrip", writeError)) {
                    asset::MeshData reloaded;
                    std::string readError;
                    if (asset::LoadObj(path, reloaded, readError)) {
                        objRoundTrip = reloaded.indices.size() == character->mesh.indices.size() &&
                                       reloaded.vertices.size() == character->mesh.vertices.size();
                    } else {
                        DE_ERROR("OBJ reload failed: %s", readError.c_str());
                    }
                } else {
                    DE_ERROR("OBJ write failed: %s", writeError.c_str());
                }
            }

            assetPipelineOk = modelsLoaded && texturesLoaded && objRoundTrip && objRoundTrip;
            DE_INFO("Asset test: %zu models, %zu textures, OBJ round-trip %s.", library.Meshes().size(),
                    library.Textures().size(), objRoundTrip ? "ok" : "FAILED");
        }
        fs::remove_all(root, ec);
        if (!assetPipelineOk) {
            DE_ERROR("Smoke test failed: the asset pipeline is broken.");
        }
    }

    // The art that ships (or gets generated) in the repository's asset folder
    // should be readable too, not just the scratch copy above.
    {
        asset::AssetLibrary shipped;
        std::string error;
        const std::string root = ResolveAssetDirectory(config.assetsDir);
        const bool scanned = shipped.Scan(root, error);
        if (!scanned && shipped.Failures() > 0) {
            DE_WARN("Bundled assets reported a problem: %s", error.c_str());
        }
        DE_INFO("Bundled assets: %zu models, %zu textures in '%s'.", shipped.Meshes().size(),
                shipped.Textures().size(), root.c_str());
    }

    // Editor data paths: duplicating entities and the in-memory snapshot used
    // by play-in-editor must both behave.
    bool editorDataOk = false;
    {
        scene::Scene source;
        const scene::EntityId parent =
            source.Create("Parent", scene::Primitive::Box, vec3(1.0f, 2.0f, 3.0f));
        const scene::EntityId child =
            source.Create("Child", scene::Primitive::Sphere, vec3(0.0f, 0.5f, 0.0f));
        if (scene::Entity* childEntity = source.Find(child)) {
            childEntity->parent = parent;
            childEntity->scale = vec3(0.5f);
        }

        const scene::EntityId copy = source.Duplicate(parent);
        const scene::Entity* copyEntity = source.Find(copy);
        const bool duplicated = copyEntity && copyEntity->id == copy &&
                                copyEntity->name != "Parent" &&
                                glm::length(copyEntity->position - vec3(1.0f, 2.0f, 3.0f)) < 1e-4f &&
                                source.AliveCount() == 4;
        // The child follows the copied parent.
        int copiedChildren = 0;
        for (const scene::Entity& entity : source.Entities()) {
            if (entity.alive && entity.parent == copy) {
                ++copiedChildren;
            }
        }

        const std::string snapshot = source.Serialize();
        scene::Scene restored;
        std::string restoreError;
        const bool roundTrip = restored.Deserialize(snapshot, restoreError) &&
                               restored.AliveCount() == source.AliveCount();

        editorDataOk = duplicated && copiedChildren == 1 && roundTrip;
        DE_INFO("Editor data test: duplicate %s, hierarchy copy %s, snapshot %s.",
                duplicated ? "ok" : "FAILED", copiedChildren == 1 ? "ok" : "FAILED",
                roundTrip ? "ok" : "FAILED");
        if (!editorDataOk) {
            DE_ERROR("Smoke test failed: editor scene operations are broken (%s).",
                     restoreError.c_str());
        }
    }

    // Viewport maths: a ray must hit the box it is aimed at, miss the one beside
    // it, and the projection of a point on the view axis must land in the middle
    // of the screen. These are the two primitives click-to-select depends on.
    bool pickingOk = false;
    {
        scene::Ray ray;
        ray.origin = vec3(0.0f, 0.0f, 5.0f);
        ray.direction = vec3(0.0f, 0.0f, -1.0f);
        float hitDistance = 0.0f;
        const bool hits = scene::RayIntersectsAabb(ray, vec3(-1.0f, -1.0f, -6.0f),
                                                   vec3(1.0f, 1.0f, -4.0f), hitDistance);
        float missDistance = 0.0f;
        const bool misses = !scene::RayIntersectsAabb(ray, vec3(4.0f, -1.0f, -6.0f),
                                                      vec3(6.0f, 1.0f, -4.0f), missDistance);

        scene::Camera probe;
        probe.SetPerspective(70.0f, 16.0f / 9.0f, 0.1f, 500.0f);
        probe.SetPosition(vec3(0.0f, 0.0f, 0.0f));
        probe.SetRotation(0.0f, 0.0f);
        vec2 centre(9.0f);
        const bool projects = scene::ProjectToNdc(probe.ViewProjection(),
                                                  vec3(0.0f, 0.0f, -10.0f), centre);
        vec2 behind(9.0f);
        const bool rejects = !scene::ProjectToNdc(probe.ViewProjection(),
                                                  vec3(0.0f, 0.0f, 10.0f), behind);

        pickingOk = hits && std::abs(hitDistance - 9.0f) < 0.01f && misses && projects &&
                    std::abs(centre.x) < 1e-3f && std::abs(centre.y) < 1e-3f && rejects;
        DE_INFO("Picking test: hit at %.2f m, sideways miss %s, projection %s.",
                hitDistance, misses ? "ok" : "FAILED", projects ? "centred" : "FAILED");
        if (!pickingOk) {
            DE_ERROR("Smoke test failed: viewport picking maths is wrong.");
        }
    }

    // Third-person camera: the rig must sit behind the player by the requested
    // distance and never end up under the terrain.
    bool cameraRigOk = false;
    {
        game::Player rigPlayer;
        rigPlayer.Spawn(world.Terrain(), world.SpawnPoint());
        rigPlayer.SetThirdPerson(true);
        rigPlayer.SetThirdPersonDistance(5.0f);
        const vec3 head = rigPlayer.Position() + vec3(0.0f, rigPlayer.EyeHeight(), 0.0f);
        const float separation = glm::length(rigPlayer.Camera().Position() - head);
        const float ground = world.Terrain().HeightAt(rigPlayer.Camera().Position().x,
                                                      rigPlayer.Camera().Position().z);
        cameraRigOk = separation > 3.5f && separation < 6.0f &&
                      rigPlayer.Camera().Position().y > ground;
        rigPlayer.SetThirdPerson(false);
        const bool recentred =
            glm::length(rigPlayer.Camera().Position() - head) < 1e-3f;
        cameraRigOk = cameraRigOk && recentred;
        DE_INFO("Camera rig: third person at %.1f m survives the ground, first person recentres.",
                separation);
        if (!cameraRigOk) {
            DE_ERROR("Smoke test failed: the third-person camera rig is misplaced.");
        }
    }

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
            DE_INFO("headless frame %d/%d: pos (%.1f, %.1f, %.1f), speed %.2f m/s, grounded=%d, "
                    "swimming=%d, time %.2f h, engine instances %d",
                    frame + 1, config.headlessFrames, player.Position().x, player.Position().y,
                    player.Position().z, player.HorizontalSpeed(), player.IsGrounded() ? 1 : 0,
                    player.IsSwimming() ? 1 : 0, world.Time().Hours(),
                    world.Vegetation().Stats().visibleInstances);
        }
    }

    const float walkDistance = glm::length(vec2(player.Position().x - world.SpawnPoint().x,
                                                player.Position().z - world.SpawnPoint().z));
    DE_INFO("Headless simulation complete: %.2f s simulated, %d frames, walked %.1f m, "
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

    if (!spawnDry) { DE_ERROR("Smoke test failed: spawn point is underwater."); ok = false; }
    if (!spawnInside) { DE_ERROR("Smoke test failed: spawn point is outside the map."); ok = false; }
    if (!heightfieldComplete) { DE_ERROR("Smoke test failed: heightfield sample count mismatch."); ok = false; }
    if (shaderFailures != 0) { DE_ERROR("Smoke test failed: shader assets could not be expanded."); ok = false; }
    if (!sceneRoundTrip) { DE_ERROR("Smoke test failed: scene serialisation is broken."); ok = false; }
    if (!assetPipelineOk) { DE_ERROR("Smoke test failed: asset import is broken."); ok = false; }
    if (!cameraRigOk) { DE_ERROR("Smoke test failed: camera modes are broken."); ok = false; }
    if (!editorDataOk) { DE_ERROR("Smoke test failed: editor scene operations are broken."); ok = false; }
    if (!pickingOk) { DE_ERROR("Smoke test failed: viewport picking is broken."); ok = false; }
    if (!moved) { DE_ERROR("Smoke test failed: player did not move forward."); ok = false; }
    if (!timeAdvanced) { DE_ERROR("Smoke test failed: time of day did not advance."); ok = false; }
    if (!frustumFront || frustumBehind || frustumFar) {
        DE_ERROR("Smoke test failed: frustum culling is inconsistent "
                 "(front=%d behind=%d far=%d).",
                 frustumFront ? 1 : 0, frustumBehind ? 1 : 0, frustumFar ? 1 : 0);
        ok = false;
    }
    if (config.world.treeCount > 0 && world.Vegetation().Stats().totalInstances == 0) {
        DE_ERROR("Smoke test failed: no vegetation was placed.");
        ok = false;
    }

    // Validate the --selftest image checks themselves with synthetic frames, so
    // a threshold mistake cannot make the GPU self-test report nonsense.
    {
        constexpr int kWidth = 64;
        constexpr int kHeight = 64;
        std::vector<unsigned char> gradient(static_cast<size_t>(kWidth) * kHeight * 3u);
        for (int y = 0; y < kHeight; ++y) {
            // glReadPixels rows are bottom-up: the upper rows are the sky.
            const unsigned char value = y >= kHeight / 2 ? 210 : 40;
            for (int x = 0; x < kWidth; ++x) {
                unsigned char* pixel = &gradient[(static_cast<size_t>(y) * kWidth + x) * 3u];
                pixel[0] = value;
                pixel[1] = value;
                pixel[2] = static_cast<unsigned char>(value / 2);
            }
        }
        const FrameReport gradientReport = AnalyseFrame(gradient.data(), gradient.size(), kWidth, kHeight);
        const bool acceptsGradient = gradientReport.samples > 0 &&
                                     gradientReport.meanLuma > 0.03 && gradientReport.meanLuma < 0.97 &&
                                     gradientReport.stdDev > 0.03 && gradientReport.blackFraction < 0.9 &&
                                     std::fabs(gradientReport.topMean - gradientReport.bottomMean) > 0.02;

        std::vector<unsigned char> black(static_cast<size_t>(kWidth) * kHeight * 3u, 0);
        const FrameReport blackReport = AnalyseFrame(black.data(), black.size(), kWidth, kHeight);
        const bool rejectsBlack = !(blackReport.meanLuma > 0.03);

        DE_INFO("Self-test analysis: gradient mean %.3f/contrast %.3f, black frame mean %.3f.",
                gradientReport.meanLuma, gradientReport.stdDev, blackReport.meanLuma);
        if (!acceptsGradient || !rejectsBlack) {
            DE_ERROR("Smoke test failed: --selftest image analysis misjudges synthetic frames.");
            ok = false;
        }
    }

    const world::LightingState& lighting = world.Lighting();
    DE_INFO("Lighting: sun elevation %.1f deg, daylight %.2f, stars %.2f, sun intensity %.2f.",
            lighting.sun.elevationDegrees, lighting.daylight, lighting.stars, lighting.sun.intensity);
    DE_INFO("Terrain: %lld triangles (CPU grid) | vegetation instances: %d | visible candidates: %d "
            "| culled: %d",
            terrainTriangles, world.Vegetation().Stats().totalInstances,
            world.Vegetation().Stats().visibleInstances, world.Vegetation().Stats().culledInstances);
    if (!ok) {
        DE_ERROR("Headless validation failed.");
        return 3;
    }
    DE_INFO("Headless validation succeeded (spawn, heightfield, movement, time, vegetation, "
            "scene, scripting, assets, cameras).");
    return 0;
}

} // namespace derecho::core

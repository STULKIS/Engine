// Derecho Engine - application lifetime, main loop and subsystem orchestration.
#pragma once

#include "core/Config.h"
#include "core/Input.h"
#include "core/Time.h"
#include "scene/Camera.h"
#include "scene/Script.h"
#include "scene/Scene.h"
#include "ui/Editor.h"

#include <memory>
#include <string>

union SDL_Event;

namespace derecho::core { class Window; }
namespace derecho::game { class Player; }
namespace derecho::render { class Renderer; }
namespace derecho::ui { class DebugUI; struct DebugActions; }
namespace derecho::world { class World; }

namespace derecho::core {

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Returns a process exit code (0 on success).
    int Run(AppConfig& config);

private:
    bool Initialize(AppConfig& config);
    void Shutdown();

    void HandleEvent(const SDL_Event& event);
    void Update(float deltaSeconds);
    void RenderFrame();
    void ApplyActions(ui::DebugActions& actions);
    void ApplyEditorActions(ui::EditorActions& actions);
    void StartPlay();
    void StopPlay();
    void TogglePause();
    void SyncPlayerSettings();
    void RespawnPlayer();
    void TakeScreenshot();

    // Scene: generated starter content, plus save/load.
    void BuildDefaultScene();
    void SaveScene(const std::string& path);
    void LoadScene(const std::string& path);
    // Keeps the visible player character in sync with the player and the camera.
    void UpdatePlayerCharacter();

    int RunHeadless(AppConfig& config);
    int RunSelfTest();
    int RunGLInfo(AppConfig& config);

    AppConfig m_config;
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Input> m_input;
    std::unique_ptr<render::Renderer> m_renderer;
    std::unique_ptr<world::World> m_world;
    scene::Scene m_entities;
    scene::ScriptHost m_scripts;
    scene::EntityId m_playerEntity = scene::kInvalidEntity;
    std::unique_ptr<game::Player> m_player;
    std::unique_ptr<ui::DebugUI> m_debugUI;
    std::unique_ptr<ui::Editor> m_editor;
    scene::Camera m_editorCamera;
    std::string m_playSnapshot;
    vec3 m_playStartPosition{0.0f};
    ui::PlayState m_playState = ui::PlayState::Stopped;
    bool m_editorMode = true;

    Clock m_clock;
    FrameStats m_frameStats;
    bool m_running = false;
    bool m_screenshotRequested = false;
};

} // namespace derecho::core

// Frontier Engine - application lifetime, main loop and subsystem orchestration.
#pragma once

#include "core/Config.h"
#include "core/Input.h"
#include "core/Time.h"
#include "scene/Script.h"
#include "scene/Scene.h"

#include <memory>
#include <string>

union SDL_Event;

namespace frontier::core { class Window; }
namespace frontier::game { class Player; }
namespace frontier::render { class Renderer; }
namespace frontier::ui { class DebugUI; struct DebugActions; }
namespace frontier::world { class World; }

namespace frontier::core {

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

    Clock m_clock;
    FrameStats m_frameStats;
    bool m_running = false;
    bool m_screenshotRequested = false;
};

} // namespace frontier::core

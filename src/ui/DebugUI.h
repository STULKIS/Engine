// Derecho Engine - ImGui debug / tuning panel.
#pragma once

#include "core/Config.h"
#include "core/Time.h"
#include "scene/Scene.h"

namespace derecho::core { class Window; }
namespace derecho::world { class World; }
namespace derecho::game { class Player; }
namespace derecho::render { class Renderer; }

namespace derecho::ui {

struct DebugActions {
    bool regenerateWorld = false;
    bool respawnPlayer = false;
    bool takeScreenshot = false;
    bool requestQuit = false;
    bool toggleFullscreen = false;
    bool toggleMouseCapture = false;
    bool saveConfig = false;
    bool saveScene = false;
    bool loadScene = false;
};

class DebugUI {
public:
    bool Initialize(core::Window& window, bool showDemo);
    void Shutdown();

    void NewFrame();
    void Draw(core::AppConfig& config, world::World& world, scene::Scene& entities,
              game::Player& player, render::Renderer& renderer,
              const core::FrameStats& frameStats, DebugActions& actions);
    void Render();

    bool Visible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }
    void ToggleVisible() { m_visible = !m_visible; }
    bool WantsMouse() const;
    bool WantsKeyboard() const;

private:
    void DrawOverview(core::AppConfig&, world::World&, game::Player&, render::Renderer&,
                      const core::FrameStats&, DebugActions&);
    void DrawWorldPanel(world::World&, DebugActions&);
    void DrawScenePanel(core::AppConfig&, scene::Scene&, render::Renderer&, DebugActions&);
    void DrawRenderPanel(core::AppConfig&, render::Renderer&);
    void DrawPlayerPanel(core::AppConfig&, game::Player&, DebugActions&);
    void DrawControlsPanel(world::World&, game::Player&, render::Renderer&);

    bool m_initialized = false;
    bool m_visible = true;
    bool m_showDemo = false;
    bool m_showAbout = false;
    scene::EntityId m_selectedEntity = scene::kInvalidEntity;
};

} // namespace derecho::ui

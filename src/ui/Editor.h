// Derecho Engine - the in-engine editor.
//
// A fixed, Unity/Unreal-shaped shell: menu bar, toolbar, hierarchy, inspector,
// asset browser, console, statistics and a viewport that shows the actual
// composited frame. The viewport supports click-to-select, a translate/rotate/
// scale gizmo, free-fly navigation, and Play/Pause/Stop with a scene snapshot
// restored on stop (play-in-editor).
#pragma once

#include "core/Config.h"
#include "core/Time.h"
#include "math/Math.h"
#include "scene/Scene.h"

#include <string>
#include <vector>

namespace derecho::core { class Window; }
namespace derecho::game { class Player; }
namespace derecho::render { class Renderer; }
namespace derecho::world { class World; }

namespace derecho::scene { class Camera; }

namespace derecho::ui {

enum class GizmoMode { Translate, Rotate, Scale };
enum class PlayState { Stopped, Playing, Paused };

struct EditorActions {
    bool quit = false;
    bool saveScene = false;
    bool reloadScene = false;
    bool newScene = false;
    bool play = false;
    bool pause = false;
    bool stop = false;
    bool deleteEntity = false;
    bool duplicateEntity = false;
    bool focusSelected = false;
    bool toggleFullscreen = false;
    bool requestScreenshot = false;
};

class Editor {
public:
    bool Initialize();
    void Shutdown();

    void NewFrame();
    void Draw(core::AppConfig& config, world::World& world, scene::Scene& entities,
              game::Player& player, render::Renderer& renderer, const core::FrameStats& stats,
              scene::Camera& editorCamera, EditorActions& actions);
    void Render();

    // True when the editor owns the frame (play mode stopped).
    bool InEditorMode() const { return m_playState == PlayState::Stopped; }
    PlayState State() const { return m_playState; }
    void SetState(PlayState state) { m_playState = state; }

    scene::EntityId Selected() const { return m_selected; }
    void Select(scene::EntityId id) { m_selected = id; }
    GizmoMode Mode() const { return m_mode; }

    // Viewport geometry, so the engine can aspect-correct and the camera can be
    // driven only while the viewport is active.
    bool ViewportHovered() const { return m_viewportHovered; }
    vec2 ViewportImageSize() const { return m_imageSize; }

    bool WantsMouse() const;
    bool WantsKeyboard() const;

private:
    void DrawMenuBar(core::AppConfig& config, scene::Scene& entities, render::Renderer& renderer,
                     scene::Camera& editorCamera, EditorActions& actions);
    void DrawToolbar(core::AppConfig& config, scene::Scene& entities, EditorActions& actions);
    void DrawHierarchy(scene::Scene& entities, EditorActions& actions);
    void DrawInspector(core::AppConfig& config, scene::Scene& entities, render::Renderer& renderer,
                       scene::Camera& editorCamera, EditorActions& actions);
    void DrawAssets(scene::Scene& entities, render::Renderer& renderer, scene::Camera& editorCamera);
    void DrawConsole();
    void DrawStats(world::World& world, game::Player& player, render::Renderer& renderer,
                   const core::FrameStats& stats, const scene::Camera& camera);
    void DrawViewport(core::AppConfig& config, scene::Scene& entities, render::Renderer& renderer,
                      scene::Camera& editorCamera, game::Player& player);

    // Editor camera: WASD + Q/E, right mouse to look, scroll to change speed.
    void UpdateCamera(scene::Camera& camera, world::World& world, float deltaSeconds);

    // Click-to-select and gizmo dragging inside the viewport image.
    void HandleViewportInput(scene::Scene& entities, const scene::Camera& camera);
    void DrawGizmo(scene::Scene& entities, const scene::Camera& camera);
    bool ProjectToViewport(const vec3& world, const scene::Camera& camera, vec2& screen) const;

    scene::EntityId PickEntity(scene::Scene& entities, const scene::Camera& camera,
                               const vec2& mouse) const;
    scene::EntityId AddEntity(scene::Scene& entities, scene::Primitive primitive,
                              const std::string& name, const scene::Camera& camera);

    bool m_initialized = false;
    bool m_showDemo = false;
    bool m_showAbout = false;
    bool m_showLayoutSettings = false;

    PlayState m_playState = PlayState::Stopped;
    GizmoMode m_mode = GizmoMode::Translate;
    scene::EntityId m_selected = scene::kInvalidEntity;

    // Viewport rect in screen space (image area, letterboxed to the frame).
    vec2 m_imagePos{0.0f};
    vec2 m_imageSize{0.0f};
    bool m_viewportHovered = false;
    bool m_viewportFocused = false;
    bool m_looking = false;

    // Gizmo drag state.
    int m_dragAxis = -1;
    vec2 m_dragStartMouse{0.0f};
    vec3 m_dragStartValue{0.0f};

    // Valid only during Draw(); the panels need them for picking and placement.
    render::Renderer* m_rendererForPicking = nullptr;
    world::World* m_worldForEditing = nullptr;
    scene::Camera* m_editorCameraTarget = nullptr;

    float m_cameraSpeed = 14.0f;
    float m_fpsSmoothed = 0.0f;
    bool m_consoleAutoScroll = true;
    int m_consoleLevelFilter = 1; // 0 trace, 1 info, 2 warn, 3 error
    char m_search[64] = {0};
};

} // namespace derecho::ui

// Derecho Engine - in-engine editor implementation.
#include "ui/Editor.h"

#include "asset/AssetLibrary.h"
#include "core/Log.h"
#include "game/Player.h"
#include "render/Renderer.h"
#include "scene/Camera.h"
#include "scene/Picking.h"
#include "scene/Script.h"
#include "scene/Terrain.h"
#include "world/World.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstring>

namespace derecho::ui {

namespace {

constexpr float kLeftWidth = 296.0f;
constexpr float kRightWidth = 336.0f;
constexpr float kConsoleHeight = 176.0f;
constexpr float kAssetsHeight = 250.0f;
constexpr float kToolbarHeight = 40.0f;
constexpr float kGap = 4.0f;

ImTextureID ToTextureId(unsigned int id) {
    return reinterpret_cast<ImTextureID>(static_cast<intptr_t>(id));
}

const ImU32 kAxisColors[3] = {IM_COL32(224, 82, 82, 255), IM_COL32(110, 210, 110, 255),
                              IM_COL32(96, 150, 240, 255)};

const char* PlayStateName(PlayState state) {
    switch (state) {
        case PlayState::Playing: return "PLAYING";
        case PlayState::Paused: return "PAUSED";
        default: return "EDITOR";
    }
}

// World-space AABB of an entity (mesh bounds when it has one, else the
// primitive's unit bounds), transformed by the entity's world matrix.
bool EntityWorldBounds(const scene::Scene& entities, const scene::Entity& entity,
                       const render::Renderer& renderer, vec3& outMin, vec3& outMax) {
    vec3 localMin(-0.5f);
    vec3 localMax(0.5f);
    if (!entity.mesh.empty()) {
        if (const asset::MeshAsset* mesh = renderer.Assets().FindMesh(entity.mesh)) {
            localMin = mesh->mesh.boundsMin;
            localMax = mesh->mesh.boundsMax;
        }
    }
    const mat4 world = entities.WorldMatrix(entity);
    outMin = vec3(1e30f);
    outMax = vec3(-1e30f);
    for (int corner = 0; corner < 8; ++corner) {
        const vec3 local((corner & 1) ? localMax.x : localMin.x,
                         (corner & 2) ? localMax.y : localMin.y,
                         (corner & 4) ? localMax.z : localMin.z);
        const vec3 p = vec3(world * vec4(local, 1.0f));
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    }
    return true;
}

float DistanceToSegment(const vec2& point, const vec2& a, const vec2& b) {
    const vec2 ab = b - a;
    const float lengthSq = glm::dot(ab, ab);
    if (lengthSq < 1e-5f) {
        return glm::length(point - a);
    }
    const float t = glm::clamp(glm::dot(point - a, ab) / lengthSq, 0.0f, 1.0f);
    return glm::length(point - (a + ab * t));
}

} // namespace

bool Editor::Initialize() {
    // A compact dark theme: neutral greys, one accent colour, sharp corners.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.086f, 0.090f, 0.102f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.106f, 0.110f, 0.125f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.098f, 0.102f, 0.118f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.098f, 0.102f, 0.118f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.38f, 0.45f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.17f, 0.19f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.28f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.35f, 0.42f, 1.00f);
    // The palette follows the logo: hazard crimson accent, tungsten greys.
    colors[ImGuiCol_CheckMark] = ImVec4(0.78f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.72f, 0.24f, 0.26f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.30f, 0.31f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.88f, 0.89f, 0.91f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.54f, 0.58f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.21f, 0.23f, 0.27f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.24f, 0.25f, 0.29f, 0.60f);

    m_initialized = true;
    return true;
}

void Editor::Shutdown() {
    m_initialized = false;
}

void Editor::NewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

bool Editor::WantsMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool Editor::WantsKeyboard() const {
    // Text fields always win; otherwise the editor still wants WASD for the
    // camera, so only report capture while typing.
    return ImGui::GetIO().WantTextInput;
}

void Editor::Render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ------------------------------------------------------------------ camera

void Editor::UpdateCamera(scene::Camera& camera, world::World& world, float deltaSeconds) {
    if (!m_viewportHovered && !m_looking) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();

    // Right mouse drag looks around, like Unreal (LMB is selection).
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_viewportHovered) {
        m_looking = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_looking = false;
    }
    if (m_looking) {
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            camera.Rotate(io.MouseDelta.x * 0.0038f, -io.MouseDelta.y * 0.0038f);
        }
    }

    const float wheel = io.MouseWheel;
    if (m_viewportHovered && wheel != 0.0f) {
        m_cameraSpeed = glm::clamp(m_cameraSpeed * std::pow(1.18f, wheel), 1.0f, 220.0f);
    }

    if (io.WantTextInput || !m_looking) {
        return;
    }

    float forward = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    if (ImGui::IsKeyDown(ImGuiKey_W)) forward += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S)) forward -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D)) right += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A)) right -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_E)) up += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) up -= 1.0f;
    const bool fast = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

    if (forward == 0.0f && right == 0.0f && up == 0.0f) {
        return;
    }
    const vec3 fwd = camera.Forward();
    const vec3 side = camera.Right();
    vec3 position = camera.Position();
    const float speed = m_cameraSpeed * (fast ? 3.0f : 1.0f) * deltaSeconds;
    position += fwd * (forward * speed) + side * (right * speed) + vec3(0.0f, up * speed, 0.0f);

    // Keep the editor camera out of the ground.
    const float ground = world.Terrain().HeightAt(position.x, position.z);
    if (position.y < ground + 0.6f) {
        position.y = ground + 0.6f;
    }
    camera.SetPosition(position);
}

// ---------------------------------------------------------------- viewport

bool Editor::ProjectToViewport(const vec3& world, const scene::Camera& camera, vec2& screen) const {
    vec2 ndc(0.0f);
    if (!scene::ProjectToNdc(camera.ViewProjection(), world, ndc)) {
        return false;
    }
    screen.x = m_imagePos.x + (ndc.x * 0.5f + 0.5f) * m_imageSize.x;
    screen.y = m_imagePos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * m_imageSize.y;
    return true;
}

scene::EntityId Editor::PickEntity(scene::Scene& entities, const scene::Camera& camera,
                                   const vec2& mouse) const {
    if (m_imageSize.x <= 1.0f || m_imageSize.y <= 1.0f) {
        return scene::kInvalidEntity;
    }
    const vec2 uv = (mouse - m_imagePos) / m_imageSize;
    const float ndcX = uv.x * 2.0f - 1.0f;
    const float ndcY = 1.0f - uv.y * 2.0f;
    const scene::Ray ray{camera.Position(), camera.RayDirection(ndcX, ndcY)};

    scene::EntityId best = scene::kInvalidEntity;
    float bestDistance = 1e30f;
    for (const scene::Entity& entity : entities.Entities()) {
        if (!entity.alive || !entity.visible) {
            continue;
        }
        vec3 boundsMin;
        vec3 boundsMax;
        if (!EntityWorldBounds(entities, entity, *m_rendererForPicking, boundsMin, boundsMax)) {
            continue;
        }
        float distance = 0.0f;
        if (scene::RayIntersectsAabb(ray, boundsMin, boundsMax, distance) && distance < bestDistance) {
            bestDistance = distance;
            best = entity.id;
        }
    }
    return best;
}

scene::EntityId Editor::AddEntity(scene::Scene& entities, scene::Primitive primitive,
                                  const std::string& name, const scene::Camera& camera) {
    vec3 position = camera.Position() + camera.Forward() * 7.0f;
    if (m_worldForEditing) {
        position.y = m_worldForEditing->Terrain().HeightAt(position.x, position.z) + 0.5f;
    }
    const scene::EntityId id = entities.Create(entities.UniqueName(name), primitive, position);
    if (scene::Entity* entity = entities.Find(id)) {
        const char* label = scene::PrimitiveName(primitive);
        entity->position = position;
        (void)label;
    }
    m_selected = id;
    return id;
}

void Editor::HandleViewportInput(scene::Scene& entities, const scene::Camera& camera) {
    ImGuiIO& io = ImGui::GetIO();
    if (!m_viewportHovered) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_dragAxis = -1;
        }
        return;
    }

    const bool hotkeys = !io.WantTextInput && !m_looking;
    if (hotkeys && ImGui::IsKeyPressed(ImGuiKey_W, false)) m_mode = GizmoMode::Translate;
    if (hotkeys && ImGui::IsKeyPressed(ImGuiKey_E, false)) m_mode = GizmoMode::Rotate;
    if (hotkeys && ImGui::IsKeyPressed(ImGuiKey_R, false)) m_mode = GizmoMode::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (m_selected != scene::kInvalidEntity) {
            entities.Destroy(m_selected);
            m_selected = scene::kInvalidEntity;
        }
    }
    if (hotkeys && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        if (const scene::Entity* entity = entities.Find(m_selected)) {
            const vec3 target = entity->position;
            const vec3 offset = camera.Forward() * -9.0f;
            m_editorCameraTarget->SetPosition(target + offset + vec3(0.0f, 2.5f, 0.0f));
            m_editorCameraTarget->LookAt(target);
        }
    }

    // Gizmo handles take precedence over picking.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const vec2 mouse(io.MousePos.x, io.MousePos.y);
        const scene::Entity* entity = entities.Find(m_selected);
        if (entity) {
            const vec3 origin = vec3(entities.WorldMatrix(*entity)[3]);
            const float distance = glm::length(origin - camera.Position());
            const float axisLength = std::max(1.0f, distance * 0.16f);
            vec2 originScreen;
            if (ProjectToViewport(origin, camera, originScreen)) {
                float bestPixels = 12.0f;
                int bestAxis = -1;
                for (int axis = 0; axis < 3; ++axis) {
                    vec3 direction(0.0f);
                    direction[axis] = 1.0f;
                    vec2 tipScreen;
                    if (!ProjectToViewport(origin + direction * axisLength, camera, tipScreen)) {
                        continue;
                    }
                    const float pixels = DistanceToSegment(mouse, originScreen, tipScreen);
                    if (pixels < bestPixels) {
                        bestPixels = pixels;
                        bestAxis = axis;
                    }
                }
                if (bestAxis >= 0) {
                    m_dragAxis = bestAxis;
                    m_dragStartMouse = mouse;
                    m_dragStartValue = m_mode == GizmoMode::Translate
                                           ? entity->position
                                           : (m_mode == GizmoMode::Rotate ? entity->rotation
                                                                          : entity->scale);
                } else {
                    m_selected = PickEntity(entities, camera, mouse);
                }
            }
        } else {
            m_selected = PickEntity(entities, camera, mouse);
        }
    }

    // Continue a drag.
    if (m_dragAxis >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        scene::Entity* entity = entities.Find(m_selected);
        if (!entity) {
            m_dragAxis = -1;
            return;
        }
        const vec3 origin = m_mode == GizmoMode::Translate
                                ? m_dragStartValue
                                : vec3(entities.WorldMatrix(*entity)[3]);
        const float distance = glm::length(origin - camera.Position());
        const float axisLength = std::max(1.0f, distance * 0.16f);
        vec2 originScreen;
        vec3 direction(0.0f);
        direction[m_dragAxis] = 1.0f;
        vec2 tipScreen;
        if (!ProjectToViewport(origin, camera, originScreen) ||
            !ProjectToViewport(origin + direction * axisLength, camera, tipScreen)) {
            return;
        }
        const vec2 axisScreen = tipScreen - originScreen;
        const float screenLength = glm::length(axisScreen);
        if (screenLength < 1e-3f) {
            return;
        }
        const vec2 axisDirection = axisScreen / screenLength;
        const vec2 mouse(io.MousePos.x, io.MousePos.y);
        const float pixels = glm::dot(mouse - m_dragStartMouse, axisDirection);
        const float worldPerPixel = axisLength / screenLength;

        if (m_mode == GizmoMode::Translate) {
            entity->position = m_dragStartValue;
            entity->position[m_dragAxis] += pixels * worldPerPixel;
        } else if (m_mode == GizmoMode::Rotate) {
            entity->rotation = m_dragStartValue;
            entity->rotation[m_dragAxis] += pixels * 0.55f;
        } else {
            entity->scale = m_dragStartValue;
            entity->scale[m_dragAxis] =
                std::max(0.02f, m_dragStartValue[m_dragAxis] * (1.0f + pixels * 0.004f));
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_dragAxis = -1;
    }
}

void Editor::DrawGizmo(scene::Scene& entities, const scene::Camera& camera) {
    const scene::Entity* entity = entities.Find(m_selected);
    if (!entity || !entity->alive) {
        return;
    }
    const vec3 origin = vec3(entities.WorldMatrix(*entity)[3]);
    const float distance = glm::length(origin - camera.Position());
    const float axisLength = std::max(1.0f, distance * 0.16f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    vec2 originScreen;
    if (!ProjectToViewport(origin, camera, originScreen)) {
        return;
    }

    // Selection box.
    vec3 boundsMin;
    vec3 boundsMax;
    if (m_rendererForPicking &&
        EntityWorldBounds(entities, *entity, *m_rendererForPicking, boundsMin, boundsMax)) {
        vec2 corners[8];
        bool visible = true;
        for (int corner = 0; corner < 8 && visible; ++corner) {
            const vec3 p((corner & 1) ? boundsMax.x : boundsMin.x,
                         (corner & 2) ? boundsMax.y : boundsMin.y,
                         (corner & 4) ? boundsMax.z : boundsMin.z);
            visible = ProjectToViewport(p, camera, corners[corner]);
        }
        if (visible) {
            const ImU32 color = IM_COL32(240, 176, 72, 170);
            static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                             {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : edges) {
                draw->AddLine(ImVec2(corners[edge[0]].x, corners[edge[0]].y),
                              ImVec2(corners[edge[1]].x, corners[edge[1]].y), color, 1.0f);
            }
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        vec3 direction(0.0f);
        direction[axis] = 1.0f;
        vec2 tipScreen;
        if (!ProjectToViewport(origin + direction * axisLength, camera, tipScreen)) {
            continue;
        }
        const bool active = m_dragAxis == axis;
        const float thickness = active ? 4.0f : 2.5f;
        const ImU32 color = kAxisColors[axis];
        draw->AddLine(ImVec2(originScreen.x, originScreen.y), ImVec2(tipScreen.x, tipScreen.y), color,
                      thickness);
        draw->AddCircleFilled(ImVec2(tipScreen.x, tipScreen.y), active ? 7.0f : 5.5f, color);
    }
    draw->AddCircleFilled(ImVec2(originScreen.x, originScreen.y), 3.5f, IM_COL32(235, 235, 240, 220));

    const char* modeName = m_mode == GizmoMode::Translate ? "Move"
                           : m_mode == GizmoMode::Rotate  ? "Rotate"
                                                          : "Scale";
    draw->AddText(ImVec2(originScreen.x + 12.0f, originScreen.y + 8.0f),
                  IM_COL32(230, 230, 235, 200), modeName);
}

void Editor::DrawViewport(core::AppConfig& config, scene::Scene& entities,
                          render::Renderer& renderer, scene::Camera& editorCamera,
                          game::Player& player) {
    const bool playing = m_playState != PlayState::Stopped;
    const scene::Camera& camera = playing ? player.Camera() : editorCamera;

    // Header strip: which camera is live and the viewport resolution.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##viewport", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    m_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    m_viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

    // Letterbox the composited frame to preserve its aspect ratio.
    const float frameAspect = static_cast<float>(renderer.OutputWidth()) /
                              static_cast<float>(std::max(1, renderer.OutputHeight()));
    float width = available.x;
    float height = width / frameAspect;
    if (height > available.y) {
        height = available.y;
        width = height * frameAspect;
    }
    const ImVec2 imagePos(origin.x + (available.x - width) * 0.5f,
                          origin.y + (available.y - height) * 0.5f);
    m_imagePos = vec2(imagePos.x, imagePos.y);
    m_imageSize = vec2(width, height);

    if (const unsigned int frame = renderer.FrameTexture(); frame != 0) {
        ImGui::SetCursorScreenPos(imagePos);
        ImGui::Image(ToTextureId(frame), ImVec2(width, height), ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
    } else {
        ImGui::SetCursorScreenPos(imagePos);
        ImGui::Dummy(ImVec2(width, height));
    }

    // Overlays live inside the clipped viewport region.
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("##overlay", available, false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    if (m_playState != PlayState::Stopped) {
        ImGui::SetCursorPos(ImVec2(12.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.42f, 1.0f));
        ImGui::TextUnformatted(m_playState == PlayState::Paused ? "PAUSED" : "PLAYING");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("Esc stops, F5 pauses");
    } else {
        ImGui::SetCursorPos(ImVec2(12.0f, 10.0f));
        ImGui::TextDisabled("RMB look · WASD/QE fly · Shift fast · wheel speed %.0f", m_cameraSpeed);
        ImGui::SameLine();
        ImGui::SetCursorPosX(available.x - 210.0f);
        ImGui::TextDisabled("%.0fx%.0f", m_imageSize.x, m_imageSize.y);
    }

    if (m_playState == PlayState::Stopped) {
        HandleViewportInput(entities, camera);
        DrawGizmo(entities, camera);
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    (void)config;
}

// ------------------------------------------------------------------ panels

void Editor::DrawMenuBar(core::AppConfig& config, scene::Scene& entities,
                         render::Renderer& renderer, scene::Camera& editorCamera,
                         EditorActions& actions) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Scene", "Ctrl+S")) actions.saveScene = true;
        if (ImGui::MenuItem("Reload Scene From Disk", "Ctrl+R")) actions.reloadScene = true;
        if (ImGui::MenuItem("New Starter Scene")) actions.newScene = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Screenshot", "F12")) actions.requestScreenshot = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) actions.quit = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const bool hasSelection = entities.Find(m_selected) != nullptr;
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) {
            actions.duplicateEntity = true;
        }
        if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) {
            actions.deleteEntity = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Focus Selection", "F", false, hasSelection)) {
            actions.focusSelected = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject")) {
        if (ImGui::MenuItem("Empty (Box)")) AddEntity(entities, scene::Primitive::Box, "Entity", editorCamera);
        if (ImGui::MenuItem("Sphere")) AddEntity(entities, scene::Primitive::Sphere, "Sphere", editorCamera);
        if (ImGui::MenuItem("Cylinder")) AddEntity(entities, scene::Primitive::Cylinder, "Cylinder", editorCamera);
        if (ImGui::MenuItem("Cone")) AddEntity(entities, scene::Primitive::Cone, "Cone", editorCamera);
        if (!renderer.Assets().Meshes().empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("Imported models");
            for (const asset::MeshAsset& mesh : renderer.Assets().Meshes()) {
                if (ImGui::MenuItem(mesh.name.c_str())) {
                    const scene::EntityId id =
                        AddEntity(entities, scene::Primitive::Box, mesh.name, editorCamera);
                    if (scene::Entity* entity = entities.Find(id)) {
                        entity->mesh = mesh.name;
                        entity->color = vec3(1.0f);
                    }
                }
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::TextDisabled("Render style");
        const bool anime = config.render.style == core::RenderStyle::Anime;
        if (ImGui::MenuItem("Realistic", nullptr, !anime)) config.render.style = core::RenderStyle::Realistic;
        if (ImGui::MenuItem("Anime (NPR)", nullptr, anime)) config.render.style = core::RenderStyle::Anime;
        ImGui::Separator();
        ImGui::MenuItem("Wireframe", "F2", &config.render.wireframe);
        ImGui::MenuItem("Shadows", "F3", &config.render.showShadows);
        ImGui::MenuItem("Foliage", "F4", &config.render.showFoliage);
        ImGui::MenuItem("Water", "F5", &config.render.showWater);
        ImGui::Separator();
        if (ImGui::MenuItem("Fullscreen", "F11")) actions.toggleFullscreen = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Derecho Engine")) m_showAbout = true;
        if (ImGui::MenuItem("ImGui Demo")) m_showDemo = true;
        ImGui::EndMenu();
    }

    // Right-aligned state readout in the menu bar.
    char status[128];
    std::snprintf(status, sizeof(status), "%s   ·   %zu entities   ·   %s",
                  PlayStateName(m_playState), entities.AliveCount(), entities.Path().c_str());
    const float textWidth = ImGui::CalcTextSize(status).x;
    ImGui::SameLine(std::max(0.0f, ImGui::GetWindowWidth() - textWidth - 18.0f));
    ImGui::TextDisabled("%s", status);

    ImGui::EndMenuBar();
}

void Editor::DrawToolbar(core::AppConfig& config, scene::Scene& entities, EditorActions& actions) {
    ImGui::BeginChild("##toolbar", ImVec2(0.0f, kToolbarHeight), false,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(6.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.24f, 1.0f));
    if (ImGui::Button("Play", ImVec2(58.0f, 26.0f))) actions.play = true;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Pause", ImVec2(58.0f, 26.0f))) actions.pause = true;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Stop", ImVec2(52.0f, 26.0f))) actions.stop = true;
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 1.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("Gizmo");
    for (int mode = 0; mode < 3; ++mode) {
        ImGui::SameLine();
        const bool active = static_cast<int>(m_mode) == mode;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.20f, 0.21f, 1.0f));
        if (ImGui::Button(mode == 0 ? "Move (W)" : mode == 1 ? "Rotate (E)" : "Scale (R)",
                          ImVec2(mode == 0 ? 76.0f : 86.0f, 26.0f))) {
            m_mode = static_cast<GizmoMode>(mode);
        }
        if (active) ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 1.0f));
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(56.0f, 26.0f))) actions.saveScene = true;
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(64.0f, 26.0f))) actions.reloadScene = true;

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 1.0f));
    ImGui::SameLine();
    bool anime = config.render.style == core::RenderStyle::Anime;
    if (ImGui::Checkbox("Anime", &anime)) {
        config.render.style = anime ? core::RenderStyle::Anime : core::RenderStyle::Realistic;
    }
    ImGui::SameLine();
    bool outlines = config.render.outlines;
    if (ImGui::Checkbox("Ink", &outlines)) {
        config.render.outlines = outlines;
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 190.0f);
    ImGui::TextColored(ImVec4(0.85f, 0.86f, 0.9f, 1.0f), "%.0f FPS", m_fpsSmoothed);
    ImGui::SameLine();
    ImGui::TextDisabled("· %zu objects", entities.AliveCount());

    ImGui::EndChild();
}

void Editor::DrawHierarchy(scene::Scene& entities, EditorActions& actions) {
    ImGui::TextDisabled("HIERARCHY");
    ImGui::Separator();
    if (ImGui::Button("Add")) {
        ImGui::OpenPopup("add-entity");
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && entities.Find(m_selected)) {
        actions.duplicateEntity = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && entities.Find(m_selected)) {
        actions.deleteEntity = true;
    }

    if (ImGui::BeginPopup("add-entity")) {
        for (int i = 0; i < static_cast<int>(scene::Primitive::Count); ++i) {
            const auto primitive = static_cast<scene::Primitive>(i);
            if (ImGui::MenuItem(scene::PrimitiveName(primitive))) {
                AddEntity(entities, primitive, scene::PrimitiveName(primitive), *m_editorCameraTarget);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::BeginChild("##tree", ImVec2(0.0f, 0.0f), false);

    // Roots first, then children under their parent, so the tree reads like a
    // scene graph rather than a flat list.
    std::function<void(scene::EntityId)> drawNode = [&](scene::EntityId id) {
        const scene::Entity* entity = entities.Find(id);
        if (!entity) {
            return;
        }
        ImGui::PushID(static_cast<int>(id));
        std::vector<scene::Entity> children;
        for (const scene::Entity& candidate : entities.Entities()) {
            if (candidate.alive && candidate.parent == id) {
                children.push_back(candidate);
            }
        }
        const bool isLeaf = children.empty();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_DefaultOpen;
        if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (m_selected == id) flags |= ImGuiTreeNodeFlags_Selected;

        // Visibility toggle in front of the name.
        bool visible = entity->visible;
        if (ImGui::Checkbox("##visible", &visible)) {
            if (scene::Entity* mutableEntity = entities.Find(id)) {
                mutableEntity->visible = visible;
            }
        }
        ImGui::SameLine();

        std::string label = entity->name;
        if (!entity->mesh.empty()) label += "  [" + entity->mesh + "]";
        const bool open = ImGui::TreeNodeEx("node", flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_selected = id;
        }
        if (ImGui::BeginPopupContextItem("ctx")) {
            if (ImGui::MenuItem("Duplicate")) {
                m_selected = id;
                actions.duplicateEntity = true;
            }
            if (ImGui::MenuItem("Delete")) {
                m_selected = id;
                actions.deleteEntity = true;
            }
            if (ImGui::MenuItem("Focus")) {
                m_selected = id;
                actions.focusSelected = true;
            }
            ImGui::EndPopup();
        }
        if (open && !isLeaf) {
            for (const scene::Entity& child : children) {
                drawNode(child.id);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    for (const scene::Entity& entity : entities.Entities()) {
        if (entity.alive && entity.parent == scene::kInvalidEntity) {
            drawNode(entity.id);
        }
    }

    ImGui::EndChild();
}

void Editor::DrawInspector(core::AppConfig& config, scene::Scene& entities,
                           render::Renderer& renderer, scene::Camera& editorCamera,
                           EditorActions& actions) {
    ImGui::TextDisabled("INSPECTOR");
    ImGui::Separator();

    scene::Entity* entity = entities.Find(m_selected);
    if (!entity) {
        ImGui::TextDisabled("No selection.");
        ImGui::TextWrapped("Click an object in the viewport or pick one in the hierarchy.");
        return;
    }

    char name[64];
    std::snprintf(name, sizeof(name), "%s", entity->name.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##name", name, sizeof(name))) {
        entity->name = name;
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat3("Position", &entity->position.x, 0.05f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat3("Rotation", &entity->rotation.x, 0.5f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::DragFloat3("Scale", &entity->scale.x, 0.02f, 0.01f, 200.0f);
        if (ImGui::Button("Reset transform")) {
            entity->rotation = vec3(0.0f);
            entity->scale = vec3(1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Focus")) {
            const vec3 target = entity->position;
            editorCamera.SetPosition(target - editorCamera.Forward() * 9.0f + vec3(0.0f, 2.5f, 0.0f));
            editorCamera.LookAt(target);
        }
    }

    if (ImGui::CollapsingHeader("Mesh & material", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& assets = renderer.Assets();
        std::string currentModel = entity->mesh.empty() ? "Primitive" : entity->mesh;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##model", currentModel.c_str())) {
            if (ImGui::Selectable("Primitive", entity->mesh.empty())) entity->mesh.clear();
            for (const asset::MeshAsset& mesh : assets.Meshes()) {
                if (ImGui::Selectable(mesh.name.c_str(), entity->mesh == mesh.name)) {
                    entity->mesh = mesh.name;
                }
            }
            ImGui::EndCombo();
        }
        if (entity->mesh.empty()) {
            const char* primitives[] = {"Box", "Sphere", "Cylinder", "Cone"};
            int index = static_cast<int>(entity->primitive);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##primitive", &index, primitives, 4)) {
                entity->primitive = static_cast<scene::Primitive>(index);
            }
        }

        std::string currentTexture = entity->texture.empty() ? "Flat tint" : entity->texture;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##texture", currentTexture.c_str())) {
            if (ImGui::Selectable("Flat tint", entity->texture.empty())) entity->texture.clear();
            for (const asset::TextureAsset& texture : assets.Textures()) {
                if (ImGui::Selectable(texture.name.c_str(), entity->texture == texture.name)) {
                    entity->texture = texture.name;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::ColorEdit3("Colour", &entity->color.x);
        ImGui::Checkbox("Cast shadow", &entity->castShadow);
        ImGui::SameLine();
        ImGui::Checkbox("Visible", &entity->visible);
    }

    if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& types = scene::ScriptTypes();
        std::string current = entity->script.empty() ? "None" : entity->script;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##script", current.c_str())) {
            if (ImGui::Selectable("None", entity->script.empty())) {
                entity->script.clear();
                entity->scriptParams.clear();
            }
            for (const scene::ScriptType& type : types) {
                if (ImGui::Selectable(type.name, entity->script == type.name)) {
                    entity->script = type.name;
                    entity->scriptParams.clear();
                    for (const scene::ScriptParam& param : type.params) {
                        entity->scriptParams.emplace_back(param.name, param.defaultValue);
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (const scene::ScriptType* type = scene::FindScriptType(entity->script)) {
            ImGui::TextDisabled("%s", type->description);
            for (const scene::ScriptParam& param : type->params) {
                float value = scene::ScriptParamValue(*entity, param);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat(param.label, &value, param.minimum, param.maximum, param.format)) {
                    scene::SetScriptParam(*entity, param.name, value);
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Statistics")) {
        vec3 boundsMin;
        vec3 boundsMax;
        EntityWorldBounds(entities, *entity, renderer, boundsMin, boundsMax);
        const vec3 size = boundsMax - boundsMin;
        ImGui::Text("World size  %.2f x %.2f x %.2f m", size.x, size.y, size.z);
        ImGui::Text("Entity id    %u", entity->id);
        ImGui::Text("Parent       %s", entity->parent == scene::kInvalidEntity ? "none" : "set");
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Duplicate")) actions.duplicateEntity = true;
    ImGui::SameLine();
    if (ImGui::Button("Delete")) actions.deleteEntity = true;
    (void)config;
}

void Editor::DrawAssets(scene::Scene& entities, render::Renderer& renderer,
                        scene::Camera& editorCamera) {
    ImGui::TextDisabled("ASSETS");
    ImGui::Separator();
    const auto& assets = renderer.Assets();
    ImGui::Text("%zu models · %zu textures", assets.Meshes().size(), assets.Textures().size());

    ImGui::BeginChild("##assets", ImVec2(0.0f, 0.0f), false);
    if (ImGui::TreeNodeEx("Models", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const asset::MeshAsset& mesh : assets.Meshes()) {
            ImGui::PushID(mesh.name.c_str());
            const vec3 size = mesh.mesh.boundsMax - mesh.mesh.boundsMin;
            if (ImGui::Selectable(mesh.name.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    const scene::EntityId id = AddEntity(entities, scene::Primitive::Box,
                                                         mesh.name, editorCamera);
                    if (scene::Entity* entity = entities.Find(id)) {
                        entity->mesh = mesh.name;
                        entity->color = vec3(1.0f);
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%zu triangles\n%.2f x %.2f x %.2f m\ndouble-click to place",
                                  mesh.path.c_str(), mesh.mesh.indices.size() / 3, size.x, size.y,
                                  size.z);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const asset::TextureAsset& texture : assets.Textures()) {
            ImGui::PushID(texture.name.c_str());
            if (const unsigned int id = renderer.AssetTextureId(texture.name); id != 0) {
                ImGui::Image(ToTextureId(id), ImVec2(22.0f, 22.0f));
                ImGui::SameLine();
            }
            ImGui::Text("%s", texture.name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s · %dx%d", texture.path.c_str(), texture.image.width,
                                  texture.image.height);
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

void Editor::DrawConsole() {
    ImGui::TextDisabled("CONSOLE");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##level", &m_consoleLevelFilter, "All\0Info\0Warnings\0Errors\0");
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &m_consoleAutoScroll);

    ImGui::Separator();
    ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const auto history = core::Log::History();
    for (const core::Log::Entry& entry : history) {
        const int level = static_cast<int>(entry.level);
        if (m_consoleLevelFilter == 1 && level < static_cast<int>(core::LogLevel::Info)) continue;
        if (m_consoleLevelFilter == 2 && level < static_cast<int>(core::LogLevel::Warn)) continue;
        if (m_consoleLevelFilter == 3 && level != static_cast<int>(core::LogLevel::Error)) continue;
        ImVec4 color(0.80f, 0.82f, 0.86f, 1.0f);
        if (level == static_cast<int>(core::LogLevel::Warn)) color = ImVec4(0.95f, 0.78f, 0.35f, 1.0f);
        if (level == static_cast<int>(core::LogLevel::Error)) color = ImVec4(0.96f, 0.45f, 0.45f, 1.0f);
        if (level == static_cast<int>(core::LogLevel::Debug)) color = ImVec4(0.62f, 0.68f, 0.76f, 1.0f);
        ImGui::TextDisabled("%s", entry.clock.c_str());
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", entry.text.c_str());
    }
    if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void Editor::DrawStats(world::World& world, game::Player& player, render::Renderer& renderer,
                       const core::FrameStats& stats, const scene::Camera& camera) {
    ImGui::TextDisabled("STATISTICS");
    ImGui::Separator();
    ImGui::Text("%.1f FPS   %.2f ms", stats.smoothedFps, stats.smoothedDelta * 1000.0f);
    ImGui::Text("shadow %.2f  main %.2f  post %.2f ms", renderer.Stats().shadowPassMs,
                renderer.Stats().mainPassMs, renderer.Stats().postPassMs);
    ImGui::Separator();
    ImGui::Text("draw calls   %d (+%d shadow)", renderer.Stats().drawCalls,
                renderer.Stats().shadowDrawCalls);
    ImGui::Text("triangles    %.1f k", static_cast<double>(renderer.Stats().triangles) / 1000.0);
    ImGui::Text("instances    %d visible / %d culled", renderer.Stats().visibleInstances,
                renderer.Stats().culledInstances);
    ImGui::Separator();
    const vec3 position = camera.Position();
    ImGui::Text("camera  %.1f, %.1f, %.1f", position.x, position.y, position.z);
    ImGui::Text("time    %.2f h   wind %.2f", world.Time().Hours(), world.WindStrength());
    ImGui::Text("player  %s  (%.1f m/s)", player.IsGrounded() ? "grounded" : "airborne",
                player.HorizontalSpeed());
    ImGui::Separator();
    ImGui::TextDisabled("Render target %dx%d", renderer.OutputWidth(), renderer.OutputHeight());
}

// -------------------------------------------------------------------- draw

void Editor::Draw(core::AppConfig& config, world::World& world, scene::Scene& entities,
                  game::Player& player, render::Renderer& renderer, const core::FrameStats& stats,
                  scene::Camera& editorCamera, EditorActions& actions) {
    m_rendererForPicking = &renderer;
    m_worldForEditing = &world;
    m_editorCameraTarget = &editorCamera;
    m_fpsSmoothed = stats.smoothedFps;

    if (m_playState == PlayState::Stopped) {
        UpdateCamera(editorCamera, world, stats.deltaSeconds);
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##derecho-editor", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    DrawMenuBar(config, entities, renderer, editorCamera, actions);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float bodyHeight = std::max(120.0f, available.y - kToolbarHeight - kGap);

    // Toolbar strip.
    DrawToolbar(config, entities, actions);

    // Left column: hierarchy over assets.
    ImGui::BeginChild("##left", ImVec2(kLeftWidth, bodyHeight));
    const float assetsHeight = std::min(kAssetsHeight, bodyHeight * 0.45f);
    ImGui::BeginChild("##hierarchy", ImVec2(0.0f, bodyHeight - assetsHeight - kGap), true);
    DrawHierarchy(entities, actions);
    ImGui::EndChild();
    ImGui::BeginChild("##assets-panel", ImVec2(0.0f, assetsHeight), true);
    DrawAssets(entities, renderer, editorCamera);
    ImGui::EndChild();
    ImGui::EndChild();

    // Centre column: viewport over console.
    ImGui::SameLine();
    const float centreWidth = std::max(200.0f, available.x - kLeftWidth - kRightWidth - 2.0f * kGap);
    ImGui::BeginChild("##centre", ImVec2(centreWidth, bodyHeight));
    ImGui::BeginChild("##viewport-panel", ImVec2(0.0f, bodyHeight - kConsoleHeight - kGap), true);
    DrawViewport(config, entities, renderer, editorCamera, player);
    ImGui::EndChild();
    ImGui::BeginChild("##console-panel", ImVec2(0.0f, kConsoleHeight), true);
    DrawConsole();
    ImGui::EndChild();
    ImGui::EndChild();

    // Right column: inspector over statistics.
    ImGui::SameLine();
    const float inspectorHeight = bodyHeight - 210.0f;
    ImGui::BeginChild("##right", ImVec2(kRightWidth, bodyHeight));
    ImGui::BeginChild("##inspector", ImVec2(0.0f, inspectorHeight), true);
    DrawInspector(config, entities, renderer, editorCamera, actions);
    ImGui::EndChild();
    ImGui::BeginChild("##stats", ImVec2(0.0f, std::max(80.0f, bodyHeight - inspectorHeight - kGap)), true);
    DrawStats(world, player, renderer, stats, (m_playState == PlayState::Stopped) ? editorCamera
                                                                                  : player.Camera());
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar();

    if (m_showAbout) {
        ImGui::SetNextWindowSize(ImVec2(440.0f, 300.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("About Derecho Engine", &m_showAbout)) {
            if (const unsigned int logo = renderer.LogoTextureId(); logo != 0) {
                ImGui::Image(ToTextureId(logo), ImVec2(120.0f, 120.0f));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextWrapped("Derecho Engine - an original procedural outdoor sandbox and editor.");
            ImGui::Spacing();
            ImGui::BulletText("C++20 / CMake / SDL2 / OpenGL 4.5 / GLM / Dear ImGui");
            ImGui::BulletText("Play-in-editor, JSON scenes, native scripts, OBJ/PNG/TGA import");
            ImGui::EndGroup();
        }
        ImGui::End();
    }
    if (m_showDemo) {
        ImGui::ShowDemoWindow(&m_showDemo);
    }
}

} // namespace derecho::ui

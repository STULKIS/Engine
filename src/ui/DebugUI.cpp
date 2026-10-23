#include "ui/DebugUI.h"

#include "core/Log.h"
#include "core/Window.h"
#include "game/Player.h"
#include "render/Renderer.h"
#include "world/World.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <cstdio>

namespace frontier::ui {

bool DebugUI::Initialize(core::Window& window, bool showDemo) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "frontier_imgui.ini";
    io.LogFilename = nullptr;
    m_showDemo = showDemo;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.062f, 0.075f, 0.94f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.11f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.17f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.42f, 0.38f, 0.55f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.52f, 0.45f, 0.75f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.33f, 0.3f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.45f, 0.4f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.72f, 0.55f, 1.0f);

    if (!ImGui_ImplSDL2_InitForOpenGL(window.Handle(), window.Context())) {
        FR_ERROR("ImGui SDL2 backend initialization failed.");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 450")) {
        FR_ERROR("ImGui OpenGL3 backend initialization failed.");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    m_initialized = true;
    FR_INFO("ImGui debug UI initialized (Dear ImGui %s).", IMGUI_VERSION);
    return true;
}

void DebugUI::Shutdown() {
    if (!m_initialized) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
}

void DebugUI::NewFrame() {
    if (!m_initialized) {
        return;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::Draw(core::AppConfig& config, world::World& world, game::Player& player,
                   render::Renderer& renderer, const core::FrameStats& frameStats,
                   DebugActions& actions) {
    if (!m_initialized) {
        return;
    }

    if (m_visible) {
        ImGui::SetNextWindowSize(ImVec2(430.0f, 560.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Frontier Engine", &m_visible, ImGuiWindowFlags_NoCollapse)) {
            DrawOverview(config, world, player, renderer, frameStats, actions);
            ImGui::Separator();
            DrawWorldPanel(world, actions);
            ImGui::Separator();
            DrawRenderPanel(config, renderer);
            ImGui::Separator();
            DrawPlayerPanel(config, player, actions);
            ImGui::Separator();
            DrawControlsPanel(world, player, renderer);

            ImGui::Spacing();
            if (ImGui::Button("Save config")) {
                actions.saveConfig = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Screenshot (F12)")) {
                actions.takeScreenshot = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit")) {
                actions.requestQuit = true;
            }
            ImGui::SameLine();
            ImGui::Checkbox("About", &m_showAbout);
        }
        ImGui::End();
    }

    if (m_showAbout) {
        ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("About Frontier Engine", &m_showAbout, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextWrapped("Frontier Engine - an original procedural first-person outdoor sandbox.");
            ImGui::Spacing();
            ImGui::TextWrapped("All terrain, vegetation, textures and shaders in this build are generated "
                               "procedurally by the engine. No CryEngine code, shaders, assets or branding "
                               "are used.");
            ImGui::Spacing();
            ImGui::BulletText("C++20 / CMake / SDL2 / OpenGL 4.5 / GLM / Dear ImGui");
            ImGui::BulletText("Renderer: forward shading, PCF shadows, height fog, ACES");
            ImGui::BulletText("World: heightfield lake, instanced forests, wind, LOD");
            ImGui::BulletText("Press F1 to toggle this panel");
        }
        ImGui::End();
    }

    if (m_showDemo) {
        ImGui::ShowDemoWindow(&m_showDemo);
    }
}

void DebugUI::DrawOverview(core::AppConfig& config, world::World& world, game::Player& player,
                           render::Renderer& renderer, const core::FrameStats& frameStats,
                           DebugActions& actions) {
    (void)config;
    const render::RenderStats& stats = renderer.Stats();
    const scene::TimeOfDay& time = world.Time();

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "Frontier Engine");
    ImGui::SameLine();
    ImGui::TextDisabled("| procedural outdoor sandbox");

    ImGui::Spacing();
    ImGui::Text("Frame");
    ImGui::SameLine(110.0f);
    ImGui::Text("%.1f FPS  (%.2f ms)", frameStats.smoothedFps, frameStats.smoothedDelta * 1000.0f);
    ImGui::SameLine(300.0f);
    ImGui::TextDisabled("%s", player.IsGrounded() ? "grounded" : (player.IsSwimming() ? "swimming" : "airborne"));

    ImGui::Text("Frame range");
    ImGui::SameLine(110.0f);
    ImGui::Text("%.2f / %.2f ms", frameStats.minFrameMs, frameStats.maxFrameMs);

    ImGui::Text("Draw calls");
    ImGui::SameLine(110.0f);
    ImGui::Text("%d", stats.drawCalls);
    ImGui::SameLine(190.0f);
    ImGui::Text("Triangles %.2f M", static_cast<double>(stats.triangles) / 1'000'000.0);

    ImGui::Text("Instances");
    ImGui::SameLine(110.0f);
    ImGui::Text("%d visible / %d culled", stats.visibleInstances, stats.culledInstances);

    ImGui::Text("Pass timings");
    ImGui::SameLine(110.0f);
    ImGui::Text("shadow %.2f ms | scene %.2f ms | post %.2f ms",
                stats.shadowPassMs, stats.mainPassMs, stats.postPassMs);

    ImGui::Text("Target");
    ImGui::SameLine(110.0f);
    ImGui::Text("%dx%d, %dx MSAA, shadow %d",
                stats.sceneWidth, stats.sceneHeight, stats.msaaSamples, stats.shadowMapSize);

    ImGui::Spacing();
    ImGui::Text("Time of day");
    ImGui::SameLine(110.0f);
    ImGui::Text("%s  %s", time.ClockString().c_str(), time.PhaseName().c_str());
    ImGui::SameLine(300.0f);
    if (ImGui::SmallButton(time.Paused() ? "Resume (F6)" : "Pause (F6)")) {
        world.Time().SetPaused(!time.Paused());
    }

    float hours = time.Hours();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##timeofday", &hours, 0.0f, 24.0f, "%.2f h", ImGuiSliderFlags_AlwaysClamp)) {
        world.Time().SetHours(hours);
    }

    ImGui::Spacing();
    const world::LightingState& lighting = world.Lighting();
    ImGui::Text("Sun elevation");
    ImGui::SameLine(110.0f);
    ImGui::Text("%.1f deg", lighting.sun.elevationDegrees);
    ImGui::Text("Spawn");
    ImGui::SameLine(110.0f);
    ImGui::Text("(%.0f, %.0f, %.0f)", world.SpawnPoint().x, world.SpawnPoint().y, world.SpawnPoint().z);

    if (ImGui::Button("Respawn (R)")) {
        actions.respawnPlayer = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate world")) {
        actions.regenerateWorld = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Capture mouse")) {
        actions.toggleMouseCapture = true;
    }
}

void DebugUI::DrawWorldPanel(world::World& world, DebugActions& actions) {
    if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) {
        const core::WorldConfig& settings = world.Settings();
        const scene::Terrain& terrain = world.Terrain();
        ImGui::Text("Seed %d | %.0f m | %d quads", settings.seed, settings.terrainSize,
                    settings.terrainResolution);
        ImGui::Text("Height %.1f .. %.1f m | water %.1f m", terrain.MinHeight(), terrain.MaxHeight(),
                    terrain.WaterLevel());
        ImGui::Text("Triangles: terrain %.0fk, source foliage %.0fk",
                    static_cast<double>(terrain.TriangleCount()) / 1000.0,
                    static_cast<double>(world.Vegetation().TotalTriangles()) / 1000.0);
        ImGui::Text("Wind: %.2f m/s from %s", world.WindStrength(),
                    world.WindDirection().x > 0.0f ? "south-west" : "north-east");

        static int seedField = settings.seed;
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("New seed", &seedField, 1, 100);
        ImGui::SameLine();
        if (ImGui::Button("Rebuild terrain")) {
            core::WorldConfig updated = settings;
            updated.seed = seedField;
            world.Regenerate(updated);
            actions.respawnPlayer = true;
        }

        static float dayLength = settings.dayLengthMinutes;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("Day length (min)", &dayLength, 1.0f, 120.0f, "%.0f")) {
            world.Time().SetDayLengthMinutes(dayLength);
        }
        float timeScale = world.Time().TimeScale();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("Time scale", &timeScale, 0.0f, 8.0f, "%.2fx")) {
            world.Time().SetTimeScale(timeScale);
        }
    }
}

void DebugUI::DrawRenderPanel(core::AppConfig& config, render::Renderer& renderer) {
    (void)renderer;
    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Shadows (F3)", &config.render.showShadows);
        ImGui::SameLine();
        ImGui::Checkbox("Foliage shadows", &config.render.shadowsFromFoliage);
        ImGui::Checkbox("Water (F5)", &config.render.showWater);
        ImGui::SameLine();
        ImGui::Checkbox("Sky", &config.render.showSky);
        ImGui::Checkbox("Foliage (F4)", &config.render.showFoliage);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe (F2)", &config.render.wireframe);

        ImGui::Checkbox("Bloom", &config.render.bloom);
        ImGui::SameLine();
        ImGui::Checkbox("HDR", &config.render.hdr);
        ImGui::Checkbox("VSync", &config.window.vsync);

        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Exposure", &config.render.exposure, 0.2f, 3.0f, "%.2f");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Bloom strength", &config.render.bloomStrength, 0.0f, 1.5f, "%.2f");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Fog density", &config.render.fogDensity, 0.0f, 0.01f, "%.5f");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("View distance", &config.render.viewDistance, 100.0f, 1500.0f, "%.0f m");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Render scale", &config.render.renderScale, 0.5f, 1.5f, "%.2f");

        const char* shadowSizes[] = {"1024", "2048", "4096"};
        int shadowIndex = config.render.shadowMapSize <= 1024 ? 0 : (config.render.shadowMapSize <= 2048 ? 1 : 2);
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::Combo("Shadow map", &shadowIndex, shadowSizes, 3)) {
            config.render.shadowMapSize = shadowIndex == 0 ? 1024 : (shadowIndex == 1 ? 2048 : 4096);
        }

        ImGui::Checkbox("God rays", &config.render.godRays);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderFloat("Strength##godrays", &config.render.godRayStrength, 0.0f, 2.0f, "%.2f");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Shadow near range", &config.render.shadowNearRadius, 15.0f, 100.0f, "%.0f m");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Contrast", &config.render.contrast, 0.7f, 1.6f, "%.2f");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Temperature", &config.render.temperature, -1.0f, 1.0f, "%.2f");

        const char* styleLabels[] = {"Realistic", "Anime (NPR)"};
        int styleIndex = config.render.style == core::RenderStyle::Anime ? 1 : 0;
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::Combo("Style (F7)", &styleIndex, styleLabels, 2)) {
            config.render.style = styleIndex == 1 ? core::RenderStyle::Anime : core::RenderStyle::Realistic;
        }

        if (config.render.style == core::RenderStyle::Anime) {
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderInt("Cel bands", &config.render.celSteps, 2, 8);
            ImGui::SameLine();
            ImGui::Checkbox("Outlines", &config.render.outlines);
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderFloat("Outline width", &config.render.outlineThickness, 0.5f, 4.0f, "%.1f px");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderFloat("Outline strength", &config.render.outlineStrength, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderFloat("Rim light", &config.render.rimStrength, 0.0f, 1.5f, "%.2f");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderFloat("Specular", &config.render.specularStrength, 0.0f, 2.0f, "%.2f");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderFloat("Warm shadow edge", &config.render.warmEdge, 0.0f, 2.0f, "%.2f");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderFloat("Leaf translucency", &config.render.leafTranslucency, 0.0f, 1.5f, "%.2f");
            ImGui::SetNextItemWidth(190.0f);
            ImGui::SliderInt("Posterize", &config.render.posterizeLevels, 0, 24);
        }
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Saturation", &config.render.saturation, 0.0f, 2.0f, "%.2f");

        const char* msaaLabels[] = {"Off", "2x", "4x", "8x"};
        int msaaIndex = config.render.msaaSamples <= 1 ? 0 : (config.render.msaaSamples <= 2 ? 1
                            : (config.render.msaaSamples <= 4 ? 2 : 3));
        ImGui::SetNextItemWidth(190.0f);
        if (ImGui::Combo("MSAA", &msaaIndex, msaaLabels, 4)) {
            config.render.msaaSamples = msaaIndex == 0 ? 1 : (msaaIndex == 1 ? 2 : (msaaIndex == 2 ? 4 : 8));
        }
    }
}

void DebugUI::DrawPlayerPanel(core::AppConfig& config, game::Player& player, DebugActions& actions) {
    if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen)) {
        const vec3& position = player.Position();
        ImGui::Text("Position  %.1f, %.1f, %.1f", position.x, position.y, position.z);
        ImGui::Text("Speed     %.2f m/s  %s", player.HorizontalSpeed(),
                    player.IsSwimming() ? "(swimming)" : (player.IsGrounded() ? "(grounded)" : "(airborne)"));

        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Move speed", &player.Settings().walkSpeed, 1.0f, 20.0f, "%.1f m/s");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Sprint multiplier", &player.Settings().sprintMultiplier, 1.0f, 3.0f, "%.2fx");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Jump velocity", &player.Settings().jumpVelocity, 2.0f, 12.0f, "%.1f");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("FOV", &config.game.fieldOfView, 50.0f, 110.0f, "%.0f deg");
        ImGui::SetNextItemWidth(190.0f);
        ImGui::SliderFloat("Mouse sensitivity", &config.game.mouseSensitivity, 0.02f, 0.4f, "%.2f");
        ImGui::Checkbox("Invert Y", &config.game.invertY);
        ImGui::SameLine();
        if (ImGui::Button("Respawn")) {
            actions.respawnPlayer = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Capture mouse")) {
            actions.toggleMouseCapture = true;
        }
    }
}

void DebugUI::DrawControlsPanel(world::World& world, game::Player& player, render::Renderer& renderer) {
    if (ImGui::CollapsingHeader("Controls & diagnostics")) {
        (void)world;
        (void)player;
        (void)renderer;
        ImGui::BulletText("WASD move, mouse look, Space jump, Shift sprint, Ctrl walk");
        ImGui::BulletText("F1 panel, F2 wireframe, F3 shadows, F4 foliage, F5 water");
        ImGui::BulletText("F6 pause time, F11 fullscreen, F12 screenshot, R respawn");
        ImGui::BulletText("Esc releases the mouse (click the window to capture)");
    }
}

void DebugUI::Render() {
    if (!m_initialized) {
        return;
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool DebugUI::WantsMouse() const {
    if (!m_initialized) {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::WantsKeyboard() const {
    if (!m_initialized) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace frontier::ui

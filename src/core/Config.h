// Frontier Engine - runtime configuration (defaults + config file + CLI overrides).
#pragma once

#include <string>
#include <vector>

namespace frontier::core {

struct WindowConfig {
    int width = 1600;
    int height = 900;
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
    std::string title = "Frontier Engine";
};

// Overall look of the frame. "Anime" is a hand-authored NPR pipeline: banded
// cel lighting, rim light, hard shadow terminator, silhouette outlines and a
// stylised sky. It is original work (no engine or anime asset is copied).
enum class RenderStyle { Realistic = 0, Anime = 1 };

struct RenderConfig {
    int shadowMapSize = 2048;
    int msaaSamples = 4;
    bool hdr = true;
    float renderScale = 1.0f;
    float exposure = 1.0f;
    float fogDensity = 0.0018f;
    float fogHeightFalloff = 0.055f;
    float viewDistance = 900.0f;
    bool bloom = true;
    float bloomStrength = 0.35f;
    bool wireframe = false;
    bool showSky = true;
    bool showWater = true;
    bool showFoliage = true;
    bool showShadows = true;
    bool shadowsFromFoliage = true;

    // --- Lighting / shadows --------------------------------------------------
    float shadowNearRadius = 45.0f;   // metres covered by the near shadow cascade
    float specularStrength = 0.9f;    // stylised highlight
    float warmEdge = 0.5f;            // warm band at the cel terminator
    float leafTranslucency = 0.65f;   // light through foliage

    // --- Post ---------------------------------------------------------------
    bool godRays = true;
    float godRayStrength = 0.5f;
    float contrast = 1.05f;
    float temperature = 0.0f;         // -1 cool .. +1 warm

    // --- Stylised (NPR) rendering -------------------------------------------
    RenderStyle style = RenderStyle::Realistic;
    int celSteps = 3;                 // light bands in cel shading (2..8)
    bool outlines = true;             // silhouette outlines (anime only)
    float outlineThickness = 1.2f;    // pixels
    float outlineStrength = 0.75f;     // 0..1
    float rimStrength = 0.45f;        // rim light contribution
    float saturation = 1.06f;         // colour grade
    int posterizeLevels = 0;          // 0 = off, else quantise the final image
};

struct WorldConfig {
    int seed = 1337;
    float terrainSize = 900.0f;      // metres, square
    int terrainResolution = 256;     // quads per side
    float terrainAmplitude = 58.0f;
    float waterLevel = 0.0f;
    int treeCount = 900;
    int rockCount = 320;
    int grassCount = 60000;
    float startTimeOfDay = 9.5f;     // hours
    float dayLengthMinutes = 24.0f;  // real minutes per in-game day
};

struct GameConfig {
    float fieldOfView = 68.0f;
    float mouseSensitivity = 0.11f;
    float moveSpeed = 6.5f;
    float sprintMultiplier = 1.9f;
    float jumpVelocity = 6.0f;
    float gravity = -20.0f;
    bool invertY = false;
};

struct AppConfig {
    WindowConfig window;
    RenderConfig render;
    WorldConfig world;
    GameConfig game;

    std::string logLevel = "info";
    std::string logFile = "frontier.log";
    std::string assetsDir = "assets";
    bool headless = false;   // run N frames without presenting (CI smoke test)
    int headlessFrames = 120;
    bool selftest = false;   // render a few frames and inspect the pixels
    int selftestFrames = 45;
    bool glInfo = false;     // print the OpenGL/video environment and exit
    std::string loadScene;   // --scene=<path>: load this scene at start
    std::string saveScene;   // --save-scene[=<path>]: write the generated scene
    bool demoUI = false;
};

// Parses command line arguments. Supports --key=value / --key value pairs and
// --help. Returns false when the app should terminate immediately (help/parse error).
bool ParseCommandLine(int argc, char** argv, AppConfig& config, bool& shouldExit);

// Loads a flat `key = value` config file, overriding defaults. Unknown keys are
// logged as warnings. Sections look like `[render]`; the section name is only
// informational. Returns false if the file cannot be opened.
bool LoadConfigFile(const std::string& path, AppConfig& config);

// Writes the complete effective configuration next to the log file for debugging.
bool SaveConfigFile(const std::string& path, const AppConfig& config);

void PrintUsage(const char* executable);

} // namespace frontier::core

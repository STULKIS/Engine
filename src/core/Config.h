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

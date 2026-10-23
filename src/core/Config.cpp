#include "core/Config.h"

#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace frontier::core {
namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool ParseBool(std::string value, bool& out) {
    value = Lower(Trim(value));
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        out = false;
        return true;
    }
    return false;
}

bool ParseFloat(std::string value, float& out) {
    try {
        std::size_t consumed = 0;
        out = std::stof(Trim(value), &consumed);
        return consumed > 0;
    } catch (...) {
        return false;
    }
}

bool ParseInt(std::string value, int& out) {
    try {
        std::size_t consumed = 0;
        out = std::stoi(Trim(value), &consumed);
        return consumed > 0;
    } catch (...) {
        return false;
    }
}

// Applies "section.key = value". Returns false when the key/value pair is unknown
// or cannot be parsed, so the caller can report it uniformly.
bool ApplySetting(AppConfig& c, const std::string& fullKey, const std::string& value, std::string& section) {
    const std::string key = Lower(Trim(fullKey));

    if (key == "window.width") return ParseInt(value, c.window.width);
    if (key == "window.height") return ParseInt(value, c.window.height);
    if (key == "window.fullscreen") return ParseBool(value, c.window.fullscreen);
    if (key == "window.vsync") return ParseBool(value, c.window.vsync);
    if (key == "window.resizable") return ParseBool(value, c.window.resizable);
    if (key == "window.title") { c.window.title = Trim(value); return true; }

    if (key == "render.shadowmapsize") return ParseInt(value, c.render.shadowMapSize);
    if (key == "render.msaasamples") return ParseInt(value, c.render.msaaSamples);
    if (key == "render.hdr") return ParseBool(value, c.render.hdr);
    if (key == "render.renderscale") return ParseFloat(value, c.render.renderScale);
    if (key == "render.exposure") return ParseFloat(value, c.render.exposure);
    if (key == "render.fogdensity") return ParseFloat(value, c.render.fogDensity);
    if (key == "render.fogheightfalloff") return ParseFloat(value, c.render.fogHeightFalloff);
    if (key == "render.viewdistance") return ParseFloat(value, c.render.viewDistance);
    if (key == "render.bloom") return ParseBool(value, c.render.bloom);
    if (key == "render.bloomstrength") return ParseFloat(value, c.render.bloomStrength);
    if (key == "render.wireframe") return ParseBool(value, c.render.wireframe);
    if (key == "render.showsky") return ParseBool(value, c.render.showSky);
    if (key == "render.showwater") return ParseBool(value, c.render.showWater);
    if (key == "render.showfoliage") return ParseBool(value, c.render.showFoliage);
    if (key == "render.showshadows") return ParseBool(value, c.render.showShadows);
    if (key == "render.shadowsfromfoliage") return ParseBool(value, c.render.shadowsFromFoliage);

    if (key == "world.seed") return ParseInt(value, c.world.seed);
    if (key == "world.terrainsize") return ParseFloat(value, c.world.terrainSize);
    if (key == "world.terrainresolution") return ParseInt(value, c.world.terrainResolution);
    if (key == "world.terrainamplitude") return ParseFloat(value, c.world.terrainAmplitude);
    if (key == "world.waterlevel") return ParseFloat(value, c.world.waterLevel);
    if (key == "world.treecount") return ParseInt(value, c.world.treeCount);
    if (key == "world.rockcount") return ParseInt(value, c.world.rockCount);
    if (key == "world.grasscount") return ParseInt(value, c.world.grassCount);
    if (key == "world.starttimeofday") return ParseFloat(value, c.world.startTimeOfDay);
    if (key == "world.daylengthminutes") return ParseFloat(value, c.world.dayLengthMinutes);

    if (key == "game.fov") return ParseFloat(value, c.game.fieldOfView);
    if (key == "game.mousesensitivity") return ParseFloat(value, c.game.mouseSensitivity);
    if (key == "game.movespeed") return ParseFloat(value, c.game.moveSpeed);
    if (key == "game.sprintmultiplier") return ParseFloat(value, c.game.sprintMultiplier);
    if (key == "game.jumpvelocity") return ParseFloat(value, c.game.jumpVelocity);
    if (key == "game.gravity") return ParseFloat(value, c.game.gravity);
    if (key == "game.inverty") return ParseBool(value, c.game.invertY);

    if (key == "app.loglevel") { c.logLevel = Lower(Trim(value)); return true; }
    if (key == "app.logfile") { c.logFile = Trim(value); return true; }
    if (key == "app.assetsdir") { c.assetsDir = Trim(value); return true; }
    if (key == "app.headless") return ParseBool(value, c.headless);
    if (key == "app.headlessframes") return ParseInt(value, c.headlessFrames);
    if (key == "app.demoui") return ParseBool(value, c.demoUI);

    // Accept bare keys: map them to their canonical section.
    static const char* kSections[] = {"window.", "render.", "world.", "game.", "app."};
    for (const char* prefix : kSections) {
        if (key.rfind(prefix, 0) == 0) {
            return false;
        }
    }
    if (key == "width") return ParseInt(value, c.window.width);
    if (key == "height") return ParseInt(value, c.window.height);
    if (key == "seed") return ParseInt(value, c.world.seed);
    (void)section;
    return false;
}

} // namespace

bool LoadConfigFile(const std::string& path, AppConfig& config) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    std::string section;
    std::string line;
    int lineNumber = 0;
    int applied = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = Lower(Trim(trimmed.substr(1, trimmed.size() - 2)));
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            FR_WARN("%s:%d: expected 'key = value', ignoring line", path.c_str(), lineNumber);
            continue;
        }
        const std::string key = Trim(trimmed.substr(0, eq));
        const std::string value = Trim(trimmed.substr(eq + 1));
        const std::string fullKey = section.empty() ? key : (section + "." + key);
        std::string sink;
        if (ApplySetting(config, fullKey, value, sink)) {
            ++applied;
        } else {
            FR_WARN("%s:%d: unknown key '%s'", path.c_str(), lineNumber, fullKey.c_str());
        }
    }
    FR_INFO("Loaded %d setting(s) from %s", applied, path.c_str());
    return true;
}

bool SaveConfigFile(const std::string& path, const AppConfig& c) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }
    file << "# Frontier Engine configuration (generated)\n"
         << "[window]\n"
         << "width = " << c.window.width << "\nheight = " << c.window.height
         << "\nfullscreen = " << (c.window.fullscreen ? "true" : "false")
         << "\nvsync = " << (c.window.vsync ? "true" : "false")
         << "\nresizable = " << (c.window.resizable ? "true" : "false")
         << "\ntitle = " << c.window.title << "\n\n"
         << "[render]\n"
         << "shadowMapSize = " << c.render.shadowMapSize
         << "\nmsaaSamples = " << c.render.msaaSamples
         << "\nhdr = " << (c.render.hdr ? "true" : "false")
         << "\nrenderScale = " << c.render.renderScale
         << "\nexposure = " << c.render.exposure
         << "\nfogDensity = " << c.render.fogDensity
         << "\nfogHeightFalloff = " << c.render.fogHeightFalloff
         << "\nviewDistance = " << c.render.viewDistance
         << "\nbloom = " << (c.render.bloom ? "true" : "false")
         << "\nbloomStrength = " << c.render.bloomStrength
         << "\nshowSky = " << (c.render.showSky ? "true" : "false")
         << "\nshowWater = " << (c.render.showWater ? "true" : "false")
         << "\nshowFoliage = " << (c.render.showFoliage ? "true" : "false")
         << "\nshowShadows = " << (c.render.showShadows ? "true" : "false")
         << "\nshadowsFromFoliage = " << (c.render.shadowsFromFoliage ? "true" : "false") << "\n\n"
         << "[world]\n"
         << "seed = " << c.world.seed
         << "\nterrainSize = " << c.world.terrainSize
         << "\nterrainResolution = " << c.world.terrainResolution
         << "\nterrainAmplitude = " << c.world.terrainAmplitude
         << "\nwaterLevel = " << c.world.waterLevel
         << "\ntreeCount = " << c.world.treeCount
         << "\nrockCount = " << c.world.rockCount
         << "\ngrassCount = " << c.world.grassCount
         << "\nstartTimeOfDay = " << c.world.startTimeOfDay
         << "\ndayLengthMinutes = " << c.world.dayLengthMinutes << "\n\n"
         << "[game]\n"
         << "fov = " << c.game.fieldOfView
         << "\nmouseSensitivity = " << c.game.mouseSensitivity
         << "\nmoveSpeed = " << c.game.moveSpeed
         << "\nsprintMultiplier = " << c.game.sprintMultiplier
         << "\njumpVelocity = " << c.game.jumpVelocity
         << "\ngravity = " << c.game.gravity
         << "\ninvertY = " << (c.game.invertY ? "true" : "false") << "\n\n"
         << "[app]\n"
         << "logLevel = " << c.logLevel
         << "\nlogFile = " << c.logFile
         << "\nassetsDir = " << c.assetsDir << "\n";
    return true;
}

void PrintUsage(const char* exe) {
    std::printf(
        "Frontier Engine - procedural first-person outdoor sandbox\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  --config=<path>        Load a key=value configuration file\n"
        "  --width=<px>           Window width (default 1600)\n"
        "  --height=<px>          Window height (default 900)\n"
        "  --fullscreen[=0|1]     Borderless fullscreen\n"
        "  --vsync[=0|1]          Vertical sync\n"
        "  --seed=<int>           Terrain / vegetation seed (default 1337)\n"
        "  --time=<hours>         Initial time of day, 0..24 (default 9.5)\n"
        "  --day-length=<minutes> Real minutes per in-game day (default 24)\n"
        "  --fov=<degrees>        Vertical field of view (default 68)\n"
        "  --sensitivity=<f>      Mouse sensitivity (default 0.11)\n"
        "  --view-distance=<m>    Far plane / culling distance (default 900)\n"
        "  --terrain-resolution=<n> Terrain quads per side (default 256)\n"
        "  --trees=<n>            Tree count (default 900)\n"
        "  --rocks=<n>            Rock count (default 320)\n"
        "  --grass=<n>            Grass clump count (default 60000)\n"
        "  --amplitude=<m>        Terrain relief amplitude (default 58)\n"
        "  --shadow-size=<px>     Shadow map resolution, power of two (default 2048)\n"
        "  --msaa=<1|2|4|8>       MSAA sample count for the HDR target (default 4)\n"
        "  --assets=<path>        Asset directory containing shaders/ (default assets)\n"
        "  --log-level=<level>    trace|debug|info|warn|error (default info)\n"
        "  --log-file=<path>      Tee log output to a file (default frontier.log)\n"
        "  --render-scale=<f>     Internal 3D resolution scale, 0.25..2 (default 1)\n"
        "  --no-hdr               Disable the HDR scene target\n"
        "  --no-bloom             Disable bloom\n"
        "  --safe-mode            Low-end preset: no MSAA/HDR/bloom, 1024 shadow map,\n"
        "                         reduced grass and 0.85 render scale\n"
        "  --wireframe            Start in wireframe debug mode\n"
        "  --headless[=frames]    Render without a window (smoke test)\n"
        "  --selftest[=frames]    Open the window, render, verify the pixels, exit\n"
        "  --glinfo               Print the SDL/OpenGL environment and exit\n"
        "  --demo-ui              Open the ImGui style demo on start\n"
        "  --help                 Print this help\n"
        "\n"
        "Controls: WASD move, mouse look, Space jump, Shift sprint, Esc release mouse,\n"
        "F1 debug panel, F2 toggle wireframe, F3 shadows, F4 foliage, F5 water,\n"
        "F6 time pause, [ / ] scrub time of day, R respawn, F11 fullscreen.\n",
        exe);
}

bool ParseCommandLine(int argc, char** argv, AppConfig& config, bool& shouldExit) {
    shouldExit = false;

    // First pass: pick up --config so explicit flags win regardless of order.
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.rfind("--config=", 0) == 0) {
            LoadConfigFile(std::string(arg.substr(9)), config);
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0) {
            FR_WARN("Ignoring positional argument '%s'", arg.c_str());
            continue;
        }
        arg = arg.substr(2);

        std::string key = arg;
        std::string value;
        bool hasValue = false;
        if (const auto eq = arg.find('='); eq != std::string::npos) {
            key = arg.substr(0, eq);
            value = arg.substr(eq + 1);
            hasValue = true;
        }
        key = Lower(Trim(key));

        auto inferredValue = [&]() { return hasValue ? value : std::string("1"); };

        if (key == "help" || key == "h") {
            PrintUsage(argv[0]);
            shouldExit = true;
            return true;
        }
        if (key == "config") {
            continue; // already handled
        }
        if (key == "width") { if (!ApplySetting(config, "window.width", inferredValue(), value)) { shouldExit = true; return false; } continue; }
        if (key == "height") { if (!ApplySetting(config, "window.height", inferredValue(), value)) { shouldExit = true; return false; } continue; }
        if (key == "fullscreen") { ApplySetting(config, "window.fullscreen", inferredValue(), value); continue; }
        if (key == "windowed") { ApplySetting(config, "window.fullscreen", "false", value); continue; }
        if (key == "vsync") { ApplySetting(config, "window.vsync", inferredValue(), value); continue; }
        if (key == "seed") { ApplySetting(config, "world.seed", inferredValue(), value); continue; }
        if (key == "time" || key == "time-of-day") { ApplySetting(config, "world.starttimeofday", inferredValue(), value); continue; }
        if (key == "day-length") { ApplySetting(config, "world.daylengthminutes", inferredValue(), value); continue; }
        if (key == "fov") { ApplySetting(config, "game.fov", inferredValue(), value); continue; }
        if (key == "sensitivity") { ApplySetting(config, "game.mousesensitivity", inferredValue(), value); continue; }
        if (key == "view-distance") { ApplySetting(config, "render.viewdistance", inferredValue(), value); continue; }
        if (key == "shadow-size") { ApplySetting(config, "render.shadowmapsize", inferredValue(), value); continue; }
        if (key == "msaa") { ApplySetting(config, "render.msaasamples", inferredValue(), value); continue; }
        if (key == "terrain-resolution") { ApplySetting(config, "world.terrainresolution", inferredValue(), value); continue; }
        if (key == "trees") { ApplySetting(config, "world.treecount", inferredValue(), value); continue; }
        if (key == "rocks") { ApplySetting(config, "world.rockcount", inferredValue(), value); continue; }
        if (key == "grass") { ApplySetting(config, "world.grasscount", inferredValue(), value); continue; }
        if (key == "amplitude") { ApplySetting(config, "world.terrainamplitude", inferredValue(), value); continue; }
        if (key == "assets") { ApplySetting(config, "app.assetsdir", inferredValue(), value); continue; }
        if (key == "log-level") { ApplySetting(config, "app.loglevel", inferredValue(), value); continue; }
        if (key == "log-file") { ApplySetting(config, "app.logfile", inferredValue(), value); continue; }
        if (key == "render-scale") { ApplySetting(config, "render.renderscale", inferredValue(), value); continue; }
        if (key == "no-hdr") { ApplySetting(config, "render.hdr", "false", value); continue; }
        if (key == "no-bloom") { ApplySetting(config, "render.bloom", "false", value); continue; }
        if (key == "safe-mode" || key == "safe") {
            ApplySetting(config, "render.msaasamples", "1", value);
            ApplySetting(config, "render.hdr", "false", value);
            ApplySetting(config, "render.bloom", "false", value);
            ApplySetting(config, "render.shadowmapsize", "1024", value);
            ApplySetting(config, "render.renderscale", "0.85", value);
            ApplySetting(config, "world.grasscount", "25000", value);
            ApplySetting(config, "world.treecount", "600", value);
            FR_INFO("Safe mode enabled: no MSAA/HDR/bloom, 1024 shadow map, lighter world.");
            continue;
        }
        if (key == "no-fog") { ApplySetting(config, "render.fogdensity", "0", value); continue; }
        if (key == "wireframe") { ApplySetting(config, "render.wireframe", inferredValue(), value); continue; }
        if (key == "glinfo" || key == "gl-info") { config.glInfo = true; continue; }
        if (key == "selftest" || key == "self-test") {
            config.selftest = true;
            config.selftestFrames = hasValue ? std::max(5, std::atoi(value.c_str())) : 45;
            continue;
        }
        if (key == "headless") {
            config.headless = true;
            if (hasValue) {
                ApplySetting(config, "app.headlessframes", value, value);
            }
            continue;
        }
        if (key == "frames") { ApplySetting(config, "app.headlessframes", inferredValue(), value); continue; }
        if (key == "demo-ui") { ApplySetting(config, "app.demoui", "true", value); continue; }

        FR_WARN("Unknown option '--%s' (see --help)", key.c_str());
    }

    // Sanity clamping keeps the engine robust against nonsense input.
    config.window.width = std::clamp(config.window.width, 320, 7680);
    config.window.height = std::clamp(config.window.height, 240, 4320);
    config.render.msaaSamples = std::clamp(config.render.msaaSamples, 1, 8);
    config.render.renderScale = std::clamp(config.render.renderScale, 0.25f, 2.0f);
    config.render.exposure = std::clamp(config.render.exposure, 0.05f, 8.0f);
    config.render.viewDistance = std::clamp(config.render.viewDistance, 50.0f, 5000.0f);
    config.render.shadowMapSize = std::clamp(config.render.shadowMapSize, 256, 8192);
    config.world.terrainResolution = std::clamp(config.world.terrainResolution, 16, 1024);
    config.world.terrainSize = std::clamp(config.world.terrainSize, 100.0f, 8000.0f);
    config.world.terrainAmplitude = std::clamp(config.world.terrainAmplitude, 1.0f, 400.0f);
    config.world.treeCount = std::clamp(config.world.treeCount, 0, 20000);
    config.world.rockCount = std::clamp(config.world.rockCount, 0, 20000);
    config.world.grassCount = std::clamp(config.world.grassCount, 0, 500000);
    config.world.startTimeOfDay = std::fmod(std::max(0.0f, config.world.startTimeOfDay), 24.0f);
    config.world.dayLengthMinutes = std::clamp(config.world.dayLengthMinutes, 1.0f, 1440.0f);
    config.game.fieldOfView = std::clamp(config.game.fieldOfView, 40.0f, 110.0f);
    config.game.mouseSensitivity = std::clamp(config.game.mouseSensitivity, 0.01f, 1.0f);
    config.game.moveSpeed = std::clamp(config.game.moveSpeed, 0.5f, 40.0f);
    config.game.sprintMultiplier = std::clamp(config.game.sprintMultiplier, 1.0f, 5.0f);
    config.game.gravity = std::clamp(config.game.gravity, -60.0f, -1.0f);
    config.headlessFrames = std::clamp(config.headlessFrames, 1, 100000);
    return true;
}

} // namespace frontier::core

// Frontier Engine entry point.
//
// Start-up is deliberately loud about failures: the engine is a console
// application, but double-clicking the executable on Windows shows a console
// window that disappears the moment the process exits. So on a failed start we
// (a) keep the log file next to the executable, (b) raise a message box that
// says what went wrong and where the log is, and (c) wait for Enter when a
// console is attached, so the text is readable instead of flashing past.
#include "core/Config.h"
#include "core/Engine.h"
#include "core/Log.h"

#include <SDL.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

// Directory containing this executable (empty when it cannot be determined).
std::string ExecutableDirectory() {
    if (char* basePath = SDL_GetBasePath()) {
        std::string path(basePath);
        SDL_free(basePath);
        return path;
    }
    return {};
}

// Makes a relative log path land next to the executable. Double-clicking from
// Explorer and starting from a shell then write the same file.
std::string ResolveLogPath(const std::string& configured) {
    if (configured.empty()) {
        return configured;
    }
    const fs::path path(configured);
    if (path.is_absolute()) {
        return configured;
    }
    const std::string directory = ExecutableDirectory();
    if (directory.empty()) {
        return configured;
    }
    std::error_code ec;
    const fs::path candidate = fs::path(directory) / path;
    if (fs::exists(fs::path(directory), ec)) {
        return candidate.string();
    }
    return configured;
}

bool StdinIsInteractive() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

// A message box is the only thing a double-clicking user will see. SDL can show
// one before SDL_Init(); if there is no display server it simply fails and we
// fall back to the console text.
void ReportFailure(const std::string& summary, const std::string& details, const std::string& logPath) {
    std::string text = summary;
    if (!details.empty()) {
        text += "\n\nLast error:\n" + details;
    }
    if (!logPath.empty()) {
        text += "\n\nFull log:\n" + logPath;
    }
    text += "\n\nTo see the output directly, run the executable from a terminal:\n"
            "  cd <this folder>\n"
            "  .\\frontier.exe      (Windows)\n"
            "  ./frontier          (Linux)\n"
            "\nUseful diagnostics:\n"
            "  frontier.exe --selftest     render a frame and verify the result\n"
            "  frontier.exe --safe-mode    disable MSAA, HDR and bloom\n"
            "  frontier.exe --headless=60  no window/GPU smoke test\n";

    std::fprintf(stderr, "\n%s\n", text.c_str());
    std::fflush(stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Frontier Engine", text.c_str(), nullptr);
}

} // namespace

int main(int argc, char** argv) {
    frontier::core::AppConfig config;

    bool shouldExit = false;
    if (!frontier::core::ParseCommandLine(argc, argv, config, shouldExit)) {
        std::fprintf(stderr, "Invalid command line. Use --help for usage.\n");
        return 2;
    }
    if (shouldExit) {
        return 0;
    }

    config.logFile = ResolveLogPath(config.logFile);

    frontier::core::LogLevel level = frontier::core::LogLevel::Info;
    frontier::core::ParseLogLevel(config.logLevel, level);
    frontier::core::Log::Init(config.logFile, level);

    FR_INFO("==============================================================");
    FR_INFO(" Frontier Engine 0.1.0 - procedural outdoor sandbox");
    FR_INFO("==============================================================");

    int exitCode = 1;
    try {
        frontier::core::Engine engine;
        exitCode = engine.Run(config);
    } catch (const std::exception& error) {
        FR_ERROR("Unhandled exception: %s", error.what());
        exitCode = 4;
    } catch (...) {
        FR_ERROR("Unhandled non-standard exception.");
        exitCode = 4;
    }

    FR_INFO("Exit code: %d", exitCode);

    const std::string lastError = frontier::core::Log::LastError();
    const bool interactiveRun = !config.headless && !config.selftest;
    if (exitCode != 0 && interactiveRun) {
        ReportFailure("Frontier Engine could not start (exit code " + std::to_string(exitCode) + ").",
                      lastError, config.logFile);
        if (StdinIsInteractive()) {
            std::fprintf(stderr, "\nPress Enter to close this window...");
            std::fflush(stderr);
            std::getchar();
        }
    }

    frontier::core::Log::Shutdown();
    return exitCode;
}

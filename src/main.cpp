// Frontier Engine entry point.
#include "core/Config.h"
#include "core/Engine.h"
#include "core/Log.h"

#include <cstdio>
#include <string>

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

    frontier::core::LogLevel level = frontier::core::LogLevel::Info;
    frontier::core::ParseLogLevel(config.logLevel, level);
    frontier::core::Log::Init(config.logFile, level);

    FR_INFO("==============================================================");
    FR_INFO(" Frontier Engine 0.1.0 - procedural outdoor sandbox");
    FR_INFO("==============================================================");

    frontier::core::Engine engine;
    const int exitCode = engine.Run(config);

    FR_INFO("Exit code: %d", exitCode);
    frontier::core::Log::Shutdown();
    return exitCode;
}

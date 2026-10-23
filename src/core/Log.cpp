#include "core/Log.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace frontier::core {

bool Log::s_initialized = false;

namespace {

std::mutex g_logMutex;
std::FILE* g_file = nullptr;
LogLevel g_level = LogLevel::Info;
std::string g_lastError;

const char* ShortFileName(const char* path) {
    const char* lastSlash = std::strrchr(path, '/');
    return lastSlash ? lastSlash + 1 : path;
}

} // namespace

const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off:   return "OFF";
    }
    return "?";
}

bool ParseLogLevel(const std::string& text, LogLevel& out) {
    if (text == "trace") { out = LogLevel::Trace; return true; }
    if (text == "debug") { out = LogLevel::Debug; return true; }
    if (text == "info")  { out = LogLevel::Info;  return true; }
    if (text == "warn")  { out = LogLevel::Warn;  return true; }
    if (text == "error") { out = LogLevel::Error; return true; }
    if (text == "off")   { out = LogLevel::Off;   return true; }
    return false;
}

void Log::Init(const std::string& filePath, LogLevel level) {
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_level = level;
        if (!filePath.empty()) {
            g_file = std::fopen(filePath.c_str(), "w");
            if (!g_file) {
                std::fprintf(stderr, "[WARN] Could not open log file '%s'\n", filePath.c_str());
            }
        }
        s_initialized = true;
    }
    Write(LogLevel::Info, __FILE__, __LINE__, "Logging initialized (%s)", filePath.empty() ? "console only" : filePath.c_str());
}

void Log::Shutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    s_initialized = false;
}

void Log::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_level = level;
}

LogLevel Log::Level() { return g_level; }

std::string Log::LastError() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_lastError;
}

void Log::Write(LogLevel level, const char* file, int line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    WriteV(level, file, line, fmt, args);
    va_end(args);
}

void Log::WriteV(LogLevel level, const char* file, int line, const char* fmt, va_list args) {
    if (static_cast<int>(level) < static_cast<int>(g_level) || g_level == LogLevel::Off) {
        return;
    }

    char message[4096];
    std::vsnprintf(message, sizeof(message), fmt, args);

    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif

    const char* levelText = ToString(level);
    const char* shortFile = ShortFileName(file);

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (level == LogLevel::Error) {
        g_lastError = message;
    }
    std::fprintf(stdout, "[%02d:%02d:%02d.%03d] [%-5s] %s (%s:%d)\n",
                 tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, static_cast<int>(ms.count()),
                 levelText, message, shortFile, line);
    std::fflush(stdout);
    if (g_file) {
        std::fprintf(g_file, "[%02d:%02d:%02d.%03d] [%-5s] %s (%s:%d)\n",
                     tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, static_cast<int>(ms.count()),
                     levelText, message, shortFile, line);
        std::fflush(g_file);
    }
}

} // namespace frontier::core

// Derecho Engine - lightweight logging.
#pragma once

#include <cstdarg>
#include <string>

namespace derecho::core {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

const char* ToString(LogLevel level);
bool ParseLogLevel(const std::string& text, LogLevel& out);

// Thread-friendly (mutex protected) logger writing to stdout and, optionally,
// to a log file. Timestamps are local time with millisecond resolution.
class Log {
public:
    static void Init(const std::string& filePath = {}, LogLevel level = LogLevel::Info);
    static void Shutdown();

    static void SetLevel(LogLevel level);
    static LogLevel Level();

    // The most recent message logged at Error level (empty if none). Used to
    // explain a failed start-up in a dialog box when there is no console.
    static std::string LastError();

    static void Write(LogLevel level, const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 4, 5)))
#endif
        ;

    static void WriteV(LogLevel level, const char* file, int line, const char* fmt, va_list args);

private:
    static bool s_initialized;
};

} // namespace derecho::core

#define DE_LOG(level, ...) ::derecho::core::Log::Write((level), __FILE__, __LINE__, __VA_ARGS__)
#define DE_TRACE(...) DE_LOG(::derecho::core::LogLevel::Trace, __VA_ARGS__)
#define DE_DEBUG(...) DE_LOG(::derecho::core::LogLevel::Debug, __VA_ARGS__)
#define DE_INFO(...) DE_LOG(::derecho::core::LogLevel::Info, __VA_ARGS__)
#define DE_WARN(...) DE_LOG(::derecho::core::LogLevel::Warn, __VA_ARGS__)
#define DE_ERROR(...) DE_LOG(::derecho::core::LogLevel::Error, __VA_ARGS__)

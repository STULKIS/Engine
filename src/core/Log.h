// Frontier Engine - lightweight logging.
#pragma once

#include <cstdarg>
#include <string>

namespace frontier::core {

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

    static void Write(LogLevel level, const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 4, 5)))
#endif
        ;

    static void WriteV(LogLevel level, const char* file, int line, const char* fmt, va_list args);

private:
    static bool s_initialized;
};

} // namespace frontier::core

#define FR_LOG(level, ...) ::frontier::core::Log::Write((level), __FILE__, __LINE__, __VA_ARGS__)
#define FR_TRACE(...) FR_LOG(::frontier::core::LogLevel::Trace, __VA_ARGS__)
#define FR_DEBUG(...) FR_LOG(::frontier::core::LogLevel::Debug, __VA_ARGS__)
#define FR_INFO(...) FR_LOG(::frontier::core::LogLevel::Info, __VA_ARGS__)
#define FR_WARN(...) FR_LOG(::frontier::core::LogLevel::Warn, __VA_ARGS__)
#define FR_ERROR(...) FR_LOG(::frontier::core::LogLevel::Error, __VA_ARGS__)

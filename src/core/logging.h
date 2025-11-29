#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

// Central log levels; lower level means less verbose output.
enum class LogLevel : uint8_t
{
    None = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4
};

constexpr LogLevel DEFAULT_LOG_LEVEL = LogLevel::Info;

void setLogLevel(LogLevel level);
LogLevel getLogLevel();
bool isLogLevelEnabled(LogLevel level);
void logMessage(LogLevel level, const char *source, const char *code, const String &message);

#define LOG_MESSAGE(level, source, code, message)             \
    do                                                        \
    {                                                         \
        if (isLogLevelEnabled(level))                         \
        {                                                     \
            logMessage(level, source, code, String(message)); \
        }                                                     \
    } while (0)

#define LOG_ERROR(source, code, message) LOG_MESSAGE(LogLevel::Error, source, code, message)
#define LOG_WARN(source, code, message) LOG_MESSAGE(LogLevel::Warn, source, code, message)
#define LOG_INFO(source, code, message) LOG_MESSAGE(LogLevel::Info, source, code, message)
#define LOG_DEBUG(source, code, message) LOG_MESSAGE(LogLevel::Debug, source, code, message)

#endif // LOGGING_H

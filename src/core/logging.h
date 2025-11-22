#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

// Central log levels; lower level means less verbose output.
enum LogLevel : uint8_t
{
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3
};

#ifndef DEFAULT_LOG_LEVEL
#define DEFAULT_LOG_LEVEL LOG_LEVEL_INFO
#endif

void setLogLevel(LogLevel level);
LogLevel getLogLevel();
bool isLogLevelEnabled(LogLevel level);
void logMessage(LogLevel level, const char *source, const char *code, const String &message);

#define LOG_MESSAGE(level, source, code, message)                     \
    do                                                                \
    {                                                                 \
        if (isLogLevelEnabled(level))                                 \
        {                                                             \
            logMessage(level, source, code, String(message));         \
        }                                                             \
    } while (0)

#define LOG_ERROR(source, code, message) LOG_MESSAGE(LOG_LEVEL_ERROR, source, code, message)
#define LOG_INFO(source, code, message) LOG_MESSAGE(LOG_LEVEL_INFO, source, code, message)
#define LOG_DEBUG(source, code, message) LOG_MESSAGE(LOG_LEVEL_DEBUG, source, code, message)

#endif // LOGGING_H

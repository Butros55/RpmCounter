#include "logging.h"

namespace
{
    LogLevel g_logLevel = static_cast<LogLevel>(DEFAULT_LOG_LEVEL);

    const char *levelLabel(LogLevel level)
    {
        switch (level)
        {
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        default:
            return "OFF";
        }
    }
}

void setLogLevel(LogLevel level)
{
    g_logLevel = level;
}

LogLevel getLogLevel()
{
    return g_logLevel;
}

bool isLogLevelEnabled(LogLevel level)
{
    return static_cast<int>(level) <= static_cast<int>(g_logLevel) && level != LOG_LEVEL_NONE;
}

void logMessage(LogLevel level, const char *source, const char *code, const String &message)
{
    unsigned long now = millis();
    Serial.print("[");
    Serial.print(now);
    Serial.print("][");
    Serial.print(levelLabel(level));
    Serial.print("][");
    Serial.print(source);
    Serial.print("][");
    Serial.print(code);
    Serial.print("] ");
    Serial.println(message);
}

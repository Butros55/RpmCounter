#include "logging.h"

namespace
{
    LogLevel g_logLevel = DEFAULT_LOG_LEVEL;

    const char *levelLabel(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Debug:
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
    return static_cast<int>(level) <= static_cast<int>(g_logLevel) && level != LogLevel::None;
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

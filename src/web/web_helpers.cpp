#include "web_helpers.h"
#include <core/utils.h>

String colorToHex(const RgbColor &color)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", color.r, color.g, color.b);
    return String(buf);
}

RgbColor parseHexColor(const String &value, const RgbColor &fallback)
{
    if (value.length() != 7 || value[0] != '#')
        return fallback;
    RgbColor c{};
    c.r = static_cast<uint8_t>(strtol(value.substring(1, 3).c_str(), nullptr, 16));
    c.g = static_cast<uint8_t>(strtol(value.substring(3, 5).c_str(), nullptr, 16));
    c.b = static_cast<uint8_t>(strtol(value.substring(5, 7).c_str(), nullptr, 16));
    return c;
}

String jsonEscape(const String &input)
{
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i)
    {
        char c = input[i];
        switch (c)
        {
        case '\\':
        case '"':
            out += '\\';
            out += c;
            break;
        case '\n':
            out += F("\\n");
            break;
        case '\r':
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

String htmlEscape(const String &input)
{
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i)
    {
        char c = input[i];
        switch (c)
        {
        case '&':
            out += F("&amp;");
            break;
        case '<':
            out += F("&lt;");
            break;
        case '>':
            out += F("&gt;");
            break;
        case '\"':
            out += F("&quot;");
            break;
        case '\'':
            out += F("&#39;");
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

String safeLabel(const String &value, const String &fallback)
{
    String trimmed = value;
    trimmed.trim();
    if (trimmed.isEmpty())
        return fallback;
    return trimmed;
}

void enforceOrder(int &g, int &y, int &r, int &b)
{
    g = clampInt(g, 0, 100);
    if (y < g)
        y = g;
    y = clampInt(y, 0, 100);
    if (r < y)
        r = y;
    r = clampInt(r, 0, 100);
    b = clampInt(b, 0, 100);
}

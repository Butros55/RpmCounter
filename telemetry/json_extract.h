#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>

namespace telemetry_json
{
    inline bool is_json_number_char(char value)
    {
        return std::isdigit(static_cast<unsigned char>(value)) != 0 ||
               value == '-' ||
               value == '+' ||
               value == '.' ||
               value == 'e' ||
               value == 'E';
    }

    inline bool extract_json_token(const std::string &payload, std::initializer_list<const char *> keys, std::string &token)
    {
        for (const char *key : keys)
        {
            const std::string needle = std::string("\"") + key + "\"";
            const size_t keyPos = payload.find(needle);
            if (keyPos == std::string::npos)
            {
                continue;
            }

            size_t valuePos = payload.find(':', keyPos + needle.size());
            if (valuePos == std::string::npos)
            {
                continue;
            }

            ++valuePos;
            while (valuePos < payload.size() && std::isspace(static_cast<unsigned char>(payload[valuePos])) != 0)
            {
                ++valuePos;
            }
            if (valuePos >= payload.size())
            {
                continue;
            }

            if (payload[valuePos] == '"')
            {
                const size_t start = valuePos + 1;
                const size_t end = payload.find('"', start);
                if (end == std::string::npos)
                {
                    continue;
                }
                token = payload.substr(start, end - start);
                return true;
            }

            const size_t start = valuePos;
            size_t end = start;
            while (end < payload.size() &&
                   std::isspace(static_cast<unsigned char>(payload[end])) == 0 &&
                   payload[end] != ',' &&
                   payload[end] != '}' &&
                   payload[end] != ']')
            {
                ++end;
            }
            if (end == start)
            {
                continue;
            }

            token = payload.substr(start, end - start);
            return true;
        }

        return false;
    }

    inline bool extract_json_number(const std::string &payload, std::initializer_list<const char *> keys, double &value)
    {
        std::string token;
        if (!extract_json_token(payload, keys, token))
        {
            return false;
        }

        try
        {
            value = std::stod(token);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    inline bool extract_json_bool(const std::string &payload, std::initializer_list<const char *> keys, bool &value)
    {
        std::string token;
        if (!extract_json_token(payload, keys, token))
        {
            return false;
        }

        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (token == "true")
        {
            value = true;
            return true;
        }
        if (token == "false")
        {
            value = false;
            return true;
        }
        return false;
    }

    inline bool extract_json_loose_bool(const std::string &payload, std::initializer_list<const char *> keys, bool &value)
    {
        std::string token;
        if (!extract_json_token(payload, keys, token))
        {
            return false;
        }

        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
        if (token == "true" || token == "1" || token == "yes" || token == "on")
        {
            value = true;
            return true;
        }
        if (token == "false" || token == "0" || token == "no" || token == "off")
        {
            value = false;
            return true;
        }
        return false;
    }

    inline bool extract_json_gear(const std::string &payload, std::initializer_list<const char *> keys, int &gear)
    {
        double numericGear = 0.0;
        if (extract_json_number(payload, keys, numericGear))
        {
            gear = std::max(0, static_cast<int>(std::lround(numericGear)));
            return true;
        }

        std::string token;
        if (!extract_json_token(payload, keys, token))
        {
            return false;
        }

        if (token == "N" || token == "n" || token == "R" || token == "r")
        {
            gear = 0;
            return true;
        }

        try
        {
            gear = std::max(0, std::stoi(token));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace telemetry_json

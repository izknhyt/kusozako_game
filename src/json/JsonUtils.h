#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace json
{
struct JsonValue
{
    enum class Type
    {
        Null,
        Number,
        String,
        Object,
        Array,
        Bool
    };

    Type type = Type::Null;
    double number = 0.0;
    bool boolean = false;
    std::string string;
    std::unordered_map<std::string, JsonValue> object;
    std::vector<JsonValue> array;
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string &src) : text(src) {}

    std::optional<JsonValue> parse()
    {
        skipWhitespace();
        auto value = parseValue();
        if (!value.has_value())
        {
            return std::nullopt;
        }
        skipWhitespace();
        if (pos != text.size())
        {
            return std::nullopt;
        }
        return value;
    }

  private:
    const std::string &text;
    std::size_t pos = 0;

    void skipWhitespace()
    {
        while (pos < text.size())
        {
            const char c = text[pos];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            {
                ++pos;
            }
            else
            {
                break;
            }
        }
    }

    std::optional<JsonValue> parseValue()
    {
        if (pos >= text.size())
        {
            return std::nullopt;
        }
        const char c = text[pos];
        if (c == 'n')
        {
            return parseNull();
        }
        if (c == 't' || c == 'f')
        {
            return parseBool();
        }
        if (c == '"')
        {
            return parseString();
        }
        if (c == '{')
        {
            return parseObject();
        }
        if (c == '[')
        {
            return parseArray();
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            return parseNumber();
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull()
    {
        if (text.compare(pos, 4, "null") == 0)
        {
            pos += 4;
            JsonValue v;
            v.type = JsonValue::Type::Null;
            return v;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseBool()
    {
        if (text.compare(pos, 4, "true") == 0)
        {
            pos += 4;
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.boolean = true;
            return v;
        }
        if (text.compare(pos, 5, "false") == 0)
        {
            pos += 5;
            JsonValue v;
            v.type = JsonValue::Type::Bool;
            v.boolean = false;
            return v;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber()
    {
        std::size_t start = pos;
        if (text[pos] == '-')
        {
            ++pos;
        }
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
        {
            ++pos;
        }
        if (pos < text.size() && text[pos] == '.')
        {
            ++pos;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            {
                ++pos;
            }
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E'))
        {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
            {
                ++pos;
            }
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])))
            {
                ++pos;
            }
        }
        if (pos == start || (text[pos - 1] == '+' || text[pos - 1] == '-'))
        {
            return std::nullopt;
        }
        try
        {
            double value = std::stod(text.substr(start, pos - start));
            JsonValue v;
            v.type = JsonValue::Type::Number;
            v.number = value;
            return v;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<JsonValue> parseString()
    {
        if (text[pos] != '"')
        {
            return std::nullopt;
        }
        ++pos;
        std::string result;
        while (pos < text.size())
        {
            char c = text[pos++];
            if (c == '"')
            {
                JsonValue v;
                v.type = JsonValue::Type::String;
                v.string = std::move(result);
                return v;
            }
            if (c == '\\' && pos < text.size())
            {
                char escaped = text[pos++];
                switch (escaped)
                {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u':
                {
                    if (pos + 4 > text.size())
                    {
                        return std::nullopt;
                    }
                    std::string hex = text.substr(pos, 4);
                    pos += 4;
                    char16_t code = static_cast<char16_t>(std::stoi(hex, nullptr, 16));
                    if (code <= 0x7F)
                    {
                        result.push_back(static_cast<char>(code));
                    }
                    else if (code <= 0x7FF)
                    {
                        result.push_back(static_cast<char>(0xC0 | ((code >> 6) & 0x1F)));
                        result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    else
                    {
                        result.push_back(static_cast<char>(0xE0 | ((code >> 12) & 0x0F)));
                        result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: return std::nullopt;
                }
            }
            else
            {
                result.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseArray()
    {
        if (text[pos] != '[')
        {
            return std::nullopt;
        }
        ++pos;
        JsonValue arrayValue;
        arrayValue.type = JsonValue::Type::Array;
        skipWhitespace();
        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            return arrayValue;
        }
        while (pos < text.size())
        {
            skipWhitespace();
            auto value = parseValue();
            if (!value.has_value())
            {
                return std::nullopt;
            }
            arrayValue.array.push_back(std::move(*value));
            skipWhitespace();
            if (pos < text.size() && text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (pos < text.size() && text[pos] == ']')
            {
                ++pos;
                return arrayValue;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseObject()
    {
        if (text[pos] != '{')
        {
            return std::nullopt;
        }
        ++pos;
        JsonValue objValue;
        objValue.type = JsonValue::Type::Object;
        skipWhitespace();
        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            return objValue;
        }
        while (pos < text.size())
        {
            skipWhitespace();
            auto key = parseString();
            if (!key.has_value() || key->type != JsonValue::Type::String)
            {
                return std::nullopt;
            }
            skipWhitespace();
            if (pos >= text.size() || text[pos] != ':')
            {
                return std::nullopt;
            }
            ++pos;
            skipWhitespace();
            auto value = parseValue();
            if (!value.has_value())
            {
                return std::nullopt;
            }
            objValue.object.emplace(std::move(key->string), std::move(*value));
            skipWhitespace();
            if (pos < text.size() && text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (pos < text.size() && text[pos] == '}')
            {
                ++pos;
                return objValue;
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
};

inline std::optional<JsonValue> parseJson(const std::string &text)
{
    JsonParser parser(text);
    return parser.parse();
}

inline const JsonValue *getObjectField(const JsonValue &obj, const std::string &key)
{
    if (obj.type != JsonValue::Type::Object)
    {
        return nullptr;
    }
    auto it = obj.object.find(key);
    if (it == obj.object.end())
    {
        return nullptr;
    }
    return &it->second;
}

inline float getNumber(const JsonValue &obj, const std::string &key, float fallback)
{
    if (const JsonValue *value = getObjectField(obj, key))
    {
        if (value->type == JsonValue::Type::Number)
        {
            return static_cast<float>(value->number);
        }
    }
    return fallback;
}

inline int getInt(const JsonValue &obj, const std::string &key, int fallback)
{
    if (const JsonValue *value = getObjectField(obj, key))
    {
        if (value->type == JsonValue::Type::Number)
        {
            return static_cast<int>(value->number);
        }
    }
    return fallback;
}

inline bool getBool(const JsonValue &obj, const std::string &key, bool fallback)
{
    if (const JsonValue *value = getObjectField(obj, key))
    {
        if (value->type == JsonValue::Type::Bool)
        {
            return value->boolean;
        }
    }
    return fallback;
}

inline std::string getString(const JsonValue &obj, const std::string &key, std::string fallback)
{
    if (const JsonValue *value = getObjectField(obj, key))
    {
        if (value->type == JsonValue::Type::String)
        {
            return value->string;
        }
    }
    return fallback;
}

inline std::vector<float> getNumberArray(const JsonValue &obj, const std::string &key)
{
    std::vector<float> result;
    if (const JsonValue *value = getObjectField(obj, key))
    {
        if (value->type == JsonValue::Type::Array)
        {
            for (const JsonValue &elem : value->array)
            {
                if (elem.type == JsonValue::Type::Number)
                {
                    result.push_back(static_cast<float>(elem.number));
                }
            }
        }
    }
    return result;
}

inline std::vector<std::string> getStringArray(const JsonValue &obj, const std::string &key)
{
    std::vector<std::string> result;
    if (const JsonValue *value = getObjectField(obj, key))
    {
        if (value->type == JsonValue::Type::Array)
        {
            for (const JsonValue &elem : value->array)
            {
                if (elem.type == JsonValue::Type::String)
                {
                    result.push_back(elem.string);
                }
            }
        }
        else if (value->type == JsonValue::Type::String)
        {
            result.push_back(value->string);
        }
    }
    return result;
}

} // namespace json


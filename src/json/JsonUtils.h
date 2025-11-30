#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
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

inline bool jsonEquals(const JsonValue &a, const JsonValue &b)
{
    if (a.type != b.type)
    {
        return false;
    }
    switch (a.type)
    {
    case JsonValue::Type::Null: return true;
    case JsonValue::Type::Number: return a.number == b.number;
    case JsonValue::Type::String: return a.string == b.string;
    case JsonValue::Type::Bool: return a.boolean == b.boolean;
    case JsonValue::Type::Array:
        if (a.array.size() != b.array.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.array.size(); ++i)
        {
            if (!jsonEquals(a.array[i], b.array[i]))
            {
                return false;
            }
        }
        return true;
    case JsonValue::Type::Object:
        if (a.object.size() != b.object.size())
        {
            return false;
        }
        for (const auto &kv : a.object)
        {
            auto it = b.object.find(kv.first);
            if (it == b.object.end())
            {
                return false;
            }
            if (!jsonEquals(kv.second, it->second))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

inline void writeJsonString(const std::string &value, std::string &out)
{
    out.push_back('"');
    for (unsigned char c : value)
    {
        switch (c)
        {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
            if (c < 0x20 || c > 0x7E)
            {
                std::ostringstream oss;
                oss << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c);
                out.append(oss.str());
            }
            else
            {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

inline void writeIndent(std::string &out, int indent)
{
    for (int i = 0; i < indent; ++i)
    {
        out.push_back(' ');
    }
}

inline void writeJsonValue(const JsonValue &value, std::string &out, int indent, int indentSize, bool pretty)
{
    auto writeChild = [&](const JsonValue &child, int childIndent) {
        writeJsonValue(child, out, childIndent, indentSize, pretty);
    };

    switch (value.type)
    {
    case JsonValue::Type::Null:
        out.append("null");
        return;
    case JsonValue::Type::Bool:
        out.append(value.boolean ? "true" : "false");
        return;
    case JsonValue::Type::Number:
    {
        std::ostringstream oss;
        oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value.number;
        out.append(oss.str());
        return;
    }
    case JsonValue::Type::String:
        writeJsonString(value.string, out);
        return;
    case JsonValue::Type::Array:
        out.push_back('[');
        if (!value.array.empty())
        {
            if (pretty)
            {
                out.push_back('\n');
            }
            for (std::size_t i = 0; i < value.array.size(); ++i)
            {
                if (pretty)
                {
                    writeIndent(out, indent + indentSize);
                }
                writeChild(value.array[i], indent + indentSize);
                if (i + 1 < value.array.size())
                {
                    out.push_back(',');
                }
                if (pretty)
                {
                    out.push_back('\n');
                }
            }
            if (pretty)
            {
                writeIndent(out, indent);
            }
        }
        out.push_back(']');
        return;
    case JsonValue::Type::Object:
    {
        out.push_back('{');
        if (!value.object.empty())
        {
            if (pretty)
            {
                out.push_back('\n');
            }
            std::vector<std::string> keys;
            keys.reserve(value.object.size());
            for (const auto &kv : value.object)
            {
                keys.push_back(kv.first);
            }
            std::sort(keys.begin(), keys.end());
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                const std::string &key = keys[i];
                const auto &child = value.object.at(key);
                if (pretty)
                {
                    writeIndent(out, indent + indentSize);
                }
                writeJsonString(key, out);
                out.append(pretty ? ": " : ":");
                writeChild(child, indent + indentSize);
                if (i + 1 < keys.size())
                {
                    out.push_back(',');
                }
                if (pretty)
                {
                    out.push_back('\n');
                }
            }
            if (pretty)
            {
                writeIndent(out, indent);
            }
        }
        out.push_back('}');
        return;
    }
    }
}

inline std::string serializeJson(const JsonValue &value, bool pretty = true, int indentSize = 2)
{
    std::string out;
    writeJsonValue(value, out, 0, indentSize, pretty);
    return out;
}

} // namespace json

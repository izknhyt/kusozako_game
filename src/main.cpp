#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

static Vec2 operator+(const Vec2 &a, const Vec2 &b) { return {a.x + b.x, a.y + b.y}; }
static Vec2 operator-(const Vec2 &a, const Vec2 &b) { return {a.x - b.x, a.y - b.y}; }
static Vec2 operator*(const Vec2 &a, float s) { return {a.x * s, a.y * s}; }
static Vec2 operator/(const Vec2 &a, float s) { return {a.x / s, a.y / s}; }
static Vec2 &operator+=(Vec2 &a, const Vec2 &b)
{
    a.x += b.x;
    a.y += b.y;
    return a;
}

static Vec2 lerp(const Vec2 &a, const Vec2 &b, float t)
{
    return a + (b - a) * t;
}

static float dot(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }
static float lengthSq(const Vec2 &v) { return dot(v, v); }
static float length(const Vec2 &v) { return std::sqrt(lengthSq(v)); }
static Vec2 normalize(const Vec2 &v)
{
    const float len = length(v);
    return len > 0.0001f ? v / len : Vec2{0.0f, 0.0f};
}

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
        const std::string_view numStr(&text[start], pos - start);
        char *endPtr = nullptr;
        const double value = std::strtod(numStr.data(), &endPtr);
        if (endPtr == numStr.data())
        {
            return std::nullopt;
        }
        JsonValue v;
        v.type = JsonValue::Type::Number;
        v.number = value;
        return v;
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
            const char c = text[pos++];
            if (c == '"')
            {
                JsonValue v;
                v.type = JsonValue::Type::String;
                v.string = std::move(result);
                return v;
            }
            if (c == '\\')
            {
                if (pos >= text.size())
                {
                    return std::nullopt;
                }
                const char esc = text[pos++];
                switch (esc)
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
                    // Minimal MVP: skip \u handling (no such sequences in our assets).
                    return std::nullopt;
                default:
                    return std::nullopt;
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
        JsonValue v;
        v.type = JsonValue::Type::Array;
        skipWhitespace();
        if (pos < text.size() && text[pos] == ']')
        {
            ++pos;
            return v;
        }
        while (true)
        {
            skipWhitespace();
            auto elem = parseValue();
            if (!elem.has_value())
            {
                return std::nullopt;
            }
            v.array.push_back(std::move(*elem));
            skipWhitespace();
            if (pos < text.size() && text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (pos < text.size() && text[pos] == ']')
            {
                ++pos;
                break;
            }
            return std::nullopt;
        }
        return v;
    }

    std::optional<JsonValue> parseObject()
    {
        if (text[pos] != '{')
        {
            return std::nullopt;
        }
        ++pos;
        JsonValue v;
        v.type = JsonValue::Type::Object;
        skipWhitespace();
        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            return v;
        }
        while (true)
        {
            skipWhitespace();
            auto key = parseString();
            if (!key.has_value())
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
            v.object.emplace(std::move(key->string), std::move(*value));
            skipWhitespace();
            if (pos < text.size() && text[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (pos < text.size() && text[pos] == '}')
            {
                ++pos;
                break;
            }
            return std::nullopt;
        }
        return v;
    }
};

std::optional<JsonValue> loadJsonFile(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open JSON: " << path << '\n';
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    JsonParser parser(text);
    return parser.parse();
}

const JsonValue *getObjectField(const JsonValue &obj, const std::string &key)
{
    if (obj.type != JsonValue::Type::Object)
    {
        return nullptr;
    }
    auto it = obj.object.find(key);
    return it == obj.object.end() ? nullptr : &it->second;
}

float getNumber(const JsonValue &obj, const std::string &key, float fallback)
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

int getInt(const JsonValue &obj, const std::string &key, int fallback)
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

bool getBool(const JsonValue &obj, const std::string &key, bool fallback)
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

std::string getString(const JsonValue &obj, const std::string &key, std::string fallback)
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

std::vector<float> getNumberArray(const JsonValue &obj, const std::string &key)
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

struct GameConfig
{
    float fixed_dt = 1.0f / 60.0f;
    float pixels_per_unit = 16.0f;
    int base_hp = 300;
    Vec2 base_aabb = {32.0f, 32.0f};
    float yuna_interval = 0.75f;
    int yuna_max = 200;
    Vec2 yuna_offset_px = {48.0f, 0.0f};
    float yuna_scatter_y = 16.0f;
    float victory_grace = 5.0f;
    float telemetry_duration = 3.0f;
    float restart_delay = 2.0f;
    std::string enemy_script = "assets/spawn_level1.json";
    std::string map_path = "assets/maps/level1.tmx";
    int rng_seed = 1337;
};

struct EntityStats
{
    float radius = 4.0f;
    float speed_u_s = 1.8f;
    float hp = 10.0f;
    float dps = 3.0f;
    std::string spritePrefix;
};

struct CommanderStats
{
    Vec2 aabb = {24.0f, 24.0f};
    float radius = 12.0f;
    float speed_u_s = 1.6f;
    float hp = 60.0f;
    float dps = 15.0f;
    std::string spritePrefix;
};

struct WallbreakerStats
{
    float radius = 12.0f;
    float speed_u_s = 1.0f;
    float hp = 60.0f;
    float dps_wall = 15.0f;
    float dps_unit = 5.0f;
    float dps_base = 5.0f;
    bool ignoreKnockback = true;
    float preferWallRadiusPx = 256.0f;
    std::string spritePrefix;
};

struct EntityCatalog
{
    CommanderStats commander;
    EntityStats yuna;
    EntityStats slime;
    WallbreakerStats wallbreaker;
};

struct MapDefs
{
    int tile_size = 16;
    Vec2 base_tile = {70.0f, 22.0f};
    Vec2 spawn_tile_yuna = {69.0f, 22.0f};
    std::unordered_map<std::string, Vec2> gate_tiles;
};

struct SpawnSet
{
    std::string gate;
    int count = 0;
    float interval = 0.3f;
};

struct Wave
{
    float time = 0.0f;
    std::vector<SpawnSet> sets;
    std::string telemetry;
};

struct SpawnScript
{
    float y_jitter = 0.0f;
    std::vector<Wave> waves;
};

std::optional<GameConfig> parseGameConfig(const std::string &path)
{
    auto json = loadJsonFile(path);
    if (!json.has_value())
    {
        return std::nullopt;
    }
    GameConfig cfg;
    cfg.fixed_dt = getNumber(*json, "fixed_dt", cfg.fixed_dt);
    cfg.pixels_per_unit = getNumber(*json, "pixels_per_unit", cfg.pixels_per_unit);
    if (const JsonValue *base = getObjectField(*json, "base"))
    {
        cfg.base_hp = getInt(*base, "hp", cfg.base_hp);
        std::vector<float> aabb = getNumberArray(*base, "aabb_px");
        if (aabb.size() >= 2)
        {
            cfg.base_aabb = {aabb[0], aabb[1]};
        }
    }
    if (const JsonValue *spawn = getObjectField(*json, "spawn"))
    {
        cfg.yuna_interval = getNumber(*spawn, "yuna_interval_s", cfg.yuna_interval);
        cfg.yuna_max = getInt(*spawn, "yuna_max", cfg.yuna_max);
        std::vector<float> offset = getNumberArray(*spawn, "yuna_offset_px");
        if (offset.size() >= 2)
        {
            cfg.yuna_offset_px = {offset[0], offset[1]};
        }
        cfg.yuna_scatter_y = getNumber(*spawn, "yuna_scatter_y_px", cfg.yuna_scatter_y);
    }
    if (const JsonValue *victory = getObjectField(*json, "victory"))
    {
        cfg.victory_grace = getNumber(*victory, "grace_period_s", cfg.victory_grace);
    }
    if (const JsonValue *result = getObjectField(*json, "result"))
    {
        cfg.telemetry_duration = getNumber(*result, "telemetry_duration_s", cfg.telemetry_duration);
        cfg.restart_delay = getNumber(*result, "restart_delay_s", cfg.restart_delay);
    }
    cfg.enemy_script = getString(*json, "enemy_script", cfg.enemy_script);
    cfg.map_path = getString(*json, "map", cfg.map_path);
    cfg.rng_seed = getInt(*json, "rng_seed", cfg.rng_seed);
    return cfg;
}

std::optional<EntityCatalog> parseEntityCatalog(const std::string &path)
{
    auto json = loadJsonFile(path);
    if (!json.has_value())
    {
        return std::nullopt;
    }

    EntityCatalog catalog;
    catalog.commander.spritePrefix = "commander";
    catalog.yuna.spritePrefix = "yuna_front";
    catalog.slime.spritePrefix = "slime_walk";
    catalog.wallbreaker.spritePrefix = "elite_wallbreaker";

    const JsonValue *commander = getObjectField(*json, "commander");
    const JsonValue *yuna = getObjectField(*json, "yuna");
    const JsonValue *slime = getObjectField(*json, "slime");
    if (!commander || !yuna || !slime)
    {
        return std::nullopt;
    }

    std::vector<float> commanderAabb = getNumberArray(*commander, "aabb_px");
    if (commanderAabb.size() >= 2)
    {
        catalog.commander.aabb = {commanderAabb[0], commanderAabb[1]};
    }
    catalog.commander.radius = getNumber(*commander, "r_px", catalog.commander.radius);
    catalog.commander.speed_u_s = getNumber(*commander, "speed_u_s", catalog.commander.speed_u_s);
    catalog.commander.hp = getNumber(*commander, "hp", catalog.commander.hp);
    catalog.commander.dps = getNumber(*commander, "dps", catalog.commander.dps);
    catalog.commander.spritePrefix = getString(*commander, "sprite_prefix", catalog.commander.spritePrefix);

    catalog.yuna.radius = getNumber(*yuna, "r_px", catalog.yuna.radius);
    catalog.yuna.speed_u_s = getNumber(*yuna, "speed_u_s", catalog.yuna.speed_u_s);
    catalog.yuna.hp = getNumber(*yuna, "hp", catalog.yuna.hp);
    catalog.yuna.dps = getNumber(*yuna, "dps", catalog.yuna.dps);
    catalog.yuna.spritePrefix = getString(*yuna, "sprite_prefix", catalog.yuna.spritePrefix);

    catalog.slime.radius = getNumber(*slime, "r_px", catalog.slime.radius);
    catalog.slime.speed_u_s = getNumber(*slime, "speed_u_s", catalog.slime.speed_u_s);
    catalog.slime.hp = getNumber(*slime, "hp", catalog.slime.hp);
    catalog.slime.dps = getNumber(*slime, "dps", catalog.slime.dps);
    catalog.slime.spritePrefix = getString(*slime, "sprite_prefix", catalog.slime.spritePrefix);

    if (const JsonValue *elite = getObjectField(*json, "elite_wallbreaker"))
    {
        catalog.wallbreaker.radius = getNumber(*elite, "r_px", catalog.wallbreaker.radius);
        catalog.wallbreaker.speed_u_s = getNumber(*elite, "speed_u_s", catalog.wallbreaker.speed_u_s);
        catalog.wallbreaker.hp = getNumber(*elite, "hp", catalog.wallbreaker.hp);
        if (const JsonValue *dps = getObjectField(*elite, "dps"))
        {
            catalog.wallbreaker.dps_wall = getNumber(*dps, "wall", catalog.wallbreaker.dps_wall);
            catalog.wallbreaker.dps_unit = getNumber(*dps, "unit", catalog.wallbreaker.dps_unit);
            catalog.wallbreaker.dps_base = getNumber(*dps, "base", catalog.wallbreaker.dps_base);
        }
        if (const JsonValue *traits = getObjectField(*elite, "traits"))
        {
            catalog.wallbreaker.ignoreKnockback = getBool(*traits, "ignore_knockback", catalog.wallbreaker.ignoreKnockback);
            catalog.wallbreaker.preferWallRadiusPx = getNumber(*traits, "prefer_wall_radius_px", catalog.wallbreaker.preferWallRadiusPx);
        }
        catalog.wallbreaker.spritePrefix = getString(*elite, "sprite_prefix", catalog.wallbreaker.spritePrefix);
    }

    return catalog;
}

std::optional<MapDefs> parseMapDefs(const std::string &path)
{
    auto json = loadJsonFile(path);
    if (!json.has_value())
    {
        return std::nullopt;
    }
    MapDefs defs;
    defs.tile_size = getInt(*json, "tile_size_px", defs.tile_size);
    std::vector<float> baseTile = getNumberArray(*json, "base_tile");
    if (baseTile.size() >= 2)
    {
        defs.base_tile = {baseTile[0], baseTile[1]};
    }
    std::vector<float> spawnTile = getNumberArray(*json, "spawn_tile_yuna");
    if (spawnTile.size() >= 2)
    {
        defs.spawn_tile_yuna = {spawnTile[0], spawnTile[1]};
    }
    if (const JsonValue *gates = getObjectField(*json, "gate_tiles"))
    {
        for (const auto &kv : gates->object)
        {
            const JsonValue &arr = kv.second;
            if (arr.type == JsonValue::Type::Array && arr.array.size() >= 2 &&
                arr.array[0].type == JsonValue::Type::Number && arr.array[1].type == JsonValue::Type::Number)
            {
                defs.gate_tiles[kv.first] = {static_cast<float>(arr.array[0].number), static_cast<float>(arr.array[1].number)};
            }
        }
    }
    return defs;
}

std::optional<SpawnScript> parseSpawnScript(const std::string &path)
{
    auto json = loadJsonFile(path);
    if (!json.has_value())
    {
        return std::nullopt;
    }
    SpawnScript script;
    script.y_jitter = getNumber(*json, "y_jitter_px", 0.0f);
    if (const JsonValue *waves = getObjectField(*json, "waves"))
    {
        if (waves->type == JsonValue::Type::Array)
        {
            for (const JsonValue &waveVal : waves->array)
            {
                if (waveVal.type != JsonValue::Type::Object)
                {
                    continue;
                }
                Wave wave;
                wave.time = getNumber(waveVal, "t", 0.0f);
                wave.telemetry = getString(waveVal, "telemetry", "");
                if (const JsonValue *sets = getObjectField(waveVal, "sets"))
                {
                    if (sets->type == JsonValue::Type::Array)
                    {
                        for (const JsonValue &setVal : sets->array)
                        {
                            if (setVal.type != JsonValue::Type::Object)
                            {
                                continue;
                            }
                            SpawnSet set;
                            set.gate = getString(setVal, "gate", "A");
                            set.count = getInt(setVal, "count", 1);
                            set.interval = getNumber(setVal, "interval_s", 0.3f);
                            wave.sets.push_back(set);
                        }
                    }
                }
                script.waves.push_back(std::move(wave));
            }
        }
    }
    std::sort(script.waves.begin(), script.waves.end(), [](const Wave &a, const Wave &b) { return a.time < b.time; });
    return script;
}

Vec2 tileToWorld(const Vec2 &tile, int tileSize)
{
    return {tile.x * tileSize + tileSize * 0.5f, tile.y * tileSize + tileSize * 0.5f};
}

Vec2 leftmostGateWorld(const MapDefs &defs)
{
    Vec2 best{0.0f, 0.0f};
    float bestX = std::numeric_limits<float>::infinity();
    for (const auto &kv : defs.gate_tiles)
    {
        Vec2 gateWorld = tileToWorld(kv.second, defs.tile_size);
        if (gateWorld.x < bestX)
        {
            bestX = gateWorld.x;
            best = gateWorld;
        }
    }
    if (std::isinf(bestX))
    {
        return tileToWorld(defs.base_tile, defs.tile_size);
    }
    return best;
}

struct Unit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 4.0f;
};

struct CommanderUnit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 12.0f;
    bool alive = true;
};

struct ActiveSpawn
{
    Vec2 position;
    int remaining = 0;
    float interval = 0.3f;
    float timer = 0.0f;
};

struct Glyph
{
    SDL_Rect src{0, 0, 0, 0};
    int xoffset = 0;
    int yoffset = 0;
    int xadvance = 0;
};

class BitmapFont
{
  public:
    bool load(const std::string &fntPath)
    {
        std::ifstream file(fntPath);
        if (!file.is_open())
        {
            std::cerr << "Failed to open font metrics: " << fntPath << '\n';
            return false;
        }
        std::string line;
        while (std::getline(file, line))
        {
            if (line.rfind("char id=", 0) == 0)
            {
                parseCharLine(line);
            }
            else if (line.rfind("common ", 0) == 0)
            {
                parseCommonLine(line);
            }
        }
        return true;
    }

    void setTexture(SDL_Texture *tex)
    {
        texture = tex;
    }

    void drawText(SDL_Renderer *renderer, const std::string &text, int x, int y) const
    {
        if (!texture)
        {
            return;
        }
        int penX = x;
        const int penY = y;
        for (const unsigned char c : text)
        {
            auto it = glyphs.find(static_cast<int>(c));
            if (it == glyphs.end())
            {
                penX += lineHeight / 2;
                continue;
            }
            const Glyph &g = it->second;
            SDL_Rect dst{penX + g.xoffset, penY + g.yoffset, g.src.w, g.src.h};
            SDL_RenderCopy(renderer, texture, &g.src, &dst);
            penX += g.xadvance;
        }
    }

    int getLineHeight() const { return lineHeight; }

    int measureText(const std::string &text) const
    {
        int width = 0;
        for (const unsigned char c : text)
        {
            auto it = glyphs.find(static_cast<int>(c));
            if (it == glyphs.end())
            {
                width += lineHeight / 2;
                continue;
            }
            width += it->second.xadvance;
        }
        return width;
    }

  private:
    void parseCharLine(const std::string &line)
    {
        std::istringstream iss(line);
        std::string token;
        Glyph glyph;
        int id = 0;
        while (iss >> token)
        {
            auto eq = token.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            const std::string key = token.substr(0, eq);
            const std::string value = token.substr(eq + 1);
            if (key == "id")
            {
                id = std::stoi(value);
            }
            else if (key == "x")
            {
                glyph.src.x = std::stoi(value);
            }
            else if (key == "y")
            {
                glyph.src.y = std::stoi(value);
            }
            else if (key == "width")
            {
                glyph.src.w = std::stoi(value);
            }
            else if (key == "height")
            {
                glyph.src.h = std::stoi(value);
            }
            else if (key == "xoffset")
            {
                glyph.xoffset = std::stoi(value);
            }
            else if (key == "yoffset")
            {
                glyph.yoffset = std::stoi(value);
            }
            else if (key == "xadvance")
            {
                glyph.xadvance = std::stoi(value);
            }
        }
        glyphs[id] = glyph;
    }

    void parseCommonLine(const std::string &line)
    {
        std::istringstream iss(line);
        std::string token;
        while (iss >> token)
        {
            auto eq = token.find('=');
            if (eq == std::string::npos)
            {
                continue;
            }
            const std::string key = token.substr(0, eq);
            const std::string value = token.substr(eq + 1);
            if (key == "lineHeight")
            {
                lineHeight = std::stoi(value);
            }
        }
    }

    std::unordered_map<int, Glyph> glyphs;
    SDL_Texture *texture = nullptr;
    int lineHeight = 16;
};

struct TileMap
{
    int width = 0;
    int height = 0;
    int tileWidth = 16;
    int tileHeight = 16;
    int tilesetColumns = 1;
    SDL_Texture *tileset = nullptr;
    std::vector<int> floor;
    std::vector<int> block;
    std::vector<int> deco;
};

struct Atlas
{
    SDL_Texture *texture = nullptr;
    std::unordered_map<std::string, SDL_Rect> frames;

    const SDL_Rect *getFrame(const std::string &name) const
    {
        auto it = frames.find(name);
        return it == frames.end() ? nullptr : &it->second;
    }
};

std::string parentDirectory(const std::string &path)
{
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        return std::string();
    }
    return path.substr(0, pos);
}

std::optional<std::string> findAttribute(const std::string &tag, const std::string &name)
{
    const std::string needle = name + "=\"";
    auto pos = tag.find(needle);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    pos += needle.size();
    auto end = tag.find('"', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    return tag.substr(pos, end - pos);
}

std::vector<int> parseCsvInts(const std::string &data)
{
    std::vector<int> values;
    std::string token;
    for (char c : data)
    {
        if ((c >= '0' && c <= '9') || c == '-')
        {
            token.push_back(c);
        }
        else if (c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t')
        {
            if (!token.empty())
            {
                values.push_back(std::stoi(token));
                token.clear();
            }
        }
    }
    if (!token.empty())
    {
        values.push_back(std::stoi(token));
    }
    return values;
}

bool extractLayer(const std::string &xml, const std::string &layerName, std::vector<int> &outTiles)
{
    std::size_t searchPos = 0;
    while (true)
    {
        std::size_t layerPos = xml.find("<layer", searchPos);
        if (layerPos == std::string::npos)
        {
            return false;
        }
        std::size_t layerEnd = xml.find('>', layerPos);
        if (layerEnd == std::string::npos)
        {
            return false;
        }
        std::string layerTag = xml.substr(layerPos, layerEnd - layerPos + 1);
        auto nameAttr = findAttribute(layerTag, "name");
        if (nameAttr && *nameAttr == layerName)
        {
            std::size_t dataPos = xml.find("<data", layerEnd);
            if (dataPos == std::string::npos)
            {
                return false;
            }
            std::size_t dataStart = xml.find('>', dataPos);
            if (dataStart == std::string::npos)
            {
                return false;
            }
            std::size_t dataEnd = xml.find("</data>", dataStart);
            if (dataEnd == std::string::npos)
            {
                return false;
            }
            std::string csv = xml.substr(dataStart + 1, dataEnd - dataStart - 1);
            outTiles = parseCsvInts(csv);
            return true;
        }
        searchPos = layerEnd;
    }
}

bool loadTileMap(const std::string &tmxPath, SDL_Renderer *renderer, TileMap &outMap)
{
    std::ifstream file(tmxPath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open TMX: " << tmxPath << '\n';
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string xml = buffer.str();

    std::size_t mapPos = xml.find("<map");
    if (mapPos == std::string::npos)
    {
        std::cerr << "Invalid TMX: missing <map> tag\n";
        return false;
    }
    std::size_t mapEnd = xml.find('>', mapPos);
    std::string mapTag = xml.substr(mapPos, mapEnd - mapPos + 1);
    auto widthAttr = findAttribute(mapTag, "width");
    auto heightAttr = findAttribute(mapTag, "height");
    auto tileWidthAttr = findAttribute(mapTag, "tilewidth");
    auto tileHeightAttr = findAttribute(mapTag, "tileheight");
    if (!widthAttr || !heightAttr || !tileWidthAttr || !tileHeightAttr)
    {
        std::cerr << "Invalid TMX: missing map attributes\n";
        return false;
    }
    outMap.width = std::stoi(*widthAttr);
    outMap.height = std::stoi(*heightAttr);
    outMap.tileWidth = std::stoi(*tileWidthAttr);
    outMap.tileHeight = std::stoi(*tileHeightAttr);

    std::size_t tilesetPos = xml.find("<tileset", mapEnd);
    if (tilesetPos == std::string::npos)
    {
        std::cerr << "Invalid TMX: missing <tileset>\n";
        return false;
    }
    std::size_t tilesetEnd = xml.find('>', tilesetPos);
    std::string tilesetTag = xml.substr(tilesetPos, tilesetEnd - tilesetPos + 1);
    auto columnsAttr = findAttribute(tilesetTag, "columns");
    if (columnsAttr)
    {
        outMap.tilesetColumns = std::max(1, std::stoi(*columnsAttr));
    }

    std::size_t imagePos = xml.find("<image", tilesetEnd);
    if (imagePos == std::string::npos)
    {
        std::cerr << "Invalid TMX: missing <image>\n";
        return false;
    }
    std::size_t imageEnd = xml.find('>', imagePos);
    std::string imageTag = xml.substr(imagePos, imageEnd - imagePos + 1);
    auto sourceAttr = findAttribute(imageTag, "source");
    if (!sourceAttr)
    {
        std::cerr << "Invalid TMX: image source not found\n";
        return false;
    }

    if (outMap.tileset)
    {
        SDL_DestroyTexture(outMap.tileset);
        outMap.tileset = nullptr;
    }

    std::string baseDir = parentDirectory(tmxPath);
    std::string imagePath = baseDir.empty() ? *sourceAttr : baseDir + "/" + *sourceAttr;
    outMap.tileset = IMG_LoadTexture(renderer, imagePath.c_str());
    if (!outMap.tileset)
    {
        std::cerr << "Failed to load tileset texture: " << imagePath << " -> " << IMG_GetError() << '\n';
        return false;
    }

    if (!extractLayer(xml, "Floor", outMap.floor))
    {
        std::cerr << "TMX missing Floor layer\n";
        return false;
    }
    if (!extractLayer(xml, "Block", outMap.block))
    {
        outMap.block.assign(outMap.width * outMap.height, 0);
    }
    if (!extractLayer(xml, "Deco", outMap.deco))
    {
        outMap.deco.assign(outMap.width * outMap.height, 0);
    }
    return true;
}

bool loadAtlas(const std::string &atlasPath, SDL_Renderer *renderer, Atlas &atlas)
{
    auto json = loadJsonFile(atlasPath);
    if (!json)
    {
        std::cerr << "Failed to load atlas json: " << atlasPath << '\n';
        return false;
    }
    atlas.frames.clear();
    if (atlas.texture)
    {
        SDL_DestroyTexture(atlas.texture);
        atlas.texture = nullptr;
    }

    const JsonValue *meta = getObjectField(*json, "meta");
    std::string imageName = "atlas.png";
    if (meta)
    {
        imageName = getString(*meta, "image", imageName);
    }
    std::string baseDir = parentDirectory(atlasPath);
    std::string imagePath = baseDir.empty() ? imageName : baseDir + "/" + imageName;
    atlas.texture = IMG_LoadTexture(renderer, imagePath.c_str());
    if (!atlas.texture)
    {
        std::cerr << "Failed to load atlas texture: " << imagePath << " -> " << IMG_GetError() << '\n';
        return false;
    }

    const JsonValue *frames = getObjectField(*json, "frames");
    if (!frames || frames->type != JsonValue::Type::Object)
    {
        std::cerr << "Atlas json missing frames object\n";
        return false;
    }

    for (const auto &kv : frames->object)
    {
        const JsonValue &frame = kv.second;
        const JsonValue *xywh = getObjectField(frame, "xywh");
        if (!xywh || xywh->type != JsonValue::Type::Array || xywh->array.size() < 4)
        {
            continue;
        }
        SDL_Rect rect{};
        rect.x = static_cast<int>(xywh->array[0].number);
        rect.y = static_cast<int>(xywh->array[1].number);
        rect.w = static_cast<int>(xywh->array[2].number);
        rect.h = static_cast<int>(xywh->array[3].number);
        atlas.frames[kv.first] = rect;
    }
    return true;
}

struct HUDState
{
    std::string telemetryText;
    float telemetryTimer = 0.0f;
    std::string resultText;
    float resultTimer = 0.0f;
};

enum class GameResult
{
    Playing,
    Victory,
    GameOver
};

std::string normalizeTelemetry(const std::string &text)
{
    bool asciiOnly = true;
    for (unsigned char c : text)
    {
        if (c < 32 || c > 126)
        {
            asciiOnly = false;
            break;
        }
    }
    if (asciiOnly)
    {
        return text;
    }
    if (text.find("左から敵") != std::string::npos)
    {
        return "Enemies from the left!";
    }
    if (text.find("増援が接近") != std::string::npos)
    {
        return "Reinforcements approaching!";
    }
    if (text.find("左右から敵") != std::string::npos)
    {
        return "Enemies on both sides!";
    }
    if (text.find("総攻撃") != std::string::npos)
    {
        return "All-out assault!";
    }
    return "Wave incoming!";
}

struct Simulation
{
    GameConfig config;
    EntityStats yunaStats;
    EntityStats slimeStats;
    WallbreakerStats wallbreakerStats;
    CommanderStats commanderStats;
    CommanderUnit commander;
    MapDefs mapDefs;
    SpawnScript spawnScript;
    std::vector<Unit> yunas;
    std::vector<Unit> enemies;
    std::vector<ActiveSpawn> activeSpawns;
    std::size_t nextWave = 0;
    float spawnTimer = 0.0f;
    float yunaSpawnTimer = 0.0f;
    float simTime = 0.0f;
    float timeSinceLastEnemySpawn = 0.0f;
    float restartCooldown = 0.0f;
    float baseHp = 0.0f;
    bool spawnEnabled = true;
    GameResult result = GameResult::Playing;
    HUDState hud;
    std::mt19937 rng;
    std::uniform_real_distribution<float> scatterY;
    std::uniform_real_distribution<float> gateJitter;

    Vec2 basePos;
    Vec2 yunaSpawnPos;

    void reset()
    {
        simTime = 0.0f;
        timeSinceLastEnemySpawn = 0.0f;
        restartCooldown = 0.0f;
        yunaSpawnTimer = 0.0f;
        yunas.clear();
        enemies.clear();
        activeSpawns.clear();
        nextWave = 0;
        spawnEnabled = true;
        result = GameResult::Playing;
        baseHp = static_cast<float>(config.base_hp);
        hud = {};
        rng.seed(static_cast<std::mt19937::result_type>(config.rng_seed));
        scatterY = std::uniform_real_distribution<float>(-config.yuna_scatter_y, config.yuna_scatter_y);
        gateJitter = std::uniform_real_distribution<float>(-spawnScript.y_jitter, spawnScript.y_jitter);
        basePos = tileToWorld(mapDefs.base_tile, mapDefs.tile_size);
        yunaSpawnPos = tileToWorld(mapDefs.spawn_tile_yuna, mapDefs.tile_size) + config.yuna_offset_px;
        commander.hp = commanderStats.hp;
        commander.radius = commanderStats.radius;
        commander.pos = yunaSpawnPos;
        commander.alive = true;
    }

    void pushTelemetry(const std::string &text)
    {
        hud.telemetryText = normalizeTelemetry(text);
        hud.telemetryTimer = config.telemetry_duration;
    }

    void setResult(GameResult r, const std::string &text)
    {
        if (result != GameResult::Playing)
        {
            return;
        }
        result = r;
        spawnEnabled = false;
        hud.resultText = text;
        hud.resultTimer = config.telemetry_duration;
        restartCooldown = config.restart_delay;
    }

    void update(float dt, const Vec2 &commanderMoveInput)
    {
        simTime += dt;
        if (hud.telemetryTimer > 0.0f)
        {
            hud.telemetryTimer = std::max(0.0f, hud.telemetryTimer - dt);
        }
        if (hud.resultTimer > 0.0f)
        {
            hud.resultTimer = std::max(0.0f, hud.resultTimer - dt);
        }
        if (timeSinceLastEnemySpawn < 10000.0f)
        {
            timeSinceLastEnemySpawn += dt;
        }
        if (restartCooldown > 0.0f)
        {
            restartCooldown = std::max(0.0f, restartCooldown - dt);
        }

        updateWaves();
        updateActiveSpawns(dt);
        updateYunaSpawn(dt);
        updateCommander(dt, commanderMoveInput);
        updateUnits(dt);
        evaluateResult();
    }

    void updateCommander(float dt, const Vec2 &moveInput)
    {
        if (!commander.alive)
        {
            return;
        }
        Vec2 dir = moveInput;
        if (dir.x != 0.0f || dir.y != 0.0f)
        {
            dir = normalize(dir);
        }
        const float speedPx = commanderStats.speed_u_s * config.pixels_per_unit;
        commander.pos += dir * (speedPx * dt);
    }

    void updateWaves()
    {
        if (!spawnEnabled)
        {
            return;
        }
        while (nextWave < spawnScript.waves.size() && simTime >= spawnScript.waves[nextWave].time)
        {
            const Wave &wave = spawnScript.waves[nextWave];
            for (const SpawnSet &set : wave.sets)
            {
                auto gateIt = mapDefs.gate_tiles.find(set.gate);
                if (gateIt == mapDefs.gate_tiles.end())
                {
                    continue;
                }
                ActiveSpawn active;
                active.position = tileToWorld(gateIt->second, mapDefs.tile_size);
                active.remaining = set.count;
                active.interval = set.interval;
                active.timer = 0.0f;
                activeSpawns.push_back(active);
            }
            if (!wave.telemetry.empty())
            {
                pushTelemetry(wave.telemetry);
            }
            ++nextWave;
        }
    }

    void updateActiveSpawns(float dt)
    {
        if (!spawnEnabled)
        {
            return;
        }
        for (ActiveSpawn &spawn : activeSpawns)
        {
            if (spawn.remaining <= 0)
            {
                continue;
            }
            spawn.timer -= dt;
            if (spawn.timer <= 0.0f)
            {
                spawnOneEnemy(spawn.position);
                spawn.timer += spawn.interval;
                --spawn.remaining;
                if (spawn.remaining <= 0)
                {
                    spawn.timer = 0.0f;
                }
            }
        }
        activeSpawns.erase(std::remove_if(activeSpawns.begin(), activeSpawns.end(), [](const ActiveSpawn &s) { return s.remaining <= 0; }), activeSpawns.end());
    }

    void spawnOneEnemy(Vec2 gatePos)
    {
        Unit enemy;
        enemy.pos = gatePos;
        enemy.pos.y += gateJitter(rng);
        enemy.hp = slimeStats.hp;
        enemy.radius = slimeStats.radius;
        enemies.push_back(enemy);
        timeSinceLastEnemySpawn = 0.0f;
    }

    void updateYunaSpawn(float dt)
    {
        if (!spawnEnabled)
        {
            return;
        }
        if (static_cast<int>(yunas.size()) >= config.yuna_max)
        {
            return;
        }
        yunaSpawnTimer -= dt;
        if (yunaSpawnTimer <= 0.0f)
        {
            Unit yuna;
            yuna.pos = yunaSpawnPos;
            yuna.pos.y += scatterY(rng);
            yuna.hp = yunaStats.hp;
            yuna.radius = yunaStats.radius;
            yunas.push_back(yuna);
            yunaSpawnTimer += config.yuna_interval;
        }
    }

    void updateUnits(float dt)
    {
        // Update Yuna movement and combat
        for (Unit &yuna : yunas)
        {
            if (!enemies.empty())
            {
                auto nearestIt = std::min_element(enemies.begin(), enemies.end(), [&yuna](const Unit &a, const Unit &b) {
                    return lengthSq(a.pos - yuna.pos) < lengthSq(b.pos - yuna.pos);
                });
                Vec2 dir = normalize(nearestIt->pos - yuna.pos);
                const float speedPx = yunaStats.speed_u_s * config.pixels_per_unit;
                yuna.pos += dir * (speedPx * dt);
            }
            else
            {
                Vec2 dir = normalize(basePos - yuna.pos);
                const float speedPx = yunaStats.speed_u_s * config.pixels_per_unit;
                yuna.pos += dir * (speedPx * dt);
            }
        }
        // Enemy movement towards base
        for (Unit &enemy : enemies)
        {
            Vec2 dir = normalize(basePos - enemy.pos);
            const float speedPx = slimeStats.speed_u_s * config.pixels_per_unit;
            enemy.pos += dir * (speedPx * dt);
        }
        if (commander.alive)
        {
            for (Unit &enemy : enemies)
            {
                const float r = commander.radius + enemy.radius;
                if (lengthSq(commander.pos - enemy.pos) <= r * r)
                {
                    enemy.hp -= commanderStats.dps * dt;
                    commander.hp -= slimeStats.dps * dt;
                    if (commander.hp <= 0.0f)
                    {
                        commander.hp = 0.0f;
                        commander.alive = false;
                        setResult(GameResult::GameOver, "Commander Down");
                        break;
                    }
                }
            }
        }
        // Combat Yuna vs Enemy
        for (Unit &yuna : yunas)
        {
            for (Unit &enemy : enemies)
            {
                const float r = yuna.radius + enemy.radius;
                if (lengthSq(yuna.pos - enemy.pos) <= r * r)
                {
                    enemy.hp -= yunaStats.dps * dt;
                    yuna.hp -= slimeStats.dps * dt;
                }
            }
        }
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Unit &e) { return e.hp <= 0.0f; }), enemies.end());
        yunas.erase(std::remove_if(yunas.begin(), yunas.end(), [](const Unit &y) { return y.hp <= 0.0f; }), yunas.end());
        // Enemy vs base
        const float baseRadius = std::max(config.base_aabb.x, config.base_aabb.y) * 0.5f;
        for (Unit &enemy : enemies)
        {
            const float r = baseRadius + enemy.radius;
            if (lengthSq(enemy.pos - basePos) <= r * r)
            {
                baseHp -= slimeStats.dps * dt;
                if (baseHp <= 0.0f)
                {
                    baseHp = 0.0f;
                    setResult(GameResult::GameOver, "Game Over");
                    break;
                }
            }
        }
    }

    void evaluateResult()
    {
        if (result != GameResult::Playing)
        {
            return;
        }
        const bool wavesFinished = nextWave >= spawnScript.waves.size() && activeSpawns.empty();
        const bool noEnemies = enemies.empty();
        if (wavesFinished && noEnemies && timeSinceLastEnemySpawn >= config.victory_grace)
        {
            setResult(GameResult::Victory, "Victory");
        }
    }

    bool canRestart() const { return restartCooldown <= 0.0f; }
};

struct Camera
{
    Vec2 position{0.0f, 0.0f};
    float speed = 320.0f;
};

Vec2 worldToScreen(const Vec2 &world, const Camera &camera)
{
    return {world.x - camera.position.x, world.y - camera.position.y};
}

void drawFilledCircle(SDL_Renderer *renderer, const Vec2 &pos, float radius)
{
    const int r = static_cast<int>(radius);
    const int cx = static_cast<int>(pos.x);
    const int cy = static_cast<int>(pos.y);
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            if (dx * dx + dy * dy <= r * r)
            {
                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    }
}

void drawTileLayer(SDL_Renderer *renderer, const TileMap &map, const std::vector<int> &tiles, const Camera &camera, int screenW, int screenH)
{
    if (!map.tileset)
    {
        return;
    }
    const int totalTiles = static_cast<int>(tiles.size());
    if (totalTiles == 0)
    {
        return;
    }
    for (int y = 0; y < map.height; ++y)
    {
        for (int x = 0; x < map.width; ++x)
        {
            const int index = y * map.width + x;
            if (index < 0 || index >= totalTiles)
            {
                continue;
            }
            const int gid = tiles[index];
            if (gid <= 0)
            {
                continue;
            }
            const int tileIndex = gid - 1;
            const int srcX = (tileIndex % map.tilesetColumns) * map.tileWidth;
            const int srcY = (tileIndex / map.tilesetColumns) * map.tileHeight;
            SDL_Rect src{srcX, srcY, map.tileWidth, map.tileHeight};
            SDL_Rect dst{
                static_cast<int>(x * map.tileWidth - camera.position.x),
                static_cast<int>(y * map.tileHeight - camera.position.y),
                map.tileWidth,
                map.tileHeight};
            if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x > screenW || dst.y > screenH)
            {
                continue;
            }
            SDL_RenderCopy(renderer, map.tileset, &src, &dst);
        }
    }
}

void renderScene(SDL_Renderer *renderer, const Simulation &sim, const Camera &camera, const BitmapFont &font, const TileMap &map, const Atlas &atlas, int screenW, int screenH, float fps)
{
    SDL_SetRenderDrawColor(renderer, 26, 32, 38, 255);
    SDL_RenderClear(renderer);

    drawTileLayer(renderer, map, map.floor, camera, screenW, screenH);
    if (map.tileset)
    {
        SDL_SetTextureColorMod(map.tileset, 190, 190, 200);
        drawTileLayer(renderer, map, map.block, camera, screenW, screenH);
        SDL_SetTextureColorMod(map.tileset, 255, 255, 255);
    }
    drawTileLayer(renderer, map, map.deco, camera, screenW, screenH);

    // Draw base
    const Vec2 baseScreen = worldToScreen(sim.basePos, camera);

    if (atlas.texture)
    {
        if (const SDL_Rect *baseFrame = atlas.getFrame("base_box"))
        {
            SDL_Rect dest{
                static_cast<int>(baseScreen.x - baseFrame->w * 0.5f),
                static_cast<int>(baseScreen.y - baseFrame->h * 0.5f),
                baseFrame->w,
                baseFrame->h};
            SDL_RenderCopy(renderer, atlas.texture, baseFrame, &dest);
        }
        else
        {
            SDL_FRect baseRect{baseScreen.x - sim.config.base_aabb.x * 0.5f, baseScreen.y - sim.config.base_aabb.y * 0.5f, sim.config.base_aabb.x, sim.config.base_aabb.y};
            SDL_SetRenderDrawColor(renderer, 130, 90, 50, 255);
            SDL_RenderFillRectF(renderer, &baseRect);
        }
    }
    else
    {
        SDL_FRect baseRect{baseScreen.x - sim.config.base_aabb.x * 0.5f, baseScreen.y - sim.config.base_aabb.y * 0.5f, sim.config.base_aabb.x, sim.config.base_aabb.y};
        SDL_SetRenderDrawColor(renderer, 130, 90, 50, 255);
        SDL_RenderFillRectF(renderer, &baseRect);
    }

    const SDL_Rect *commanderFrame = nullptr;
    const SDL_Rect *yunaFrame = nullptr;
    const SDL_Rect *enemyFrame = nullptr;
    if (!sim.commanderStats.spritePrefix.empty())
    {
        commanderFrame = atlas.getFrame(sim.commanderStats.spritePrefix + "_0");
    }
    if (!sim.yunaStats.spritePrefix.empty())
    {
        yunaFrame = atlas.getFrame(sim.yunaStats.spritePrefix + "_0");
    }
    if (!sim.slimeStats.spritePrefix.empty())
    {
        enemyFrame = atlas.getFrame(sim.slimeStats.spritePrefix + "_0");
    }
    const SDL_Rect *friendRing = atlas.getFrame("ring_friend");
    const SDL_Rect *enemyRing = atlas.getFrame("ring_enemy");

    if (sim.commander.alive)
    {
        Vec2 commanderScreen = worldToScreen(sim.commander.pos, camera);
        if (atlas.texture && commanderFrame)
        {
            SDL_Rect dest{
                static_cast<int>(commanderScreen.x - commanderFrame->w * 0.5f),
                static_cast<int>(commanderScreen.y - commanderFrame->h * 0.5f),
                commanderFrame->w,
                commanderFrame->h};
            SDL_RenderCopy(renderer, atlas.texture, commanderFrame, &dest);
            if (friendRing)
            {
                SDL_Rect ringDest{
                    dest.x + (dest.w - friendRing->w) / 2,
                    dest.y + dest.h - friendRing->h,
                    friendRing->w,
                    friendRing->h};
                SDL_RenderCopy(renderer, atlas.texture, friendRing, &ringDest);
            }
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
            drawFilledCircle(renderer, commanderScreen, sim.commander.radius);
        }
    }

    if (atlas.texture && yunaFrame)
    {
        for (const Unit &yuna : sim.yunas)
        {
            Vec2 screenPos = worldToScreen(yuna.pos, camera);
            SDL_Rect dest{
                static_cast<int>(screenPos.x - yunaFrame->w * 0.5f),
                static_cast<int>(screenPos.y - yunaFrame->h * 0.5f),
                yunaFrame->w,
                yunaFrame->h};
            SDL_RenderCopy(renderer, atlas.texture, yunaFrame, &dest);
            if (friendRing)
            {
                SDL_Rect ringDest{
                    dest.x + (dest.w - friendRing->w) / 2,
                    dest.y + dest.h - friendRing->h,
                    friendRing->w,
                    friendRing->h};
                SDL_RenderCopy(renderer, atlas.texture, friendRing, &ringDest);
            }
        }
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 240, 190, 60, 255);
        for (const Unit &yuna : sim.yunas)
        {
            Vec2 screenPos = worldToScreen(yuna.pos, camera);
            drawFilledCircle(renderer, screenPos, yuna.radius);
        }
    }

    if (atlas.texture && enemyFrame)
    {
        for (const Unit &enemy : sim.enemies)
        {
            Vec2 screenPos = worldToScreen(enemy.pos, camera);
            SDL_Rect dest{
                static_cast<int>(screenPos.x - enemyFrame->w * 0.5f),
                static_cast<int>(screenPos.y - enemyFrame->h * 0.5f),
                enemyFrame->w,
                enemyFrame->h};
            SDL_RenderCopy(renderer, atlas.texture, enemyFrame, &dest);
            if (enemyRing)
            {
                SDL_Rect ringDest{
                    dest.x + (dest.w - enemyRing->w) / 2,
                    dest.y + dest.h - enemyRing->h,
                    enemyRing->w,
                    enemyRing->h};
                SDL_RenderCopy(renderer, atlas.texture, enemyRing, &ringDest);
            }
        }
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, 80, 160, 220, 255);
        for (const Unit &enemy : sim.enemies)
        {
            Vec2 screenPos = worldToScreen(enemy.pos, camera);
            drawFilledCircle(renderer, screenPos, enemy.radius);
        }
    }

    // Ambient vignette overlay for dungeon mood
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 8, 24, 140);
    SDL_Rect overlay{0, 0, screenW, screenH};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // HUD text
    int y = 16;
    const int baseHpInt = static_cast<int>(std::round(std::max(sim.baseHp, 0.0f)));
    const float hpRatio = sim.config.base_hp > 0 ? std::clamp(baseHpInt / static_cast<float>(sim.config.base_hp), 0.0f, 1.0f) : 0.0f;
    SDL_Rect barBg{screenW / 2 - 160, 20, 320, 20};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 28, 22, 40, 200);
    SDL_RenderFillRect(renderer, &barBg);
    SDL_Rect barFill{barBg.x + 4, barBg.y + 4, static_cast<int>((barBg.w - 8) * hpRatio), barBg.h - 8};
    SDL_SetRenderDrawColor(renderer, 255, 166, 64, 230);
    SDL_RenderFillRect(renderer, &barFill);
    SDL_SetRenderDrawColor(renderer, 90, 70, 120, 230);
    SDL_RenderDrawRect(renderer, &barBg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    font.drawText(renderer, "Base HP", barBg.x, barBg.y - font.getLineHeight());
    font.drawText(renderer, std::to_string(baseHpInt), barBg.x + barBg.w + 12, barBg.y - 2);

    y = barBg.y + barBg.h + 16;
    const int commanderHpInt = static_cast<int>(std::round(std::max(sim.commander.hp, 0.0f)));
    font.drawText(renderer, "Allies: " + std::to_string(static_cast<int>(sim.yunas.size())), 20, y);
    y += font.getLineHeight();
    if (sim.commander.alive)
    {
        font.drawText(renderer, "Commander HP: " + std::to_string(commanderHpInt), 20, y);
    }
    else
    {
        font.drawText(renderer, "Commander: Down", 20, y);
    }
    y += font.getLineHeight();
    font.drawText(renderer, "Enemies: " + std::to_string(static_cast<int>(sim.enemies.size())), 20, y);
    y += font.getLineHeight();
    std::ostringstream fpsText;
    fpsText << "FPS: " << static_cast<int>(fps);
    font.drawText(renderer, fpsText.str(), 12, y);

    if (!sim.hud.telemetryText.empty() && sim.hud.telemetryTimer > 0.0f)
    {
        const int textWidth = font.measureText(sim.hud.telemetryText);
        font.drawText(renderer, sim.hud.telemetryText, screenW - textWidth - 12, 12);
    }
    if (!sim.hud.resultText.empty() && sim.hud.resultTimer > 0.0f)
    {
        const int textWidth = font.measureText(sim.hud.resultText);
        font.drawText(renderer, sim.hud.resultText, screenW / 2 - textWidth / 2, screenH / 2 - font.getLineHeight());
    }

    SDL_RenderPresent(renderer);
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const int screenWidth = 1280;
    const int screenHeight = 720;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0)
    {
        std::cerr << "IMG_Init failed: " << IMG_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Kusozako MVP", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screenWidth, screenHeight, SDL_WINDOW_SHOWN);
    if (!window)
    {
        std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    auto configOpt = parseGameConfig("assets/game.json");
    auto entityCatalogOpt = parseEntityCatalog("assets/entities.json");
    auto mapDefsOpt = parseMapDefs("assets/map_defs.json");
    std::optional<SpawnScript> spawnScriptOpt;
    if (configOpt)
    {
        spawnScriptOpt = parseSpawnScript(configOpt->enemy_script);
    }

    if (!configOpt || !entityCatalogOpt || !mapDefsOpt || !spawnScriptOpt)
    {
        std::cerr << "Failed to load configuration files.\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    TileMap tileMap;
    if (!loadTileMap(configOpt->map_path, renderer, tileMap))
    {
        std::cerr << "Continuing without tilemap visuals.\n";
    }

    Atlas atlas;
    if (!loadAtlas("assets/atlas.json", renderer, atlas))
    {
        std::cerr << "Continuing without atlas visuals.\n";
    }

    Simulation sim;
    sim.config = *configOpt;
    sim.yunaStats = entityCatalogOpt->yuna;
    sim.slimeStats = entityCatalogOpt->slime;
    sim.wallbreakerStats = entityCatalogOpt->wallbreaker;
    sim.commanderStats = entityCatalogOpt->commander;
    sim.mapDefs = *mapDefsOpt;
    sim.spawnScript = *spawnScriptOpt;
    sim.reset();

    BitmapFont font;
    if (!font.load("assets/ui/hud_font.fnt"))
    {
        std::cerr << "Failed to load HUD font metrics.\n";
    }
    SDL_Texture *fontTex = IMG_LoadTexture(renderer, "assets/ui/hud_font.png");
    if (!fontTex)
    {
        std::cerr << "Failed to load HUD font texture: " << IMG_GetError() << '\n';
    }
    font.setTexture(fontTex);

    Camera camera;
    Vec2 baseCameraTarget{sim.basePos.x - screenWidth * 0.5f, sim.basePos.y - screenHeight * 0.5f};
    Vec2 introFocus = leftmostGateWorld(sim.mapDefs);
    Vec2 introCameraTarget{introFocus.x - screenWidth * 0.5f, introFocus.y - screenHeight * 0.5f};
    camera.position = introCameraTarget;
    const float introDuration = 3.0f;
    float introTimer = introDuration;
    bool introActive = true;

    bool running = true;
    Uint64 prevCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;
    double fpsTimer = 0.0;
    int frames = 0;
    float currentFps = 60.0f;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
            {
                switch (event.key.keysym.scancode)
                {
                case SDL_SCANCODE_ESCAPE:
                    running = false;
                    break;
                case SDL_SCANCODE_SPACE:
                    camera.position = {sim.commander.pos.x - screenWidth * 0.5f, sim.commander.pos.y - screenHeight * 0.5f};
                    introActive = false;
                    introTimer = 0.0f;
                    break;
                case SDL_SCANCODE_TAB:
                    camera.position = {sim.basePos.x - screenWidth * 0.5f, sim.basePos.y - screenHeight * 0.5f};
                    introActive = false;
                    introTimer = 0.0f;
                    break;
                case SDL_SCANCODE_R:
                    if (sim.result != GameResult::Playing && sim.canRestart())
                    {
                        sim.reset();
                        baseCameraTarget = {sim.basePos.x - screenWidth * 0.5f, sim.basePos.y - screenHeight * 0.5f};
                        introFocus = leftmostGateWorld(sim.mapDefs);
                        introCameraTarget = {introFocus.x - screenWidth * 0.5f, introFocus.y - screenHeight * 0.5f};
                        camera.position = introCameraTarget;
                        introTimer = introDuration;
                        introActive = true;
                    }
                    break;
                default:
                    break;
                }
            }
        }

        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const double frameTime = (nowCounter - prevCounter) / frequency;
        prevCounter = nowCounter;
        accumulator += frameTime;
        fpsTimer += frameTime;
        ++frames;
        if (fpsTimer >= 1.0)
        {
            currentFps = static_cast<float>(frames / fpsTimer);
            fpsTimer = 0.0;
            frames = 0;
        }

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const float dt = sim.config.fixed_dt;
        while (accumulator >= dt)
        {
            Vec2 commanderInput{0.0f, 0.0f};
            if (!introActive)
            {
                if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) commanderInput.y -= 1.0f;
                if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) commanderInput.y += 1.0f;
                if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) commanderInput.x -= 1.0f;
                if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) commanderInput.x += 1.0f;
            }

            sim.update(dt, commanderInput);
            accumulator -= dt;
        }

        const float frameSeconds = static_cast<float>(frameTime);
        if (introActive)
        {
            introTimer = std::max(0.0f, introTimer - frameSeconds);
            const float t = std::clamp(1.0f - (introTimer / introDuration), 0.0f, 1.0f);
            const float eased = t * t * (3.0f - 2.0f * t);
            camera.position = lerp(introCameraTarget, baseCameraTarget, eased);
            if (introTimer <= 0.0f)
            {
                introActive = false;
                camera.position = lerp(introCameraTarget, baseCameraTarget, 1.0f);
            }
        }
        else
        {
            Vec2 targetCamera{sim.commander.pos.x - screenWidth * 0.5f, sim.commander.pos.y - screenHeight * 0.5f};
            const float followFactor = std::clamp(frameSeconds * 6.0f, 0.0f, 1.0f);
            camera.position = lerp(camera.position, targetCamera, followFactor);
        }

        const Vec2 cameraOffset = camera.position;
        Camera renderCamera = camera;
        renderCamera.position = cameraOffset;
        renderScene(renderer, sim, renderCamera, font, tileMap, atlas, screenWidth, screenHeight, currentFps);
    }

    if (fontTex)
    {
        SDL_DestroyTexture(fontTex);
    }
    if (atlas.texture)
    {
        SDL_DestroyTexture(atlas.texture);
    }
    if (tileMap.tileset)
    {
        SDL_DestroyTexture(tileMap.tileset);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}

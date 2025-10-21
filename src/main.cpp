#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <numeric>
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

struct RenderStats
{
    int drawCalls = 0;
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

struct RespawnSettings
{
    float base = 5.0f;
    float scale = 5.0f;
    float k = 1.0f;
    float floor = 0.0f;
    float invuln = 0.0f;
};

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
    RespawnSettings yuna_respawn{5.0f, 5.0f, 1.0f, 0.0f, 2.0f};
    RespawnSettings commander_respawn{8.0f, 5.0f, 2.0f, 12.0f, 2.0f};
    int commander_auto_reinforce = 0;
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

enum class EnemyArchetype
{
    Slime,
    Wallbreaker
};

enum class ArmyStance
{
    RushNearest,
    PushForward,
    FollowLeader,
    DefendBase
};

enum class Formation
{
    Swarm,
    Wedge,
    Line,
    Ring
};

enum class SkillType
{
    ToggleFollow,
    MakeWall,
    SpawnRate,
    Detonate
};

struct SkillDef
{
    std::string id;
    std::string displayName;
    SkillType type = SkillType::ToggleFollow;
    int hotkey = 1;
    float cooldown = 0.0f;
    float mana = 0.0f;
    float radius = 0.0f;
    float duration = 0.0f;
    int lenTiles = 0;
    float hpPerSegment = 0.0f;
    float multiplier = 1.0f;
    float damage = 0.0f;
    float respawnPenalty = 1.0f;
    float spawnSlowMult = 1.0f;
    float spawnSlowDuration = 0.0f;
    float respawnBonusPerHit = 0.0f;
    float respawnBonusCap = 0.0f;
};

struct RuntimeSkill
{
    SkillDef def;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
};

EnemyArchetype enemyTypeFromString(const std::string &typeId)
{
    if (typeId == "elite_wallbreaker" || typeId == "wallbreaker" || typeId == "wall_breaker")
    {
        return EnemyArchetype::Wallbreaker;
    }
    return EnemyArchetype::Slime;
}

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
    std::string typeId = "slime";
    EnemyArchetype type = EnemyArchetype::Slime;
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
    std::unordered_map<std::string, Vec2> gate_tiles;
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
    if (const JsonValue *respawn = getObjectField(*json, "respawn"))
    {
        if (const JsonValue *chibi = getObjectField(*respawn, "chibi"))
        {
            cfg.yuna_respawn.base = getNumber(*chibi, "base_s", cfg.yuna_respawn.base);
            cfg.yuna_respawn.scale = getNumber(*chibi, "scale_s", cfg.yuna_respawn.scale);
            cfg.yuna_respawn.k = getNumber(*chibi, "k", cfg.yuna_respawn.k);
            cfg.yuna_respawn.invuln = getNumber(*chibi, "invuln_s", cfg.yuna_respawn.invuln);
        }
        if (const JsonValue *commander = getObjectField(*respawn, "commander"))
        {
            cfg.commander_respawn.base = getNumber(*commander, "base_s", cfg.commander_respawn.base);
            cfg.commander_respawn.scale = getNumber(*commander, "scale_s", cfg.commander_respawn.scale);
            cfg.commander_respawn.k = getNumber(*commander, "k", cfg.commander_respawn.k);
            cfg.commander_respawn.floor = getNumber(*commander, "floor_s", cfg.commander_respawn.floor);
            cfg.commander_respawn.invuln = getNumber(*commander, "invuln_s", cfg.commander_respawn.invuln);
        }
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
    if (const JsonValue *onDeath = getObjectField(*json, "on_commander_death"))
    {
        cfg.commander_auto_reinforce = getInt(*onDeath, "auto_reinforce_chibi", cfg.commander_auto_reinforce);
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
    if (const JsonValue *gates = getObjectField(*json, "gates"))
    {
        if (gates->type == JsonValue::Type::Object)
        {
            for (const auto &kv : gates->object)
            {
                const JsonValue &gateObj = kv.second;
                if (gateObj.type != JsonValue::Type::Object)
                {
                    continue;
                }
                if (const JsonValue *tile = getObjectField(gateObj, "tile"))
                {
                    if (tile->type == JsonValue::Type::Array && tile->array.size() >= 2 &&
                        tile->array[0].type == JsonValue::Type::Number && tile->array[1].type == JsonValue::Type::Number)
                    {
                        script.gate_tiles[kv.first] = {
                            static_cast<float>(tile->array[0].number),
                            static_cast<float>(tile->array[1].number)};
                    }
                }
            }
        }
    }
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
                            set.typeId = getString(setVal, "type", set.typeId);
                            set.type = enemyTypeFromString(set.typeId);
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

SkillType skillTypeFromString(const std::string &type)
{
    if (type == "make_wall")
    {
        return SkillType::MakeWall;
    }
    if (type == "spawn_rate")
    {
        return SkillType::SpawnRate;
    }
    if (type == "detonate" || type == "self_destruct")
    {
        return SkillType::Detonate;
    }
    return SkillType::ToggleFollow;
}

std::string prettifySkillName(const std::string &id)
{
    std::string result;
    result.reserve(id.size());
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        char c = id[i];
        if (c == '_')
        {
            result.push_back(' ');
        }
        else
        {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    bool capitalize = true;
    for (char &c : result)
    {
        if (capitalize && std::isalpha(static_cast<unsigned char>(c)))
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalize = false;
        }
        else if (c == ' ')
        {
            capitalize = true;
        }
    }
    return result;
}

std::optional<std::vector<SkillDef>> parseSkillCatalog(const std::string &path)
{
    auto json = loadJsonFile(path);
    if (!json.has_value())
    {
        return std::nullopt;
    }
    if (json->type != JsonValue::Type::Object)
    {
        std::cerr << "skills.json root must be an object\n";
        return std::nullopt;
    }
    std::vector<SkillDef> defs;
    defs.reserve(json->object.size());
    for (const auto &kv : json->object)
    {
        const JsonValue &node = kv.second;
        if (node.type != JsonValue::Type::Object)
        {
            continue;
        }
        SkillDef def;
        def.id = kv.first;
        def.displayName = prettifySkillName(kv.first);
        def.hotkey = getInt(node, "key", def.hotkey);
        def.cooldown = getNumber(node, "cooldown_s", def.cooldown);
        def.mana = getNumber(node, "mana", def.mana);
        def.radius = getNumber(node, "radius_px", def.radius);
        std::string typeId = getString(node, "type", "toggle_follow");
        def.type = skillTypeFromString(typeId);
        switch (def.type)
        {
        case SkillType::ToggleFollow:
            // radius already handled
            break;
        case SkillType::MakeWall:
            def.lenTiles = getInt(node, "len_tiles", def.lenTiles);
            def.duration = getNumber(node, "life_s", def.duration);
            def.hpPerSegment = getNumber(node, "hp_per_segment", def.hpPerSegment);
            break;
        case SkillType::SpawnRate:
            def.multiplier = getNumber(node, "mult", def.multiplier);
            def.duration = getNumber(node, "duration_s", def.duration);
            break;
        case SkillType::Detonate:
            def.damage = getNumber(node, "damage", def.damage);
            def.respawnPenalty = getNumber(node, "respawn_penalty_ratio", def.respawnPenalty);
            if (const JsonValue *slow = getObjectField(node, "spawn_slow"))
            {
                def.spawnSlowMult = getNumber(*slow, "mult", def.spawnSlowMult);
                def.spawnSlowDuration = getNumber(*slow, "duration_s", def.spawnSlowDuration);
            }
            def.respawnBonusPerHit = getNumber(node, "respawn_bonus_per_hit_s", def.respawnBonusPerHit);
            def.respawnBonusCap = getNumber(node, "respawn_bonus_cap_s", def.respawnBonusCap);
            break;
        }
        if (def.radius < 0.0f)
        {
            def.radius = 0.0f;
        }
        if (def.lenTiles <= 0)
        {
            def.lenTiles = 1;
        }
        if (def.hpPerSegment <= 0.0f)
        {
            def.hpPerSegment = 1.0f;
        }
        if (def.multiplier <= 0.0f)
        {
            def.multiplier = 1.0f;
        }
        if (def.spawnSlowMult <= 0.0f)
        {
            def.spawnSlowMult = 1.0f;
        }
        defs.push_back(def);
    }
    std::sort(defs.begin(), defs.end(), [](const SkillDef &a, const SkillDef &b) { return a.hotkey < b.hotkey; });
    return defs;
}

std::vector<SkillDef> buildDefaultSkills()
{
    std::vector<SkillDef> defs;
    SkillDef rally;
    rally.id = "rally";
    rally.displayName = "Rally";
    rally.type = SkillType::ToggleFollow;
    rally.hotkey = 1;
    rally.cooldown = 3.0f;
    rally.radius = 160.0f;
    defs.push_back(rally);

    SkillDef wall;
    wall.id = "wall";
    wall.displayName = "Wall";
    wall.type = SkillType::MakeWall;
    wall.hotkey = 2;
    wall.cooldown = 15.0f;
    wall.lenTiles = 8;
    wall.duration = 20.0f;
    wall.hpPerSegment = 10.0f;
    defs.push_back(wall);

    SkillDef surge;
    surge.id = "surge";
    surge.displayName = "Surge";
    surge.type = SkillType::SpawnRate;
    surge.hotkey = 3;
    surge.cooldown = 40.0f;
    surge.multiplier = 1.5f;
    surge.duration = 20.0f;
    defs.push_back(surge);

    SkillDef selfDestruct;
    selfDestruct.id = "self_destruct";
    selfDestruct.displayName = "Self Destruct";
    selfDestruct.type = SkillType::Detonate;
    selfDestruct.hotkey = 4;
    selfDestruct.cooldown = 60.0f;
    selfDestruct.radius = 128.0f;
    selfDestruct.damage = 80.0f;
    selfDestruct.respawnPenalty = 2.0f;
    selfDestruct.spawnSlowMult = 2.0f;
    selfDestruct.spawnSlowDuration = 10.0f;
    selfDestruct.respawnBonusPerHit = 0.5f;
    selfDestruct.respawnBonusCap = 6.0f;
    defs.push_back(selfDestruct);

    return defs;
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

std::vector<Vec2> computeFormationOffsets(Formation formation, std::size_t count)
{
    std::vector<Vec2> offsets;
    offsets.reserve(count);
    if (count == 0)
    {
        return offsets;
    }
    if (count == 1)
    {
        offsets.push_back({0.0f, 32.0f});
        return offsets;
    }
    constexpr float pi = 3.14159265358979323846f;
    switch (formation)
    {
    case Formation::Swarm:
    case Formation::Ring:
    {
        const float radius = formation == Formation::Ring ? 40.0f : 48.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            const float angle = (static_cast<float>(i) / static_cast<float>(count)) * 2.0f * pi;
            offsets.push_back({std::cos(angle) * radius, std::sin(angle) * radius});
        }
        break;
    }
    case Formation::Line:
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const float offsetX = (static_cast<float>(i) - (static_cast<float>(count) - 1.0f) * 0.5f) * 24.0f;
            offsets.push_back({offsetX, 32.0f});
        }
        break;
    }
    case Formation::Wedge:
    {
        std::size_t produced = 0;
        int row = 0;
        while (produced < count)
        {
            const int rowCount = row + 1;
            for (int i = 0; i < rowCount && produced < count; ++i)
            {
                const float offsetX = (static_cast<float>(i) - (rowCount - 1) * 0.5f) * 26.0f;
                const float offsetY = 32.0f + row * 28.0f;
                offsets.push_back({offsetX, offsetY});
                ++produced;
            }
            ++row;
        }
        break;
    }
    }
    return offsets;
}

const char *stanceLabel(ArmyStance stance)
{
    switch (stance)
    {
    case ArmyStance::RushNearest: return "Rush Nearest";
    case ArmyStance::PushForward: return "Push Forward";
    case ArmyStance::FollowLeader: return "Follow Leader";
    case ArmyStance::DefendBase: return "Defend Base";
    }
    return "Unknown";
}

const char *formationLabel(Formation formation)
{
    switch (formation)
    {
    case Formation::Swarm: return "Swarm";
    case Formation::Wedge: return "Wedge";
    case Formation::Line: return "Line";
    case Formation::Ring: return "Ring";
    }
    return "Unknown";
}

struct Unit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 4.0f;
    bool followBySkill = false;
    bool followByStance = false;
    bool effectiveFollower = false;
    Vec2 formationOffset{0.0f, 0.0f};
};

struct CommanderUnit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 12.0f;
    bool alive = true;
};

struct EnemyUnit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 0.0f;
    EnemyArchetype type = EnemyArchetype::Slime;
};

struct WallSegment
{
    Vec2 pos;
    float hp = 0.0f;
    float life = 0.0f;
    float radius = 0.0f;
};

struct ActiveSpawn
{
    Vec2 position;
    int remaining = 0;
    float interval = 0.3f;
    float timer = 0.0f;
    EnemyArchetype type = EnemyArchetype::Slime;
};

class TextRenderer
{
  public:
    TextRenderer() = default;
    ~TextRenderer() { unload(); }

    bool load(const std::string &fontPath, int pointSize)
    {
        unload();
        font = TTF_OpenFont(fontPath.c_str(), pointSize);
        if (!font)
        {
            std::cerr << "Failed to load font: " << fontPath << " -> " << TTF_GetError() << '\n';
            return false;
        }
        lineHeight = TTF_FontLineSkip(font);
        return true;
    }

    void unload()
    {
        if (font)
        {
            TTF_CloseFont(font);
            font = nullptr;
            lineHeight = 0;
        }
    }

    int getLineHeight() const { return lineHeight; }

    int measureText(const std::string &text) const
    {
        if (!font)
        {
            return 0;
        }
        int width = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &width, nullptr) != 0)
        {
            return 0;
        }
        return width;
    }

    void drawText(SDL_Renderer *renderer, const std::string &text, int x, int y, RenderStats *stats = nullptr,
                  SDL_Color color = {255, 255, 255, 255}) const
    {
        if (!font || text.empty())
        {
            return;
        }
        SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surface)
        {
            std::cerr << "TTF_RenderUTF8_Blended failed: " << TTF_GetError() << '\n';
            return;
        }
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!texture)
        {
            std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << '\n';
            SDL_FreeSurface(surface);
            return;
        }
        SDL_Rect dst{x, y, surface->w, surface->h};
        if (SDL_RenderCopy(renderer, texture, nullptr, &dst) == 0)
        {
            if (stats)
            {
                ++stats->drawCalls;
            }
        }
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
    }

    bool isLoaded() const { return font != nullptr; }

  private:
    TTF_Font *font = nullptr;
    int lineHeight = 0;
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

inline void countedRenderClear(SDL_Renderer *renderer, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderClear(renderer);
}

inline void countedRenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst,
                              RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderCopy(renderer, texture, src, dst);
}

inline void countedRenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderFillRect(renderer, rect);
}

inline void countedRenderFillRectF(SDL_Renderer *renderer, const SDL_FRect *rect, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderFillRectF(renderer, rect);
}

inline void countedRenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderDrawRect(renderer, rect);
}

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
    Defeat
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
    std::vector<EnemyUnit> enemies;
    std::vector<ActiveSpawn> activeSpawns;
    std::vector<WallSegment> walls;
    std::vector<RuntimeSkill> skills;
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

    ArmyStance stance = ArmyStance::RushNearest;
    Formation formation = Formation::Swarm;
    int selectedSkill = 0;
    bool rallyState = false;
    float spawnRateMultiplier = 1.0f;
    float spawnSlowMultiplier = 1.0f;
    float spawnSlowTimer = 0.0f;
    float commanderRespawnTimer = 0.0f;
    float commanderInvulnTimer = 0.0f;
    int reinforcementQueue = 0;

    Vec2 basePos;
    Vec2 yunaSpawnPos;
    std::vector<float> yunaRespawnTimers;

    void configureSkills(const std::vector<SkillDef> &defs)
    {
        skills.clear();
        skills.reserve(defs.size());
        for (const SkillDef &def : defs)
        {
            RuntimeSkill runtime;
            runtime.def = def;
            runtime.cooldownRemaining = 0.0f;
            runtime.activeTimer = 0.0f;
            skills.push_back(runtime);
        }
        selectedSkill = 0;
    }

    void reset()
    {
        simTime = 0.0f;
        timeSinceLastEnemySpawn = 0.0f;
        restartCooldown = 0.0f;
        yunaSpawnTimer = 0.0f;
        yunas.clear();
        enemies.clear();
        activeSpawns.clear();
        walls.clear();
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
        commanderRespawnTimer = 0.0f;
        commanderInvulnTimer = 0.0f;
        reinforcementQueue = 0;
        spawnRateMultiplier = 1.0f;
        spawnSlowMultiplier = 1.0f;
        spawnSlowTimer = 0.0f;
        rallyState = false;
        stance = ArmyStance::RushNearest;
        formation = Formation::Swarm;
        selectedSkill = 0;
        for (RuntimeSkill &skill : skills)
        {
            skill.cooldownRemaining = 0.0f;
            skill.activeTimer = 0.0f;
        }
        yunaRespawnTimers.clear();
    }

    float clampOverkillRatio(float overkill, float maxHp) const
    {
        if (maxHp <= 0.0f)
        {
            return 0.0f;
        }
        return std::clamp(overkill / maxHp, 0.0f, 3.0f);
    }

    float computeChibiRespawnTime(float overkillRatio) const
    {
        float time = config.yuna_respawn.base + config.yuna_respawn.k * overkillRatio * config.yuna_respawn.scale;
        return std::max(0.0f, time);
    }

    float computeCommanderRespawnTime(float overkillRatio) const
    {
        float time = config.commander_respawn.base + config.commander_respawn.k * overkillRatio * config.commander_respawn.scale;
        if (time < config.commander_respawn.floor)
        {
            time = config.commander_respawn.floor;
        }
        return time;
    }

    void enqueueYunaRespawn(float overkillRatio)
    {
        yunaRespawnTimers.push_back(computeChibiRespawnTime(overkillRatio));
    }

    void updateSkillTimers(float dt)
    {
        for (RuntimeSkill &skill : skills)
        {
            if (skill.cooldownRemaining > 0.0f)
            {
                skill.cooldownRemaining = std::max(0.0f, skill.cooldownRemaining - dt);
            }
            if (skill.activeTimer > 0.0f)
            {
                skill.activeTimer = std::max(0.0f, skill.activeTimer - dt);
                if (skill.activeTimer <= 0.0f)
                {
                    if (skill.def.type == SkillType::SpawnRate)
                    {
                        spawnRateMultiplier = 1.0f;
                    }
                }
            }
        }
        if (spawnSlowTimer > 0.0f)
        {
            spawnSlowTimer = std::max(0.0f, spawnSlowTimer - dt);
            if (spawnSlowTimer <= 0.0f)
            {
                spawnSlowMultiplier = 1.0f;
            }
        }
        if (commanderInvulnTimer > 0.0f && commander.alive)
        {
            commanderInvulnTimer = std::max(0.0f, commanderInvulnTimer - dt);
        }
    }

    void updateCommanderRespawn(float dt)
    {
        if (commander.alive)
        {
            return;
        }
        if (commanderRespawnTimer > 0.0f)
        {
            commanderRespawnTimer = std::max(0.0f, commanderRespawnTimer - dt);
            if (commanderRespawnTimer <= 0.0f)
            {
                commander.alive = true;
                commander.hp = commanderStats.hp;
                commander.pos = yunaSpawnPos;
                commanderInvulnTimer = config.commander_respawn.invuln;
            }
        }
    }

    void updateWalls(float dt)
    {
        for (WallSegment &wall : walls)
        {
            if (wall.life > 0.0f)
            {
                wall.life = std::max(0.0f, wall.life - dt);
            }
        }
        walls.erase(std::remove_if(walls.begin(), walls.end(), [](const WallSegment &wall) {
                        return wall.life <= 0.0f || wall.hp <= 0.0f;
                    }),
                    walls.end());
    }

    void spawnYunaUnit()
    {
        Unit yuna;
        yuna.pos = yunaSpawnPos;
        yuna.pos.y += scatterY(rng);
        yuna.hp = yunaStats.hp;
        yuna.radius = yunaStats.radius;
        yunas.push_back(yuna);
    }

    void scheduleCommanderRespawn(float penaltyMultiplier, float bonusSeconds, float overkillRatio)
    {
        commander.alive = false;
        commander.hp = 0.0f;
        const float penalty = std::max(1.0f, penaltyMultiplier);
        const float bonus = std::max(0.0f, bonusSeconds);
        float respawnTime = computeCommanderRespawnTime(overkillRatio);
        respawnTime = std::max(config.commander_respawn.floor, respawnTime * penalty - bonus);
        commanderRespawnTimer = respawnTime;
        commanderInvulnTimer = 0.0f;
        reinforcementQueue += config.commander_auto_reinforce;
        rallyState = false;
        for (Unit &yuna : yunas)
        {
            yuna.followBySkill = false;
            yuna.followByStance = false;
            yuna.effectiveFollower = false;
        }
        hud.resultText = "Commander Down";
        hud.resultTimer = config.telemetry_duration;
    }

    void spawnWallSegments(const SkillDef &def, const Vec2 &worldTarget)
    {
        if (!commander.alive)
        {
            return;
        }
        Vec2 direction = normalize(worldTarget - commander.pos);
        if (lengthSq(direction) < 0.0001f)
        {
            direction = {-1.0f, 0.0f};
        }
        const float spacing = static_cast<float>(mapDefs.tile_size);
        Vec2 start = commander.pos + direction * spacing;
        std::vector<Vec2> segmentPositions;
        segmentPositions.reserve(def.lenTiles);
        for (int i = 0; i < def.lenTiles; ++i)
        {
            segmentPositions.push_back(start + direction * (spacing * static_cast<float>(i)));
        }

        if (yunas.empty())
        {
            pushTelemetry("Need chibi allies for wall");
            return;
        }

        const int maxSegments = std::min(static_cast<int>(segmentPositions.size()), static_cast<int>(yunas.size()));
        std::vector<char> taken(yunas.size(), 0);
        std::vector<std::size_t> convertIndices;
        std::vector<Vec2> chosenPositions;
        convertIndices.reserve(maxSegments);
        chosenPositions.reserve(maxSegments);

        for (int i = 0; i < maxSegments; ++i)
        {
            float bestDist = std::numeric_limits<float>::max();
            std::size_t bestIndex = std::numeric_limits<std::size_t>::max();
            for (std::size_t idx = 0; idx < yunas.size(); ++idx)
            {
                if (taken[idx])
                {
                    continue;
                }
                const float dist = lengthSq(yunas[idx].pos - segmentPositions[static_cast<std::size_t>(i)]);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIndex = idx;
                }
            }
            if (bestIndex == std::numeric_limits<std::size_t>::max())
            {
                break;
            }
            taken[bestIndex] = 1;
            convertIndices.push_back(bestIndex);
            chosenPositions.push_back(segmentPositions[static_cast<std::size_t>(i)]);
        }

        if (convertIndices.empty())
        {
            pushTelemetry("Need chibi allies for wall");
            return;
        }

        std::sort(convertIndices.begin(), convertIndices.end(), std::greater<>());
        for (std::size_t idx : convertIndices)
        {
            enqueueYunaRespawn(0.0f);
            yunas.erase(yunas.begin() + static_cast<std::ptrdiff_t>(idx));
        }

        for (const Vec2 &segmentPos : chosenPositions)
        {
            WallSegment segment;
            segment.pos = segmentPos;
            segment.hp = def.hpPerSegment;
            segment.life = def.duration;
            segment.radius = spacing * 0.5f;
            walls.push_back(segment);
        }
        pushTelemetry("Wall deployed");
    }

    void detonateCommander(const SkillDef &def)
    {
        if (!commander.alive)
        {
            return;
        }
        const float radiusSq = def.radius * def.radius;
        int hits = 0;
        for (EnemyUnit &enemy : enemies)
        {
            if (lengthSq(enemy.pos - commander.pos) <= radiusSq)
            {
                enemy.hp -= def.damage;
                ++hits;
            }
        }
        spawnSlowMultiplier = def.spawnSlowMult;
        spawnSlowTimer = def.spawnSlowDuration;
        const float bonus = std::min(def.respawnBonusCap, def.respawnBonusPerHit * static_cast<float>(hits));
        commander.hp = 0.0f;
        scheduleCommanderRespawn(def.respawnPenalty, bonus, 0.0f);
        pushTelemetry("Self Destruct!");
    }

    void activateSkillAtIndex(int index, const Vec2 &worldTarget)
    {
        if (index < 0 || index >= static_cast<int>(skills.size()))
        {
            return;
        }
        RuntimeSkill &skill = skills[static_cast<std::size_t>(index)];
        if (skill.cooldownRemaining > 0.0f)
        {
            return;
        }
        switch (skill.def.type)
        {
        case SkillType::ToggleFollow:
            rallyState = !rallyState;
            if (rallyState)
            {
                const float radiusSq = skill.def.radius * skill.def.radius;
                for (Unit &yuna : yunas)
                {
                    if (lengthSq(yuna.pos - worldTarget) <= radiusSq)
                    {
                        yuna.followBySkill = true;
                    }
                }
                pushTelemetry("Rally!");
            }
            else
            {
                for (Unit &yuna : yunas)
                {
                    yuna.followBySkill = false;
                }
                pushTelemetry("Rally dismissed");
            }
            skill.cooldownRemaining = skill.def.cooldown;
            break;
        case SkillType::MakeWall:
            spawnWallSegments(skill.def, worldTarget);
            skill.cooldownRemaining = skill.def.cooldown;
            break;
        case SkillType::SpawnRate:
            spawnRateMultiplier = skill.def.multiplier;
            skill.activeTimer = skill.def.duration;
            skill.cooldownRemaining = skill.def.cooldown;
            pushTelemetry("Spawn surge");
            break;
        case SkillType::Detonate:
            detonateCommander(skill.def);
            skill.cooldownRemaining = skill.def.cooldown;
            break;
        }
    }

    void activateSelectedSkill(const Vec2 &worldTarget)
    {
        activateSkillAtIndex(selectedSkill, worldTarget);
    }

    void selectSkillByHotkey(int hotkey)
    {
        for (std::size_t i = 0; i < skills.size(); ++i)
        {
            if (skills[i].def.hotkey == hotkey)
            {
                selectedSkill = static_cast<int>(i);
                return;
            }
        }
    }

    void cycleFormation(int direction)
    {
        static const std::array<Formation, 4> order{Formation::Swarm, Formation::Wedge, Formation::Line, Formation::Ring};
        auto it = std::find(order.begin(), order.end(), formation);
        if (it == order.end())
        {
            formation = Formation::Swarm;
            return;
        }
        int index = static_cast<int>(std::distance(order.begin(), it));
        index = (index + direction + static_cast<int>(order.size())) % static_cast<int>(order.size());
        formation = order[static_cast<std::size_t>(index)];
        std::string message = std::string("Formation: ") + formationLabel(formation);
        pushTelemetry(message);
    }

    void setStance(ArmyStance newStance)
    {
        if (stance == newStance)
        {
            return;
        }
        stance = newStance;
        std::string message = std::string("Stance: ") + stanceLabel(stance);
        pushTelemetry(message);
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

    float enemySpeed(const EnemyUnit &enemy) const
    {
        const float speed = enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerStats.speed_u_s : slimeStats.speed_u_s;
        return speed * config.pixels_per_unit;
    }

    float enemyDpsAgainstUnits(const EnemyUnit &enemy) const
    {
        return enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerStats.dps_unit : slimeStats.dps;
    }

    float enemyDpsAgainstBase(const EnemyUnit &enemy) const
    {
        return enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerStats.dps_base : slimeStats.dps;
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

        updateSkillTimers(dt);
        updateWaves();
        updateActiveSpawns(dt);
        updateYunaSpawn(dt);
        updateCommanderRespawn(dt);
        updateCommander(dt, commanderMoveInput);
        updateWalls(dt);
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
                Vec2 gateTile{};
                bool foundGate = false;
                if (auto scriptGate = spawnScript.gate_tiles.find(set.gate); scriptGate != spawnScript.gate_tiles.end())
                {
                    gateTile = scriptGate->second;
                    foundGate = true;
                }
                else if (auto mapGate = mapDefs.gate_tiles.find(set.gate); mapGate != mapDefs.gate_tiles.end())
                {
                    gateTile = mapGate->second;
                    foundGate = true;
                }
                if (!foundGate)
                {
                    continue;
                }
                ActiveSpawn active;
                active.position = tileToWorld(gateTile, mapDefs.tile_size);
                active.remaining = set.count;
                active.interval = set.interval;
                active.timer = 0.0f;
                active.type = set.type;
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
                spawnOneEnemy(spawn.position, spawn.type);
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

    void spawnOneEnemy(Vec2 gatePos, EnemyArchetype type)
    {
        EnemyUnit enemy;
        enemy.pos = gatePos;
        enemy.pos.y += gateJitter(rng);
        enemy.type = type;
        if (type == EnemyArchetype::Wallbreaker)
        {
            enemy.hp = wallbreakerStats.hp;
            enemy.radius = wallbreakerStats.radius;
        }
        else
        {
            enemy.hp = slimeStats.hp;
            enemy.radius = slimeStats.radius;
        }
        enemies.push_back(enemy);
        timeSinceLastEnemySpawn = 0.0f;
    }

    void updateYunaSpawn(float dt)
    {
        if (!spawnEnabled)
        {
            return;
        }
        const float rateMultiplier = std::max(spawnRateMultiplier, 0.1f);
        const float slowMultiplier = std::max(spawnSlowMultiplier, 0.1f);
        yunaSpawnTimer -= dt;
        const float minInterval = 0.1f;
        const float spawnInterval = std::max(minInterval, (config.yuna_interval / rateMultiplier) * slowMultiplier);
        while (yunaSpawnTimer <= 0.0f)
        {
            if (static_cast<int>(yunas.size()) < config.yuna_max)
            {
                spawnYunaUnit();
                yunaSpawnTimer += spawnInterval;
            }
            else
            {
                yunaSpawnTimer = 0.0f;
                break;
            }
        }

        const float respawnRate = rateMultiplier / slowMultiplier;
        for (float &timer : yunaRespawnTimers)
        {
            timer -= dt * respawnRate;
        }
        std::vector<float> remainingRespawns;
        remainingRespawns.reserve(yunaRespawnTimers.size());
        for (float timer : yunaRespawnTimers)
        {
            if (timer <= 0.0f && static_cast<int>(yunas.size()) < config.yuna_max)
            {
                spawnYunaUnit();
            }
            else
            {
                remainingRespawns.push_back(std::max(timer, 0.0f));
            }
        }
        yunaRespawnTimers.swap(remainingRespawns);

        while (reinforcementQueue > 0 && static_cast<int>(yunas.size()) < config.yuna_max)
        {
            spawnYunaUnit();
            --reinforcementQueue;
        }
    }

    void updateUnits(float dt)
    {
        constexpr std::size_t followLimit = 30;
        const float yunaSpeedPx = yunaStats.speed_u_s * config.pixels_per_unit;
        const float followerSnapDistSq = 16.0f;

        for (Unit &yuna : yunas)
        {
            yuna.followByStance = false;
            yuna.effectiveFollower = false;
        }

        if (stance == ArmyStance::FollowLeader && commander.alive)
        {
            std::vector<std::pair<float, Unit *>> distances;
            distances.reserve(yunas.size());
            for (Unit &yuna : yunas)
            {
                distances.emplace_back(lengthSq(yuna.pos - commander.pos), &yuna);
            }
            std::sort(distances.begin(), distances.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            const std::size_t take = std::min<std::size_t>(followLimit, distances.size());
            for (std::size_t i = 0; i < take; ++i)
            {
                distances[i].second->followByStance = true;
            }
        }

        std::vector<std::pair<float, Unit *>> skillFollowers;
        skillFollowers.reserve(yunas.size());
        for (Unit &yuna : yunas)
        {
            if (yuna.followBySkill)
            {
                skillFollowers.emplace_back(lengthSq(yuna.pos - commander.pos), &yuna);
            }
        }
        std::sort(skillFollowers.begin(), skillFollowers.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

        std::vector<Unit *> followers;
        followers.reserve(followLimit);
        for (auto &entry : skillFollowers)
        {
            if (followers.size() >= followLimit)
            {
                break;
            }
            entry.second->effectiveFollower = true;
            followers.push_back(entry.second);
        }
        if (followers.size() < followLimit)
        {
            for (Unit &yuna : yunas)
            {
                if (followers.size() >= followLimit)
                {
                    break;
                }
                if (!yuna.followBySkill && yuna.followByStance)
                {
                    yuna.effectiveFollower = true;
                    followers.push_back(&yuna);
                }
            }
        }

        auto formationOffsets = computeFormationOffsets(formation, followers.size());
        for (std::size_t i = 0; i < followers.size(); ++i)
        {
            followers[i]->formationOffset = formationOffsets[i];
        }

        const std::size_t totalFollowers = followers.size();
        const std::size_t totalDefenders = yunas.size() > totalFollowers ? yunas.size() - totalFollowers : 0;
        const std::size_t safeDefenders = totalDefenders > 0 ? totalDefenders : 1;
        std::size_t defendIndex = 0;
        std::size_t supportIndex = 0;

        auto nearestEnemy = [this](const Vec2 &pos) -> EnemyUnit * {
            EnemyUnit *best = nullptr;
            float bestDist = std::numeric_limits<float>::max();
            for (EnemyUnit &enemy : enemies)
            {
                const float dist = lengthSq(enemy.pos - pos);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    best = &enemy;
                }
            }
            return best;
        };

        for (Unit &yuna : yunas)
        {
            Vec2 velocity{0.0f, 0.0f};
            if (yuna.effectiveFollower && commander.alive)
            {
                Vec2 desiredPos = commander.pos + yuna.formationOffset;
                Vec2 toTarget = desiredPos - yuna.pos;
                if (lengthSq(toTarget) > followerSnapDistSq)
                {
                    velocity = normalize(toTarget) * yunaSpeedPx;
                }
            }
            else
            {
                switch (stance)
                {
                case ArmyStance::RushNearest:
                {
                    if (EnemyUnit *target = nearestEnemy(yuna.pos))
                    {
                        velocity = normalize(target->pos - yuna.pos) * yunaSpeedPx;
                    }
                    else
                    {
                        velocity = normalize(basePos - yuna.pos) * yunaSpeedPx;
                    }
                    break;
                }
                case ArmyStance::PushForward:
                {
                    EnemyUnit *target = nearestEnemy(yuna.pos);
                    Vec2 pushTarget{std::max(basePos.x - mapDefs.tile_size * 25.0f, mapDefs.tile_size * 4.0f), basePos.y};
                    if (target && lengthSq(target->pos - yuna.pos) < 160.0f * 160.0f)
                    {
                        velocity = normalize(target->pos - yuna.pos) * yunaSpeedPx;
                    }
                    else
                    {
                        velocity = normalize(pushTarget - yuna.pos) * yunaSpeedPx;
                    }
                    break;
                }
                case ArmyStance::FollowLeader:
                {
                    if (commander.alive)
                    {
                        const float lane = static_cast<float>(supportIndex) - static_cast<float>(safeDefenders - 1) * 0.5f;
                        Vec2 supportPos{commander.pos.x - 96.0f, commander.pos.y + lane * 20.0f};
                        ++supportIndex;
                        velocity = normalize(supportPos - yuna.pos) * yunaSpeedPx;
                    }
                    else if (EnemyUnit *target = nearestEnemy(yuna.pos))
                    {
                        velocity = normalize(target->pos - yuna.pos) * yunaSpeedPx;
                    }
                    else
                    {
                        velocity = normalize(basePos - yuna.pos) * yunaSpeedPx;
                    }
                    break;
                }
                case ArmyStance::DefendBase:
                {
                    EnemyUnit *target = nearestEnemy(basePos);
                    if (target && lengthSq(target->pos - basePos) < (mapDefs.tile_size * 22.0f) * (mapDefs.tile_size * 22.0f))
                    {
                        velocity = normalize(target->pos - yuna.pos) * yunaSpeedPx;
                    }
                    else
                    {
                        const float angle = 2.0f * 3.14159265358979323846f * (static_cast<float>(defendIndex) / static_cast<float>(safeDefenders));
                        Vec2 ringTarget{basePos.x + std::cos(angle) * 120.0f, basePos.y + std::sin(angle) * 80.0f};
                        ++defendIndex;
                        velocity = normalize(ringTarget - yuna.pos) * yunaSpeedPx;
                    }
                    break;
                }
                }
            }
            if (velocity.x != 0.0f || velocity.y != 0.0f)
            {
                yuna.pos += velocity * dt;
            }
        }

        for (EnemyUnit &enemy : enemies)
        {
            Vec2 target = basePos;
            if (enemy.type == EnemyArchetype::Wallbreaker)
            {
                const float preferRadius = wallbreakerStats.preferWallRadiusPx;
                if (preferRadius > 0.0f)
                {
                    const float preferRadiusSq = preferRadius * preferRadius;
                    WallSegment *bestWall = nullptr;
                    float bestDistSq = preferRadiusSq;
                    for (WallSegment &wall : walls)
                    {
                        if (wall.hp <= 0.0f)
                        {
                            continue;
                        }
                        const float distSq = lengthSq(wall.pos - enemy.pos);
                        if (distSq < bestDistSq)
                        {
                            bestDistSq = distSq;
                            bestWall = &wall;
                        }
                    }
                    if (bestWall)
                    {
                        target = bestWall->pos;
                    }
                }
            }

            Vec2 dir = normalize(target - enemy.pos);
            const float speedPx = enemySpeed(enemy);
            enemy.pos += dir * (speedPx * dt);
        }

        for (EnemyUnit &enemy : enemies)
        {
            for (WallSegment &wall : walls)
            {
                const float combined = enemy.radius + wall.radius;
                const float distSq = lengthSq(enemy.pos - wall.pos);
                if (distSq <= combined * combined)
                {
                    float dist = std::sqrt(std::max(distSq, 0.0001f));
                    Vec2 normal = dist > 0.0f ? (enemy.pos - wall.pos) / dist : Vec2{1.0f, 0.0f};
                    const float overlap = combined - dist;
                    enemy.pos += normal * overlap;
                    const float dps = enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerStats.dps_wall : slimeStats.dps;
                    wall.hp -= dps * dt;
                }
            }
        }

        std::vector<float> yunaDamage(yunas.size(), 0.0f);
        float commanderDamage = 0.0f;

        if (commander.alive)
        {
            for (EnemyUnit &enemy : enemies)
            {
                const float combined = commander.radius + enemy.radius;
                if (lengthSq(commander.pos - enemy.pos) <= combined * combined)
                {
                    enemy.hp -= commanderStats.dps * dt;
                    if (commanderInvulnTimer <= 0.0f)
                    {
                        commanderDamage += enemyDpsAgainstUnits(enemy) * dt;
                    }
                }
            }
        }

        for (std::size_t i = 0; i < yunas.size(); ++i)
        {
            Unit &yuna = yunas[i];
            for (EnemyUnit &enemy : enemies)
            {
                const float combined = yuna.radius + enemy.radius;
                if (lengthSq(yuna.pos - enemy.pos) <= combined * combined)
                {
                    enemy.hp -= yunaStats.dps * dt;
                    yunaDamage[i] += enemyDpsAgainstUnits(enemy) * dt;
                }
            }
        }

        enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const EnemyUnit &e) { return e.hp <= 0.0f; }), enemies.end());

        if (commander.alive && commanderDamage > 0.0f)
        {
            const float hpBefore = commander.hp;
            commander.hp -= commanderDamage;
            if (commander.hp <= 0.0f)
            {
                const float overkill = std::max(0.0f, commanderDamage - std::max(hpBefore, 0.0f));
                const float ratio = clampOverkillRatio(overkill, commanderStats.hp);
                scheduleCommanderRespawn(1.0f, 0.0f, ratio);
            }
        }

        if (!yunaDamage.empty())
        {
            std::vector<Unit> survivors;
            survivors.reserve(yunas.size());
            for (std::size_t i = 0; i < yunas.size(); ++i)
            {
                Unit &yuna = yunas[i];
                if (yuna.hp <= 0.0f)
                {
                    enqueueYunaRespawn(0.0f);
                    continue;
                }
                if (yunaDamage[i] > 0.0f)
                {
                    const float hpBefore = yuna.hp;
                    yuna.hp -= yunaDamage[i];
                    if (yuna.hp <= 0.0f)
                    {
                        const float overkill = std::max(0.0f, yunaDamage[i] - std::max(hpBefore, 0.0f));
                        const float ratio = clampOverkillRatio(overkill, yunaStats.hp);
                        enqueueYunaRespawn(ratio);
                        continue;
                    }
                }
                survivors.push_back(yuna);
            }
            yunas.swap(survivors);
        }

        const float baseRadius = std::max(config.base_aabb.x, config.base_aabb.y) * 0.5f;
        for (EnemyUnit &enemy : enemies)
        {
            const float combined = baseRadius + enemy.radius;
            if (lengthSq(enemy.pos - basePos) <= combined * combined)
            {
                baseHp -= enemyDpsAgainstBase(enemy) * dt;
                if (baseHp <= 0.0f)
                {
                    baseHp = 0.0f;
                    setResult(GameResult::Defeat, "Defeat");
                    break;
                }
            }
        }

        walls.erase(std::remove_if(walls.begin(), walls.end(), [](const WallSegment &wall) { return wall.hp <= 0.0f; }), walls.end());
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

struct FramePerf
{
    float fps = 0.0f;
    float msUpdate = 0.0f;
    float msRender = 0.0f;
    int drawCalls = 0;
    int entities = 0;
};

Vec2 worldToScreen(const Vec2 &world, const Camera &camera)
{
    return {world.x - camera.position.x, world.y - camera.position.y};
}

Vec2 screenToWorld(int screenX, int screenY, const Camera &camera)
{
    return {static_cast<float>(screenX) + camera.position.x, static_cast<float>(screenY) + camera.position.y};
}

void drawFilledCircle(SDL_Renderer *renderer, const Vec2 &pos, float radius, RenderStats &stats)
{
    ++stats.drawCalls;
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

void drawTileLayer(SDL_Renderer *renderer, const TileMap &map, const std::vector<int> &tiles, const Camera &camera, int screenW,
                   int screenH, RenderStats &stats)
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
            countedRenderCopy(renderer, map.tileset, &src, &dst, stats);
        }
    }
}

void renderScene(SDL_Renderer *renderer, const Simulation &sim, const Camera &camera, const TextRenderer &font,
                 const TextRenderer &debugFont, const TileMap &map,
                 const Atlas &atlas, int screenW, int screenH, FramePerf &perf, bool showDebugHud)
{
    RenderStats stats;
    SDL_SetRenderDrawColor(renderer, 26, 32, 38, 255);
    countedRenderClear(renderer, stats);

    drawTileLayer(renderer, map, map.floor, camera, screenW, screenH, stats);
    if (map.tileset)
    {
        SDL_SetTextureColorMod(map.tileset, 190, 190, 200);
        drawTileLayer(renderer, map, map.block, camera, screenW, screenH, stats);
        SDL_SetTextureColorMod(map.tileset, 255, 255, 255);
    }
    drawTileLayer(renderer, map, map.deco, camera, screenW, screenH, stats);

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
            countedRenderCopy(renderer, atlas.texture, baseFrame, &dest, stats);
        }
        else
        {
            SDL_FRect baseRect{baseScreen.x - sim.config.base_aabb.x * 0.5f, baseScreen.y - sim.config.base_aabb.y * 0.5f, sim.config.base_aabb.x, sim.config.base_aabb.y};
            SDL_SetRenderDrawColor(renderer, 130, 90, 50, 255);
            countedRenderFillRectF(renderer, &baseRect, stats);
        }
    }
    else
    {
        SDL_FRect baseRect{baseScreen.x - sim.config.base_aabb.x * 0.5f, baseScreen.y - sim.config.base_aabb.y * 0.5f, sim.config.base_aabb.x, sim.config.base_aabb.y};
        SDL_SetRenderDrawColor(renderer, 130, 90, 50, 255);
        countedRenderFillRectF(renderer, &baseRect, stats);
    }

    const SDL_Rect *commanderFrame = nullptr;
    const SDL_Rect *yunaFrame = nullptr;
    const SDL_Rect *enemyFrame = nullptr;
    const SDL_Rect *wallbreakerFrame = nullptr;
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
    if (!sim.wallbreakerStats.spritePrefix.empty())
    {
        wallbreakerFrame = atlas.getFrame(sim.wallbreakerStats.spritePrefix + "_0");
    }
    const SDL_Rect *friendRing = atlas.getFrame("ring_friend");
    const SDL_Rect *enemyRing = atlas.getFrame("ring_enemy");

    struct FriendlySprite
    {
        float y = 0.0f;
        bool commander = false;
        std::size_t index = 0;
    };

    std::vector<Uint8> yunaAlpha(sim.yunas.size(), 255);
    const float crowdRadiusSq = 32.0f * 32.0f;
    for (std::size_t i = 0; i < sim.yunas.size(); ++i)
    {
        int neighbors = 0;
        for (std::size_t j = 0; j < sim.yunas.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }
            if (lengthSq(sim.yunas[i].pos - sim.yunas[j].pos) <= crowdRadiusSq)
            {
                ++neighbors;
                if (neighbors >= 4)
                {
                    yunaAlpha[i] = static_cast<Uint8>(255 * 0.3f);
                    break;
                }
            }
        }
    }

    std::vector<FriendlySprite> friendSprites;
    friendSprites.reserve(sim.yunas.size() + (sim.commander.alive ? 1 : 0));
    if (sim.commander.alive)
    {
        friendSprites.push_back(FriendlySprite{sim.commander.pos.y, true, 0});
    }
    for (std::size_t i = 0; i < sim.yunas.size(); ++i)
    {
        friendSprites.push_back(FriendlySprite{sim.yunas[i].pos.y, false, i});
    }
    std::sort(friendSprites.begin(), friendSprites.end(), [](const FriendlySprite &a, const FriendlySprite &b) {
        return a.y < b.y;
    });

    if (atlas.texture)
    {
        for (const FriendlySprite &sprite : friendSprites)
        {
            if (sprite.commander)
            {
                Vec2 commanderScreen = worldToScreen(sim.commander.pos, camera);
                if (commanderFrame)
                {
                    SDL_Rect dest{
                        static_cast<int>(commanderScreen.x - commanderFrame->w * 0.5f),
                        static_cast<int>(commanderScreen.y - commanderFrame->h * 0.5f),
                        commanderFrame->w,
                        commanderFrame->h};
                    countedRenderCopy(renderer, atlas.texture, commanderFrame, &dest, stats);
                    if (friendRing)
                    {
                        SDL_Rect ringDest{
                            dest.x + (dest.w - friendRing->w) / 2,
                            dest.y + dest.h - friendRing->h,
                            friendRing->w,
                            friendRing->h};
                        countedRenderCopy(renderer, atlas.texture, friendRing, &ringDest, stats);
                    }
                }
                else
                {
                    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
                    drawFilledCircle(renderer, commanderScreen, sim.commander.radius, stats);
                }
                continue;
            }

            const Unit &yuna = sim.yunas[sprite.index];
            Vec2 screenPos = worldToScreen(yuna.pos, camera);
            if (yunaFrame)
            {
                SDL_SetTextureAlphaMod(atlas.texture, yunaAlpha[sprite.index]);
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - yunaFrame->w * 0.5f),
                    static_cast<int>(screenPos.y - yunaFrame->h * 0.5f),
                    yunaFrame->w,
                    yunaFrame->h};
                countedRenderCopy(renderer, atlas.texture, yunaFrame, &dest, stats);
                SDL_SetTextureAlphaMod(atlas.texture, 255);
                if (friendRing)
                {
                    SDL_Rect ringDest{
                        dest.x + (dest.w - friendRing->w) / 2,
                        dest.y + dest.h - friendRing->h,
                        friendRing->w,
                        friendRing->h};
                    countedRenderCopy(renderer, atlas.texture, friendRing, &ringDest, stats);
                }
            }
            else
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 240, 190, 60, yunaAlpha[sprite.index]);
                drawFilledCircle(renderer, screenPos, yuna.radius, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }
        }
        SDL_SetTextureAlphaMod(atlas.texture, 255);
    }
    else
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const FriendlySprite &sprite : friendSprites)
        {
            if (sprite.commander)
            {
                Vec2 commanderScreen = worldToScreen(sim.commander.pos, camera);
                SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
                drawFilledCircle(renderer, commanderScreen, sim.commander.radius, stats);
                continue;
            }
            const Unit &yuna = sim.yunas[sprite.index];
            Vec2 screenPos = worldToScreen(yuna.pos, camera);
            SDL_SetRenderDrawColor(renderer, 240, 190, 60, yunaAlpha[sprite.index]);
            drawFilledCircle(renderer, screenPos, yuna.radius, stats);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_SetRenderDrawColor(renderer, 120, 150, 200, 255);
    for (const WallSegment &wall : sim.walls)
    {
        Vec2 screenPos = worldToScreen(wall.pos, camera);
        drawFilledCircle(renderer, screenPos, wall.radius, stats);
    }

    std::vector<std::size_t> enemyOrder(sim.enemies.size());
    std::iota(enemyOrder.begin(), enemyOrder.end(), 0);
    std::sort(enemyOrder.begin(), enemyOrder.end(), [&](std::size_t a, std::size_t b) {
        return sim.enemies[a].pos.y < sim.enemies[b].pos.y;
    });

    if (atlas.texture)
    {
        for (std::size_t idx : enemyOrder)
        {
            const EnemyUnit &enemy = sim.enemies[idx];
            const SDL_Rect *frame = enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerFrame : enemyFrame;
            Vec2 screenPos = worldToScreen(enemy.pos, camera);
            if (frame)
            {
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - frame->w * 0.5f),
                    static_cast<int>(screenPos.y - frame->h * 0.5f),
                    frame->w,
                    frame->h};
                countedRenderCopy(renderer, atlas.texture, frame, &dest, stats);
                if (enemyRing)
                {
                    SDL_Rect ringDest{
                        dest.x + (dest.w - enemyRing->w) / 2,
                        dest.y + dest.h - enemyRing->h,
                        enemyRing->w,
                        enemyRing->h};
                    countedRenderCopy(renderer, atlas.texture, enemyRing, &ringDest, stats);
                }
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, enemy.type == EnemyArchetype::Wallbreaker ? 200 : 80,
                                       enemy.type == EnemyArchetype::Wallbreaker ? 80 : 160,
                                       enemy.type == EnemyArchetype::Wallbreaker ? 80 : 220, 255);
                drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            }
        }
    }
    else
    {
        for (std::size_t idx : enemyOrder)
        {
            const EnemyUnit &enemy = sim.enemies[idx];
            SDL_SetRenderDrawColor(renderer, enemy.type == EnemyArchetype::Wallbreaker ? 200 : 80,
                                   enemy.type == EnemyArchetype::Wallbreaker ? 80 : 160,
                                   enemy.type == EnemyArchetype::Wallbreaker ? 80 : 220, 255);
            Vec2 screenPos = worldToScreen(enemy.pos, camera);
            drawFilledCircle(renderer, screenPos, enemy.radius, stats);
        }
    }

    // Ambient vignette overlay for dungeon mood
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 8, 24, 140);
    SDL_Rect overlay{0, 0, screenW, screenH};
    countedRenderFillRect(renderer, &overlay, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    const int lineHeight = std::max(font.getLineHeight(), 18);
    const int debugLineHeight = std::max(debugFont.isLoaded() ? debugFont.getLineHeight() : lineHeight, 14);

    auto measureWithFallback = [](const TextRenderer &renderer, const std::string &text, int approxHeight) {
        const int measured = renderer.measureText(text);
        if (measured > 0)
        {
            return measured;
        }
        const int approxWidth = std::max(approxHeight / 2, 8);
        return static_cast<int>(text.size()) * approxWidth;
    };
    const int baseHpInt = static_cast<int>(std::round(std::max(sim.baseHp, 0.0f)));
    const float hpRatio = sim.config.base_hp > 0 ? std::clamp(baseHpInt / static_cast<float>(sim.config.base_hp), 0.0f, 1.0f) : 0.0f;
    SDL_Rect barBg{screenW / 2 - 160, 20, 320, 20};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 28, 22, 40, 200);
    countedRenderFillRect(renderer, &barBg, stats);
    SDL_Rect barFill{barBg.x + 4, barBg.y + 4, static_cast<int>((barBg.w - 8) * hpRatio), barBg.h - 8};
    SDL_SetRenderDrawColor(renderer, 255, 166, 64, 230);
    countedRenderFillRect(renderer, &barFill, stats);
    SDL_SetRenderDrawColor(renderer, 90, 70, 120, 230);
    countedRenderDrawRect(renderer, &barBg, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    font.drawText(renderer, "Base HP", barBg.x, barBg.y - lineHeight, &stats);
    font.drawText(renderer, std::to_string(baseHpInt), barBg.x + barBg.w + 12, barBg.y - 2, &stats);

    const int commanderHpInt = static_cast<int>(std::round(std::max(sim.commander.hp, 0.0f)));
    std::vector<std::string> infoLines;
    infoLines.push_back("Allies: " + std::to_string(static_cast<int>(sim.yunas.size())));
    if (sim.commander.alive)
    {
        infoLines.push_back("Commander HP: " + std::to_string(commanderHpInt));
    }
    else
    {
        infoLines.push_back("Commander: Down");
    }
    infoLines.push_back("Enemies: " + std::to_string(static_cast<int>(sim.enemies.size())));
    if (!sim.commander.alive)
    {
        std::ostringstream respawnText;
        respawnText << "Commander respawn in " << std::fixed << std::setprecision(1) << sim.commanderRespawnTimer << "s";
        infoLines.push_back(respawnText.str());
    }
    infoLines.push_back("");
    infoLines.push_back(std::string("Stance (F1-F4): ") + stanceLabel(sim.stance));
    infoLines.push_back(std::string("Formation (Z/X): ") + formationLabel(sim.formation));
    infoLines.push_back("");
    infoLines.push_back("Skills (Right Click):");
    for (std::size_t i = 0; i < sim.skills.size(); ++i)
    {
        const RuntimeSkill &skill = sim.skills[i];
        std::ostringstream skillLabel;
        skillLabel << (static_cast<int>(i) == sim.selectedSkill ? "> " : "  ");
        skillLabel << skill.def.hotkey << ": " << skill.def.displayName;
        if (skill.cooldownRemaining > 0.0f)
        {
            skillLabel << " [" << std::fixed << std::setprecision(1) << skill.cooldownRemaining << "s]";
        }
        else if (skill.def.type == SkillType::SpawnRate && skill.activeTimer > 0.0f)
        {
            skillLabel << " (active " << std::fixed << std::setprecision(1) << skill.activeTimer << "s)";
        }
        infoLines.push_back(skillLabel.str());
    }

    if (!infoLines.empty())
    {
        int infoPanelWidth = 0;
        for (const std::string &line : infoLines)
        {
            infoPanelWidth = std::max(infoPanelWidth, measureWithFallback(font, line, lineHeight));
        }
        const int infoPadding = 8;
        const int infoPanelHeight = static_cast<int>(infoLines.size()) * lineHeight + infoPadding * 2;
        SDL_Rect infoPanel{12, barBg.y + barBg.h + 20, infoPanelWidth + infoPadding * 2, infoPanelHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        countedRenderFillRect(renderer, &infoPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        int infoY = infoPanel.y + infoPadding;
        for (const std::string &line : infoLines)
        {
            if (!line.empty())
            {
                font.drawText(renderer, line, infoPanel.x + infoPadding, infoY, &stats);
            }
            infoY += lineHeight;
        }
    }

    int topRightAnchorY = 12;
    if (showDebugHud)
    {
        std::vector<std::string> perfLines;
        std::ostringstream line1;
        line1 << std::fixed << std::setprecision(1) << "FPS " << perf.fps << "  Ents " << perf.entities;
        perfLines.push_back(line1.str());
        std::ostringstream line2;
        line2 << std::fixed << std::setprecision(2) << "Upd " << perf.msUpdate << "ms  Ren " << perf.msRender << "ms";
        perfLines.push_back(line2.str());
        std::ostringstream line3;
        line3 << "Draw " << perf.drawCalls;
        perfLines.push_back(line3.str());

        int debugWidth = 0;
        for (const std::string &line : perfLines)
        {
            debugWidth = std::max(debugWidth, measureWithFallback(debugFont, line, debugLineHeight));
        }
        const int debugPadX = 10;
        const int debugPadY = 8;
        SDL_Rect debugPanel{screenW - (debugWidth + debugPadX * 2) - 12, topRightAnchorY,
                            debugWidth + debugPadX * 2, static_cast<int>(perfLines.size()) * debugLineHeight + debugPadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        countedRenderFillRect(renderer, &debugPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        int debugY = debugPanel.y + debugPadY;
        for (const std::string &line : perfLines)
        {
            debugFont.drawText(renderer, line, debugPanel.x + debugPadX, debugY, &stats);
            debugY += debugLineHeight;
        }
        topRightAnchorY += debugPanel.h + 12;
    }

    if (!sim.hud.telemetryText.empty() && sim.hud.telemetryTimer > 0.0f)
    {
        const int telePadX = 12;
        const int telePadY = 6;
        const int textWidth = measureWithFallback(font, sim.hud.telemetryText, lineHeight);
        SDL_Rect telePanel{screenW - (textWidth + telePadX * 2) - 12, topRightAnchorY,
                           textWidth + telePadX * 2, lineHeight + telePadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        countedRenderFillRect(renderer, &telePanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, sim.hud.telemetryText, telePanel.x + telePadX, telePanel.y + telePadY, &stats);
        topRightAnchorY += telePanel.h + 12;
    }
    if (!sim.hud.resultText.empty() && sim.hud.resultTimer > 0.0f)
    {
        const int resultPadX = 24;
        const int resultPadY = 12;
        const int textWidth = measureWithFallback(font, sim.hud.resultText, lineHeight);
        SDL_Rect resultPanel{screenW / 2 - (textWidth + resultPadX * 2) / 2,
                             screenH / 2 - lineHeight - resultPadY,
                             textWidth + resultPadX * 2,
                             lineHeight + resultPadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        countedRenderFillRect(renderer, &resultPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, sim.hud.resultText, resultPanel.x + resultPadX, resultPanel.y + resultPadY, &stats);
    }

    perf.drawCalls = stats.drawCalls;
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
    auto skillsOpt = parseSkillCatalog("assets/skills.json");

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
    std::vector<SkillDef> skillDefs = skillsOpt ? *skillsOpt : buildDefaultSkills();
    sim.configureSkills(skillDefs);
    sim.reset();

    if (TTF_Init() != 0)
    {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    TextRenderer hudFont;
    if (!hudFont.load("assets/ui/NotoSansJP-Regular.ttf", 22))
    {
        std::cerr << "Failed to load HUD font (NotoSansJP-Regular.ttf).\n";
    }
    TextRenderer debugFont;
    if (!debugFont.load("assets/ui/NotoSansJP-Regular.ttf", 18))
    {
        std::cerr << "Failed to load debug font fallback, using HUD font size.\n";
    }

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
    FramePerf framePerf;
    framePerf.fps = currentFps;
    double perfLogTimer = 0.0;
    double updateAccum = 0.0;
    double renderAccum = 0.0;
    double entityAccum = 0.0;
    int perfLogFrames = 0;
    bool showDebugHud = false;

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
                case SDL_SCANCODE_F1:
                    sim.setStance(ArmyStance::RushNearest);
                    break;
                case SDL_SCANCODE_F2:
                    sim.setStance(ArmyStance::PushForward);
                    break;
                case SDL_SCANCODE_F3:
                    sim.setStance(ArmyStance::FollowLeader);
                    break;
                case SDL_SCANCODE_F4:
                    sim.setStance(ArmyStance::DefendBase);
                    break;
                case SDL_SCANCODE_F10:
                    showDebugHud = !showDebugHud;
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
                case SDL_SCANCODE_Z:
                    sim.cycleFormation(-1);
                    break;
                case SDL_SCANCODE_X:
                    sim.cycleFormation(1);
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
                case SDL_SCANCODE_1:
                case SDL_SCANCODE_2:
                case SDL_SCANCODE_3:
                case SDL_SCANCODE_4:
                {
                    const int hotkey = static_cast<int>(event.key.keysym.scancode - SDL_SCANCODE_1) + 1;
                    sim.selectSkillByHotkey(hotkey);
                    break;
                }
                default:
                    break;
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                Vec2 worldPos = screenToWorld(event.button.x, event.button.y, camera);
                sim.activateSelectedSkill(worldPos);
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
        const Uint64 updateStart = SDL_GetPerformanceCounter();
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
        const Uint64 updateEnd = SDL_GetPerformanceCounter();
        const double updateMs = (updateEnd - updateStart) * 1000.0 / frequency;
        framePerf.msUpdate = static_cast<float>(updateMs);

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
        framePerf.fps = currentFps;
        framePerf.entities = static_cast<int>(sim.yunas.size() + sim.enemies.size() + (sim.commander.alive ? 1 : 0));
        const Uint64 renderStart = SDL_GetPerformanceCounter();
        renderScene(renderer, sim, renderCamera, hudFont, debugFont, tileMap, atlas, screenWidth, screenHeight, framePerf,
                     showDebugHud);
        const Uint64 renderEnd = SDL_GetPerformanceCounter();
        const double renderMs = (renderEnd - renderStart) * 1000.0 / frequency;
        framePerf.msRender = static_cast<float>(renderMs);

        perfLogTimer += frameTime;
        updateAccum += updateMs;
        renderAccum += renderMs;
        entityAccum += static_cast<double>(framePerf.entities);
        ++perfLogFrames;
        if (perfLogTimer >= 1.0 && perfLogFrames > 0)
        {
            const double avgFps = static_cast<double>(perfLogFrames) / perfLogTimer;
            const double avgUpdate = updateAccum / perfLogFrames;
            const double avgRender = renderAccum / perfLogFrames;
            const double avgEntities = entityAccum / perfLogFrames;
            const bool spike = (avgUpdate + avgRender) > 9.0;
            std::ostringstream logLine;
            logLine << std::fixed << std::setprecision(1) << "fps=" << avgFps << " ents="
                    << static_cast<int>(std::round(avgEntities));
            if (spike)
            {
                logLine << " ★";
            }
            std::cout << logLine.str() << '\n';
            std::cout.flush();
            perfLogTimer = 0.0;
            updateAccum = 0.0;
            renderAccum = 0.0;
            entityAccum = 0.0;
            perfLogFrames = 0;
        }
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
    hudFont.unload();
    debugFont.unload();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}

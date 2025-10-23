#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include "app/GameApplication.h"
#include "assets/AssetManager.h"
#include "config/AppConfig.h"
#include "config/AppConfigLoader.h"
#include "json/JsonUtils.h"
#include "scenes/Scene.h"
#include "scenes/SceneStack.h"
#include "events/EventBus.h"
#include "services/ServiceLocator.h"
#include "telemetry/TelemetrySink.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
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

using namespace json;

std::optional<JsonValue> loadJsonDocument(AssetManager &assets, const std::string &path,
                                          AssetManager::AssetLoadStatus *outStatus = nullptr)
{
    auto document = assets.acquireJson(path);
    AssetManager::AssetLoadStatus status = document.status();
    if (outStatus)
    {
        *outStatus = status;
    }
    if (!document.get())
    {
        return std::nullopt;
    }
    JsonValue value = *document.get();
    return value;
}



enum class TemperamentBehavior
{
    ChargeNearest,
    FleeNearest,
    FollowYuna,
    RaidGate,
    Homebound,
    Wander,
    Doze,
    GuardBase,
    TargetTag,
    Mimic
};

const char *temperamentBehaviorName(TemperamentBehavior behavior)
{
    switch (behavior)
    {
    case TemperamentBehavior::ChargeNearest: return "Charge";
    case TemperamentBehavior::FleeNearest: return "Flee";
    case TemperamentBehavior::FollowYuna: return "Follow";
    case TemperamentBehavior::RaidGate: return "Raid";
    case TemperamentBehavior::Homebound: return "Home";
    case TemperamentBehavior::Wander: return "Wander";
    case TemperamentBehavior::Doze: return "Doze";
    case TemperamentBehavior::GuardBase: return "Guard";
    case TemperamentBehavior::TargetTag: return "Target";
    case TemperamentBehavior::Mimic: return "Mimic";
    }
    return "Unknown";
}

struct RuntimeSkill
{
    SkillDef def;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
};

struct TemperamentState
{
    const TemperamentDefinition *definition = nullptr;
    TemperamentBehavior currentBehavior = TemperamentBehavior::Wander;
    TemperamentBehavior lastBehavior = TemperamentBehavior::Wander;
    bool mimicActive = false;
    TemperamentBehavior mimicBehavior = TemperamentBehavior::Wander;
    float mimicCooldown = 0.0f;
    float mimicDuration = 0.0f;
    Vec2 wanderDirection{1.0f, 0.0f};
    float wanderTimer = 0.0f;
    float sleepTimer = 0.0f;
    float sleepRemaining = 0.0f;
    bool sleeping = false;
    float catchupTimer = 0.0f;
    float cryTimer = 0.0f;
    float cryPauseTimer = 0.0f;
    bool crying = false;
    float panicTimer = 0.0f;
    float chargeDashTimer = 0.0f;
};

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

const char *orderLabel(ArmyStance stance)
{
    switch (stance)
    {
    case ArmyStance::RushNearest: return "突撃";
    case ArmyStance::PushForward: return "前進";
    case ArmyStance::FollowLeader: return "追従";
    case ArmyStance::DefendBase: return "防衛";
    }
    return "";
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
    TemperamentState temperament;
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
    float speedPx = 0.0f;
    float dpsUnit = 0.0f;
    float dpsBase = 0.0f;
    float dpsWall = 0.0f;
    bool noOverlap = false;
};

struct WallSegment
{
    Vec2 pos;
    float hp = 0.0f;
    float life = 0.0f;
    float radius = 0.0f;
};

struct GateRuntime
{
    std::string id;
    Vec2 pos;
    float radius = 24.0f;
    float hp = 0.0f;
    float maxHp = 0.0f;
    bool destroyed = false;
};

struct ActiveSpawn
{
    Vec2 position;
    int remaining = 0;
    float interval = 0.3f;
    float timer = 0.0f;
    EnemyArchetype type = EnemyArchetype::Slime;
    std::string gateId;
};

class TextRenderer
{
  public:
    TextRenderer() = default;
    ~TextRenderer() { unload(); }

    bool load(AssetManager &assets, const std::string &fontPath, int pointSize)
    {
        unload();
        auto fontRef = assets.acquireFont(fontPath, pointSize);
        if (!fontRef.get())
        {
            if (!fontRef.status().message.empty())
            {
                std::cerr << fontRef.status().message << '\n';
            }
            return false;
        }
        lineHeight = TTF_FontLineSkip(fontRef.getRaw());
        font = std::move(fontRef);
        return lineHeight > 0;
    }

    void unload()
    {
        font.reset();
        lineHeight = 0;
    }

    int getLineHeight() const { return lineHeight; }

    int measureText(const std::string &text) const
    {
        if (!font.get())
        {
            return 0;
        }
        int width = 0;
        if (TTF_SizeUTF8(font.getRaw(), text.c_str(), &width, nullptr) != 0)
        {
            return 0;
        }
        return width;
    }

    void drawText(SDL_Renderer *renderer, const std::string &text, int x, int y, RenderStats *stats = nullptr,
                  SDL_Color color = {255, 255, 255, 255}) const
    {
        if (!font.get() || text.empty())
        {
            return;
        }
        SDL_Surface *surface = TTF_RenderUTF8_Blended(font.getRaw(), text.c_str(), color);
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

    bool isLoaded() const { return static_cast<bool>(font.get()); }

  private:
    AssetManager::FontReference font;
    int lineHeight = 0;
};

struct TileMap
{
    int width = 0;
    int height = 0;
    int tileWidth = 16;
    int tileHeight = 16;
    int tilesetColumns = 1;
    AssetManager::TextureReference tileset;
    std::vector<int> floor;
    std::vector<int> block;
    std::vector<int> deco;
};

struct Atlas
{
    AssetManager::TextureReference texture;
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

bool loadTileMap(AssetManager &assets, const std::string &tmxPath, TileMap &outMap)
{
    const std::string resolvedPath = assets.resolvePath(tmxPath);
    std::ifstream file(resolvedPath);
    if (!file.is_open())
    {
        std::cerr << "Failed to open TMX: " << resolvedPath << '\n';
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

    std::string baseDir = parentDirectory(resolvedPath);
    std::string imagePath = baseDir.empty() ? *sourceAttr : baseDir + "/" + *sourceAttr;
    auto tilesetTexture = assets.acquireTexture(imagePath);
    AssetManager::AssetLoadStatus textureStatus = tilesetTexture.status();
    if (!tilesetTexture.get())
    {
        if (!textureStatus.message.empty())
        {
            std::cerr << textureStatus.message << '\n';
        }
        else
        {
            std::cerr << "Failed to load tileset texture: " << imagePath << '\n';
        }
        return false;
    }
    if (!textureStatus.ok && !textureStatus.message.empty())
    {
        std::cerr << textureStatus.message << '\n';
    }
    outMap.tileset = std::move(tilesetTexture);

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
    return textureStatus.ok;
}

bool loadAtlas(AssetManager &assets, const std::string &atlasPath, Atlas &atlas)
{
    AssetManager::AssetLoadStatus jsonStatus;
    auto json = loadJsonDocument(assets, atlasPath, &jsonStatus);
    if (!json)
    {
        if (!jsonStatus.message.empty())
        {
            std::cerr << jsonStatus.message << '\n';
        }
        else
        {
            std::cerr << "Failed to load atlas json: " << atlasPath << '\n';
        }
        return false;
    }
    if (!jsonStatus.ok && !jsonStatus.message.empty())
    {
        std::cerr << jsonStatus.message << '\n';
    }
    atlas.frames.clear();

    const JsonValue *meta = getObjectField(*json, "meta");
    std::string imageName = "atlas.png";
    if (meta)
    {
        imageName = getString(*meta, "image", imageName);
    }
    const std::string resolvedAtlas = assets.resolvePath(atlasPath);
    std::string baseDir = parentDirectory(resolvedAtlas);
    std::string imagePath = baseDir.empty() ? imageName : baseDir + "/" + imageName;
    auto textureRef = assets.acquireTexture(imagePath);
    AssetManager::AssetLoadStatus textureStatus = textureRef.status();
    if (!textureRef.get())
    {
        if (!textureStatus.message.empty())
        {
            std::cerr << textureStatus.message << '\n';
        }
        else
        {
            std::cerr << "Failed to load atlas texture: " << imagePath << '\n';
        }
        return false;
    }
    if (!textureStatus.ok && !textureStatus.message.empty())
    {
        std::cerr << textureStatus.message << '\n';
    }
    atlas.texture = std::move(textureRef);

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
    return textureStatus.ok;
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
    TemperamentConfig temperamentConfig;
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
    std::vector<GateRuntime> gates;
    std::vector<RuntimeSkill> skills;
    Vec2 worldMin{0.0f, 0.0f};
    Vec2 worldMax{1280.0f, 720.0f};
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

    bool hasMission = false;
    MissionConfig missionConfig;
    MissionMode missionMode = MissionMode::None;
    MissionUIOptions missionUI;
    MissionFailConditions missionFail;
    float missionTimer = 0.0f;
    float missionVictoryCountdown = -1.0f;

    struct BossRuntime
    {
        bool active = false;
        float hp = 0.0f;
        float maxHp = 0.0f;
        float speedPx = 0.0f;
        float radius = 0.0f;
        MissionBossMechanic mechanic;
        float cycleTimer = 0.0f;
        float windupTimer = 0.0f;
        bool inWindup = false;
    } boss;

    struct CaptureRuntime
    {
        MissionCaptureZone config;
        Vec2 worldPos{0.0f, 0.0f};
        float progress = 0.0f;
        bool captured = false;
    };
    std::vector<CaptureRuntime> captureZones;
    int capturedZones = 0;
    int captureGoal = 0;

    struct SurvivalRuntime
    {
        float elapsed = 0.0f;
        float duration = 0.0f;
        float pacingTimer = 0.0f;
        float spawnMultiplier = 1.0f;
        std::vector<MissionSurvivalElite> elites;
        std::size_t nextElite = 0;
    } survival;

    std::unordered_set<std::string> disabledGates;

    ArmyStance stance = ArmyStance::RushNearest;
    ArmyStance defaultStance = ArmyStance::RushNearest;
    bool orderActive = false;
    float orderTimer = 0.0f;
    float orderDuration = 10.0f;
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

    void setWorldBounds(float width, float height)
    {
        if (width <= 0.0f || height <= 0.0f)
        {
            worldMin = {0.0f, 0.0f};
            worldMax = {1280.0f, 720.0f};
            return;
        }
        worldMin = {0.0f, 0.0f};
        worldMax = {width, height};
    }

    void clampToWorld(Vec2 &pos, float radius) const
    {
        const float minX = worldMin.x + radius;
        const float maxX = worldMax.x - radius;
        const float minY = worldMin.y + radius;
        const float maxY = worldMax.y - radius;
        if (minX <= maxX)
        {
            pos.x = std::clamp(pos.x, minX, maxX);
        }
        if (minY <= maxY)
        {
            pos.y = std::clamp(pos.y, minY, maxY);
        }
    }

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
        stance = defaultStance;
        orderActive = false;
        orderTimer = 0.0f;
        orderDuration = temperamentConfig.orderDuration;
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
        stance = defaultStance;
        formation = Formation::Swarm;
        selectedSkill = 0;
        for (RuntimeSkill &skill : skills)
        {
            skill.cooldownRemaining = 0.0f;
            skill.activeTimer = 0.0f;
        }
        yunaRespawnTimers.clear();
        rebuildGates();
        initializeMissionState();
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

    Vec2 randomUnitVector()
    {
        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265358979323846f);
        const float angle = angleDist(rng);
        return {std::cos(angle), std::sin(angle)};
    }

    float randomRange(const TemperamentRange &range)
    {
        if (range.max <= range.min)
        {
            return range.min;
        }
        std::uniform_real_distribution<float> dist(range.min, range.max);
        return dist(rng);
    }

    const TemperamentDefinition *selectTemperamentDefinition()
    {
        if (temperamentConfig.definitions.empty())
        {
            return nullptr;
        }
        if (temperamentConfig.cumulativeWeights.empty())
        {
            return &temperamentConfig.definitions.front();
        }
        const float total = temperamentConfig.cumulativeWeights.back();
        if (total <= 0.0f)
        {
            return &temperamentConfig.definitions.front();
        }
        std::uniform_real_distribution<float> dist(0.0f, total);
        const float roll = dist(rng);
        auto it = std::lower_bound(temperamentConfig.cumulativeWeights.begin(), temperamentConfig.cumulativeWeights.end(), roll);
        if (it == temperamentConfig.cumulativeWeights.end())
        {
            return &temperamentConfig.definitions.back();
        }
        std::size_t index = static_cast<std::size_t>(std::distance(temperamentConfig.cumulativeWeights.begin(), it));
        if (index >= temperamentConfig.definitions.size())
        {
            index = temperamentConfig.definitions.size() - 1;
        }
        return &temperamentConfig.definitions[index];
    }

    void assignTemperament(Unit &yuna)
    {
        yuna.temperament = {};
        const TemperamentDefinition *def = selectTemperamentDefinition();
        yuna.temperament.definition = def;
        if (!def)
        {
            return;
        }
        TemperamentState &state = yuna.temperament;
        if (def->behavior == TemperamentBehavior::Mimic)
        {
            state.currentBehavior = def->mimicDefault;
            state.mimicActive = false;
            state.mimicBehavior = def->mimicDefault;
            state.mimicCooldown = randomRange(def->mimicEvery);
            state.mimicDuration = 0.0f;
        }
        else
        {
            state.currentBehavior = def->behavior;
        }
        state.lastBehavior = state.currentBehavior;
        state.wanderDirection = randomUnitVector();
        state.wanderTimer = randomRange(temperamentConfig.wanderTurnInterval);
        state.sleepTimer = randomRange(temperamentConfig.sleepEvery);
        state.sleepRemaining = temperamentConfig.sleepDuration;
        state.sleeping = false;
        state.catchupTimer = 0.0f;
        state.cryTimer = def->cryPauseEvery.max > 0.0f ? randomRange(def->cryPauseEvery) : 0.0f;
        state.cryPauseTimer = 0.0f;
        state.crying = false;
        state.panicTimer = 0.0f;
        state.chargeDashTimer = state.currentBehavior == TemperamentBehavior::ChargeNearest ? temperamentConfig.chargeDash.duration : 0.0f;
    }

    void spawnYunaUnit()
    {
        Unit yuna;
        yuna.pos = yunaSpawnPos;
        yuna.pos.y += scatterY(rng);
        yuna.hp = yunaStats.hp;
        yuna.radius = yunaStats.radius;
        yunas.push_back(yuna);
        assignTemperament(yunas.back());
        clampToWorld(yunas.back().pos, yunas.back().radius);
    }

    GateRuntime *findGate(const std::string &id)
    {
        for (GateRuntime &gate : gates)
        {
            if (gate.id == id)
            {
                return &gate;
            }
        }
        return nullptr;
    }

    const GateRuntime *findGate(const std::string &id) const
    {
        for (const GateRuntime &gate : gates)
        {
            if (gate.id == id)
            {
                return &gate;
            }
        }
        return nullptr;
    }

    void destroyGate(GateRuntime &gate, bool silent = false)
    {
        if (gate.destroyed)
        {
            return;
        }
        gate.destroyed = true;
        gate.hp = 0.0f;
        disabledGates.insert(gate.id);
        activeSpawns.erase(std::remove_if(activeSpawns.begin(), activeSpawns.end(),
                                          [&](const ActiveSpawn &spawn) { return spawn.gateId == gate.id; }),
                          activeSpawns.end());
        if (!silent)
        {
            pushTelemetry(std::string("Gate ") + gate.id + " destroyed!");
        }
    }

    void rebuildGates()
    {
        gates.clear();
        auto upsertGate = [&](const std::string &id, const Vec2 &tile) {
            if (id.empty())
            {
                return;
            }
            Vec2 world = tileToWorld(tile, mapDefs.tile_size);
            for (GateRuntime &gate : gates)
            {
                if (gate.id == id)
                {
                    gate.pos = world;
                    gate.radius = config.gate_radius;
                    gate.maxHp = config.gate_hp;
                    gate.hp = gate.maxHp;
                    gate.destroyed = false;
                    return;
                }
            }
            GateRuntime gate;
            gate.id = id;
            gate.pos = world;
            gate.radius = config.gate_radius;
            gate.maxHp = config.gate_hp;
            gate.hp = gate.maxHp;
            gate.destroyed = false;
            gates.push_back(gate);
        };

        for (const auto &kv : mapDefs.gate_tiles)
        {
            upsertGate(kv.first, kv.second);
        }
        for (const auto &kv : spawnScript.gate_tiles)
        {
            upsertGate(kv.first, kv.second);
        }
    }

    void disableGate(const std::string &gate)
    {
        if (gate.empty())
        {
            return;
        }
        disabledGates.insert(gate);
        if (GateRuntime *runtime = findGate(gate))
        {
            destroyGate(*runtime, true);
        }
        activeSpawns.erase(std::remove_if(activeSpawns.begin(), activeSpawns.end(),
                                          [&](const ActiveSpawn &spawn) { return spawn.gateId == gate; }),
                          activeSpawns.end());
    }

    void initializeMissionState()
    {
        missionTimer = 0.0f;
        missionVictoryCountdown = -1.0f;
        boss = {};
        captureZones.clear();
        capturedZones = 0;
        captureGoal = 0;
        survival = {};
        disabledGates.clear();

        if (!hasMission)
        {
            missionMode = MissionMode::None;
            missionUI = {};
            missionFail = {};
            return;
        }

        missionMode = missionConfig.mode;
        missionUI = missionConfig.ui;
        missionFail = missionConfig.fail;

        if (missionMode == MissionMode::Boss)
        {
            spawnMissionBoss();
        }
        if (missionMode == MissionMode::Capture)
        {
            for (const MissionCaptureZone &zone : missionConfig.captureZones)
            {
                CaptureRuntime runtime;
                runtime.config = zone;
                runtime.worldPos = tileToWorld(zone.tile, mapDefs.tile_size);
                captureZones.push_back(runtime);
            }
            captureGoal = missionConfig.win.requireCaptured > 0
                              ? missionConfig.win.requireCaptured
                              : static_cast<int>(captureZones.size());
        }
        if (missionMode == MissionMode::Survival)
        {
            survival.duration = missionConfig.survival.duration > 0.0f ? missionConfig.survival.duration : missionConfig.win.surviveTime;
            survival.spawnMultiplier = 1.0f;
            survival.pacingTimer = missionConfig.survival.pacingStep;
            survival.elites = missionConfig.survival.elites;
            survival.nextElite = 0;
        }
    }

    void spawnMissionBoss()
    {
        if (missionConfig.boss.hp <= 0.0f)
        {
            return;
        }
        Vec2 world = tileToWorld(missionConfig.boss.tile, mapDefs.tile_size);
        EnemyUnit bossUnit;
        bossUnit.type = EnemyArchetype::Boss;
        bossUnit.pos = world;
        bossUnit.hp = missionConfig.boss.hp;
        bossUnit.radius = missionConfig.boss.radius_px > 0.0f ? missionConfig.boss.radius_px : 32.0f;
        bossUnit.speedPx = missionConfig.boss.speed_u_s * config.pixels_per_unit;
        bossUnit.dpsUnit = slimeStats.dps;
        bossUnit.dpsBase = slimeStats.dps;
        bossUnit.dpsWall = slimeStats.dps;
        bossUnit.noOverlap = missionConfig.boss.noOverlap;
        enemies.push_back(bossUnit);
        boss.active = true;
        boss.hp = bossUnit.hp;
        boss.maxHp = bossUnit.hp;
        boss.speedPx = bossUnit.speedPx;
        boss.radius = bossUnit.radius;
        boss.mechanic = missionConfig.boss.slam;
        boss.cycleTimer = boss.mechanic.period;
        boss.windupTimer = 0.0f;
        boss.inWindup = false;
        timeSinceLastEnemySpawn = 0.0f;
    }

    void performBossSlam(const EnemyUnit &bossEnemy)
    {
        if (boss.mechanic.radius <= 0.0f || boss.mechanic.damage <= 0.0f)
        {
            return;
        }
        const float radiusSq = boss.mechanic.radius * boss.mechanic.radius;
        bool hitSomething = false;

        if (commander.alive && lengthSq(commander.pos - bossEnemy.pos) <= radiusSq)
        {
            const float hpBefore = commander.hp;
            commander.hp -= boss.mechanic.damage;
            Vec2 push = normalize(commander.pos - bossEnemy.pos) * 48.0f;
            if (lengthSq(push) > 0.0f)
            {
                commander.pos += push;
                clampToWorld(commander.pos, commanderStats.radius);
            }
            if (commander.hp <= 0.0f)
            {
                const float overkill = std::max(0.0f, boss.mechanic.damage - std::max(hpBefore, 0.0f));
                const float ratio = clampOverkillRatio(overkill, commanderStats.hp);
                scheduleCommanderRespawn(1.0f, 0.0f, ratio);
            }
            hitSomething = true;
        }

        if (!yunas.empty())
        {
            std::vector<Unit> survivors;
            survivors.reserve(yunas.size());
            for (Unit &yuna : yunas)
            {
                if (lengthSq(yuna.pos - bossEnemy.pos) <= radiusSq)
                {
                    Vec2 push = normalize(yuna.pos - bossEnemy.pos) * 40.0f;
                    if (lengthSq(push) > 0.0f)
                    {
                        yuna.pos += push;
                        clampToWorld(yuna.pos, yuna.radius);
                    }
                    const float hpBefore = yuna.hp;
                    yuna.hp -= boss.mechanic.damage;
                    if (yuna.hp <= 0.0f)
                    {
                        const float overkill = std::max(0.0f, boss.mechanic.damage - std::max(hpBefore, 0.0f));
                        const float ratio = clampOverkillRatio(overkill, yunaStats.hp);
                        enqueueYunaRespawn(ratio);
                        hitSomething = true;
                        continue;
                    }
                    hitSomething = true;
                }
                survivors.push_back(yuna);
            }
            yunas.swap(survivors);
        }

        if (hitSomething)
        {
            pushTelemetry("Boss Slam!");
        }
    }

    void spawnMissionElite(const MissionSurvivalElite &elite)
    {
        if (disabledGates.find(elite.gate) != disabledGates.end())
        {
            return;
        }
        Vec2 gateTile{};
        bool foundGate = false;
        if (auto scriptGate = spawnScript.gate_tiles.find(elite.gate); scriptGate != spawnScript.gate_tiles.end())
        {
            gateTile = scriptGate->second;
            foundGate = true;
        }
        else if (auto mapGate = mapDefs.gate_tiles.find(elite.gate); mapGate != mapDefs.gate_tiles.end())
        {
            gateTile = mapGate->second;
            foundGate = true;
        }
        if (!foundGate)
        {
            return;
        }
        Vec2 world = tileToWorld(gateTile, mapDefs.tile_size);
        spawnOneEnemy(world, elite.type);
    }

    void updateBossMechanics(float dt)
    {
        if (!boss.active)
        {
            return;
        }
        EnemyUnit *bossEnemy = nullptr;
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.type == EnemyArchetype::Boss)
            {
                bossEnemy = &enemy;
                break;
            }
        }
        if (!bossEnemy)
        {
            boss.active = false;
            if (missionVictoryCountdown < 0.0f)
            {
                missionVictoryCountdown = std::max(config.victory_grace, 5.0f);
                pushTelemetry("Boss defeated!");
            }
            return;
        }
        boss.hp = bossEnemy->hp;
        bossEnemy->speedPx = boss.speedPx;
        if (boss.mechanic.period > 0.0f)
        {
            boss.cycleTimer -= dt;
            if (!boss.inWindup && boss.mechanic.windup > 0.0f && boss.cycleTimer <= boss.mechanic.windup)
            {
                boss.inWindup = true;
                boss.windupTimer = boss.mechanic.windup;
            }
            if (boss.inWindup)
            {
                boss.windupTimer -= dt;
                if (boss.windupTimer <= 0.0f)
                {
                    performBossSlam(*bossEnemy);
                    boss.inWindup = false;
                    boss.cycleTimer = boss.mechanic.period;
                }
            }
            else if (boss.mechanic.windup <= 0.0f && boss.cycleTimer <= 0.0f)
            {
                performBossSlam(*bossEnemy);
                boss.cycleTimer = boss.mechanic.period;
            }
            else if (boss.cycleTimer <= 0.0f)
            {
                boss.cycleTimer = boss.mechanic.period;
            }
        }
    }

    void updateCaptureMission(float dt)
    {
        for (CaptureRuntime &zone : captureZones)
        {
            if (zone.captured)
            {
                continue;
            }
            const float radiusSq = zone.config.radius_px * zone.config.radius_px;
            int allies = 0;
            for (const Unit &yuna : yunas)
            {
                if (lengthSq(yuna.pos - zone.worldPos) <= radiusSq)
                {
                    ++allies;
                }
            }
            if (commander.alive && lengthSq(commander.pos - zone.worldPos) <= radiusSq)
            {
                ++allies;
            }
            int foes = 0;
            for (const EnemyUnit &enemy : enemies)
            {
                if (lengthSq(enemy.pos - zone.worldPos) <= radiusSq)
                {
                    ++foes;
                }
            }
            if (foes == 0 && allies > 0)
            {
                if (zone.config.capture_s > 0.0f)
                {
                    zone.progress += dt / zone.config.capture_s;
                }
                else
                {
                    zone.progress = 1.0f;
                }
            }
            else if (zone.config.decay_s > 0.0f)
            {
                zone.progress -= dt / zone.config.decay_s;
            }
            zone.progress = std::clamp(zone.progress, 0.0f, 1.0f);
            if (!zone.captured && zone.progress >= 1.0f)
            {
                zone.captured = true;
                ++capturedZones;
                if (!zone.config.onCapture.disableGate.empty())
                {
                    disableGate(zone.config.onCapture.disableGate);
                }
                if (!zone.config.onCapture.telemetry.empty())
                {
                    pushTelemetry(zone.config.onCapture.telemetry);
                }
            }
        }
        if (captureGoal > 0 && capturedZones >= captureGoal && missionVictoryCountdown < 0.0f)
        {
            missionVictoryCountdown = config.victory_grace;
            pushTelemetry("Zones secured");
        }
    }

    void updateSurvivalMission(float dt)
    {
        survival.elapsed += dt;
        if (missionConfig.survival.pacingStep > 0.0f && missionConfig.survival.pacingMultiplier > 0.0f)
        {
            survival.pacingTimer -= dt;
            if (survival.pacingTimer <= 0.0f)
            {
                survival.spawnMultiplier *= missionConfig.survival.pacingMultiplier;
                survival.pacingTimer += missionConfig.survival.pacingStep;
            }
        }
        while (survival.nextElite < survival.elites.size() && survival.elapsed >= survival.elites[survival.nextElite].time)
        {
            spawnMissionElite(survival.elites[survival.nextElite]);
            ++survival.nextElite;
        }
        if (survival.duration > 0.0f && survival.elapsed >= survival.duration && missionVictoryCountdown < 0.0f)
        {
            missionVictoryCountdown = 0.0f;
        }
    }

    void updateMission(float dt)
    {
        if (missionMode == MissionMode::None)
        {
            return;
        }
        missionTimer += dt;
        switch (missionMode)
        {
        case MissionMode::Boss:
            updateBossMechanics(dt);
            break;
        case MissionMode::Capture:
            updateCaptureMission(dt);
            break;
        case MissionMode::Survival:
            updateSurvivalMission(dt);
            break;
        case MissionMode::None:
            break;
        }
        if (missionVictoryCountdown >= 0.0f)
        {
            missionVictoryCountdown = std::max(0.0f, missionVictoryCountdown - dt);
            if (missionVictoryCountdown <= 0.0f)
            {
                setResult(GameResult::Victory, "Victory");
            }
        }
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
        const float knockbackDistance = def.radius * 0.5f;
        for (EnemyUnit &enemy : enemies)
        {
            const Vec2 offset = enemy.pos - commander.pos;
            const float distanceSq = lengthSq(offset);
            if (distanceSq <= radiusSq)
            {
                enemy.hp -= def.damage;
                if (!enemy.noOverlap && distanceSq > 0.0001f)
                {
                    enemy.pos += normalize(offset) * knockbackDistance;
                }
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

    void issueOrder(ArmyStance newStance)
    {
        stance = newStance;
        orderActive = true;
        orderTimer = orderDuration;
        std::string message = std::string("Order: ") + stanceLabel(stance);
        pushTelemetry(message);
    }

    bool isOrderActive() const { return orderActive; }

    float orderTimeRemaining() const { return std::max(orderTimer, 0.0f); }

    ArmyStance currentOrder() const { return stance; }

    void pushTelemetry(const std::string &text)
    {
        hud.telemetryText = normalizeTelemetry(text);
        hud.telemetryTimer = config.telemetry_duration;
    }

    void updateOrderTimer(float dt)
    {
        if (!orderActive)
        {
            return;
        }
        if (orderTimer > 0.0f)
        {
            orderTimer = std::max(0.0f, orderTimer - dt);
        }
        if (orderTimer <= 0.0f)
        {
            orderActive = false;
            stance = defaultStance;
        }
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

        updateSkillTimers(dt);
        updateOrderTimer(dt);
        updateWaves();
        updateActiveSpawns(dt);
        updateYunaSpawn(dt);
        updateCommanderRespawn(dt);
        updateCommander(dt, commanderMoveInput);
        updateWalls(dt);
        updateUnits(dt);
        updateMission(dt);
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
        clampToWorld(commander.pos, commanderStats.radius);
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
                if (disabledGates.find(set.gate) != disabledGates.end())
                {
                    continue;
                }
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
                active.gateId = set.gate;
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
            if (!spawn.gateId.empty())
            {
                if (const GateRuntime *gate = findGate(spawn.gateId))
                {
                    if (gate->destroyed)
                    {
                        spawn.remaining = 0;
                        continue;
                    }
                }
                else if (disabledGates.find(spawn.gateId) != disabledGates.end())
                {
                    spawn.remaining = 0;
                    continue;
                }
            }
            spawn.timer -= dt;
            if (spawn.timer <= 0.0f)
            {
                spawnOneEnemy(spawn.position, spawn.type);
                float spawnInterval = spawn.interval;
                if (missionMode == MissionMode::Survival && survival.spawnMultiplier > 0.0f)
                {
                    const float mult = std::max(survival.spawnMultiplier, 0.1f);
                    spawnInterval = spawn.interval / mult;
                }
                spawn.timer += spawnInterval;
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
            enemy.speedPx = wallbreakerStats.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = wallbreakerStats.dps_unit;
            enemy.dpsBase = wallbreakerStats.dps_base;
            enemy.dpsWall = wallbreakerStats.dps_wall;
            enemy.noOverlap = wallbreakerStats.ignoreKnockback;
        }
        else if (type == EnemyArchetype::Boss)
        {
            enemy.hp = missionConfig.boss.hp > 0.0f ? missionConfig.boss.hp : 500.0f;
            enemy.radius = missionConfig.boss.radius_px > 0.0f ? missionConfig.boss.radius_px : 32.0f;
            enemy.speedPx = missionConfig.boss.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = slimeStats.dps;
            enemy.dpsBase = slimeStats.dps;
            enemy.dpsWall = slimeStats.dps;
            enemy.noOverlap = missionConfig.boss.noOverlap;
        }
        else
        {
            enemy.hp = slimeStats.hp;
            enemy.radius = slimeStats.radius;
            enemy.speedPx = slimeStats.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = slimeStats.dps;
            enemy.dpsBase = slimeStats.dps;
            enemy.dpsWall = slimeStats.dps;
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

    std::vector<Vec2> collectRaidTargets() const
    {
        std::vector<Vec2> targets;
        targets.reserve(captureZones.size() + spawnScript.gate_tiles.size() + mapDefs.gate_tiles.size());
        for (const CaptureRuntime &zone : captureZones)
        {
            if (!zone.captured)
            {
                targets.push_back(zone.worldPos);
            }
        }
        std::unordered_set<std::string> seen;
        for (const auto &kv : spawnScript.gate_tiles)
        {
            if (disabledGates.find(kv.first) != disabledGates.end())
            {
                continue;
            }
            if (const GateRuntime *gate = findGate(kv.first))
            {
                if (gate->destroyed)
                {
                    continue;
                }
            }
            targets.push_back(tileToWorld(kv.second, mapDefs.tile_size));
            seen.insert(kv.first);
        }
        for (const auto &kv : mapDefs.gate_tiles)
        {
            if (disabledGates.find(kv.first) != disabledGates.end())
            {
                continue;
            }
            if (const GateRuntime *gate = findGate(kv.first))
            {
                if (gate->destroyed)
                {
                    continue;
                }
            }
            if (seen.insert(kv.first).second)
            {
                targets.push_back(tileToWorld(kv.second, mapDefs.tile_size));
            }
        }
        return targets;
    }

    EnemyUnit *findTargetByTags(const Vec2 &from, const std::vector<std::string> &tags)
    {
        for (const std::string &tag : tags)
        {
            EnemyUnit *best = nullptr;
            float bestDist = std::numeric_limits<float>::max();
            if (tag == "boss")
            {
                for (EnemyUnit &enemy : enemies)
                {
                    if (enemy.hp > 0.0f && enemy.type == EnemyArchetype::Boss)
                    {
                        const float distSq = lengthSq(enemy.pos - from);
                        if (distSq < bestDist)
                        {
                            bestDist = distSq;
                            best = &enemy;
                        }
                    }
                }
            }
            else if (tag == "elite")
            {
                for (EnemyUnit &enemy : enemies)
                {
                    if (enemy.hp > 0.0f && enemy.type == EnemyArchetype::Wallbreaker)
                    {
                        const float distSq = lengthSq(enemy.pos - from);
                        if (distSq < bestDist)
                        {
                            bestDist = distSq;
                            best = &enemy;
                        }
                    }
                }
            }
            else if (tag == "enemy" || tag == "any")
            {
                for (EnemyUnit &enemy : enemies)
                {
                    if (enemy.hp > 0.0f && enemy.type != EnemyArchetype::Boss)
                    {
                        const float distSq = lengthSq(enemy.pos - from);
                        if (distSq < bestDist)
                        {
                            bestDist = distSq;
                            best = &enemy;
                        }
                    }
                }
            }
            if (best)
            {
                return best;
            }
        }
        return nullptr;
    }

    Vec2 computeTemperamentVelocity(Unit &yuna,
                                    float dt,
                                    float baseSpeed,
                                    const std::function<EnemyUnit *(const Vec2 &)> &nearestEnemy,
                                    const std::vector<Vec2> &raidTargets)
    {
        TemperamentState &state = yuna.temperament;
        if (!state.definition)
        {
            return {0.0f, 0.0f};
        }
        const TemperamentDefinition &def = *state.definition;

        if (def.behavior == TemperamentBehavior::Mimic)
        {
            if (state.mimicActive)
            {
                state.mimicDuration -= dt;
                if (state.mimicDuration <= 0.0f)
                {
                    state.mimicActive = false;
                    state.currentBehavior = def.mimicDefault;
                    state.mimicCooldown = randomRange(def.mimicEvery);
                }
            }
            if (!state.mimicActive)
            {
                if (state.mimicCooldown > 0.0f)
                {
                    state.mimicCooldown = std::max(0.0f, state.mimicCooldown - dt);
                }
                if (state.mimicCooldown <= 0.0f && !def.mimicPool.empty())
                {
                    std::uniform_int_distribution<std::size_t> pick(0, def.mimicPool.size() - 1);
                    state.mimicBehavior = def.mimicPool[pick(rng)];
                    state.currentBehavior = state.mimicBehavior;
                    state.mimicActive = true;
                    state.mimicDuration = randomRange(def.mimicDuration);
                    if (state.mimicDuration <= 0.0f)
                    {
                        state.mimicDuration = def.mimicDuration.max > 0.0f ? def.mimicDuration.max : 1.0f;
                    }
                }
                else if (!state.mimicActive)
                {
                    state.currentBehavior = def.mimicDefault;
                }
            }
        }
        else
        {
            state.currentBehavior = def.behavior;
        }

        if (state.lastBehavior != state.currentBehavior)
        {
            if (state.currentBehavior == TemperamentBehavior::ChargeNearest)
            {
                state.chargeDashTimer = temperamentConfig.chargeDash.duration;
            }
            if (state.currentBehavior == TemperamentBehavior::Wander || state.currentBehavior == TemperamentBehavior::Homebound || state.currentBehavior == TemperamentBehavior::GuardBase)
            {
                state.wanderDirection = randomUnitVector();
                state.wanderTimer = randomRange(temperamentConfig.wanderTurnInterval);
            }
            state.lastBehavior = state.currentBehavior;
        }

        float dashTime = state.chargeDashTimer;
        if (state.chargeDashTimer > 0.0f)
        {
            state.chargeDashTimer = std::max(0.0f, state.chargeDashTimer - dt);
        }
        float catchupTime = state.catchupTimer;
        if (state.catchupTimer > 0.0f)
        {
            state.catchupTimer = std::max(0.0f, state.catchupTimer - dt);
        }
        bool panicking = state.panicTimer > 0.0f;
        if (state.panicTimer > 0.0f)
        {
            state.panicTimer = std::max(0.0f, state.panicTimer - dt);
        }

        const bool dozing = state.currentBehavior == TemperamentBehavior::Doze;
        if (dozing)
        {
            if (state.sleeping)
            {
                state.sleepRemaining = std::max(0.0f, state.sleepRemaining - dt);
                if (state.sleepRemaining <= 0.0f)
                {
                    state.sleeping = false;
                    state.sleepTimer = randomRange(temperamentConfig.sleepEvery);
                    state.sleepRemaining = temperamentConfig.sleepDuration;
                }
            }
            else
            {
                state.sleepTimer = std::max(0.0f, state.sleepTimer - dt);
                if (state.sleepTimer <= 0.0f)
                {
                    state.sleeping = true;
                    state.sleepRemaining = temperamentConfig.sleepDuration;
                }
            }
        }
        else
        {
            state.sleeping = false;
        }

        if (def.cryPauseEvery.max > 0.0f)
        {
            if (state.crying)
            {
                state.cryPauseTimer = std::max(0.0f, state.cryPauseTimer - dt);
                if (state.cryPauseTimer <= 0.0f)
                {
                    state.crying = false;
                    state.cryTimer = randomRange(def.cryPauseEvery);
                }
            }
            else
            {
                state.cryTimer = std::max(0.0f, state.cryTimer - dt);
                if (state.cryTimer <= 0.0f)
                {
                    state.crying = true;
                    state.cryPauseTimer = def.cryPauseDuration > 0.0f ? def.cryPauseDuration : 0.1f;
                }
            }
        }
        else
        {
            state.crying = false;
        }

        if (state.sleeping || state.crying)
        {
            return {0.0f, 0.0f};
        }

        if (panicking)
        {
            if (EnemyUnit *threat = nearestEnemy(yuna.pos))
            {
                Vec2 dir = normalize(yuna.pos - threat->pos);
                if (lengthSq(dir) > 0.0f)
                {
                    return dir * baseSpeed;
                }
            }
        }

        float speed = baseSpeed;
        auto ensureWander = [&]() {
            if (state.wanderTimer <= 0.0f || lengthSq(state.wanderDirection) < 0.0001f)
            {
                state.wanderDirection = randomUnitVector();
                state.wanderTimer = randomRange(temperamentConfig.wanderTurnInterval);
            }
        };
        if (state.wanderTimer > 0.0f)
        {
            state.wanderTimer = std::max(0.0f, state.wanderTimer - dt);
        }

        switch (state.currentBehavior)
        {
        case TemperamentBehavior::ChargeNearest:
        {
            if (EnemyUnit *target = nearestEnemy(yuna.pos))
            {
                Vec2 dir = normalize(target->pos - yuna.pos);
                if (dashTime > 0.0f)
                {
                    speed *= temperamentConfig.chargeDash.multiplier;
                }
                return dir * speed;
            }
            Vec2 dir = normalize(basePos - yuna.pos);
            return dir * speed;
        }
        case TemperamentBehavior::FleeNearest:
        {
            if (EnemyUnit *threat = nearestEnemy(yuna.pos))
            {
                const float fear = temperamentConfig.fearRadius;
                if (fear <= 0.0f || lengthSq(threat->pos - yuna.pos) <= fear * fear)
                {
                    Vec2 dir = normalize(yuna.pos - threat->pos);
                    if (lengthSq(dir) > 0.0f)
                    {
                        return dir * speed;
                    }
                }
            }
            Vec2 dir = normalize(basePos - yuna.pos);
            return dir * speed;
        }
        case TemperamentBehavior::FollowYuna:
        {
            Vec2 target = commander.alive ? commander.pos : basePos;
            Vec2 toTarget = target - yuna.pos;
            const float distSq = lengthSq(toTarget);
            if (commander.alive && distSq > temperamentConfig.followCatchup.distance * temperamentConfig.followCatchup.distance)
            {
                if (state.catchupTimer <= 0.0f)
                {
                    state.catchupTimer = temperamentConfig.followCatchup.duration;
                }
                catchupTime = std::max(catchupTime, state.catchupTimer);
            }
            if (catchupTime > 0.0f || state.catchupTimer > 0.0f)
            {
                speed *= temperamentConfig.followCatchup.multiplier;
            }
            if (distSq > 1.0f)
            {
                return normalize(toTarget) * speed;
            }
            return Vec2{0.0f, 0.0f};
        }
        case TemperamentBehavior::RaidGate:
        {
            Vec2 target = basePos;
            float best = std::numeric_limits<float>::max();
            for (const Vec2 &candidate : raidTargets)
            {
                const float distSq = lengthSq(candidate - yuna.pos);
                if (distSq < best)
                {
                    best = distSq;
                    target = candidate;
                }
            }
            if (best == std::numeric_limits<float>::max())
            {
                if (EnemyUnit *enemy = nearestEnemy(yuna.pos))
                {
                    target = enemy->pos;
                }
            }
            Vec2 dir = normalize(target - yuna.pos);
            return dir * speed;
        }
        case TemperamentBehavior::Homebound:
        {
            const float homeRadius = def.homeRadius > 0.0f ? def.homeRadius : 48.0f;
            const float avoidRadius = def.avoidEnemyRadius > 0.0f ? def.avoidEnemyRadius : homeRadius * 2.0f;
            Vec2 toBase = basePos - yuna.pos;
            if (lengthSq(toBase) > homeRadius * homeRadius)
            {
                return normalize(toBase) * speed;
            }
            if (EnemyUnit *threat = nearestEnemy(basePos))
            {
                if (lengthSq(threat->pos - basePos) <= avoidRadius * avoidRadius)
                {
                    Vec2 away = basePos - threat->pos;
                    if (lengthSq(away) > 0.0f)
                    {
                        Vec2 target = basePos + normalize(away) * std::max(homeRadius, 8.0f);
                        return normalize(target - yuna.pos) * speed;
                    }
                }
            }
            ensureWander();
            Vec2 wander = normalize(state.wanderDirection);
            Vec2 desired = normalize(wander * homeRadius + toBase * 0.3f);
            if (lengthSq(desired) < 0.0001f)
            {
                desired = normalize(toBase);
            }
            return desired * speed;
        }
        case TemperamentBehavior::Wander:
        case TemperamentBehavior::Doze:
        {
            ensureWander();
            Vec2 dir = normalize(state.wanderDirection);
            return dir * speed;
        }
        case TemperamentBehavior::GuardBase:
        {
            const float guardRadius = mapDefs.tile_size * 22.0f;
            if (EnemyUnit *target = nearestEnemy(basePos))
            {
                if (lengthSq(target->pos - basePos) <= guardRadius * guardRadius)
                {
                    return normalize(target->pos - yuna.pos) * speed;
                }
            }
            ensureWander();
            Vec2 dir = normalize(state.wanderDirection);
            Vec2 guardTarget{basePos.x + dir.x * 120.0f, basePos.y + dir.y * 80.0f};
            return normalize(guardTarget - yuna.pos) * speed;
        }
        case TemperamentBehavior::TargetTag:
        {
            if (EnemyUnit *target = findTargetByTags(yuna.pos, def.targetTags))
            {
                return normalize(target->pos - yuna.pos) * speed;
            }
            if (EnemyUnit *enemy = nearestEnemy(yuna.pos))
            {
                return normalize(enemy->pos - yuna.pos) * speed;
            }
            Vec2 dir = normalize(basePos - yuna.pos);
            return dir * speed;
        }
        case TemperamentBehavior::Mimic:
        {
            ensureWander();
            Vec2 dir = normalize(state.wanderDirection);
            return dir * speed;
        }
        }
        return {0.0f, 0.0f};
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

        if (orderActive && stance == ArmyStance::FollowLeader && commander.alive)
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

        std::vector<Vec2> raidTargets = collectRaidTargets();

        for (Unit &yuna : yunas)
        {
            Vec2 temperamentVelocity = computeTemperamentVelocity(yuna, dt, yunaSpeedPx, nearestEnemy, raidTargets);
            Vec2 velocity{0.0f, 0.0f};
            const bool panicActive = yuna.temperament.panicTimer > 0.0f;

            if (panicActive)
            {
                velocity = temperamentVelocity;
            }
            else if (yuna.effectiveFollower && commander.alive)
            {
                Vec2 desiredPos = commander.pos + yuna.formationOffset;
                Vec2 toTarget = desiredPos - yuna.pos;
                if (lengthSq(toTarget) > followerSnapDistSq)
                {
                    velocity = normalize(toTarget) * yunaSpeedPx;
                }
                else
                {
                    velocity = temperamentVelocity;
                }
            }
            else if (orderActive)
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
            else
            {
                velocity = temperamentVelocity;
            }

            if (velocity.x != 0.0f || velocity.y != 0.0f)
            {
                yuna.pos += velocity * dt;
                clampToWorld(yuna.pos, yuna.radius);
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
            float speedPx = enemy.speedPx;
            if (speedPx <= 0.0f)
            {
                const float speedUnits = enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerStats.speed_u_s : slimeStats.speed_u_s;
                speedPx = speedUnits * config.pixels_per_unit;
            }
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
                    wall.hp -= enemy.dpsWall * dt;
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
                        commanderDamage += enemy.dpsUnit * dt;
                    }
                }
            }
            for (GateRuntime &gate : gates)
            {
                if (gate.destroyed)
                {
                    continue;
                }
                const float combined = commander.radius + gate.radius;
                if (lengthSq(commander.pos - gate.pos) <= combined * combined)
                {
                    gate.hp = std::max(0.0f, gate.hp - commanderStats.dps * dt);
                    if (gate.hp <= 0.0f)
                    {
                        destroyGate(gate);
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
                    yunaDamage[i] += enemy.dpsUnit * dt;
                }
            }
            for (GateRuntime &gate : gates)
            {
                if (gate.destroyed)
                {
                    continue;
                }
                const float combined = yuna.radius + gate.radius;
                if (lengthSq(yuna.pos - gate.pos) <= combined * combined)
                {
                    gate.hp = std::max(0.0f, gate.hp - yunaStats.dps * dt);
                    if (gate.hp <= 0.0f)
                    {
                        destroyGate(gate);
                    }
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
                    if (yuna.temperament.definition && yuna.temperament.definition->panicOnHit > 0.0f)
                    {
                        yuna.temperament.panicTimer = std::max(yuna.temperament.panicTimer, yuna.temperament.definition->panicOnHit);
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
                baseHp -= enemy.dpsBase * dt;
                if (baseHp <= 0.0f)
                {
                    baseHp = 0.0f;
                    if (!hasMission || missionFail.baseHpZero)
                    {
                        setResult(GameResult::Defeat, "Defeat");
                    }
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
        if (missionMode != MissionMode::None)
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
    if (!map.tileset.get())
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
            countedRenderCopy(renderer, map.tileset.getRaw(), &src, &dst, stats);
        }
    }
}

void renderScene(SDL_Renderer *renderer, const Simulation &sim, const Camera &camera, const TextRenderer &font,
                 const TextRenderer &debugFont, const TileMap &map,
                 const Atlas &atlas, int screenW, int screenH, FramePerf &perf, bool showDebugHud)
{
    RenderStats stats;
    static int lodFrameCounter = 0;
    ++lodFrameCounter;
    const bool lodActive = sim.config.lod_threshold_entities > 0 && perf.entities >= sim.config.lod_threshold_entities;
    const bool skipActors = lodActive && sim.config.lod_skip_draw_every > 1 &&
                            (lodFrameCounter % sim.config.lod_skip_draw_every != 0);
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

    auto temperamentColorForBehavior = [](TemperamentBehavior behavior) -> SDL_Color {
        switch (behavior)
        {
        case TemperamentBehavior::ChargeNearest: return SDL_Color{255, 120, 80, 255};
        case TemperamentBehavior::FleeNearest: return SDL_Color{110, 190, 255, 255};
        case TemperamentBehavior::FollowYuna: return SDL_Color{120, 255, 170, 255};
        case TemperamentBehavior::RaidGate: return SDL_Color{220, 140, 255, 255};
        case TemperamentBehavior::Homebound: return SDL_Color{120, 230, 210, 255};
        case TemperamentBehavior::Wander: return SDL_Color{255, 230, 120, 255};
        case TemperamentBehavior::Doze: return SDL_Color{180, 200, 255, 255};
        case TemperamentBehavior::GuardBase: return SDL_Color{255, 190, 110, 255};
        case TemperamentBehavior::TargetTag: return SDL_Color{255, 140, 190, 255};
        case TemperamentBehavior::Mimic: return SDL_Color{210, 210, 210, 255};
        }
        return SDL_Color{240, 240, 240, 255};
    };

    auto drawTemperamentLabel = [&](const Unit &yuna, float spriteTopY, float centerX) {
        if (!debugFont.isLoaded() || !yuna.temperament.definition)
        {
            return;
        }
        std::string label = yuna.temperament.definition->label.empty() ? yuna.temperament.definition->id : yuna.temperament.definition->label;
        if (yuna.temperament.definition->behavior == TemperamentBehavior::Mimic && yuna.temperament.mimicActive)
        {
            label += " -> ";
            label += temperamentBehaviorName(yuna.temperament.mimicBehavior);
        }
        const int textWidth = measureWithFallback(debugFont, label, debugLineHeight);
        const int padX = 4;
        const int padY = 2;
        SDL_Rect bg{
            static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
            static_cast<int>(std::round(spriteTopY)) - (debugLineHeight + padY * 2) - 6,
            textWidth + padX * 2,
            debugLineHeight + padY * 2
        };
        if (bg.x < 4) bg.x = 4;
        if (bg.x + bg.w > screenW - 4) bg.x = screenW - bg.w - 4;
        if (bg.y < 4) bg.y = 4;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        countedRenderFillRect(renderer, &bg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_Color color = temperamentColorForBehavior(yuna.temperament.currentBehavior);
        debugFont.drawText(renderer, label, bg.x + padX, bg.y + padY, &stats, color);
    };

    SDL_SetRenderDrawColor(renderer, 26, 32, 38, 255);
    countedRenderClear(renderer, stats);

    drawTileLayer(renderer, map, map.floor, camera, screenW, screenH, stats);
    if (map.tileset.get())
    {
        SDL_SetTextureColorMod(map.tileset.getRaw(), 190, 190, 200);
        drawTileLayer(renderer, map, map.block, camera, screenW, screenH, stats);
        SDL_SetTextureColorMod(map.tileset.getRaw(), 255, 255, 255);
    }
    drawTileLayer(renderer, map, map.deco, camera, screenW, screenH, stats);

    // Draw base
    const Vec2 baseScreen = worldToScreen(sim.basePos, camera);

    if (atlas.texture.get())
    {
        if (const SDL_Rect *baseFrame = atlas.getFrame("base_box"))
        {
            SDL_Rect dest{
                static_cast<int>(baseScreen.x - baseFrame->w * 0.5f),
                static_cast<int>(baseScreen.y - baseFrame->h * 0.5f),
                baseFrame->w,
                baseFrame->h};
            countedRenderCopy(renderer, atlas.texture.getRaw(), baseFrame, &dest, stats);
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

    if (sim.missionMode == MissionMode::Capture)
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const auto &zone : sim.captureZones)
        {
            Vec2 screenPos = worldToScreen(zone.worldPos, camera);
            const int radius = static_cast<int>(zone.config.radius_px);
            SDL_Rect outline{static_cast<int>(screenPos.x) - radius, static_cast<int>(screenPos.y) - radius,
                             radius * 2, radius * 2};
            SDL_SetRenderDrawColor(renderer, 40, 160, 255, 90);
            countedRenderDrawRect(renderer, &outline, stats);
            SDL_Rect fill = outline;
            fill.h = static_cast<int>(outline.h * zone.progress);
            fill.y = outline.y + (outline.h - fill.h);
            SDL_SetRenderDrawColor(renderer, 80, 210, 255, 100);
            countedRenderFillRect(renderer, &fill, stats);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    if (!sim.gates.empty())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const GateRuntime &gate : sim.gates)
        {
            Vec2 screenPos = worldToScreen(gate.pos, camera);
            const SDL_Color baseColor = gate.destroyed ? SDL_Color{80, 90, 110, 110} : SDL_Color{70, 140, 255, 140};
            SDL_SetRenderDrawColor(renderer, baseColor.r, baseColor.g, baseColor.b, baseColor.a);
            drawFilledCircle(renderer, screenPos, gate.radius, stats);
            if (!gate.destroyed && gate.maxHp > 0.0f)
            {
                const float ratio = std::clamp(gate.hp / gate.maxHp, 0.0f, 1.0f);
                if (ratio > 0.0f)
                {
                    const float innerRadius = std::max(2.0f, gate.radius * ratio);
                    SDL_SetRenderDrawColor(renderer, 160, 210, 255, 180);
                    drawFilledCircle(renderer, screenPos, innerRadius, stats);
                }
            }
            else if (gate.destroyed)
            {
                SDL_SetRenderDrawColor(renderer, 40, 45, 60, 180);
                drawFilledCircle(renderer, screenPos, std::max(2.0f, gate.radius * 0.4f), stats);
            }

            if (debugFont.isLoaded())
            {
                const std::string label = "Gate " + gate.id;
                const int labelWidth = measureWithFallback(debugFont, label, debugLineHeight);
                const int labelPad = 4;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - labelWidth / 2 - labelPad,
                    static_cast<int>(std::round(screenPos.y - gate.radius)) - (debugLineHeight + labelPad * 2) - 4,
                    labelWidth + labelPad * 2,
                    debugLineHeight + labelPad * 2};
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawColor(renderer, 10, 20, 40, 150);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_Color textColor = gate.destroyed ? SDL_Color{170, 170, 190, 255} : SDL_Color{210, 230, 255, 255};
                debugFont.drawText(renderer, label, labelBg.x + labelPad, labelBg.y + labelPad, &stats, textColor);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
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

    if (atlas.texture.get())
    {
        for (const FriendlySprite &sprite : friendSprites)
        {
            if (skipActors && !sprite.commander)
            {
                continue;
            }
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
                    countedRenderCopy(renderer, atlas.texture.getRaw(), commanderFrame, &dest, stats);
                    if (friendRing)
                    {
                        SDL_Rect ringDest{
                            dest.x + (dest.w - friendRing->w) / 2,
                            dest.y + dest.h - friendRing->h,
                            friendRing->w,
                            friendRing->h};
                        countedRenderCopy(renderer, atlas.texture.getRaw(), friendRing, &ringDest, stats);
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
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), yunaAlpha[sprite.index]);
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - yunaFrame->w * 0.5f),
                    static_cast<int>(screenPos.y - yunaFrame->h * 0.5f),
                    yunaFrame->w,
                    yunaFrame->h};
                countedRenderCopy(renderer, atlas.texture.getRaw(), yunaFrame, &dest, stats);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
                if (friendRing)
                {
                    SDL_Rect ringDest{
                        dest.x + (dest.w - friendRing->w) / 2,
                        dest.y + dest.h - friendRing->h,
                        friendRing->w,
                        friendRing->h};
                    countedRenderCopy(renderer, atlas.texture.getRaw(), friendRing, &ringDest, stats);
                }
                drawTemperamentLabel(yuna, static_cast<float>(dest.y), static_cast<float>(dest.x + dest.w * 0.5f));
            }
            else
            {
                if (skipActors)
                {
                    continue;
                }
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 240, 190, 60, yunaAlpha[sprite.index]);
                drawFilledCircle(renderer, screenPos, yuna.radius, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                drawTemperamentLabel(yuna, screenPos.y - yuna.radius, screenPos.x);
            }
        }
        SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
    }
    else
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const FriendlySprite &sprite : friendSprites)
        {
            if (skipActors && !sprite.commander)
            {
                continue;
            }
            if (sprite.commander)
            {
                Vec2 commanderScreen = worldToScreen(sim.commander.pos, camera);
                SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
                drawFilledCircle(renderer, commanderScreen, sim.commander.radius, stats);
                continue;
            }
            if (skipActors)
            {
                continue;
            }
            const Unit &yuna = sim.yunas[sprite.index];
            Vec2 screenPos = worldToScreen(yuna.pos, camera);
            SDL_SetRenderDrawColor(renderer, 240, 190, 60, yunaAlpha[sprite.index]);
            drawFilledCircle(renderer, screenPos, yuna.radius, stats);
            drawTemperamentLabel(yuna, screenPos.y - yuna.radius, screenPos.x);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_SetRenderDrawColor(renderer, 120, 150, 200, 255);
    for (const WallSegment &wall : sim.walls)
    {
        if (skipActors)
        {
            continue;
        }
        Vec2 screenPos = worldToScreen(wall.pos, camera);
        drawFilledCircle(renderer, screenPos, wall.radius, stats);
    }

    std::vector<std::size_t> enemyOrder(sim.enemies.size());
    std::iota(enemyOrder.begin(), enemyOrder.end(), 0);
    std::sort(enemyOrder.begin(), enemyOrder.end(), [&](std::size_t a, std::size_t b) {
        return sim.enemies[a].pos.y < sim.enemies[b].pos.y;
    });

    if (atlas.texture.get())
    {
        for (std::size_t idx : enemyOrder)
        {
            const EnemyUnit &enemy = sim.enemies[idx];
            if (skipActors && enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            const SDL_Rect *frame = enemy.type == EnemyArchetype::Wallbreaker ? wallbreakerFrame : enemyFrame;
            Vec2 screenPos = worldToScreen(enemy.pos, camera);
            if (enemy.type == EnemyArchetype::Boss)
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 255, 80, 160, 110);
                drawFilledCircle(renderer, screenPos, enemy.radius + 26.0f, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }

            SDL_Rect spriteRect{};
            bool hasSpriteRect = false;
            if (frame)
            {
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - frame->w * 0.5f),
                    static_cast<int>(screenPos.y - frame->h * 0.5f),
                    frame->w,
                    frame->h};
                countedRenderCopy(renderer, atlas.texture.getRaw(), frame, &dest, stats);
                if (enemyRing)
                {
                    SDL_Rect ringDest{
                        dest.x + (dest.w - enemyRing->w) / 2,
                        dest.y + dest.h - enemyRing->h,
                        enemyRing->w,
                        enemyRing->h};
                    countedRenderCopy(renderer, atlas.texture.getRaw(), enemyRing, &ringDest, stats);
                }
                spriteRect = dest;
                hasSpriteRect = true;
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, enemy.type == EnemyArchetype::Wallbreaker ? 200 : 80,
                                       enemy.type == EnemyArchetype::Wallbreaker ? 80 : 160,
                                       enemy.type == EnemyArchetype::Wallbreaker ? 80 : 220, 255);
                drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            }

            if (enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
            {
                const std::string bossText = "BOSS";
                const int textWidth = measureWithFallback(debugFont, bossText, debugLineHeight);
                const int padX = 6;
                const int padY = 3;
                const float spriteTop = hasSpriteRect ? static_cast<float>(spriteRect.y) : screenPos.y - enemy.radius;
                const float centerX = hasSpriteRect ? static_cast<float>(spriteRect.x + spriteRect.w * 0.5f) : screenPos.x;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
                    static_cast<int>(std::round(spriteTop)) - (debugLineHeight + padY * 2) - 8,
                    textWidth + padX * 2,
                    debugLineHeight + padY * 2
                };
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 70, 0, 80, 200);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                debugFont.drawText(renderer, bossText, labelBg.x + padX, labelBg.y + padY, &stats,
                                   SDL_Color{255, 180, 255, 255});
            }
        }
    }
    else
    {
        for (std::size_t idx : enemyOrder)
        {
            const EnemyUnit &enemy = sim.enemies[idx];
            if (skipActors && enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(enemy.pos, camera);
            if (enemy.type == EnemyArchetype::Boss)
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 255, 80, 160, 110);
                drawFilledCircle(renderer, screenPos, enemy.radius + 26.0f, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }
            SDL_SetRenderDrawColor(renderer, enemy.type == EnemyArchetype::Wallbreaker ? 200 : 80,
                                   enemy.type == EnemyArchetype::Wallbreaker ? 80 : 160,
                                   enemy.type == EnemyArchetype::Wallbreaker ? 80 : 220, 255);
            drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            if (enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
            {
                const std::string bossText = "BOSS";
                const int textWidth = measureWithFallback(debugFont, bossText, debugLineHeight);
                const int padX = 6;
                const int padY = 3;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - textWidth / 2 - padX,
                    static_cast<int>(std::round(screenPos.y - enemy.radius)) - (debugLineHeight + padY * 2) - 8,
                    textWidth + padX * 2,
                    debugLineHeight + padY * 2
                };
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 70, 0, 80, 200);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                debugFont.drawText(renderer, bossText, labelBg.x + padX, labelBg.y + padY, &stats,
                                   SDL_Color{255, 180, 255, 255});
            }
        }
    }

    // Ambient vignette overlay for dungeon mood
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 8, 24, 140);
    SDL_Rect overlay{0, 0, screenW, screenH};
    countedRenderFillRect(renderer, &overlay, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    auto formatTimer = [](float seconds) {
        seconds = std::max(seconds, 0.0f);
        int total = static_cast<int>(seconds + 0.5f);
        int minutes = total / 60;
        int secs = total % 60;
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2) << secs;
        return oss.str();
    };
    int topUiAnchor = 20;
    if (sim.missionMode != MissionMode::None && sim.missionUI.showGoalText && !sim.missionUI.goalText.empty())
    {
        const int padX = 18;
        const int padY = 8;
        const int textWidth = measureWithFallback(font, sim.missionUI.goalText, lineHeight);
        SDL_Rect goalRect{screenW / 2 - (textWidth + padX * 2) / 2, topUiAnchor, textWidth + padX * 2, lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        countedRenderFillRect(renderer, &goalRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, sim.missionUI.goalText, goalRect.x + padX, goalRect.y + padY, &stats,
                      SDL_Color{230, 240, 255, 255});
        topUiAnchor = goalRect.y + goalRect.h + 10;
    }
    if (sim.missionMode != MissionMode::None && sim.missionUI.showTimer)
    {
        float timerValue = sim.missionMode == MissionMode::Survival && sim.survival.duration > 0.0f
                               ? std::max(sim.survival.duration - sim.survival.elapsed, 0.0f)
                               : sim.missionTimer;
        const std::string timerText = std::string("Time ") + formatTimer(timerValue);
        const int padX = 14;
        const int padY = 6;
        const int textWidth = measureWithFallback(font, timerText, lineHeight);
        SDL_Rect timerRect{screenW / 2 - (textWidth + padX * 2) / 2, topUiAnchor, textWidth + padX * 2,
                           lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        countedRenderFillRect(renderer, &timerRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, timerText, timerRect.x + padX, timerRect.y + padY, &stats);
        topUiAnchor = timerRect.y + timerRect.h + 10;
    }
    if (sim.isOrderActive())
    {
        std::ostringstream orderBanner;
        orderBanner << "[号令:" << orderLabel(sim.currentOrder()) << " 残り"
                    << static_cast<int>(std::ceil(sim.orderTimeRemaining())) << "s]";
        const std::string bannerText = orderBanner.str();
        const int padX = 18;
        const int padY = 8;
        const int textWidth = measureWithFallback(font, bannerText, lineHeight);
        SDL_Rect bannerRect{screenW / 2 - (textWidth + padX * 2) / 2, 12, textWidth + padX * 2, lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
        countedRenderFillRect(renderer, &bannerRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, bannerText, bannerRect.x + padX, bannerRect.y + padY, &stats,
                      SDL_Color{255, 220, 120, 255});
        topUiAnchor = bannerRect.y + bannerRect.h + 12;
    }
    const int baseHpInt = static_cast<int>(std::round(std::max(sim.baseHp, 0.0f)));
    const float hpRatio = sim.config.base_hp > 0 ? std::clamp(baseHpInt / static_cast<float>(sim.config.base_hp), 0.0f, 1.0f) : 0.0f;
    SDL_Rect barBg{screenW / 2 - 160, topUiAnchor, 320, 20};
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

    int infoPanelAnchor = barBg.y + barBg.h + 20;
    if (sim.missionMode == MissionMode::Boss && sim.missionUI.showBossHpBar && sim.boss.maxHp > 0.0f)
    {
        const float ratio = std::clamp(sim.boss.maxHp > 0.0f ? sim.boss.hp / sim.boss.maxHp : 0.0f, 0.0f, 1.0f);
        SDL_Rect bossBg{screenW / 2 - 200, barBg.y + barBg.h + 12, 400, 18};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 30, 10, 60, 200);
        countedRenderFillRect(renderer, &bossBg, stats);
        SDL_Rect bossFill{bossBg.x + 4, bossBg.y + 4, static_cast<int>((bossBg.w - 8) * ratio), bossBg.h - 8};
        SDL_SetRenderDrawColor(renderer, 180, 70, 200, 230);
        countedRenderFillRect(renderer, &bossFill, stats);
        SDL_SetRenderDrawColor(renderer, 110, 60, 150, 230);
        countedRenderDrawRect(renderer, &bossBg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, "Boss HP", bossBg.x, bossBg.y - lineHeight, &stats);
        infoPanelAnchor = bossBg.y + bossBg.h + 20;
    }

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
    if (sim.missionMode == MissionMode::Boss && sim.boss.maxHp > 0.0f)
    {
        std::ostringstream bossLine;
        bossLine << "Boss HP: " << static_cast<int>(std::round(std::max(sim.boss.hp, 0.0f))) << " / "
                 << static_cast<int>(std::round(sim.boss.maxHp));
        infoLines.push_back(bossLine.str());
    }
    if (sim.missionMode == MissionMode::Capture)
    {
        const int goal = sim.captureGoal > 0 ? sim.captureGoal : static_cast<int>(sim.captureZones.size());
        std::ostringstream captureLine;
        captureLine << "Capture: " << sim.capturedZones << "/" << goal;
        infoLines.push_back(captureLine.str());
    }
    if (sim.missionMode == MissionMode::Survival)
    {
        std::ostringstream survivalLine;
        survivalLine << std::fixed << std::setprecision(2) << "Pace x" << std::max(sim.survival.spawnMultiplier, 1.0f);
        infoLines.push_back(survivalLine.str());
    }
    if (!sim.commander.alive)
    {
        std::ostringstream respawnText;
        respawnText << "Commander respawn in " << std::fixed << std::setprecision(1) << sim.commanderRespawnTimer << "s";
        infoLines.push_back(respawnText.str());
    }
    infoLines.push_back("");
    std::ostringstream orderLine;
    orderLine << "Order (F1-F4): ";
    if (sim.isOrderActive())
    {
        orderLine << stanceLabel(sim.currentOrder()) << " [" << std::fixed << std::setprecision(1)
                  << sim.orderTimeRemaining() << "s]";
    }
    else
    {
        orderLine << "None (default " << stanceLabel(sim.defaultStance) << ")";
    }
    infoLines.push_back(orderLine.str());
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
        SDL_Rect infoPanel{12, infoPanelAnchor, infoPanelWidth + infoPadding * 2, infoPanelHeight};
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

    int topRightAnchorY = sim.isOrderActive() ? topUiAnchor : 12;
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
}


class BattleScene : public Scene
{
  public:
    BattleScene() = default;

    void onEnter(GameApplication &app, SceneStack &stack) override;
    void onExit(GameApplication &app, SceneStack &stack) override;
    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override;
    void update(double deltaSeconds, GameApplication &app, SceneStack &stack) override;
    void render(SDL_Renderer *renderer, GameApplication &app) override;

  private:
    bool m_initialized = false;
    Simulation m_sim;
    TileMap m_tileMap;
    Atlas m_atlas;
    TextRenderer m_hudFont;
    TextRenderer m_debugFont;
    Camera m_camera;
    Vec2 m_baseCameraTarget{};
    Vec2 m_introCameraTarget{};
    Vec2 m_introFocus{};
    bool m_introActive = true;
    float m_introTimer = 0.0f;
    static constexpr float m_introDuration = 3.0f;
    bool m_showDebugHud = false;
    double m_accumulator = 0.0;
    double m_fpsTimer = 0.0;
    int m_frames = 0;
    float m_currentFps = 60.0f;
    FramePerf m_framePerf{};
    double m_perfLogTimer = 0.0;
    double m_updateAccum = 0.0;
    double m_renderAccum = 0.0;
    double m_entityAccum = 0.0;
    int m_perfLogFrames = 0;
    double m_frequency = 0.0;
    double m_lastFrameSeconds = 0.0;
    double m_lastUpdateMs = 0.0;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    std::shared_ptr<TelemetrySink> m_telemetry;
    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<AssetManager> m_assetService;
};

void BattleScene::onEnter(GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (m_initialized)
    {
        return;
    }

    m_screenWidth = app.windowWidth();
    m_screenHeight = app.windowHeight();
    SDL_Renderer *renderer = app.renderer();
    ServiceLocator &locator = ServiceLocator::instance();
    m_telemetry = locator.getService<TelemetrySink>();
    m_eventBus = locator.getService<EventBus>();
    m_assetService = locator.getService<AssetManager>();

    auto telemetryNotify = [this](std::string reason, std::string detail = {}) {
        if (!m_telemetry)
        {
            return;
        }
        TelemetrySink::Payload payload{{"scene", "BattleScene"}, {"reason", std::move(reason)}};
        if (!detail.empty())
        {
            payload.emplace("detail", std::move(detail));
        }
        m_telemetry->recordEvent("scene.warning", payload);
    };

    if (!m_assetService)
    {
        std::cerr << "AssetManager service not available.\n";
        telemetryNotify("asset_manager_missing");
        return;
    }

    AssetManager &assets = *m_assetService;

    const AppConfig &appConfig = app.appConfig();
    const AppConfigLoadResult &configResult = app.appConfigResult();
    if (!configResult.success)
    {
        std::cerr << "AppConfig loaded with errors, running with fallback values.\n";
        telemetryNotify("app_config_errors", std::to_string(configResult.errors.size()));
    }

    m_tileMap = {};
    if (!loadTileMap(assets, appConfig.game.map_path, m_tileMap))
    {
        std::cerr << "Continuing without tilemap visuals.\n";
        telemetryNotify("tilemap_missing", appConfig.game.map_path);
    }

    m_atlas = {};
    if (!loadAtlas(assets, appConfig.atlasPath, m_atlas))
    {
        std::cerr << "Continuing without atlas visuals.\n";
        telemetryNotify("atlas_missing", appConfig.atlasPath);
    }

    m_sim = {};
    m_sim.config = appConfig.game;
    m_sim.temperamentConfig = appConfig.temperament;
    m_sim.yunaStats = appConfig.entityCatalog.yuna;
    m_sim.slimeStats = appConfig.entityCatalog.slime;
    m_sim.wallbreakerStats = appConfig.entityCatalog.wallbreaker;
    m_sim.commanderStats = appConfig.entityCatalog.commander;
    m_sim.mapDefs = appConfig.mapDefs;
    m_sim.spawnScript = appConfig.spawnScript;
    if (appConfig.mission && appConfig.mission->mode != MissionMode::None)
    {
        m_sim.hasMission = true;
        m_sim.missionConfig = *appConfig.mission;
    }
    else
    {
        m_sim.hasMission = false;
    }

    if (m_tileMap.width > 0 && m_tileMap.height > 0)
    {
        m_sim.setWorldBounds(static_cast<float>(m_tileMap.width * m_tileMap.tileWidth),
                             static_cast<float>(m_tileMap.height * m_tileMap.tileHeight));
    }
    else
    {
        m_sim.setWorldBounds(static_cast<float>(m_screenWidth), static_cast<float>(m_screenHeight));
    }

    std::vector<SkillDef> skillDefs = appConfig.skills.empty() ? buildDefaultSkills() : appConfig.skills;
    m_sim.configureSkills(skillDefs);
    m_sim.reset();

    if (!m_hudFont.load(assets, "assets/ui/NotoSansJP-Regular.ttf", 22))
    {
        std::cerr << "Failed to load HUD font (NotoSansJP-Regular.ttf).\n";
        telemetryNotify("hud_font_missing", "NotoSansJP-Regular.ttf");
    }
    if (!m_debugFont.load(assets, "assets/ui/NotoSansJP-Regular.ttf", 18))
    {
        std::cerr << "Failed to load debug font fallback, using HUD font size.\n";
        telemetryNotify("debug_font_missing", "NotoSansJP-Regular.ttf");
    }

    m_camera = {};
    m_baseCameraTarget = {m_sim.basePos.x - m_screenWidth * 0.5f, m_sim.basePos.y - m_screenHeight * 0.5f};
    m_introFocus = leftmostGateWorld(m_sim.mapDefs);
    m_introCameraTarget = {m_introFocus.x - m_screenWidth * 0.5f, m_introFocus.y - m_screenHeight * 0.5f};
    m_camera.position = m_introCameraTarget;
    m_introTimer = m_introDuration;
    m_introActive = true;

    m_accumulator = 0.0;
    m_fpsTimer = 0.0;
    m_frames = 0;
    m_currentFps = 60.0f;
    m_framePerf = {};
    m_framePerf.fps = m_currentFps;
    m_perfLogTimer = 0.0;
    m_updateAccum = 0.0;
    m_renderAccum = 0.0;
    m_entityAccum = 0.0;
    m_perfLogFrames = 0;
    m_showDebugHud = false;
    m_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    m_lastFrameSeconds = 0.0;
    m_lastUpdateMs = 0.0;

    m_initialized = true;

    if (m_eventBus)
    {
        EventContext context;
        context.payload = std::string("battle_scene_initialized");
        m_eventBus->dispatch("battle.scene.entered", context);
    }
}

void BattleScene::onExit(GameApplication &app, SceneStack &stack)
{
    (void)app;
    (void)stack;

    m_atlas.texture.reset();
    m_tileMap.tileset.reset();
    m_hudFont.unload();
    m_debugFont.unload();
    m_initialized = false;
    if (m_eventBus)
    {
        EventContext context;
        context.payload = std::string("battle_scene_exited");
        m_eventBus->dispatch("battle.scene.exited", context);
    }
    m_assetService.reset();
    m_eventBus.reset();
    m_telemetry.reset();
}

void BattleScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (!m_initialized)
    {
        return;
    }

    if (event.type == SDL_KEYDOWN && event.key.repeat == 0)
    {
        switch (event.key.keysym.scancode)
        {
        case SDL_SCANCODE_ESCAPE:
            app.requestQuit();
            break;
        case SDL_SCANCODE_F1:
            m_sim.issueOrder(ArmyStance::RushNearest);
            break;
        case SDL_SCANCODE_F2:
            m_sim.issueOrder(ArmyStance::PushForward);
            break;
        case SDL_SCANCODE_F3:
            m_sim.issueOrder(ArmyStance::FollowLeader);
            break;
        case SDL_SCANCODE_F4:
            m_sim.issueOrder(ArmyStance::DefendBase);
            break;
        case SDL_SCANCODE_F10:
            m_showDebugHud = !m_showDebugHud;
            break;
        case SDL_SCANCODE_SPACE:
            m_camera.position = {m_sim.commander.pos.x - m_screenWidth * 0.5f,
                                 m_sim.commander.pos.y - m_screenHeight * 0.5f};
            m_introActive = false;
            m_introTimer = 0.0f;
            break;
        case SDL_SCANCODE_TAB:
            m_camera.position = {m_sim.basePos.x - m_screenWidth * 0.5f,
                                 m_sim.basePos.y - m_screenHeight * 0.5f};
            m_introActive = false;
            m_introTimer = 0.0f;
            break;
        case SDL_SCANCODE_Z:
            m_sim.cycleFormation(-1);
            break;
        case SDL_SCANCODE_X:
            m_sim.cycleFormation(1);
            break;
        case SDL_SCANCODE_R:
            if (m_sim.result != GameResult::Playing && m_sim.canRestart())
            {
                m_sim.reset();
                m_baseCameraTarget = {m_sim.basePos.x - m_screenWidth * 0.5f,
                                      m_sim.basePos.y - m_screenHeight * 0.5f};
                m_introFocus = leftmostGateWorld(m_sim.mapDefs);
                m_introCameraTarget = {m_introFocus.x - m_screenWidth * 0.5f,
                                       m_introFocus.y - m_screenHeight * 0.5f};
                m_camera.position = m_introCameraTarget;
                m_introTimer = m_introDuration;
                m_introActive = true;
            }
            break;
        case SDL_SCANCODE_1:
        case SDL_SCANCODE_2:
        case SDL_SCANCODE_3:
        case SDL_SCANCODE_4:
        {
            const int hotkey = static_cast<int>(event.key.keysym.scancode - SDL_SCANCODE_1) + 1;
            m_sim.selectSkillByHotkey(hotkey);
            break;
        }
        default:
            break;
        }
    }
    else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT)
    {
        Vec2 worldPos = screenToWorld(event.button.x, event.button.y, m_camera);
        m_sim.activateSelectedSkill(worldPos);
    }
}

void BattleScene::update(double deltaSeconds, GameApplication &app, SceneStack &stack)
{
    (void)app;
    (void)stack;
    if (!m_initialized)
    {
        return;
    }

    m_lastFrameSeconds = deltaSeconds;
    m_accumulator += deltaSeconds;
    m_fpsTimer += deltaSeconds;
    ++m_frames;
    if (m_fpsTimer >= 1.0)
    {
        m_currentFps = static_cast<float>(m_frames / m_fpsTimer);
        m_fpsTimer = 0.0;
        m_frames = 0;
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const float dt = m_sim.config.fixed_dt;
    const Uint64 updateStart = SDL_GetPerformanceCounter();
    while (m_accumulator >= dt)
    {
        Vec2 commanderInput{0.0f, 0.0f};
        if (!m_introActive)
        {
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) commanderInput.y -= 1.0f;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) commanderInput.y += 1.0f;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) commanderInput.x -= 1.0f;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) commanderInput.x += 1.0f;
        }

        m_sim.update(dt, commanderInput);
        m_accumulator -= dt;
    }
    const Uint64 updateEnd = SDL_GetPerformanceCounter();
    const double updateMs = (updateEnd - updateStart) * 1000.0 / m_frequency;
    m_lastUpdateMs = updateMs;
    m_framePerf.msUpdate = static_cast<float>(updateMs);

    const float frameSeconds = static_cast<float>(deltaSeconds);
    if (m_introActive)
    {
        m_introTimer = std::max(0.0f, m_introTimer - frameSeconds);
        const float t = std::clamp(1.0f - (m_introTimer / m_introDuration), 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        m_camera.position = lerp(m_introCameraTarget, m_baseCameraTarget, eased);
        if (m_introTimer <= 0.0f)
        {
            m_introActive = false;
            m_camera.position = lerp(m_introCameraTarget, m_baseCameraTarget, 1.0f);
        }
    }
    else
    {
        Vec2 targetCamera{m_sim.commander.pos.x - m_screenWidth * 0.5f,
                          m_sim.commander.pos.y - m_screenHeight * 0.5f};
        const float followFactor = std::clamp(frameSeconds * 6.0f, 0.0f, 1.0f);
        m_camera.position = lerp(m_camera.position, targetCamera, followFactor);
    }

    m_framePerf.fps = m_currentFps;
}

void BattleScene::render(SDL_Renderer *renderer, GameApplication &app)
{
    (void)app;
    if (!m_initialized)
    {
        return;
    }

    const Uint64 renderStart = SDL_GetPerformanceCounter();

    const Vec2 cameraOffset = m_camera.position;
    Camera renderCamera = m_camera;
    renderCamera.position = cameraOffset;
    m_framePerf.entities = static_cast<int>(m_sim.yunas.size() + m_sim.enemies.size() + (m_sim.commander.alive ? 1 : 0));
    renderScene(renderer, m_sim, renderCamera, m_hudFont, m_debugFont, m_tileMap, m_atlas, m_screenWidth, m_screenHeight,
                m_framePerf, m_showDebugHud);
    const Uint64 renderEnd = SDL_GetPerformanceCounter();
    const double renderMs = (renderEnd - renderStart) * 1000.0 / m_frequency;
    m_framePerf.msRender = static_cast<float>(renderMs);

    m_perfLogTimer += m_lastFrameSeconds;
    m_updateAccum += m_lastUpdateMs;
    m_renderAccum += renderMs;
    m_entityAccum += static_cast<double>(m_framePerf.entities);
    ++m_perfLogFrames;
    if (m_perfLogTimer >= 1.0 && m_perfLogFrames > 0)
    {
        const double avgFps = static_cast<double>(m_perfLogFrames) / m_perfLogTimer;
        const double avgUpdate = m_updateAccum / m_perfLogFrames;
        const double avgRender = m_renderAccum / m_perfLogFrames;
        const double avgEntities = m_entityAccum / m_perfLogFrames;
        const bool spike = (avgUpdate + avgRender) > 9.0;
        if (m_telemetry)
        {
            auto formatDouble = [](double value, int precision) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(precision) << value;
                return oss.str();
            };

            TelemetrySink::Payload payload;
            payload.emplace("fps", formatDouble(avgFps, 1));
            payload.emplace("update_ms", formatDouble(avgUpdate, 2));
            payload.emplace("render_ms", formatDouble(avgRender, 2));
            payload.emplace("entities", std::to_string(static_cast<int>(std::round(avgEntities))));
            payload.emplace("spike", spike ? "true" : "false");
            m_telemetry->recordEvent("battle.performance", payload);
        }
        m_perfLogTimer = 0.0;
        m_updateAccum = 0.0;
        m_renderAccum = 0.0;
        m_entityAccum = 0.0;
        m_perfLogFrames = 0;
    }
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    auto configLoader = std::make_shared<AppConfigLoader>(std::filesystem::absolute("config"));
    GameApplication app(std::move(configLoader));
    app.sceneStack().push(std::make_unique<BattleScene>());
    return app.run();
}

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <cstdio>
#include "app/GameApplication.h"
#include "app/UiPresenter.h"
#include "app/RenderUtils.h"
#include "app/FramePerf.h"
#include "app/UiView.h"
#include "app/TextRenderer.h"
#include "assets/AssetManager.h"
#include "config/AppConfig.h"
#include "config/AppConfigLoader.h"
#include "game/CampaignState.h"
#include "game/CampShop.h"
#include "game/TrainingMath.h"
#include "json/JsonUtils.h"
#include "debug/DebugController.h"
#include "debug/DebugOverlayView.h"
#include "scenes/Scene.h"
#include "scenes/SceneStack.h"
#include "events/EventBus.h"
#include "input/ActionBuffer.h"
#include "input/InputMapper.h"
#include "services/ServiceLocator.h"
#include "telemetry/TelemetrySink.h"
#include "telemetry/PerformanceBudgetMonitor.h"
#include "world/ComponentPool.h"
#include "world/FormationUtils.h"
#include "world/LegacySimulation.h"
#include "world/LegacyTypes.h"
#include "world/MoraleTypes.h"
#include "world/SkillRuntime.h"
#include "world/WorldState.h"
#include "world/spawn/Spawner.h"
#include "world/spawn/WaveController.h"
#include "world/systems/BehaviorSystem.h"
#include "world/systems/CommanderInputSystem.h"
#include "world/systems/CombatSystem.h"
#include "world/systems/FormationSystem.h"
#include "world/systems/JobAbilitySystem.h"
#include "world/systems/MovementSystem.h"
#include "world/systems/RenderingPrepSystem.h"
#include "world/systems/MoraleSystem.h"
#include "world/systems/SystemContext.h"
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
#include <optional>
#include <string_view>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace world::systems
{

class SpawnSystem : public ISystem
{
  public:
    void update(float, SystemContext &) override {}
};

} // namespace world::systems

struct TileMap
{
    int width = 0;
    int height = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tilesetColumns = 0;
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
        return it != frames.end() ? &it->second : nullptr;
    }
};

Vec2 operator+(const Vec2 &a, const Vec2 &b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(const Vec2 &a, const Vec2 &b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(const Vec2 &a, float s) { return {a.x * s, a.y * s}; }
Vec2 operator/(const Vec2 &a, float s) { return {a.x / s, a.y / s}; }
Vec2 &operator+=(Vec2 &a, const Vec2 &b)
{
    a.x += b.x;
    a.y += b.y;
    return a;
}

Vec2 lerp(const Vec2 &a, const Vec2 &b, float t)
{
    return a + (b - a) * t;
}

float dot(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }
float lengthSq(const Vec2 &v) { return dot(v, v); }
float length(const Vec2 &v) { return std::sqrt(lengthSq(v)); }
Vec2 normalize(const Vec2 &v)
{
    const float len = length(v);
    return len > 0.0001f ? v / len : Vec2{0.0f, 0.0f};
}

using namespace json;

class BattleScene;
class CampScene;
namespace
{
bool g_showChibiLabels = true;
}

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

namespace
{

std::string trimCopy(const std::string &text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

struct TagResult
{
    std::string tag;
    std::size_t start = 0;
    std::size_t end = 0;
};

std::optional<TagResult> findTag(const std::string &xml, const std::string &name, std::size_t begin = 0)
{
    const std::string needle = "<" + name;
    const std::size_t pos = xml.find(needle, begin);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t end = xml.find('>', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    TagResult result;
    result.tag = xml.substr(pos, end - pos + 1);
    result.start = pos;
    result.end = end;
    return result;
}

std::optional<int> parseXmlIntAttribute(const std::string &tag, const std::string &attr)
{
    const std::string needle = attr + "=\"";
    std::size_t pos = tag.find(needle);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    pos += needle.size();
    const std::size_t end = tag.find('"', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    try
    {
        return std::stoi(tag.substr(pos, end - pos));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::string> parseXmlStringAttribute(const std::string &tag, const std::string &attr)
{
    const std::string needle = attr + "=\"";
    std::size_t pos = tag.find(needle);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    pos += needle.size();
    const std::size_t end = tag.find('"', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    return tag.substr(pos, end - pos);
}

bool parseCsvLayer(const std::string &csv, int expectedCount, std::vector<int> &out)
{
    out.clear();
    if (expectedCount > 0)
    {
        out.reserve(static_cast<std::size_t>(expectedCount));
    }
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        const std::string trimmed = trimCopy(token);
        if (trimmed.empty())
        {
            continue;
        }
        try
        {
            out.push_back(std::stoi(trimmed));
        }
        catch (...)
        {
            return false;
        }
    }
    if (expectedCount > 0)
    {
        if (static_cast<int>(out.size()) < expectedCount)
        {
            out.resize(static_cast<std::size_t>(expectedCount), 0);
        }
        else if (static_cast<int>(out.size()) > expectedCount)
        {
            out.resize(static_cast<std::size_t>(expectedCount));
        }
    }
    return true;
}

} // namespace

bool loadAtlas(AssetManager &assets, const std::string &path, Atlas &out)
{
    out = {};

    AssetManager::AssetLoadStatus status{};
    auto document = loadJsonDocument(assets, path, &status);
    if (!document)
    {
        return false;
    }

    const JsonValue &root = *document;
    std::string imagePathStr;
    if (const JsonValue *meta = getObjectField(root, "meta"))
    {
        imagePathStr = getString(*meta, "image", "");
    }
    std::filesystem::path atlasDir = std::filesystem::path(path).parent_path();
    std::filesystem::path imagePath;
    if (imagePathStr.empty())
    {
        imagePath = std::filesystem::path(path).replace_extension(".png");
    }
    else
    {
        std::filesystem::path candidate(imagePathStr);
        imagePath = candidate.is_absolute() ? candidate : (atlasDir / candidate);
    }
    out.texture = assets.acquireTexture(imagePath.lexically_normal().string());
    if (!out.texture.get())
    {
        return false;
    }

    if (const JsonValue *frames = getObjectField(root, "frames"))
    {
        if (frames->type == JsonValue::Type::Object)
        {
            for (const auto &entry : frames->object)
            {
                const JsonValue *frame = &entry.second;
                const JsonValue *xywh = getObjectField(*frame, "xywh");
                if (!xywh || xywh->type != JsonValue::Type::Array || xywh->array.size() < 4)
                {
                    continue;
                }
                SDL_Rect rect{};
                rect.x = static_cast<int>(xywh->array[0].number);
                rect.y = static_cast<int>(xywh->array[1].number);
                rect.w = static_cast<int>(xywh->array[2].number);
                rect.h = static_cast<int>(xywh->array[3].number);
                out.frames[entry.first] = rect;
            }
        }
    }

    return true;
}

bool loadTileMap(AssetManager &assets, const std::string &path, TileMap &out)
{
    out = {};

    const std::string resolvedPath = assets.resolvePath(path);
    std::ifstream file(resolvedPath);
    if (!file)
    {
        return false;
    }
    const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto mapTag = findTag(xml, "map");
    if (!mapTag)
    {
        return false;
    }
    out.width = parseXmlIntAttribute(mapTag->tag, "width").value_or(0);
    out.height = parseXmlIntAttribute(mapTag->tag, "height").value_or(0);
    out.tileWidth = parseXmlIntAttribute(mapTag->tag, "tilewidth").value_or(0);
    out.tileHeight = parseXmlIntAttribute(mapTag->tag, "tileheight").value_or(0);

    auto tilesetTag = findTag(xml, "tileset", mapTag->end);
    if (!tilesetTag)
    {
        return false;
    }
    out.tilesetColumns = parseXmlIntAttribute(tilesetTag->tag, "columns").value_or(0);

    const std::size_t tilesetClose = xml.find("</tileset", tilesetTag->end);
    auto imageTag = findTag(xml, "image", tilesetTag->end);
    std::string imageSource;
    if (imageTag && (tilesetClose == std::string::npos || imageTag->start < tilesetClose))
    {
        imageSource = parseXmlStringAttribute(imageTag->tag, "source").value_or("");
    }
    std::filesystem::path mapDir = std::filesystem::path(path).parent_path();
    std::filesystem::path imagePath;
    if (imageSource.empty())
    {
        imagePath = mapDir / "tileset.png";
    }
    else
    {
        std::filesystem::path candidate(imageSource);
        imagePath = candidate.is_absolute() ? candidate : (mapDir / candidate);
    }
    out.tileset = assets.acquireTexture(imagePath.lexically_normal().string());

    if (out.tilesetColumns <= 0 && out.tileset.get() && out.tileWidth > 0)
    {
        int texW = 0;
        if (SDL_QueryTexture(out.tileset.getRaw(), nullptr, nullptr, &texW, nullptr) == 0 && texW > 0)
        {
            out.tilesetColumns = std::max(1, texW / out.tileWidth);
        }
    }
    if (out.tilesetColumns <= 0)
    {
        out.tilesetColumns = 1;
    }

    const int expectedTiles = (out.width > 0 && out.height > 0) ? out.width * out.height : 0;
    std::size_t searchPos = tilesetTag->end;
    while (searchPos < xml.size())
    {
        auto layerTag = findTag(xml, "layer", searchPos);
        if (!layerTag)
        {
            break;
        }
        searchPos = layerTag->end + 1;

        const std::size_t layerClose = xml.find("</layer", layerTag->end);
        if (layerClose == std::string::npos)
        {
            break;
        }

        auto dataTag = findTag(xml, "data", layerTag->end);
        if (!dataTag || dataTag->start > layerClose)
        {
            searchPos = layerClose;
            continue;
        }
        const std::size_t dataClose = xml.find("</data>", dataTag->end);
        if (dataClose == std::string::npos || dataClose > layerClose)
        {
            searchPos = layerClose;
            continue;
        }
        const std::string dataContent = xml.substr(dataTag->end + 1, dataClose - dataTag->end - 1);
        std::vector<int> tiles;
        if (!parseCsvLayer(dataContent, expectedTiles, tiles))
        {
            searchPos = layerClose;
            continue;
        }

        auto layerName = parseXmlStringAttribute(layerTag->tag, "name");
        if (layerName)
        {
            std::string lower = *layerName;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (lower == "floor")
            {
                out.floor = std::move(tiles);
            }
            else if (lower == "block")
            {
                out.block = std::move(tiles);
            }
            else if (lower == "deco" || lower == "decor" || lower == "decoration")
            {
                out.deco = std::move(tiles);
            }
        }

        searchPos = layerClose;
    }

    const bool dimensionsValid = out.width > 0 && out.height > 0 && out.tileWidth > 0 && out.tileHeight > 0;
    return dimensionsValid && out.tileset.get();
}
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

using world::LegacySimulation;
using world::StageRuntimeState;
using world::StageEnemyBaseState;
using world::StageAllyBaseState;

namespace world
{

WorldState::WorldState()
    : m_sim(std::make_unique<LegacySimulation>()),
      m_allies(std::make_unique<ComponentPool<Unit>>()),
      m_enemies(std::make_unique<ComponentPool<EnemyUnit>>()),
      m_walls(std::make_unique<ComponentPool<WallSegment>>()),
      m_captureZones(std::make_unique<ComponentPool<CaptureRuntime>>()),
    m_waveController(std::make_unique<spawn::WaveController>()),
    m_spawner(std::make_unique<spawn::Spawner>()),
    m_frameAllocator()
{
    m_waveController->setSpawner(m_spawner.get());
    m_waveController->setStageHazardCallback([this](const std::string &hazardId, bool active) {
        if (m_sim)
        {
            m_sim->setStageHazardActive(hazardId, active);
        }
    });
    m_spawner->setGateChecks(
        [this](const std::string &gate) {
            return m_sim->disabledGates.find(gate) != m_sim->disabledGates.end();
        },
        [this](const std::string &gate) {
            if (const GateRuntime *runtime = m_sim->findGate(gate))
            {
                return runtime->destroyed;
            }
            return false;
        });
    initializeSystems();
}

WorldState::WorldState(WorldState &&other) noexcept = default;

WorldState &WorldState::operator=(WorldState &&other) noexcept = default;

WorldState::~WorldState() = default;

LegacySimulation &WorldState::legacy()
{
    return *m_sim;
}

const LegacySimulation &WorldState::legacy() const
{
    return *m_sim;
}

void WorldState::setWorldBounds(float width, float height)
{
    m_sim->setWorldBounds(width, height);
    markComponentsDirty();
}

void WorldState::configureSkills(const std::vector<SkillDef> &defs)
{
    systems::JobAbilitySystem::installDefaultHandlers(defs);
    m_sim->configureSkills(defs);
    markComponentsDirty();
}

void WorldState::reset()
{
    m_sim->reset();
    if (m_spawner)
    {
        m_spawner->clear();
        spawn::SpawnBudget budget;
        budget.maxPerFrame = m_sim->config.spawnBudget.maxPerFrame;
        m_spawner->setBudget(budget);
        m_baseSpawnBudgetMax = budget.maxPerFrame;
        if (m_enemySpawnMultiplier != 1.0f)
        {
            setEnemySpawnMultiplier(m_enemySpawnMultiplier);
        }
    }
    if (m_waveController)
    {
        m_waveController->setSpawnScript(m_sim->spawnScript, m_sim->mapDefs);
    }
    m_sim->waveScriptComplete = false;
    m_sim->spawnerIdle = true;
    if (auto *formation = formationSystem())
    {
        formation->reset(*m_sim);
    }
    markComponentsDirty();
}

systems::SystemContext WorldState::makeSystemContext(const ActionBuffer &actions)
{
    systems::MissionContext missionContext{
        m_sim->hasMission,
        m_sim->missionConfig,
        m_sim->missionMode,
        m_sim->missionUI,
        m_sim->missionFail,
        m_sim->missionTimer,
        m_sim->missionVictoryCountdown};

    systems::SystemContext context{
        *m_sim,
        m_registry,
        *m_allies,
        *m_enemies,
        *m_walls,
        *m_captureZones,
        m_sim->commander,
        m_sim->hud,
        m_sim->baseHp,
        m_sim->orderActive,
        m_sim->orderTimer,
        m_sim->waveScriptComplete,
        m_sim->spawnerIdle,
        m_sim->timeSinceLastEnemySpawn,
        m_sim->skills,
        m_sim->selectedSkill,
        m_sim->rallyState,
        m_sim->spawnRateMultiplier,
        m_sim->spawnSlowMultiplier,
        m_sim->spawnSlowTimer,
        m_sim->yunas,
        m_sim->enemies,
        m_sim->walls,
        m_sim->gates,
        m_sim->yunaRespawns,
        m_sim->commanderRespawnTimer,
        m_sim->commanderInvulnTimer,
        m_frameAllocator,
        missionContext,
        actions,
        m_eventBus,
        m_telemetry,
        false};
    return context;
}

void WorldState::initializeSystems()
{
    clearSystems();

    auto commanderInput = std::make_unique<systems::CommanderInputSystem>();
    auto formation = std::make_unique<systems::FormationSystem>();
    formation->reset(*m_sim);
    if (m_eventBus)
    {
        formation->setEventBus(std::weak_ptr<EventBus>(m_eventBus));
    }
    if (m_telemetry)
    {
        formation->setTelemetrySink(std::weak_ptr<TelemetrySink>(m_telemetry));
    }

    auto morale = std::make_unique<systems::MoraleSystem>();
    auto behavior = std::make_unique<systems::BehaviorSystem>();
    auto movement = std::make_unique<systems::MovementSystem>();
    auto combat = std::make_unique<systems::CombatSystem>();
    auto jobAbility = std::make_unique<systems::JobAbilitySystem>();
    auto spawn = std::make_unique<systems::SpawnSystem>();
    auto rendering = std::make_unique<systems::RenderingPrepSystem>();

    registerSystem(systems::SystemStage::InputProcessing, std::move(commanderInput));
    registerSystem(systems::SystemStage::CommandAndMorale, std::move(formation));
    registerSystem(systems::SystemStage::CommandAndMorale, std::move(morale));
    registerSystem(systems::SystemStage::AiDecision, std::move(behavior));
    registerSystem(systems::SystemStage::Movement, std::move(movement));
    registerSystem(systems::SystemStage::Combat, std::move(combat));
    registerSystem(systems::SystemStage::StateUpdate, std::move(jobAbility));
    registerSystem(systems::SystemStage::Spawn, std::move(spawn));
    registerSystem(systems::SystemStage::RenderingPrep, std::move(rendering));
}

void WorldState::clearSystems()
{
    m_systems.clear();
    m_systemStageOrder.clear();
    m_cachedFormationSystem = nullptr;
    m_cachedJobAbilitySystem = nullptr;
}

void WorldState::registerSystem(systems::SystemStage stage, std::unique_ptr<systems::ISystem> system)
{
    if (!system)
    {
        return;
    }
    if (!m_systemStageOrder.empty())
    {
        const systems::SystemStage lastStage = m_systemStageOrder.back();
        if (static_cast<std::uint8_t>(stage) < static_cast<std::uint8_t>(lastStage))
        {
            throw std::logic_error("WorldState::registerSystem stage order violation");
        }
    }
    if (auto *formation = dynamic_cast<systems::FormationSystem *>(system.get()))
    {
        m_cachedFormationSystem = formation;
    }
    if (auto *jobAbility = dynamic_cast<systems::JobAbilitySystem *>(system.get()))
    {
        m_cachedJobAbilitySystem = jobAbility;
    }
    m_systemStageOrder.push_back(stage);
    m_systems.push_back(std::move(system));
}

const std::vector<systems::SystemStage> &WorldState::systemStageOrder() const
{
    return m_systemStageOrder;
}

void WorldState::advanceLegacyState(float dt)
{
    if (!m_sim)
    {
        return;
    }

    m_sim->simTime += dt;
    if (m_sim->timeSinceLastEnemySpawn < 10000.0f)
    {
        m_sim->timeSinceLastEnemySpawn += dt;
    }
    if (m_sim->restartCooldown > 0.0f)
    {
        m_sim->restartCooldown = std::max(0.0f, m_sim->restartCooldown - dt);
    }

    m_sim->updateYunaSpawn(dt);
    m_sim->updateCommanderRespawn(dt);
    m_sim->updateWalls(dt);
    m_sim->updateMission(dt);
    m_sim->updateProjectiles(dt);
    m_sim->updateCommanderResources(dt);
}

void WorldState::runSpawnStage(float dt, systems::SystemContext &context)
{
    if (!m_sim)
    {
        return;
    }

    const bool stageSpawns = stageEnemySpawnsEnabled();

    if (m_sim->spawnEnabled)
    {
        if (m_waveController && !stageSpawns)
        {
            std::vector<std::string> announcements = m_waveController->advance(m_sim->simTime);
            for (const std::string &text : announcements)
            {
                if (!text.empty())
                {
                    m_sim->pushTelemetry(text);
                }
            }
        }

        if (m_spawner && !stageSpawns)
        {
            const float survivalMult = (m_sim->missionMode == MissionMode::Survival && m_sim->survival.spawnMultiplier > 0.0f)
                                           ? std::max(m_sim->survival.spawnMultiplier, 0.1f)
                                           : 1.0f;
            const float debugMult = std::max(m_enemySpawnMultiplier, 0.1f);
            const float combinedMult = std::max(survivalMult * debugMult, 0.1f);
            if (std::fabs(combinedMult - 1.0f) > 0.0001f)
            {
                m_spawner->setIntervalModifier([combinedMult](float base) {
                    if (combinedMult <= 0.0f)
                    {
                        return base;
                    }
                    return base / combinedMult;
                });
            }
            else
            {
                m_spawner->setIntervalModifier({});
            }

            const auto emitResult = m_spawner->emit(dt, [this, &context](const spawn::SpawnPayload &payload) {
                m_sim->spawnOneEnemy(payload.position, payload.type);
                context.requestComponentSync();
            });
            if (emitResult.deferred > 0)
            {
                m_sim->handleSpawnDeferral(emitResult.deferred);
            }
        }

        if (m_sim->updateStageEnemySpawns(dt))
        {
            context.requestComponentSync();
        }
    }

    if (stageSpawns)
    {
        m_sim->waveScriptComplete = true;
        m_sim->spawnerIdle = true;
    }
    else
    {
        if (m_waveController)
        {
            m_sim->waveScriptComplete = m_waveController->isComplete();
        }
        if (m_spawner)
        {
            m_sim->spawnerIdle = m_spawner->empty();
        }
    }
}

bool WorldState::stageEnemySpawnsEnabled() const
{
    return m_sim && m_sim->stage.enabled && !m_sim->stage.enemyBases.empty();
}

void WorldState::step(float dt, const ActionBuffer &actions)
{
    m_frameAllocator.reset();
    systems::SystemContext context = makeSystemContext(actions);

    for (std::size_t i = 0; i < m_systems.size(); ++i)
    {
        if (!m_systems[i])
        {
            continue;
        }

        systems::SystemStage stage = systems::SystemStage::InputProcessing;
        if (i < m_systemStageOrder.size())
        {
            stage = m_systemStageOrder[i];
        }

        switch (stage)
        {
        case systems::SystemStage::StateUpdate:
            advanceLegacyState(dt);
            m_systems[i]->update(dt, context);
            context.componentsDirty = true;
            break;
        case systems::SystemStage::Spawn:
        {
            m_systems[i]->update(dt, context);
            runSpawnStage(dt, context);
            break;
        }
        default:
            m_systems[i]->update(dt, context);
            break;
        }

        if (context.componentsDirty)
        {
            markComponentsDirty();
            context.componentsDirty = false;
        }
    }

    // Highlight commander target (ring effect)
    if (m_sim && m_sim->commander.attackTargetIndex >= 0 &&
        m_sim->commander.attackTargetIndex < static_cast<int>(m_sim->enemies.size()))
    {
        const EnemyUnit &enemy = m_sim->enemies[m_sim->commander.attackTargetIndex];
        if (enemy.hp > 0.0f)
        {
            LegacySimulation::Effect fx;
            fx.pos = enemy.pos;
            fx.radius = enemy.radius + 8.0f;
            fx.r = 255.0f;
            fx.g = 120.0f;
            fx.b = 120.0f;
            fx.a = 180.0f;
            fx.ttl = 0.12f;
            fx.cone = false;
            m_sim->effects.push_back(fx);
        }
    }
}

std::size_t WorldState::frameAllocatorCapacity() const
{
    return m_frameAllocator.capacity();
}

std::size_t WorldState::frameAllocatorUsage() const
{
    return m_frameAllocator.used();
}

void WorldState::issueOrder(ArmyStance stance)
{
    if (auto *formation = formationSystem())
    {
        formation->issueOrder(stance, *m_sim);
    }
    markComponentsDirty();
}

void WorldState::cycleFormation(int direction)
{
    if (auto *formation = formationSystem())
    {
        formation->cycleFormation(direction, *m_sim);
    }
    markComponentsDirty();
}

void WorldState::selectSkillByHotkey(int hotkey)
{
    m_sim->selectSkillByHotkey(hotkey);
    markComponentsDirty();
}

void WorldState::activateSelectedSkill(const Vec2 &worldPos)
{
    bool dirty = false;
    if (m_cachedJobAbilitySystem)
    {
        ActionBuffer emptyActions;
        systems::SystemContext context = makeSystemContext(emptyActions);
        systems::SkillCommand command{m_sim->selectedSkill, worldPos};
        m_cachedJobAbilitySystem->triggerSkill(context, command);
        dirty = context.componentsDirty;
    }
    if (dirty)
    {
        markComponentsDirty();
    }
}

void WorldState::castFireBall(float rangePx, float damage)
{
    if (!m_sim)
    {
        return;
    }
    if (m_sim->castFireBall(rangePx, damage))
    {
        markComponentsDirty();
    }
}

void WorldState::commanderAttack(const std::optional<Vec2> &targetWorld)
{
    if (!m_sim)
    {
        return;
    }
    LegacySimulation &sim = *m_sim;
    CommanderUnit &commander = sim.commander;
    if (!commander.alive)
    {
        return;
    }

    EnemyUnit *clicked = nullptr;
    float clickedDistSq = std::numeric_limits<float>::max();
    const float clickPad = 6.0f;
    if (targetWorld)
    {
        for (EnemyUnit &enemy : sim.enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float pad = enemy.radius + clickPad;
            const float distSq = lengthSq(enemy.pos - *targetWorld);
            if (distSq <= pad * pad && distSq < clickedDistSq)
            {
                clicked = &enemy;
                clickedDistSq = distSq;
            }
        }
    }

    const float baseRangePx = sim.config.pixels_per_unit * 6.0f; // ~96px melee reach
    EnemyUnit *nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();
    for (EnemyUnit &enemy : sim.enemies)
    {
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        const float reach = baseRangePx + commander.radius + enemy.radius;
        const float distSq = lengthSq(enemy.pos - commander.pos);
        if (distSq <= reach * reach && distSq < nearestDistSq)
        {
            nearest = &enemy;
            nearestDistSq = distSq;
        }
    }

    EnemyUnit *target = clicked ? clicked : nearest;
    if (!target)
    {
        commander.attackTargetIndex = -1;
        return;
    }
    // Set target index for pursuit/indicator
    const int index = static_cast<int>(&(*target) - sim.enemies.data());
    commander.attackTargetIndex = index;
    // If already in reach, apply immediate swing damage
    const float reach = baseRangePx + commander.radius + target->radius;
    if (lengthSq(target->pos - commander.pos) <= reach * reach)
    {
        const float swingSeconds = commander.attackSwingDuration > 0.0f ? commander.attackSwingDuration : 1.10f;
        const float attackDamage = sim.commanderStats.dps * swingSeconds;
        target->hp -= attackDamage;
        commander.attackLockTimer = std::max(commander.attackLockTimer, swingSeconds);
        commander.attackSwingDuration = swingSeconds;
        commander.attackSwingTimer = 0.0f;
    }
    markComponentsDirty();
}

void WorldState::commanderCancelTarget()
{
    if (!m_sim)
    {
        return;
    }
    m_sim->commander.attackTargetIndex = -1;
    m_sim->commander.focusTargetIndex = -1;
    markComponentsDirty();
}

void WorldState::commanderFocusTarget()
{
    if (!m_sim)
    {
        return;
    }
    CommanderUnit &commander = m_sim->commander;
    if (commander.attackTargetIndex >= 0 &&
        commander.attackTargetIndex < static_cast<int>(m_sim->enemies.size()) &&
        m_sim->enemies[commander.attackTargetIndex].hp > 0.0f)
    {
        commander.focusTargetIndex = commander.attackTargetIndex;
        markComponentsDirty();
    }
}

void WorldState::setCommanderGuard(bool active)
{
    if (!m_sim)
    {
        return;
    }
    m_sim->setCommanderGuard(active);
    markComponentsDirty();
}

void WorldState::setEventBus(std::shared_ptr<EventBus> bus)
{
    m_eventBus = std::move(bus);
    if (m_waveController)
    {
        m_waveController->setEventBus(m_eventBus);
    }
    if (auto *formation = formationSystem())
    {
        formation->setEventBus(std::weak_ptr<EventBus>(m_eventBus));
    }
}

void WorldState::setTelemetrySink(std::shared_ptr<TelemetrySink> sink)
{
    m_telemetry = std::move(sink);
    if (m_sim)
    {
        m_sim->setTelemetrySink(std::weak_ptr<TelemetrySink>(m_telemetry));
    }
    if (m_waveController)
    {
        m_waveController->setTelemetrySink(m_telemetry);
    }
    if (auto *formation = formationSystem())
    {
        formation->setTelemetrySink(std::weak_ptr<TelemetrySink>(m_telemetry));
    }
}

LegacySimulation::SpawnHistoryDumpResult WorldState::dumpSpawnHistory() const
{
    if (!m_sim || !m_waveController)
    {
        return {};
    }
    return m_sim->dumpSpawnHistory(*m_waveController);
}

bool WorldState::canRestart() const
{
    return m_sim->canRestart();
}

ComponentPool<Unit> &WorldState::allies()
{
    syncComponents();
    return *m_allies;
}

const ComponentPool<Unit> &WorldState::allies() const
{
    return const_cast<WorldState *>(this)->allies();
}

ComponentPool<EnemyUnit> &WorldState::enemies()
{
    syncComponents();
    return *m_enemies;
}

const ComponentPool<EnemyUnit> &WorldState::enemies() const
{
    return const_cast<WorldState *>(this)->enemies();
}

ComponentPool<WallSegment> &WorldState::walls()
{
    syncComponents();
    return *m_walls;
}

const ComponentPool<WallSegment> &WorldState::walls() const
{
    return const_cast<WorldState *>(this)->walls();
}

ComponentPool<CaptureRuntime> &WorldState::missionZones()
{
    syncComponents();
    return *m_captureZones;
}

const ComponentPool<CaptureRuntime> &WorldState::missionZones() const
{
    return const_cast<WorldState *>(this)->missionZones();
}

void WorldState::markComponentsDirty()
{
    m_componentsDirty = true;
}

void WorldState::setEnemySpawnMultiplier(float multiplier)
{
    const float clamped = std::clamp(multiplier, 0.1f, 5.0f);
    m_enemySpawnMultiplier = clamped;
    if (m_spawner)
    {
        const int baseBudget = m_baseSpawnBudgetMax > 0 ? m_baseSpawnBudgetMax : m_sim->config.spawnBudget.maxPerFrame;
        const float effective = std::max(clamped, 0.1f);
        const int adjustedBudget = std::max(1, static_cast<int>(std::round(static_cast<float>(baseBudget) * effective)));
        spawn::SpawnBudget budget;
        budget.maxPerFrame = adjustedBudget;
        m_spawner->setBudget(budget);
    }
    markComponentsDirty();
}

float WorldState::enemySpawnMultiplier() const
{
    return m_enemySpawnMultiplier;
}

bool WorldState::skipNextWave()
{
    if (stageEnemySpawnsEnabled() || !m_waveController)
    {
        return false;
    }
    std::vector<std::string> announcements;
    const bool triggered = m_waveController->triggerNextWave(m_sim->simTime, announcements);
    if (!triggered)
    {
        return false;
    }
    for (const std::string &text : announcements)
    {
        if (!text.empty())
        {
            m_sim->pushTelemetry(text);
        }
    }
    markComponentsDirty();
    return true;
}

void WorldState::rebuildMissionComponents() const
{
    m_captureZones->clear(m_registry);
    for (const CaptureRuntime &zone : m_sim->captureZones)
    {
        m_captureZones->create(m_registry, zone);
    }
}

void WorldState::syncComponents() const
{
    if (!m_componentsDirty)
    {
        return;
    }

    if (!m_allies)
    {
        m_allies = std::make_unique<ComponentPool<Unit>>();
    }
    if (!m_enemies)
    {
        m_enemies = std::make_unique<ComponentPool<EnemyUnit>>();
    }
    if (!m_walls)
    {
        m_walls = std::make_unique<ComponentPool<WallSegment>>();
    }
    if (!m_captureZones)
    {
        m_captureZones = std::make_unique<ComponentPool<CaptureRuntime>>();
    }

    m_allies->clear(m_registry);
    for (const Unit &unit : m_sim->yunas)
    {
        m_allies->create(m_registry, unit);
    }

    m_enemies->clear(m_registry);
    for (const EnemyUnit &enemy : m_sim->enemies)
    {
        m_enemies->create(m_registry, enemy);
    }

    m_walls->clear(m_registry);
    for (const WallSegment &wall : m_sim->walls)
    {
        m_walls->create(m_registry, wall);
    }

    rebuildMissionComponents();

    m_componentsDirty = false;
}

systems::FormationSystem *WorldState::formationSystem() const
{
    return m_cachedFormationSystem;
}

} // namespace world

struct Camera
{
    Vec2 position{0.0f, 0.0f};
    float speed = 320.0f;
};

Vec2 worldToScreen(const Vec2 &world, const Camera &camera)
{
    return {world.x - camera.position.x, world.y - camera.position.y};
}

Vec2 screenToWorld(int screenX, int screenY, const Camera &camera)
{
    return {static_cast<float>(screenX) + camera.position.x, static_cast<float>(screenY) + camera.position.y};
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

void renderWorld(SDL_Renderer *renderer, const LegacySimulation &sim, const FormationHudStatus *formationHud,
                 const MoraleHudStatus *moraleHud, const JobHudStatus *jobHud, const Camera &camera,
                 const TextRenderer &font, const TextRenderer &debugFont, const TileMap &map,
                 const Atlas &atlas, int screenW, int screenH, RenderStats &stats)
{
    (void)formationHud;
    (void)jobHud;

    const LegacySimulation::RenderQueue &queue = sim.renderQueue;
    const bool skipActors = queue.skipActors;
    const int lineHeight = std::max(font.getLineHeight(), 18);
    const int debugLineHeight = std::max(debugFont.isLoaded() ? debugFont.getLineHeight() : lineHeight, 14);

    auto enemyColor = [](EnemyArchetype type) -> SDL_Color {
        switch (type)
        {
        case EnemyArchetype::Boss: return SDL_Color{230, 40, 40, 255};
        case EnemyArchetype::Golem: return SDL_Color{245, 210, 50, 255};
        case EnemyArchetype::Wallbreaker: return SDL_Color{200, 140, 90, 255};
        case EnemyArchetype::Goblin: return SDL_Color{90, 210, 90, 255};
        case EnemyArchetype::Magician: return SDL_Color{170, 130, 230, 255};
        case EnemyArchetype::Bat: return SDL_Color{90, 190, 210, 255};
        case EnemyArchetype::Toritori: return SDL_Color{230, 150, 80, 255};
        case EnemyArchetype::Slime:
        default: return SDL_Color{90, 130, 230, 255};
        }
    };
    auto enemyLabel = [](EnemyArchetype type) -> const char * {
        switch (type)
        {
        case EnemyArchetype::Boss: return "DRG";
        case EnemyArchetype::Golem: return "GLM";
        case EnemyArchetype::Wallbreaker: return "WB";
        case EnemyArchetype::Goblin: return "GB";
        case EnemyArchetype::Magician: return "MG";
        case EnemyArchetype::Bat: return "BT";
        case EnemyArchetype::Toritori: return "TT";
        case EnemyArchetype::Slime:
        default: return "SL";
        }
    };

    auto measureWorldText = [](const TextRenderer &renderer, const std::string &text, int approxHeight) {
        const int measured = renderer.measureText(text);
        if (measured > 0)
        {
            return measured;
        }
        const int approxWidth = std::max(approxHeight / 2, 8);
        return static_cast<int>(text.size()) * approxWidth;
    };

    auto moraleIconColor = [](MoraleState state) -> SDL_Color {
        switch (state)
        {
        case MoraleState::LeaderDown: return SDL_Color{255, 160, 60, 220};
        case MoraleState::Panic: return SDL_Color{235, 70, 85, 230};
        case MoraleState::Mesomeso: return SDL_Color{130, 120, 255, 230};
        case MoraleState::Recovering: return SDL_Color{110, 200, 255, 220};
        case MoraleState::Shielded: return SDL_Color{80, 220, 180, 230};
        case MoraleState::Stable:
        default: return SDL_Color{255, 255, 255, 0};
        }
    };

    auto unitRingColor = [](UnitJob job) -> SDL_Color {
        switch (job)
        {
        case UnitJob::Warrior: return SDL_Color{220, 80, 80, 255};
        case UnitJob::Archer: return SDL_Color{80, 200, 120, 255};
        case UnitJob::Shield: return SDL_Color{70, 130, 230, 255};
        }
        return SDL_Color{200, 200, 200, 255};
    };
    auto drawNameLabel = [&](const LegacySimulation::RenderQueue::AllySprite &ally, const Vec2 &screenPos) {
        if (!ally.named || ally.name.empty())
        {
            return;
        }
        const TextRenderer &nameFont = debugFont.isLoaded() ? debugFont : font;
        const int textW = nameFont.measureText(ally.name);
        const int padX = 4;
        const int padY = 2;
        SDL_Rect bg{
            static_cast<int>(std::round(screenPos.x)) - textW / 2 - padX,
            static_cast<int>(std::round(screenPos.y - ally.radius)) - (nameFont.getLineHeight() + padY * 2) - 4,
            textW + padX * 2,
            nameFont.getLineHeight() + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 20, 20, 32, 200);
        countedRenderFillRect(renderer, &bg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        nameFont.drawText(renderer, ally.name, bg.x + padX, bg.y + padY, &stats, SDL_Color{255, 255, 255, 255});
    };

    auto drawMoraleIcon = [&](const Vec2 &worldPos, float radius, MoraleState state) {
        if (state == MoraleState::Stable)
        {
            return;
        }
        SDL_Color color = moraleIconColor(state);
        Vec2 screen = worldToScreen(worldPos, camera);
        const float iconRadius = std::max(4.0f, radius * 0.5f);
        Vec2 iconCenter{screen.x, screen.y - radius - iconRadius - 4.0f};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        drawFilledCircle(renderer, iconCenter, iconRadius, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    };

    auto braveryLabel = [](int axis) -> const char * {
        static const char *labels[] = {"びびり", "ひかえめ", "ふつう", "ゆうかん", "ちょとつ"};
        int idx = std::clamp(axis + 2, 0, 4);
        return labels[idx];
    };
    auto wisdomLabel = [](int axis) -> const char * {
        static const char *labels[] = {"おばか", "ぽやん", "ふつう", "なやみがち", "かんがえるこ"};
        int idx = std::clamp(axis + 2, 0, 4);
        return labels[idx];
    };
    auto temperamentColorForAxes = [](int bravery, int wisdom, bool panic) -> SDL_Color {
        if (panic)
        {
            return SDL_Color{235, 70, 85, 255};
        }
        const std::uint8_t base = 150;
        std::uint8_t r = static_cast<std::uint8_t>(std::clamp(base + bravery * 20, 60, 255));
        std::uint8_t b = static_cast<std::uint8_t>(std::clamp(base + wisdom * 20, 60, 255));
        return SDL_Color{r, 180, b, 255};
    };

    auto drawPanicBadge = [&](const LegacySimulation::RenderQueue::AllySprite &ally, float spriteTopY, float centerX) -> int {
        if (!debugFont.isLoaded())
        {
            return 0;
        }
        const char *badge = nullptr;
        SDL_Color color{235, 70, 85, 230};
        if (ally.panicTokkou)
        {
            badge = "とっこう";
            color = SDL_Color{255, 150, 90, 235};
        }
        else if (ally.panicCling)
        {
            badge = "すがる";
            color = SDL_Color{120, 200, 255, 230};
        }
        else if (ally.panicKaiten)
        {
            badge = "かいてん";
            color = SDL_Color{130, 200, 150, 230};
        }
        else if (ally.panicRunAround)
        {
            badge = "にげまどう";
            color = SDL_Color{200, 140, 255, 230};
        }
        else if (ally.panicActive)
        {
            badge = "Panic";
        }
        if (!badge)
        {
            return 0;
        }
        const int padX = 4;
        const int padY = 2;
        const int textWidth = measureWorldText(debugFont, badge, debugLineHeight);
        SDL_Rect badgeBg{
            static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
            static_cast<int>(std::round(spriteTopY)) - (debugLineHeight + padY * 2) - 10,
            textWidth + padX * 2,
            debugLineHeight + padY * 2};
        if (badgeBg.x < 4) badgeBg.x = 4;
        if (badgeBg.x + badgeBg.w > screenW - 4) badgeBg.x = screenW - badgeBg.w - 4;
        if (badgeBg.y < 4) badgeBg.y = 4;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        countedRenderFillRect(renderer, &badgeBg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        debugFont.drawText(renderer, badge, badgeBg.x + padX, badgeBg.y + padY, &stats, SDL_Color{255, 255, 255, 255});
        return badgeBg.h + 4;
    };

    auto drawTemperamentLabel = [&](const LegacySimulation::RenderQueue::AllySprite &ally, float spriteTopY, float centerX) {
        if (ally.named)
        {
            return;
        }
        if (!debugFont.isLoaded())
        {
            return;
        }
        std::string label = std::string("ゆ:") + braveryLabel(ally.braveryAxis) + " ち:" + wisdomLabel(ally.wisdomAxis);
        const char *branchLabel = nullptr;
        if (ally.panicTokkou)
        {
            branchLabel = "とっこう";
        }
        else if (ally.panicCling)
        {
            branchLabel = "すがる";
        }
        else if (ally.panicKaiten)
        {
            branchLabel = "かいてん";
        }
        else if (ally.panicRunAround)
        {
            branchLabel = "にげまどう";
        }
        if (!branchLabel && ally.panicTimer > 0.0f)
        {
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), " 残%.1fs", ally.panicTimer);
            label += timeBuf;
        }
        label += " 行動:" + std::string(chibiActionLabel(ally.action));
        if (branchLabel)
        {
            label += " [";
            label += branchLabel;
            if (ally.panicBranchTimer > 0.0f)
            {
                char timeBuf[16];
                std::snprintf(timeBuf, sizeof(timeBuf), " 残%.1fs", ally.panicBranchTimer);
                label += timeBuf;
            }
            label += "]";
        }
        else if (ally.panicActive)
        {
            label += ally.forcePanic ? " [強Panic]" : " [Panic]";
        }

        const int textWidth = measureWorldText(debugFont, label, debugLineHeight);
        const int padX = 4;
        const int padY = 2;
        const int panicOffset = drawPanicBadge(ally, spriteTopY, centerX);
        SDL_Rect bg{
            static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
            static_cast<int>(std::round(spriteTopY)) - panicOffset - (debugLineHeight + padY * 2) - 6,
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
        SDL_Color color = temperamentColorForAxes(ally.braveryAxis, ally.wisdomAxis, ally.panicActive);
        debugFont.drawText(renderer, label, bg.x + padX, bg.y + padY, &stats, color);
    };

    MoraleState commanderMorale = sim.moraleSummary.commanderState;
    std::vector<MoraleState> moraleStates(sim.yunas.size(), MoraleState::Stable);
    for (const LegacySimulation::RenderQueue::AllySprite &ally : queue.allies)
    {
        if (ally.commander)
        {
            commanderMorale = ally.morale;
        }
        else if (ally.hasUnitIndex && ally.unitIndex < moraleStates.size())
        {
            moraleStates[ally.unitIndex] = ally.morale;
        }
    }
    if (moraleHud)
    {
        for (const MoraleHudIcon &icon : moraleHud->icons)
        {
            if (icon.commander)
            {
                commanderMorale = icon.state;
            }
            else if (icon.unitIndex < moraleStates.size())
            {
                moraleStates[icon.unitIndex] = icon.state;
            }
        }
    }

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

    const StageRuntimeState &stageState = sim.stageState();

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

    if (stageState.enabled && !stageState.allyBases.empty())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const StageAllyBaseState &base : stageState.allyBases)
        {
            if (base.destroyed)
            {
                continue;
            }
            const float auraRadius = base.auraRadiusPx > 0.0f ? base.auraRadiusPx : 128.0f;
            Vec2 screenPos = worldToScreen(base.pos, camera);
            SDL_SetRenderDrawColor(renderer, 70, 180, 255, 80);
            drawFilledCircle(renderer, screenPos, auraRadius, stats);
            if (debugFont.isLoaded())
            {
                std::ostringstream oss;
                oss << "拠点オーラ (r=" << static_cast<int>(std::round(auraRadius)) << ")";
                const std::string label = oss.str();
                const int labelWidth = measureWorldText(debugFont, label, debugLineHeight);
                const int pad = 4;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - labelWidth / 2 - pad,
                    static_cast<int>(std::round(screenPos.y - auraRadius)) - (debugLineHeight + pad * 2) - 4,
                    labelWidth + pad * 2,
                    debugLineHeight + pad * 2};
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawColor(renderer, 10, 30, 50, 160);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_Color textColor{210, 240, 255, 255};
                debugFont.drawText(renderer, label, labelBg.x + pad, labelBg.y + pad, &stats, textColor);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // Gate rendering removed (legacy)

    if (stageState.enabled && !stageState.enemyBases.empty())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const StageEnemyBaseState &base : stageState.enemyBases)
        {
            Vec2 screenPos = worldToScreen(base.pos, camera);
            const float drawRadius = std::max(base.radiusPx, 32.0f);
            const SDL_Color baseColor = base.sealed ? SDL_Color{90, 90, 90, 130} : SDL_Color{180, 60, 50, 150};
            SDL_SetRenderDrawColor(renderer, baseColor.r, baseColor.g, baseColor.b, baseColor.a);
            drawFilledCircle(renderer, screenPos, drawRadius, stats);
            if (!base.sealed && base.maxHp > 0.0f)
            {
                const float ratio = std::clamp(base.hp / base.maxHp, 0.0f, 1.0f);
                if (ratio > 0.0f)
                {
                    const float innerRadius = std::max(4.0f, drawRadius * ratio);
                    SDL_SetRenderDrawColor(renderer, 230, 110, 90, 180);
                    drawFilledCircle(renderer, screenPos, innerRadius, stats);
                }
            }
            else if (base.sealed)
            {
                SDL_SetRenderDrawColor(renderer, 40, 40, 40, 180);
                drawFilledCircle(renderer, screenPos, std::max(4.0f, drawRadius * 0.35f), stats);
            }

            if (debugFont.isLoaded())
            {
                std::ostringstream oss;
                oss << "Base " << (base.id.empty() ? "???": base.id) << ' ';
                if (base.sealed)
                {
                    oss << "(Sealed)";
                }
                else if (base.maxHp > 0.0f)
                {
                    oss << static_cast<int>(std::round(std::max(base.hp, 0.0f))) << '/' << static_cast<int>(std::round(base.maxHp));
                }
                else
                {
                    oss << "Active";
                }
                const std::string label = oss.str();
                const int labelWidth = measureWorldText(debugFont, label, debugLineHeight);
                const int pad = 4;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - labelWidth / 2 - pad,
                    static_cast<int>(std::round(screenPos.y - drawRadius)) - (debugLineHeight + pad * 2) - 6,
                    labelWidth + pad * 2,
                    debugLineHeight + pad * 2};
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawColor(renderer, 28, 12, 12, 160);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_Color textColor = base.sealed ? SDL_Color{200, 200, 200, 255} : SDL_Color{255, 210, 210, 255};
                debugFont.drawText(renderer, label, labelBg.x + pad, labelBg.y + pad, &stats, textColor);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    const SDL_Rect *commanderFrame = nullptr;
    const SDL_Rect *slimeFrame = nullptr;
    const SDL_Rect *goblinFrame = nullptr;
    const SDL_Rect *magicianFrame = nullptr;
    const SDL_Rect *batFrame = nullptr;
    const SDL_Rect *toritoriFrame = nullptr;
    const SDL_Rect *golemFrame = nullptr;
    const SDL_Rect *wallbreakerFrame = nullptr;
    auto fetchFrame = [&](const std::string &prefix) -> const SDL_Rect * {
        if (prefix.empty())
        {
            return nullptr;
        }
        return atlas.getFrame(prefix + "_0");
    };
    commanderFrame = fetchFrame(sim.commanderStats.spritePrefix);
    slimeFrame = fetchFrame(sim.slimeStats.spritePrefix);
    goblinFrame = fetchFrame(sim.goblinStats.spritePrefix);
    magicianFrame = fetchFrame(sim.magicianStats.spritePrefix);
    batFrame = fetchFrame(sim.batStats.spritePrefix);
    toritoriFrame = fetchFrame(sim.toritoriStats.spritePrefix);
    golemFrame = fetchFrame(sim.golemStats.spritePrefix);
    wallbreakerFrame = fetchFrame(sim.wallbreakerStats.spritePrefix);
    auto frameForEnemy = [&](EnemyArchetype type) -> const SDL_Rect * {
        switch (type)
        {
        case EnemyArchetype::Goblin: return goblinFrame ? goblinFrame : slimeFrame;
        case EnemyArchetype::Magician: return magicianFrame ? magicianFrame : slimeFrame;
        case EnemyArchetype::Bat: return batFrame ? batFrame : slimeFrame;
        case EnemyArchetype::Toritori: return toritoriFrame ? toritoriFrame : slimeFrame;
        case EnemyArchetype::Golem: return golemFrame ? golemFrame : slimeFrame;
        case EnemyArchetype::Wallbreaker: return wallbreakerFrame ? wallbreakerFrame : slimeFrame;
        case EnemyArchetype::Boss:
            return nullptr;
        case EnemyArchetype::Slime:
        default: return slimeFrame;
        }
    };
    const SDL_Rect *friendRing = atlas.getFrame("ring_friend");
    const SDL_Rect *enemyRing = atlas.getFrame("ring_enemy");

    auto chibiFrameForAlly = [&](const LegacySimulation::RenderQueue::AllySprite &ally)
        -> std::pair<const SDL_Rect *, SDL_RendererFlip> {
        const bool moving = ally.moving;
        const char *base = moving ? "chibi_walk" : "chibi_idle";
        const int frames = 6;
        // animate by real time to avoid reliance on simulation frameCounter
        const std::uint64_t ticks = SDL_GetTicks64();
        const std::uint64_t divisorMs = moving ? 100 : 200; // ~10fps walk, ~5fps idle
        const std::uint64_t idx = ((ticks / divisorMs) % frames);
        const std::string key = std::string(base) + "_" + std::to_string(idx);
        const SDL_Rect *rect = atlas.getFrame(key);
        const SDL_RendererFlip flip = ally.facingX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        return {rect, flip};
    };

    if (atlas.texture.get())
    {
        for (const LegacySimulation::RenderQueue::AllySprite &ally : queue.allies)
        {
            if (skipActors && !ally.commander)
            {
                continue;
            }

            Vec2 screenPos = worldToScreen(ally.position, camera);
            if (ally.commander)
            {
                if (commanderFrame)
                {
                    SDL_Rect dest{
                        static_cast<int>(screenPos.x - commanderFrame->w * 0.5f),
                        static_cast<int>(screenPos.y - commanderFrame->h * 0.5f),
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
                    drawFilledCircle(renderer, screenPos, ally.radius, stats);
                }
                drawMoraleIcon(ally.position, ally.radius, commanderMorale);
                continue;
            }

            const auto [chibiFrame, flip] = chibiFrameForAlly(ally);
            const bool drawAsSquare = ally.named;
            if (drawAsSquare)
            {
                const int size = static_cast<int>(std::round(ally.radius * 2.2f));
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - size * 0.5f),
                    static_cast<int>(screenPos.y - size * 0.5f),
                    size,
                    size};
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_Color unitColor = unitRingColor(ally.job);
                SDL_SetRenderDrawColor(renderer, unitColor.r, unitColor.g, unitColor.b, ally.alpha);
                countedRenderFillRect(renderer, &dest, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        if (g_showChibiLabels)
        {
            drawTemperamentLabel(ally, static_cast<float>(dest.y), static_cast<float>(dest.x + dest.w * 0.5f));
        }
                drawNameLabel(ally, screenPos);
            }
            else if (chibiFrame)
            {
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), ally.alpha);
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - chibiFrame->w * 0.5f),
                    static_cast<int>(screenPos.y - chibiFrame->h * 0.5f),
                    chibiFrame->w,
                    chibiFrame->h};
                countedRenderCopyFlip(renderer, atlas.texture.getRaw(), chibiFrame, &dest, flip, stats);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
                if (friendRing)
                {
                    SDL_Color ringColor = unitRingColor(ally.job);
                    SDL_SetTextureColorMod(atlas.texture.getRaw(), ringColor.r, ringColor.g, ringColor.b);
                    SDL_Rect ringDest{
                        dest.x + (dest.w - friendRing->w) / 2,
                        dest.y + dest.h - friendRing->h,
                        friendRing->w,
                        friendRing->h};
                    countedRenderCopy(renderer, atlas.texture.getRaw(), friendRing, &ringDest, stats);
                    SDL_SetTextureColorMod(atlas.texture.getRaw(), 255, 255, 255);
                }
                if (!ally.named)
                {
                    if (g_showChibiLabels)
                    {
                        drawTemperamentLabel(
                            ally, static_cast<float>(dest.y), static_cast<float>(dest.x + dest.w * 0.5f));
                    }
                }
                drawNameLabel(ally, screenPos);
            }
            else
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_Color unitColor = unitRingColor(ally.job);
                SDL_SetRenderDrawColor(renderer, unitColor.r, unitColor.g, unitColor.b, ally.alpha);
                drawFilledCircle(renderer, screenPos, ally.radius, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                if (!ally.named)
                {
                    if (g_showChibiLabels)
                    {
                        drawTemperamentLabel(ally, screenPos.y - ally.radius, screenPos.x);
                    }
                }
                drawNameLabel(ally, screenPos);
            }

            MoraleState state = (ally.hasUnitIndex && ally.unitIndex < moraleStates.size()) ? moraleStates[ally.unitIndex]
                                                                                           : ally.morale;
            drawMoraleIcon(ally.position, ally.radius, state);
        }
        if (!queue.deathFx.empty())
        {
            for (const auto &fx : queue.deathFx)
            {
                const float duration = std::max(fx.duration, 0.0001f);
                const float progress = std::clamp(1.0f - (fx.timer / duration), 0.0f, 1.0f);
                const int frameIdx = std::min(3, static_cast<int>(progress * 4.0f));
                const std::string key = "chibi_death_" + std::to_string(frameIdx);
                const SDL_Rect *frame = atlas.getFrame(key);
                if (!frame)
                {
                    continue;
                }
                const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(fx.timer / duration, 0.0f, 1.0f) * 255);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), alpha);
                Vec2 screenPos = worldToScreen(fx.position, camera);
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - frame->w * 0.5f),
                    static_cast<int>(screenPos.y - frame->h * 0.5f),
                    frame->w,
                    frame->h};
                const SDL_RendererFlip flip = fx.facingX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                countedRenderCopyFlip(renderer, atlas.texture.getRaw(), frame, &dest, flip, stats);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
            }
        }
        SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
    }
    else
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const LegacySimulation::RenderQueue::AllySprite &ally : queue.allies)
        {
            if (skipActors && !ally.commander)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(ally.position, camera);
            if (ally.commander)
            {
                SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
                drawFilledCircle(renderer, screenPos, ally.radius, stats);
                drawMoraleIcon(ally.position, ally.radius, commanderMorale);
                continue;
            }

            SDL_Color unitColor = unitRingColor(ally.job);
            SDL_SetRenderDrawColor(renderer, unitColor.r, unitColor.g, unitColor.b, ally.alpha);
            drawFilledCircle(renderer, screenPos, ally.radius, stats);
            if (g_showChibiLabels)
            {
                drawTemperamentLabel(ally, screenPos.y - ally.radius, screenPos.x);
            }
            MoraleState state = (ally.hasUnitIndex && ally.unitIndex < moraleStates.size()) ? moraleStates[ally.unitIndex]
                                                                                            : ally.morale;
            drawMoraleIcon(ally.position, ally.radius, state);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_SetRenderDrawColor(renderer, 120, 150, 200, 255);
    for (const LegacySimulation::RenderQueue::WallSprite &wall : queue.walls)
    {
        if (skipActors)
        {
            continue;
        }
        Vec2 screenPos = worldToScreen(wall.position, camera);
        drawFilledCircle(renderer, screenPos, wall.radius, stats);
    }

    if (atlas.texture.get())
    {
        // Ephemeral effects (rings / cones)
        for (const auto &fx : queue.effects)
        {
            Vec2 screenPos = worldToScreen(fx.position, camera);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, fx.r, fx.g, fx.b, fx.a);
            if (fx.cone && fx.length > 0.0f)
            {
                Vec2 dir = fx.dir;
                const float dirLen = std::sqrt(lengthSq(dir));
                if (dirLen > 0.0001f)
                {
                    dir = dir / dirLen;
                }
                else
                {
                    dir = {1.0f, 0.0f};
                }
                Vec2 perp{-dir.y, dir.x};
                const float nearHalf = fx.nearWidth * 0.5f;
                const float farHalf = fx.farWidth * 0.5f;
                Vec2 p0 = screenPos + perp * nearHalf;
                Vec2 p1 = screenPos - perp * nearHalf;
                Vec2 tip = screenPos + dir * fx.length;
                Vec2 p2 = tip + perp * farHalf;
                Vec2 p3 = tip - perp * farHalf;
                SDL_Vertex verts[4];
                verts[0].position = {p0.x, p0.y};
                verts[1].position = {p1.x, p1.y};
                verts[2].position = {p2.x, p2.y};
                verts[3].position = {p3.x, p3.y};
                for (auto &v : verts)
                {
                    v.color = SDL_Color{fx.r, fx.g, fx.b, fx.a};
                }
                int indices[6] = {0, 1, 2, 1, 3, 2};
                SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
            }
            else
            {
                drawFilledCircle(renderer, screenPos, fx.radius, stats);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        // Projectiles (simple circles)
        for (const auto &proj : queue.projectiles)
        {
            if (skipActors)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(proj.position, camera);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 240, 120, 60, 220);
            drawFilledCircle(renderer, screenPos, proj.radius, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        for (const LegacySimulation::RenderQueue::EnemySprite &enemy : queue.enemies)
        {
            if (skipActors && enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            const SDL_Rect *frame = frameForEnemy(enemy.type);
            Vec2 screenPos = worldToScreen(enemy.position, camera);
            // Colored halo to distinguish archetypes
            const SDL_Color halo = enemyColor(enemy.type);
            const float haloRadius = enemy.type == EnemyArchetype::Boss ? enemy.radius + 32.0f
                                   : enemy.type == EnemyArchetype::Golem ? enemy.radius + 22.0f
                                   : enemy.type == EnemyArchetype::Wallbreaker ? enemy.radius + 16.0f
                                   : enemy.radius + 14.0f;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, halo.r, halo.g, halo.b, halo.a);
            drawFilledCircle(renderer, screenPos, haloRadius, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

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
                SDL_Color color = enemyColor(enemy.type);
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
                drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            }

            if (enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
            {
                const std::string bossText = "BOSS";
                const int textWidth = measureWorldText(debugFont, bossText, debugLineHeight);
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
            // Archetype abbreviation label (non-bossも表示)
            const TextRenderer &labelFont = debugFont.isLoaded() ? debugFont : font;
            const std::string abbrev = enemyLabel(enemy.type);
            const int abbrevWidth = labelFont.measureText(abbrev);
            const int padX = 4;
            const int padY = 2;
            SDL_Rect tagBg{
                static_cast<int>(std::round(screenPos.x)) - abbrevWidth / 2 - padX,
                static_cast<int>(std::round(screenPos.y - haloRadius)) - (labelFont.getLineHeight() + padY * 2) - 2,
                abbrevWidth + padX * 2,
                labelFont.getLineHeight() + padY * 2};
            SDL_Color labelBgColor{static_cast<Uint8>(std::min(halo.r + 30, 255)),
                                   static_cast<Uint8>(std::min(halo.g + 30, 255)),
                                   static_cast<Uint8>(std::min(halo.b + 30, 255)),
                                   200};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, labelBgColor.r, labelBgColor.g, labelBgColor.b, labelBgColor.a);
            countedRenderFillRect(renderer, &tagBg, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            labelFont.drawText(renderer, abbrev, tagBg.x + padX, tagBg.y + padY, &stats, SDL_Color{0, 0, 0, 255});
        }
    }
    else
    {
        for (const auto &proj : queue.projectiles)
        {
            if (skipActors)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(proj.position, camera);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 240, 120, 60, 220);
            drawFilledCircle(renderer, screenPos, proj.radius, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        for (const LegacySimulation::RenderQueue::EnemySprite &enemy : queue.enemies)
        {
            if (skipActors && enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(enemy.position, camera);
            const SDL_Color halo = enemyColor(enemy.type);
            const float haloRadius = enemy.type == EnemyArchetype::Boss ? enemy.radius + 32.0f
                                   : enemy.type == EnemyArchetype::Golem ? enemy.radius + 22.0f
                                   : enemy.type == EnemyArchetype::Wallbreaker ? enemy.radius + 16.0f
                                   : enemy.radius + 14.0f;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, halo.r, halo.g, halo.b, halo.a);
            drawFilledCircle(renderer, screenPos, haloRadius, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, halo.r, halo.g, halo.b, 255);
            drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            const TextRenderer &labelFont = debugFont.isLoaded() ? debugFont : font;
            const std::string abbrev = enemyLabel(enemy.type);
            const int abbrevWidth = labelFont.measureText(abbrev);
            const int padX = 4;
            const int padY = 2;
            SDL_Rect tagBg{
                static_cast<int>(std::round(screenPos.x)) - abbrevWidth / 2 - padX,
                static_cast<int>(std::round(screenPos.y - haloRadius)) - (labelFont.getLineHeight() + padY * 2) - 2,
                abbrevWidth + padX * 2,
                labelFont.getLineHeight() + padY * 2};
            SDL_Color labelBgColor{static_cast<Uint8>(std::min(halo.r + 30, 255)),
                                   static_cast<Uint8>(std::min(halo.g + 30, 255)),
                                   static_cast<Uint8>(std::min(halo.b + 30, 255)),
                                   200};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, labelBgColor.r, labelBgColor.g, labelBgColor.b, labelBgColor.a);
            countedRenderFillRect(renderer, &tagBg, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            labelFont.drawText(renderer, abbrev, tagBg.x + padX, tagBg.y + padY, &stats, SDL_Color{0, 0, 0, 255});
            if (enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
            {
                const std::string bossText = "BOSS";
                const int textWidth = measureWorldText(debugFont, bossText, debugLineHeight);
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

    // Commander HP/MP HUD
    if (sim.commander.alive)
    {
        std::ostringstream hpmp;
        hpmp << "HP " << static_cast<int>(std::round(sim.commander.hp)) << "/" << static_cast<int>(std::round(sim.commanderStats.hp))
             << "  MP " << static_cast<int>(std::round(sim.commander.mp)) << "/" << static_cast<int>(std::round(sim.commander.mpMax));
        font.drawText(renderer, hpmp.str(), 16, 16, nullptr, SDL_Color{240, 240, 255, 255});
    }

    // Enemy legend (always on)
    {
        struct LegendEntry
        {
            const char *label;
            SDL_Color color;
        };
        const LegendEntry entries[] = {
            {"DRG (Boss)", SDL_Color{230, 40, 40, 255}},
            {"GLM (Golem)", SDL_Color{245, 210, 50, 255}},
            {"WB (Wallbreaker)", SDL_Color{200, 140, 90, 255}},
            {"GB (Goblin)", SDL_Color{90, 210, 90, 255}},
            {"MG (Magician)", SDL_Color{170, 130, 230, 255}},
            {"BT (Bat)", SDL_Color{90, 190, 210, 255}},
            {"TT (Toritori)", SDL_Color{230, 150, 80, 255}},
            {"SL (Slime)", SDL_Color{90, 130, 230, 255}},
        };
        const int padX = 8;
        const int padY = 6;
        const int boxSize = 14;
        const TextRenderer &legendFont = font.isLoaded() ? font : debugFont;
        const int lineH = legendFont.isLoaded() ? legendFont.getLineHeight() : 18;
        int y = screenH - static_cast<int>(std::size(entries)) * (lineH + padY) - 12;
        const int x = screenW - 240;
        SDL_Rect panel{x - padX, y - padY, 220 + padX * 2, static_cast<int>(std::size(entries)) * (lineH + padY) + padY};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 12, 12, 24, 200);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        for (const auto &entry : entries)
        {
            SDL_Rect swatch{x, y + (lineH - boxSize) / 2, boxSize, boxSize};
            SDL_SetRenderDrawColor(renderer, entry.color.r, entry.color.g, entry.color.b, 255);
            countedRenderFillRect(renderer, &swatch, stats);
            legendFont.drawText(renderer, entry.label, x + boxSize + 8, y, nullptr, SDL_Color{230, 230, 240, 255});
            y += lineH + padY;
        }
    }
}

class BattleScene;

class CampScene : public Scene
{
  public:
    enum class Tab : int
    {
        Upgrades = 0,
        Training = 1,
        Strategies = 2,
        Shop = 3
    };

    struct RowDisplay
    {
        std::string title;
        std::string subtitle;
        std::string cost;
        std::string hint;
        int index = -1;
        bool selected = false;
        bool disabled = false;
        bool affordable = false;
    };

    struct RowHitbox
    {
        SDL_Rect rect{0, 0, 0, 0};
        int index = -1;
        bool disabled = false;
        bool affordable = false;
    };

    enum class PointerTargetKind
    {
        None,
        Tab,
        Row,
        PurchaseButton,
        DeployButton
    };

    struct PointerLatch
    {
        PointerTargetKind kind = PointerTargetKind::None;
        int index = -1;
    };

    struct UndoRecord
    {
        enum class Kind
        {
            None,
            CampUpgrade,
            Training,
            Meta,
            Token
        };

        Kind kind = Kind::None;
        std::string id;
        int cost = 0;
        int previousLevel = 0;
        int previousTokens = 0;
        double timer = 0.0;
    };

    CampScene(std::shared_ptr<CampaignState> campaign, BattleScene *battle)
        : m_campaign(std::move(campaign)), m_battle(battle)
    {
    }

    void onEnter(GameApplication &app, SceneStack &) override
    {
        AssetManager &assets = app.assetManager();
        constexpr const char *kFontPath = "assets/ui/NotoSansJP-Regular.ttf";
        if (!m_titleFont.isLoaded())
        {
            if (!m_titleFont.load(assets, kFontPath, 28))
            {
                std::cerr << "[camp] Failed to load title font (" << kFontPath << ")\n";
            }
        }
        if (!m_bodyFont.isLoaded())
        {
            if (!m_bodyFont.load(assets, kFontPath, 22))
            {
                std::cerr << "[camp] Failed to load body font (" << kFontPath << ")\n";
            }
        }

        autoFocusAll(app.appConfig());
    }

    void onExit(GameApplication &, SceneStack &) override {}

    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override
    {
        const AppConfig &config = app.appConfig();
        switch (event.type)
        {
        case SDL_KEYDOWN:
        {
            const SDL_Keycode key = event.key.keysym.sym;
            switch (key)
            {
            case SDLK_ESCAPE:
            case SDLK_SPACE:
                deploy(stack, app);
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                handlePurchase(config);
                break;
            case SDLK_UP:
                moveSelection(-1, config);
                break;
            case SDLK_DOWN:
                moveSelection(1, config);
                break;
            case SDLK_LEFT:
                if (m_tab == Tab::Strategies)
                {
                    if (const StrategyCharacter *character = strategyAt(config, currentSelection()))
                    {
                        switchStrategyOption(*character, -1);
                    }
                }
                break;
            case SDLK_RIGHT:
                if (m_tab == Tab::Strategies)
                {
                    if (const StrategyCharacter *character = strategyAt(config, currentSelection()))
                    {
                        switchStrategyOption(*character, 1);
                    }
                }
                break;
            case SDLK_z:
                if (hasUndo())
                {
                    performUndo(config);
                }
                break;
            case SDLK_1:
            case SDLK_2:
            case SDLK_3:
            case SDLK_4:
            {
                const int idx = static_cast<int>(key - SDLK_1);
                if (idx >= 0 && idx < 4)
                {
                    setTab(static_cast<Tab>(idx), config);
                }
                break;
            }
            default:
                break;
            }
            break;
        }
        case SDL_MOUSEMOTION:
            handleMouseMotion(event.motion);
            break;
        case SDL_MOUSEBUTTONDOWN:
            handleMouseButtonDown(event.button, config);
            break;
        case SDL_MOUSEBUTTONUP:
            handleMouseButtonUp(event.button, config, stack, app);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_LEAVE)
            {
                clearPointerHover();
            }
            break;
        default:
            break;
        }
    }

    void update(double deltaSeconds, GameApplication &, SceneStack &) override
    {
        if (m_feedbackTimer > 0.0)
        {
            m_feedbackTimer = std::max(0.0, m_feedbackTimer - deltaSeconds);
        }
        updateUndo(deltaSeconds);
    }

    void render(SDL_Renderer *renderer, GameApplication &app) override
    {
        if (!renderer || (!m_titleFont.isLoaded() && !m_bodyFont.isLoaded()))
        {
            return;
        }
        SDL_SetRenderDrawColor(renderer, 8, 6, 18, 255);
        SDL_RenderClear(renderer);

        const AppConfig &config = app.appConfig();
        const int screenW = app.windowWidth();
        const int screenH = app.windowHeight();

        const TextRenderer &titleFont = m_titleFont.isLoaded() ? m_titleFont : m_bodyFont;
        const TextRenderer &bodyFont = m_bodyFont.isLoaded() ? m_bodyFont : m_titleFont;
        const int headerY = 24;
        titleFont.drawText(renderer, "Camp", 32, headerY, nullptr, SDL_Color{255, 230, 180, 255});

        std::ostringstream walletText;
        walletText << "Wallet: " << (m_campaign ? m_campaign->availableMana() : 0) << " mana";
        bodyFont.drawText(renderer, walletText.str(), 32, headerY + titleFont.getLineHeight() + 10, nullptr,
                          SDL_Color{210, 220, 255, 255});

        if (m_campaign)
        {
            std::ostringstream tokenText;
            tokenText << "Mana gain tokens: " << m_campaign->manaGainTokens;
            bodyFont.drawText(renderer,
                              tokenText.str(),
                              32,
                              headerY + titleFont.getLineHeight() + bodyFont.getLineHeight() + 18,
                              nullptr,
                              SDL_Color{200, 210, 255, 255});
        }

        beginHitTestFrame();
        renderTabs(renderer, config, headerY + titleFont.getLineHeight() + bodyFont.getLineHeight() + 40);
        renderContent(renderer, config, screenW, screenH);
        renderButtons(renderer, bodyFont, screenW, headerY);
        renderUndoPrompt(renderer, bodyFont, screenW, screenH);

        if (m_feedbackTimer > 0.0 && !m_feedbackText.empty())
        {
            const int textWidth = bodyFont.measureText(m_feedbackText);
            const int x = (screenW - textWidth) / 2;
            const int y = screenH - bodyFont.getLineHeight() - 24;
            bodyFont.drawText(renderer, m_feedbackText, x, y, nullptr, SDL_Color{255, 210, 160, 255});
        }
        refreshHoverTargets();
    }

  private:
    std::shared_ptr<CampaignState> m_campaign;
    BattleScene *m_battle = nullptr;
    Tab m_tab = Tab::Upgrades;
    std::array<int, 4> m_selection{{0, 0, 0, 0}};
    TextRenderer m_titleFont;
    TextRenderer m_bodyFont;
    double m_feedbackTimer = 0.0;
    std::string m_feedbackText;
    UndoRecord m_undo;
    static constexpr double UndoWindowSeconds = 3.0;
    std::array<SDL_Rect, 4> m_tabRects{};
    std::vector<RowHitbox> m_rowHits;
    SDL_Rect m_buttonPurchase{0, 0, 0, 0};
    SDL_Rect m_buttonDeploy{0, 0, 0, 0};
    PointerLatch m_pressTarget;
    SDL_Point m_pointerPos{0, 0};
    bool m_pointerActive = false;
    int m_hoverTab = -1;
    int m_hoverRow = -1;
    PointerTargetKind m_hoverButton = PointerTargetKind::None;

    int tabIndex(Tab tab) const { return static_cast<int>(tab); }

    int currentSelection() const
    {
        return m_selection[tabIndex(m_tab)];
    }

    void setTab(Tab tab, const AppConfig &config)
    {
        if (m_tab == tab)
        {
            return;
        }
        m_tab = tab;
        clampSelection(config);
    }

    void clampSelection(const AppConfig &config)
    {
        const int idx = tabIndex(m_tab);
        const int count = entryCountForTab(m_tab, config);
        if (count <= 0)
        {
            m_selection[idx] = 0;
        }
        else if (m_selection[idx] >= count)
        {
            m_selection[idx] = count - 1;
        }
        else if (m_selection[idx] < 0)
        {
            m_selection[idx] = 0;
        }
    }

    void moveSelection(int delta, const AppConfig &config)
    {
        if (delta == 0)
        {
            return;
        }
        const int idx = tabIndex(m_tab);
        const int count = entryCountForTab(m_tab, config);
        if (count <= 0)
        {
            m_selection[idx] = 0;
            return;
        }
        int next = m_selection[idx] + delta;
        next = std::clamp(next, 0, std::max(0, count - 1));
        m_selection[idx] = next;
    }

    void handleMouseMotion(const SDL_MouseMotionEvent &motion)
    {
        updatePointerPosition(motion.x, motion.y);
    }

    void handleMouseButtonDown(const SDL_MouseButtonEvent &button, const AppConfig &config)
    {
        if (button.button != SDL_BUTTON_LEFT)
        {
            return;
        }
        updatePointerPosition(button.x, button.y);
        const SDL_Point point{button.x, button.y};
        int index = -1;
        m_pressTarget.kind = determineTargetAtPoint(point, index);
        m_pressTarget.index = index;
        if (m_pressTarget.kind == PointerTargetKind::Row && index >= 0)
        {
            const int tabIdx = tabIndex(m_tab);
            const int count = entryCountForTab(m_tab, config);
            if (index >= 0 && index < count)
            {
                m_selection[tabIdx] = index;
            }
        }
    }

    void handleMouseButtonUp(const SDL_MouseButtonEvent &button,
                             const AppConfig &config,
                             SceneStack &stack,
                             GameApplication &app)
    {
        if (button.button != SDL_BUTTON_LEFT)
        {
            return;
        }
        updatePointerPosition(button.x, button.y);
        const SDL_Point point{button.x, button.y};
        switch (m_pressTarget.kind)
        {
        case PointerTargetKind::Tab:
            if (tabIndexAt(point.x, point.y) == m_pressTarget.index && m_pressTarget.index >= 0 &&
                m_pressTarget.index < static_cast<int>(m_tabRects.size()))
            {
                setTab(static_cast<Tab>(m_pressTarget.index), config);
            }
            break;
        case PointerTargetKind::Row:
            if (rowIndexAt(point.x, point.y) == m_pressTarget.index)
            {
                if (const RowHitbox *hit = rowHitboxForIndex(m_pressTarget.index))
                {
                    if (!hit->disabled && hit->affordable)
                    {
                        handlePurchase(config);
                    }
                }
            }
            break;
        case PointerTargetKind::PurchaseButton:
            if (buttonAt(point.x, point.y) == PointerTargetKind::PurchaseButton)
            {
                handlePurchase(config);
            }
            break;
        case PointerTargetKind::DeployButton:
            if (buttonAt(point.x, point.y) == PointerTargetKind::DeployButton)
            {
                deploy(stack, app);
            }
            break;
        case PointerTargetKind::None:
        default:
            break;
        }
        m_pressTarget = {};
    }

    void beginHitTestFrame()
    {
        m_rowHits.clear();
        for (SDL_Rect &rect : m_tabRects)
        {
            rect = SDL_Rect{0, 0, 0, 0};
        }
        m_buttonPurchase = SDL_Rect{0, 0, 0, 0};
        m_buttonDeploy = SDL_Rect{0, 0, 0, 0};
    }

    void updatePointerPosition(int x, int y)
    {
        m_pointerPos.x = x;
        m_pointerPos.y = y;
        m_pointerActive = true;
        refreshHoverTargets();
    }

    void refreshHoverTargets()
    {
        if (!m_pointerActive)
        {
            m_hoverTab = -1;
            m_hoverRow = -1;
            m_hoverButton = PointerTargetKind::None;
            return;
        }
        m_hoverTab = tabIndexAt(m_pointerPos.x, m_pointerPos.y);
        m_hoverRow = rowIndexAt(m_pointerPos.x, m_pointerPos.y);
        PointerTargetKind buttonTarget = buttonAt(m_pointerPos.x, m_pointerPos.y);
        if (buttonTarget == PointerTargetKind::PurchaseButton || buttonTarget == PointerTargetKind::DeployButton)
        {
            m_hoverButton = buttonTarget;
        }
        else
        {
            m_hoverButton = PointerTargetKind::None;
        }
    }

    void clearPointerHover()
    {
        m_pointerActive = false;
        refreshHoverTargets();
    }

    PointerTargetKind determineTargetAtPoint(const SDL_Point &point, int &indexOut) const
    {
        if (PointerTargetKind buttonTarget = buttonAt(point.x, point.y); buttonTarget != PointerTargetKind::None)
        {
            indexOut = -1;
            return buttonTarget;
        }
        const int row = rowIndexAt(point.x, point.y);
        if (row >= 0)
        {
            indexOut = row;
            return PointerTargetKind::Row;
        }
        const int tabHit = tabIndexAt(point.x, point.y);
        if (tabHit >= 0)
        {
            indexOut = tabHit;
            return PointerTargetKind::Tab;
        }
        indexOut = -1;
        return PointerTargetKind::None;
    }

    void handlePurchase(const AppConfig &config)
    {
        if (!m_campaign)
        {
            pushFeedback("No campaign data");
            return;
        }
        switch (m_tab)
        {
        case Tab::Upgrades:
            if (const CampUpgradeEntry *entry = upgradeAt(config, currentSelection()))
            {
                if (purchaseUpgrade(*entry))
                {
                    autoFocusTab(m_tab, config);
                }
            }
            break;
        case Tab::Training:
            if (const TrainingEntry *entry = trainingAt(config, currentSelection()))
            {
                if (purchaseTraining(*entry))
                {
                    autoFocusTab(m_tab, config);
                }
            }
            break;
        case Tab::Shop:
            if (const MetaShopItem *item = metaAt(config, currentSelection()))
            {
                if (purchaseMeta(*item))
                {
                    autoFocusTab(m_tab, config);
                }
            }
            break;
        case Tab::Strategies:
            if (const StrategyCharacter *character = strategyAt(config, currentSelection()))
            {
                switchStrategyOption(*character, 1);
            }
            break;
        }
    }

    bool purchaseUpgrade(const CampUpgradeEntry &entry)
    {
        if (!m_campaign)
        {
            return false;
        }
        CampShop shop(*m_campaign);
        CampShop::PurchaseOutcome outcome = shop.buyUpgrade(entry);
        switch (outcome.status)
        {
        case CampShop::Status::Success:
        {
            UndoRecord record;
            record.kind = UndoRecord::Kind::CampUpgrade;
            record.id = entry.id;
            record.previousLevel = outcome.previousLevel;
            record.cost = outcome.cost;
            beginUndo(record);
            pushFeedback(entry.label + " を強化しました");
            m_campaign->saveToDisk();
            return true;
        }
        case CampShop::Status::MaxLevel:
            pushFeedback(entry.label + " は最大です");
            break;
        case CampShop::Status::InsufficientMana:
            pushFeedback("マナが足りません");
            break;
        case CampShop::Status::Invalid:
        default:
            pushFeedback("購入できません");
            break;
        }
        return false;
    }

    bool purchaseTraining(const TrainingEntry &entry)
    {
        if (!m_campaign)
        {
            return false;
        }
        CampShop shop(*m_campaign);
        CampShop::PurchaseOutcome outcome = shop.buyTraining(entry);
        switch (outcome.status)
        {
        case CampShop::Status::Success:
        {
            UndoRecord record;
            record.kind = UndoRecord::Kind::Training;
            record.id = entry.id;
            record.previousLevel = outcome.previousLevel;
            record.cost = outcome.cost;
            beginUndo(record);
            pushFeedback(entry.label + " を伸ばしました");
            m_campaign->saveToDisk();
            return true;
        }
        case CampShop::Status::MaxLevel:
            pushFeedback(entry.label + " は完了済み");
            break;
        case CampShop::Status::InsufficientMana:
            pushFeedback("マナが足りません");
            break;
        case CampShop::Status::Invalid:
        default:
            pushFeedback("購入できません");
            break;
        }
        return false;
    }

    bool purchaseMeta(const MetaShopItem &item)
    {
        if (!m_campaign)
        {
            return false;
        }
        CampShop shop(*m_campaign);
        CampShop::PurchaseOutcome outcome = shop.buyMeta(item);
        switch (outcome.status)
        {
        case CampShop::Status::Success:
        {
            UndoRecord record;
            record.cost = outcome.cost;
            record.previousLevel = outcome.previousLevel;
            record.previousTokens = outcome.previousTokens;
            if (outcome.consumable)
            {
                record.kind = UndoRecord::Kind::Token;
                pushFeedback(item.label + " を補充しました");
            }
            else
            {
                record.kind = UndoRecord::Kind::Meta;
                record.id = item.id;
                pushFeedback(item.label + " を強化しました");
            }
            beginUndo(record);
            m_campaign->saveToDisk();
            return true;
        }
        case CampShop::Status::MaxLevel:
            pushFeedback(item.label + " は最大です");
            break;
        case CampShop::Status::InsufficientMana:
            pushFeedback("マナが足りません");
            break;
        case CampShop::Status::Invalid:
        default:
            pushFeedback("購入できません");
            break;
        }
        return false;
    }

    void switchStrategyOption(const StrategyCharacter &character, int direction)
    {
        if (!m_campaign || character.options.empty() || direction == 0)
        {
            return;
        }
        const std::string currentId = m_campaign->strategyOption(character.id, character.defaultOption);
        int currentIndex = 0;
        for (std::size_t i = 0; i < character.options.size(); ++i)
        {
            if (character.options[i].id == currentId)
            {
                currentIndex = static_cast<int>(i);
                break;
            }
        }
        const int optionCount = static_cast<int>(character.options.size());
        currentIndex = (currentIndex + optionCount + direction) % optionCount;
        const StrategyOption &next = character.options[static_cast<std::size_t>(currentIndex)];
        CampShop shop(*m_campaign);
        if (shop.selectStrategy(character, next.id) == CampShop::Status::Success)
        {
            pushFeedback(character.label + " strategy set to " + next.label);
            m_campaign->saveToDisk();
        }
        else
        {
            pushFeedback("作戦を変更できません");
        }
    }

    void deploy(SceneStack &stack, GameApplication &app);
    bool hasUndo() const { return m_undo.kind != UndoRecord::Kind::None; }

    void pushFeedback(const std::string &text)
    {
        m_feedbackText = text;
        m_feedbackTimer = 2.0;
    }

    int entryCountForTab(Tab tab, const AppConfig &config) const
    {
        switch (tab)
        {
        case Tab::Upgrades: return static_cast<int>(config.campUpgrades.size());
        case Tab::Training: return static_cast<int>(config.trainingEntries.size());
        case Tab::Strategies: return static_cast<int>(config.strategyCharacters.size());
        case Tab::Shop: return static_cast<int>(config.metaShopItems.size());
        }
        return 0;
    }

    const CampUpgradeEntry *upgradeAt(const AppConfig &config, int index) const
    {
        if (index < 0 || index >= static_cast<int>(config.campUpgrades.size()))
        {
            return nullptr;
        }
        return &config.campUpgrades[static_cast<std::size_t>(index)];
    }

    const TrainingEntry *trainingAt(const AppConfig &config, int index) const
    {
        if (index < 0 || index >= static_cast<int>(config.trainingEntries.size()))
        {
            return nullptr;
        }
        return &config.trainingEntries[static_cast<std::size_t>(index)];
    }

    const StrategyCharacter *strategyAt(const AppConfig &config, int index) const
    {
        if (index < 0 || index >= static_cast<int>(config.strategyCharacters.size()))
        {
            return nullptr;
        }
        return &config.strategyCharacters[static_cast<std::size_t>(index)];
    }

    const MetaShopItem *metaAt(const AppConfig &config, int index) const
    {
        if (index < 0 || index >= static_cast<int>(config.metaShopItems.size()))
        {
            return nullptr;
        }
        return &config.metaShopItems[static_cast<std::size_t>(index)];
    }

    void autoFocusAll(const AppConfig &config)
    {
        autoFocusTab(Tab::Upgrades, config);
        autoFocusTab(Tab::Training, config);
        autoFocusTab(Tab::Shop, config);
        autoFocusTab(Tab::Strategies, config);
        clampSelection(config);
    }

    void autoFocusTab(Tab tab, const AppConfig &config)
    {
        const int idx = tabIndex(tab);
        const int count = entryCountForTab(tab, config);
        if (count <= 0)
        {
            m_selection[idx] = 0;
            return;
        }
        const int selection = findInitialSelection(tab, config);
        m_selection[idx] = std::clamp(selection, 0, count - 1);
        if (tab == m_tab)
        {
            clampSelection(config);
        }
    }

    int findInitialSelection(Tab tab, const AppConfig &config) const
    {
        const int wallet = m_campaign ? m_campaign->availableMana() : 0;
        const int count = entryCountForTab(tab, config);
        if (count <= 0)
        {
            return 0;
        }

        auto pickFromList = [&](auto begin, auto end, auto &&extractCost, auto &&isAvailable) {
            int cheapestIdx = 0;
            int cheapestCost = std::numeric_limits<int>::max();
            bool hasCheapest = false;
            for (auto it = begin; it != end; ++it)
            {
                const int idx = static_cast<int>(std::distance(begin, it));
                if (!isAvailable(*it, idx))
                {
                    continue;
                }
                const int cost = extractCost(*it, idx);
                if (wallet >= cost)
                {
                    return idx;
                }
                if (!hasCheapest || cost < cheapestCost)
                {
                    cheapestCost = cost;
                    cheapestIdx = idx;
                    hasCheapest = true;
                }
            }
            return hasCheapest ? cheapestIdx : 0;
        };

        switch (tab)
        {
        case Tab::Upgrades:
            return pickFromList(config.campUpgrades.begin(),
                                config.campUpgrades.end(),
                                [this](const CampUpgradeEntry &entry, int) {
                                    return upgradeCost(entry, m_campaign ? m_campaign->campLevel(entry.id) : 0);
                                },
                                [this](const CampUpgradeEntry &entry, int) {
                                    const int currentLevel = m_campaign ? m_campaign->campLevel(entry.id) : 0;
                                    const int maxLevel =
                                        entry.maxLevel > 0 ? entry.maxLevel : static_cast<int>(entry.costs.size());
                                    return !(maxLevel > 0 && currentLevel >= maxLevel);
                                });
        case Tab::Training:
            return pickFromList(config.trainingEntries.begin(),
                                config.trainingEntries.end(),
                                [this](const TrainingEntry &entry, int) {
                                    return trainingCost(entry, m_campaign ? m_campaign->trainingStep(entry.id) : 0);
                                },
                                [this](const TrainingEntry &entry, int) {
                                    const int currentLevel = m_campaign ? m_campaign->trainingStep(entry.id) : 0;
                                    return !trainingIsMaxed(entry, currentLevel);
                                });
        case Tab::Shop:
            return pickFromList(config.metaShopItems.begin(),
                                config.metaShopItems.end(),
                                [this](const MetaShopItem &item, int) {
                                    const bool consumable = item.type == "consumable";
                                    const int level = consumable ? 0 : (m_campaign ? m_campaign->metaLevel(item.id) : 0);
                                    return metaCost(item, level);
                                },
                                [this](const MetaShopItem &item, int) {
                                    if (item.type == "consumable")
                                    {
                                        return true;
                                    }
                                    const int currentLevel = m_campaign ? m_campaign->metaLevel(item.id) : 0;
                                    return !(item.maxLevel > 0 && currentLevel >= item.maxLevel);
                                });
        case Tab::Strategies:
        default:
            return std::clamp(m_selection[tabIndex(Tab::Strategies)], 0, count - 1);
        }
    }

    int upgradeCost(const CampUpgradeEntry &entry, int level) const
    {
        if (entry.maxLevel > 0 && level >= entry.maxLevel)
        {
            return 0;
        }
        if (!entry.costs.empty())
        {
            if (level < static_cast<int>(entry.costs.size()))
            {
                return entry.costs[static_cast<std::size_t>(level)];
            }
            return entry.costs.back();
        }
        return 0;
    }

    int trainingCost(const TrainingEntry &entry, int level) const
    {
        return trainingCostAtLevel(entry, level);
    }

    int metaCost(const MetaShopItem &item, int level) const
    {
        return item.baseCost + item.perLevelCost * std::max(level, 0);
    }

    std::string formatDelta(const CampUpgradeEntry &entry) const
    {
        std::ostringstream oss;
        if (entry.type == "percent")
        {
            oss << '+' << std::round(entry.delta * 100.0f) << '%';
        }
        else
        {
            oss << (entry.delta >= 0.0f ? "+" : "") << entry.delta;
        }
        return oss.str();
    }

    std::string formatTrainingDelta(const TrainingEntry &entry, int level) const
    {
        std::ostringstream oss;
        const float delta = trainingDeltaAtLevel(entry, level);
        if (entry.id == "mean_level")
        {
            oss << "次 Lv+" << delta;
        }
        else if (entry.id == "elite_rate")
        {
            oss << "次 +" << static_cast<int>(std::round(delta * 100.0f)) << "%";
        }
        else if (entry.id == "yuna_level")
        {
            const AllyLevelingParams params = kCommanderLevelingParams;
            const int nextLevel = trainingAbsoluteLevel(entry, level) + 1;
            auto formatPercent = [](float value) {
                std::ostringstream tmp;
                tmp << std::fixed << std::setprecision(value < 0.1f ? 3 : 2) << value;
                return tmp.str();
            };
            oss << "次 Lv" << nextLevel << "：HP+" << params.hpPerLevel;
            oss << " / DPS+" << formatPercent(params.dpsPerLevel * 100.0f) << "%";
            oss << " / SPD+" << formatPercent(params.speedPerLevel * 100.0f) << "%";
        }
        else
        {
            oss << "Next +" << delta;
        }
        return oss.str();
    }

    void renderTabs(SDL_Renderer *renderer, const AppConfig &, int startY)
    {
        if (!m_bodyFont.isLoaded())
        {
            return;
        }
        const std::array<std::string, 4> tabs{"拠点強化", "訓練", "作戦", "ルーの店"};
        const int padding = 20;
        int x = 32;
        const int height = m_bodyFont.getLineHeight() + 12;
        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            const std::string &label = tabs[i];
            const int width = m_bodyFont.measureText(label) + padding * 2;
            SDL_Rect rect{x, startY, width, height};
            m_tabRects[i] = rect;
            const bool selected = static_cast<int>(i) == tabIndex(m_tab);
            const bool hovered = static_cast<int>(i) == m_hoverTab;
            if (selected)
            {
                SDL_SetRenderDrawColor(renderer, 60, 40, 100, 220);
            }
            else if (hovered)
            {
                SDL_SetRenderDrawColor(renderer, 50, 50, 90, 200);
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, 30, 30, 60, 160);
            }
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 90, 80, 140, 255);
            SDL_RenderDrawRect(renderer, &rect);
            m_bodyFont.drawText(renderer,
                                label,
                                x + padding,
                                startY + (height - m_bodyFont.getLineHeight()) / 2,
                                nullptr,
                                SDL_Color{255, 255, 255, 255});
            x += width + 8;
        }
    }

    void renderContent(SDL_Renderer *renderer, const AppConfig &config, int screenW, int screenH)
    {
        if (!m_bodyFont.isLoaded())
        {
            return;
        }
        const int panelX = 32;
        const int panelY = 200;
        const int panelWidth = screenW - panelX * 2;
        SDL_Rect panel{panelX, panelY, panelWidth, screenH - panelY - 80};
        SDL_SetRenderDrawColor(renderer, 15, 12, 28, 200);
        SDL_RenderFillRect(renderer, &panel);

        std::vector<RowDisplay> rows;
        rows.reserve(8);
        const int wallet = m_campaign ? m_campaign->availableMana() : 0;
        const int selection = currentSelection();

        switch (m_tab)
        {
        case Tab::Upgrades:
            for (std::size_t i = 0; i < config.campUpgrades.size(); ++i)
            {
                const CampUpgradeEntry &entry = config.campUpgrades[i];
                RowDisplay row;
                row.index = static_cast<int>(i);
                const int level = m_campaign ? m_campaign->campLevel(entry.id) : 0;
                const int maxLevel = entry.maxLevel > 0 ? entry.maxLevel : static_cast<int>(entry.costs.size());
                const bool maxed = maxLevel > 0 && level >= maxLevel;
                const int cost = upgradeCost(entry, level);
                row.title = entry.label + "  [" + std::to_string(level) + "/" +
                            (maxLevel > 0 ? std::to_string(maxLevel) : std::string("-")) + "]";
                row.subtitle = formatDelta(entry) + " per level";
                row.cost = maxed ? "MAX" : (cost > 0 ? (std::to_string(cost) + " mana") : "Free");
                row.selected = static_cast<int>(i) == selection;
                row.disabled = maxed;
                row.affordable = wallet >= cost;
                if (!row.disabled && cost > 0 && wallet < cost)
                {
                    row.hint = "あと" + std::to_string(cost - wallet) + "マナ必要";
                }
                rows.push_back(row);
            }
            break;
        case Tab::Training:
            for (std::size_t i = 0; i < config.trainingEntries.size(); ++i)
            {
                const TrainingEntry &entry = config.trainingEntries[i];
                RowDisplay row;
                row.index = static_cast<int>(i);
                const int level = m_campaign ? m_campaign->trainingStep(entry.id) : 0;
                const bool maxed = trainingIsMaxed(entry, level);
                const int displayLevel = trainingIsRepeatable(entry) ? trainingAbsoluteLevel(entry, level) : level;
                const int maxLevel = trainingMaxDisplayLevel(entry);
                const int cost = trainingCost(entry, level);
                std::string maxLabel = maxLevel > 0 ? std::to_string(maxLevel) : std::string("-");
                row.title = entry.label + "  [" + std::to_string(displayLevel) + "/" + maxLabel + "]";
                row.subtitle = maxed ? "Complete" : formatTrainingDelta(entry, level);
                row.cost = std::to_string(cost) + " mana";
                row.selected = static_cast<int>(i) == selection;
                row.disabled = false;
                row.affordable = wallet >= cost;
                if (!row.disabled && cost > 0 && wallet < cost)
                {
                    row.hint = "あと" + std::to_string(cost - wallet) + "マナ必要";
                }
                rows.push_back(row);
            }
            break;
        case Tab::Strategies:
            for (std::size_t i = 0; i < config.strategyCharacters.size(); ++i)
            {
                const StrategyCharacter &character = config.strategyCharacters[i];
                const std::string current = m_campaign ? m_campaign->strategyOption(character.id, character.defaultOption)
                                                       : character.defaultOption;
                std::string label = character.label + ": ";
                for (const StrategyOption &option : character.options)
                {
                    if (option.id == current)
                    {
                        label += option.label;
                        break;
                    }
                }
                RowDisplay row;
                row.index = static_cast<int>(i);
                row.title = label;
                row.subtitle = "Use Left/Right to cycle";
                row.cost.clear();
                row.selected = static_cast<int>(i) == selection;
                row.disabled = false;
                row.affordable = true;
                rows.push_back(row);
            }
            break;
        case Tab::Shop:
            for (std::size_t i = 0; i < config.metaShopItems.size(); ++i)
            {
                const MetaShopItem &item = config.metaShopItems[i];
                RowDisplay row;
                row.index = static_cast<int>(i);
                const bool consumable = item.type == "consumable";
                const int level = consumable ? 0 : (m_campaign ? m_campaign->metaLevel(item.id) : 0);
                const bool maxed = false; // 上限撤廃
                const int cost = metaCost(item, level);
                row.title = item.label + (consumable ? "" : ("  [" + std::to_string(level) + "/-]"));
                if (consumable)
                {
                    const int stock = m_campaign ? m_campaign->manaGainTokens : 0;
                    row.subtitle = "Stocked: " + std::to_string(stock);
                }
                else
                {
                    row.subtitle.clear();
                }
                row.cost = maxed ? "MAX" : (std::to_string(cost) + " mana");
                row.selected = static_cast<int>(i) == selection;
                row.disabled = maxed;
                row.affordable = wallet >= cost;
                if (!row.disabled && cost > 0 && wallet < cost)
                {
                    row.hint = "あと" + std::to_string(cost - wallet) + "マナ必要";
                }
                rows.push_back(row);
            }
            break;
        }

        renderRows(renderer, rows, panelX + 16, panelY + 16, panelWidth - 32);
    }

    void renderButtons(SDL_Renderer *renderer, const TextRenderer &font, int screenW, int headerY)
    {
        if (!renderer || !font.isLoaded())
        {
            m_buttonPurchase = SDL_Rect{0, 0, 0, 0};
            m_buttonDeploy = SDL_Rect{0, 0, 0, 0};
            return;
        }
        const int buttonHeight = font.getLineHeight() + 12;
        const int purchaseWidth = std::max(140, font.measureText("Purchase") + 32);
        const int deployWidth = std::max(120, font.measureText("Deploy") + 32);
        const int spacing = 12;
        const int padding = 32;
        const int totalWidth = purchaseWidth + deployWidth + spacing;
        const int x = screenW - totalWidth - padding;
        const int y = headerY;
        m_buttonPurchase = SDL_Rect{x, y, purchaseWidth, buttonHeight};
        m_buttonDeploy = SDL_Rect{x + purchaseWidth + spacing, y, deployWidth, buttonHeight};

        drawButton(renderer,
                   font,
                   m_buttonPurchase,
                   "Purchase",
                   m_hoverButton == PointerTargetKind::PurchaseButton,
                   false);
        drawButton(renderer,
                   font,
                   m_buttonDeploy,
                   "Deploy",
                   m_hoverButton == PointerTargetKind::DeployButton,
                   true);
    }

    void renderRows(SDL_Renderer *renderer,
                    const std::vector<RowDisplay> &rows,
                    int x,
                    int y,
                    int width)
    {
        if (!m_bodyFont.isLoaded())
        {
            return;
        }
        const int lineHeight = std::max(m_bodyFont.getLineHeight(), 20);
        for (const RowDisplay &row : rows)
        {
            SDL_Rect rowRect{x, y, width, lineHeight + 12};
            int advance = lineHeight + 18;
            if (!row.hint.empty())
            {
                advance += 12;
            }
            SDL_Rect rowRectFull = rowRect;
            rowRectFull.h = advance;
            if (row.selected)
            {
                SDL_SetRenderDrawColor(renderer, 70, 50, 110, 200);
                SDL_RenderFillRect(renderer, &rowRectFull);
            }
            else if (row.index >= 0 && row.index == m_hoverRow)
            {
                SDL_SetRenderDrawColor(renderer, row.disabled ? 40 : 50, 45, 80, row.disabled ? 120 : 170);
                SDL_RenderFillRect(renderer, &rowRectFull);
            }
            SDL_Color titleColor = row.disabled ? SDL_Color{140, 140, 140, 255} : SDL_Color{255, 255, 255, 255};
            SDL_Color subColor = row.disabled ? SDL_Color{120, 120, 120, 255} : SDL_Color{190, 200, 230, 255};
            m_bodyFont.drawText(renderer, row.title, x + 12, y + 4, nullptr, titleColor);
            if (!row.subtitle.empty())
            {
                m_bodyFont.drawText(renderer, row.subtitle, x + 12, y + lineHeight - 4, nullptr, subColor);
            }
            if (!row.cost.empty())
            {
                SDL_Color costColor =
                    row.disabled ? SDL_Color{120, 120, 120, 255}
                                 : (row.affordable ? SDL_Color{180, 255, 200, 255} : SDL_Color{255, 180, 150, 255});
                const int costWidth = m_bodyFont.measureText(row.cost);
                m_bodyFont.drawText(renderer,
                                    row.cost,
                                    x + width - costWidth - 16,
                                    y + 4,
                                    nullptr,
                                    costColor);
            }
            if (!row.hint.empty())
            {
                SDL_Color hintColor = SDL_Color{255, 130, 130, 255};
                m_bodyFont.drawText(renderer, row.hint, x + 12, y + lineHeight + 6, nullptr, hintColor);
            }
            if (row.index >= 0)
            {
                RowHitbox hit;
                hit.rect = rowRectFull;
                hit.index = row.index;
                hit.disabled = row.disabled;
                hit.affordable = row.affordable;
                m_rowHits.push_back(hit);
            }
            y += advance;
        }
    }

    void drawButton(SDL_Renderer *renderer,
                    const TextRenderer &font,
                    const SDL_Rect &rect,
                    const std::string &label,
                    bool hovered,
                    bool accent)
    {
        if (!renderer || !font.isLoaded())
        {
            return;
        }
        const SDL_Color baseColor = accent ? SDL_Color{40, 90, 95, 210} : SDL_Color{40, 35, 80, 210};
        const SDL_Color hoverColor = accent ? SDL_Color{70, 130, 140, 235} : SDL_Color{80, 65, 140, 230};
        const SDL_Color fill = hovered ? hoverColor : baseColor;
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 120, 110, 170, 255);
        SDL_RenderDrawRect(renderer, &rect);
        const int textWidth = font.measureText(label);
        const int textX = rect.x + (rect.w - textWidth) / 2;
        const int textY = rect.y + (rect.h - font.getLineHeight()) / 2;
        font.drawText(renderer, label, textX, textY, nullptr, SDL_Color{255, 255, 255, 255});
    }

    void renderUndoPrompt(SDL_Renderer *renderer, const TextRenderer &font, int screenW, int screenH) const
    {
        if (!renderer || !font.isLoaded() || !hasUndo())
        {
            return;
        }
        std::ostringstream oss;
        oss << "[Z] 取り消し (残り" << std::fixed << std::setprecision(1) << std::max(m_undo.timer, 0.0) << "s)";
        const std::string text = oss.str();
        const int textWidth = font.measureText(text);
        const int x = screenW - textWidth - 32;
        const int y = screenH - font.getLineHeight() - 24;
        font.drawText(renderer, text, x, y, nullptr, SDL_Color{255, 230, 180, 255});
    }

    void beginUndo(const UndoRecord &record)
    {
        m_undo = record;
        m_undo.timer = UndoWindowSeconds;
    }

    void cancelUndo()
    {
        m_undo = {};
    }

    void updateUndo(double deltaSeconds)
    {
        if (!hasUndo())
        {
            return;
        }
        m_undo.timer = std::max(0.0, m_undo.timer - deltaSeconds);
        if (m_undo.timer <= 0.0)
        {
            cancelUndo();
        }
    }

    void performUndo(const AppConfig &config)
    {
        if (!hasUndo() || !m_campaign)
        {
            return;
        }
        switch (m_undo.kind)
        {
        case UndoRecord::Kind::CampUpgrade:
            setMappedLevel(m_campaign->campUpgradeLevels, m_undo.id, m_undo.previousLevel);
            break;
        case UndoRecord::Kind::Training:
            setMappedLevel(m_campaign->trainingProgress, m_undo.id, m_undo.previousLevel);
            break;
        case UndoRecord::Kind::Meta:
            setMappedLevel(m_campaign->metaLevels, m_undo.id, m_undo.previousLevel);
            break;
        case UndoRecord::Kind::Token:
            m_campaign->manaGainTokens = std::max(0, m_undo.previousTokens);
            break;
        case UndoRecord::Kind::None:
            break;
        }
        if (m_undo.cost > 0)
        {
            m_campaign->depositMana(m_undo.cost);
        }
        cancelUndo();
        pushFeedback("購入を取り消しました");
        autoFocusTab(m_tab, config);
        if (m_campaign)
        {
            m_campaign->saveToDisk();
        }
    }

    void setMappedLevel(std::unordered_map<std::string, int> &map, const std::string &id, int level)
    {
        if (level <= 0)
        {
            map.erase(id);
        }
        else
        {
            map[id] = level;
        }
    }

    int tabIndexAt(int x, int y) const
    {
        for (std::size_t i = 0; i < m_tabRects.size(); ++i)
        {
            if (rectValid(m_tabRects[i]) && pointInRect(m_tabRects[i], x, y))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int rowIndexAt(int x, int y) const
    {
        for (const RowHitbox &hit : m_rowHits)
        {
            if (pointInRect(hit.rect, x, y))
            {
                return hit.index;
            }
        }
        return -1;
    }

    PointerTargetKind buttonAt(int x, int y) const
    {
        if (rectValid(m_buttonPurchase) && pointInRect(m_buttonPurchase, x, y))
        {
            return PointerTargetKind::PurchaseButton;
        }
        if (rectValid(m_buttonDeploy) && pointInRect(m_buttonDeploy, x, y))
        {
            return PointerTargetKind::DeployButton;
        }
        return PointerTargetKind::None;
    }

    const RowHitbox *rowHitboxForIndex(int index) const
    {
        for (const RowHitbox &hit : m_rowHits)
        {
            if (hit.index == index)
            {
                return &hit;
            }
        }
        return nullptr;
    }

    static bool rectValid(const SDL_Rect &rect)
    {
        return rect.w > 0 && rect.h > 0;
    }

    static bool pointInRect(const SDL_Rect &rect, int x, int y)
    {
        return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
    }
};

class BattleScene : public Scene
{
  public:
    explicit BattleScene(std::shared_ptr<CampaignState> campaign, bool startPausedForCamp = false);

    void onEnter(GameApplication &app, SceneStack &stack) override;
    void onExit(GameApplication &app, SceneStack &stack) override;
    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override;
    void update(double deltaSeconds, GameApplication &app, SceneStack &stack) override;
    void render(SDL_Renderer *renderer, GameApplication &app) override;
    void onConfigReloaded(GameApplication &app, SceneStack &stack) override;
    void startRunFromCamp(GameApplication &app);

  private:
    void handleActionFrame(const ActionBuffer::Frame &frame, GameApplication &app);
    void applyAppConfig(GameApplication &app);
    void showTelemetryMessage(const std::string &message);
    void evaluatePerformanceBudgets(GameApplication &app);
    void raisePerformanceWarning(const telemetry::BudgetViolation &violation, GameApplication &app);

    class DebugSimulationAccessor : public debug::DebugBindings::SimulationAccessor
    {
      public:
        explicit DebugSimulationAccessor(BattleScene &scene) : m_scene(scene) {}

        void markComponentsDirty() override;
        void setEnemySpawnMultiplier(float multiplier) override;
        float enemySpawnMultiplier() const override;
        bool skipNextWave() override;

      private:
        BattleScene &m_scene;
    };

    bool m_initialized = false;
    std::shared_ptr<CampaignState> m_campaign;
    world::WorldState m_world;
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
    double m_lastInputMs = 0.0;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    std::shared_ptr<TelemetrySink> m_telemetry;
    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<AssetManager> m_assetService;
    UiPresenter m_ui;
    UiView m_uiView;
    std::shared_ptr<UiPresenter> m_uiServiceHandle;
    ActionBuffer m_actionBuffer;
    std::uint64_t m_inputSequence = 0;
    std::uint64_t m_lastProcessedSequence = 0;
    bool m_haveProcessedSequence = false;
    bool m_pendingBudgetCheck = false;
    struct StageTimings
    {
        double updateMs = 0.0;
        double renderMs = 0.0;
        double inputMs = 0.0;
        double hudMs = 0.0;
    } m_lastStageTimings{};
    PerformanceBudgetConfig m_performanceBudget{};
    telemetry::PerformanceBudgetMonitor m_budgetMonitor{};
    Uint64 m_lastBudgetWarningTick = 0;
    static constexpr Uint64 BudgetWarningCooldownMs = 1000;
    debug::DebugController m_debugController;
    debug::DebugOverlayView m_debugOverlay;
    debug::DisplayState m_debugDisplayState;
    DebugSimulationAccessor m_debugAccessor;
    bool m_showTelemetryOverlay = false;
    int m_cursorRestoreState = SDL_QUERY;
    bool m_startPausedForCamp = false;
    bool m_pausedForCamp = false;
    bool m_showChibiLabels = true;
    bool m_cursorCrosshairActive = false;
    bool m_resultOverlayActive = false;
    bool m_resultRecorded = false;
    std::string m_resultSummary;
    std::vector<float> m_speedSteps{1.0f};
    int m_speedIndex = 0;
    float m_userTimeScale = 1.0f;

    void initializeDebugBindings(GameApplication &app);
    void updateDebugToggles();
    void applyCampaignModifiers(GameApplication &app);
    void initializeRun(GameApplication &app);
    void renderResultOverlay(SDL_Renderer *renderer, GameApplication &app);
    void handleResultOverlayKey(SDL_Keycode key, GameApplication &app, SceneStack &stack);
    void cycleGameSpeed(int direction = 1);
    void resetResultState();
};

class TitleScene : public Scene
{
  public:
    explicit TitleScene(std::shared_ptr<CampaignState> campaign) : m_campaign(std::move(campaign)) {}

    void onEnter(GameApplication &app, SceneStack &) override
    {
        AssetManager &assets = app.assetManager();
        constexpr const char *kFontPath = "assets/ui/NotoSansJP-Regular.ttf";
        if (!m_titleFont.isLoaded())
        {
            m_titleFont.load(assets, kFontPath, 48);
        }
        if (!m_bodyFont.isLoaded())
        {
            m_bodyFont.load(assets, kFontPath, 24);
        }
        if (m_campaign && !m_loadedOnce)
        {
            m_campaign->loadFromDisk();
            m_loadedOnce = true;
        }
    }

    void onExit(GameApplication &, SceneStack &) override {}

    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override
    {
        if (m_started)
        {
            return;
        }
        if (event.type == SDL_KEYDOWN)
        {
            const SDL_Keycode key = event.key.keysym.sym;
            if (key == SDLK_ESCAPE)
            {
                app.requestQuit();
                return;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
            {
                start(stack);
            }
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN)
        {
            // 左クリック=YES、右クリック=NO的な操作。現状はどちらも開始に寄せる。
            if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
            {
                start(stack);
            }
        }
    }

    void update(double, GameApplication &, SceneStack &) override {}

    void render(SDL_Renderer *renderer, GameApplication &app) override
    {
        if (m_started)
        {
            return;
        }
        if (!renderer)
        {
            return;
        }
        SDL_SetRenderDrawColor(renderer, 12, 16, 24, 255);
        SDL_RenderClear(renderer);

        const int screenW = app.windowWidth();
        const int screenH = app.windowHeight();
        const char *title = "Kusozako Trial";
        const char *prompt = "Press Enter/Space to start";
        const char *subtitle = "Save will load into Camp, then battle";
        const int titleSize = 48;
        const int bodySize = 24;
        const int titleW = m_titleFont.isLoaded() ? measureText(m_titleFont, title, titleSize) : 0;
        const int promptW = m_bodyFont.isLoaded() ? measureText(m_bodyFont, prompt, bodySize) : 0;
        const int subtitleW = m_bodyFont.isLoaded() ? measureText(m_bodyFont, subtitle, bodySize - 2) : 0;
        const int centerX = screenW / 2;
        const int centerY = screenH / 2;
        if (m_titleFont.isLoaded())
        {
            m_titleFont.drawText(renderer,
                                 title,
                                 centerX - titleW / 2,
                                 centerY - titleSize - 20,
                                 nullptr,
                                 SDL_Color{255, 230, 200, 255});
        }
        if (m_bodyFont.isLoaded())
        {
            m_bodyFont.drawText(renderer,
                                prompt,
                                centerX - promptW / 2,
                                centerY + 6,
                                nullptr,
                                SDL_Color{200, 220, 255, 255});
            m_bodyFont.drawText(renderer,
                                subtitle,
                                centerX - subtitleW / 2,
                                centerY + bodySize + 14,
                                nullptr,
                                SDL_Color{180, 200, 230, 220});
        }
    }

  private:
    int measureText(const TextRenderer &renderer, const std::string &text, int approxHeight) const
    {
        const int measured = renderer.measureText(text);
        if (measured > 0)
        {
            return measured;
        }
        const int approxWidth = std::max(approxHeight / 2, 8);
        return static_cast<int>(text.size()) * approxWidth;
    }

    void start(SceneStack &stack)
    {
        if (m_started)
        {
            return;
        }
        m_started = true;
        auto battle = std::make_unique<BattleScene>(m_campaign, true);
        BattleScene *battlePtr = battle.get();
        stack.push(std::move(battle));
        stack.push(std::make_unique<CampScene>(m_campaign, battlePtr));
    }

    std::shared_ptr<CampaignState> m_campaign;
    TextRenderer m_titleFont;
    TextRenderer m_bodyFont;
    bool m_loadedOnce = false;
    bool m_started = false;
};

BattleScene::BattleScene(std::shared_ptr<CampaignState> campaign, bool startPausedForCamp)
    : m_campaign(std::move(campaign)), m_debugAccessor(*this), m_startPausedForCamp(startPausedForCamp)
{
    m_showChibiLabels = g_showChibiLabels;
}

void BattleScene::onEnter(GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (m_initialized)
    {
        return;
    }

    m_screenWidth = app.windowWidth();
    m_screenHeight = app.windowHeight();
    ServiceLocator &locator = ServiceLocator::instance();
    m_telemetry = locator.getService<TelemetrySink>();
    m_eventBus = locator.getService<EventBus>();
    m_assetService = locator.getService<AssetManager>();

    if (!m_uiServiceHandle)
    {
        m_uiServiceHandle = std::shared_ptr<UiPresenter>(&m_ui, [](UiPresenter *) {});
    }
    locator.registerService<UiPresenter>(m_uiServiceHandle);

    if (!m_assetService)
    {
        std::cerr << "AssetManager service not available.\n";
        if (m_telemetry)
        {
            TelemetrySink::Payload payload{{"scene", "BattleScene"}, {"reason", "asset_manager_missing"}};
            m_telemetry->recordEvent("scene.warning", payload);
        }
        return;
    }

    applyAppConfig(app);

    if (m_startPausedForCamp)
    {
        m_pausedForCamp = true;
    }

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

    if (m_debugController.active())
    {
        if (m_cursorRestoreState != SDL_QUERY)
        {
            SDL_ShowCursor(m_cursorRestoreState);
        }
        m_debugController.toggle();
    }
    m_cursorRestoreState = SDL_QUERY;
    m_showTelemetryOverlay = false;
    m_debugController.bindWorld({});
    ServiceLocator &locator = ServiceLocator::instance();

    m_ui.bindSimulation(nullptr);
    m_ui.setEventBus(nullptr);
    m_ui.setTelemetrySink(nullptr);
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
    locator.unregisterService<UiPresenter>();
    m_uiServiceHandle.reset();
}

void BattleScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    (void)app;
    (void)stack;
    if (!m_initialized)
    {
        return;
    }
    if (m_resultOverlayActive)
    {
        if (event.type == SDL_KEYDOWN)
        {
            handleResultOverlayKey(event.key.keysym.sym, app, stack);
        }
        return;
    }
    if (m_pausedForCamp)
    {
        return;
    }
    if (event.type == SDL_KEYDOWN)
    {
        if (event.key.keysym.sym == SDLK_l)
        {
            m_showChibiLabels = !m_showChibiLabels;
            g_showChibiLabels = m_showChibiLabels;
            return;
        }
    }
    m_debugController.handleEvent(event);
}

void BattleScene::handleActionFrame(const ActionBuffer::Frame &frame, GameApplication &app)
{
    if (m_pausedForCamp || m_resultOverlayActive)
    {
        return;
    }
    if (m_haveProcessedSequence && frame.sequence == m_lastProcessedSequence)
    {
        return;
    }
    m_haveProcessedSequence = true;
    m_lastProcessedSequence = frame.sequence;

    LegacySimulation &sim = m_world.legacy();

    auto handleSkillSelect = [this](ActionId id) {
        const int baseIndex = static_cast<int>(ActionId::SelectSkill1);
        const int actionIndex = static_cast<int>(id);
        const int skillOffset = actionIndex - baseIndex;
        if (skillOffset >= 0)
        {
            const int hotkey = skillOffset + 1;
            m_world.selectSkillByHotkey(hotkey);
        }
    };

    for (const ActionEvent &evt : frame.events)
    {
        if (!evt.pressed && evt.id != ActionId::ActivateSkill)
        {
            continue;
        }

        if (evt.id == ActionId::ToggleDebugOverlay && evt.pressed)
        {
            const bool wasActive = m_debugController.active();
            if (!wasActive)
            {
                m_cursorRestoreState = SDL_ShowCursor(SDL_QUERY);
            }
            m_debugController.handleActionToggle();
            const bool nowActive = m_debugController.active();
            if (nowActive)
            {
                SDL_ShowCursor(SDL_ENABLE);
            }
            else if (wasActive && m_cursorRestoreState != SDL_QUERY)
            {
                SDL_ShowCursor(m_cursorRestoreState);
                m_cursorRestoreState = SDL_QUERY;
            }
            continue;
        }

        if (m_debugController.active())
        {
            continue;
        }

        switch (evt.id)
        {
        case ActionId::CommanderOrderRushNearest:
            m_world.issueOrder(ArmyStance::RushNearest);
            break;
        case ActionId::CommanderOrderPushForward:
            m_world.issueOrder(ArmyStance::PushForward);
            break;
        case ActionId::CommanderOrderFollowLeader:
            m_world.issueOrder(ArmyStance::FollowLeader);
            break;
        case ActionId::CommanderOrderDefendBase:
            m_world.issueOrder(ArmyStance::DefendBase);
            break;
        case ActionId::CycleFormationPrevious:
            m_world.cycleFormation(-1);
            break;
        case ActionId::CycleFormationNext:
            m_world.cycleFormation(1);
            break;
        case ActionId::ToggleDebugHud:
            m_showDebugHud = !m_showDebugHud;
            break;
        case ActionId::ReloadConfig:
#if !defined(NDEBUG)
        {
            const bool success = app.reloadConfig();
            const auto &result = app.appConfigResult();
            std::string message = success ? std::string("Config reloaded") : std::string("Config reload errors");
            if (!success && !result.errors.empty())
            {
                message += " (";
                message += std::to_string(result.errors.size());
                message += ')';
            }
            showTelemetryMessage(message);
        }
#endif
            break;
        case ActionId::DumpSpawnHistory:
#if !defined(NDEBUG)
        {
            auto dumpResult = m_world.dumpSpawnHistory();
            if (dumpResult.success)
            {
                std::string message = "Spawn history saved";
                if (!dumpResult.path.empty())
                {
                    message += ": ";
                    message += dumpResult.path.filename().string();
                }
                showTelemetryMessage(message);
            }
            else
            {
                std::string message = "Spawn history failed";
                if (!dumpResult.error.empty())
                {
                    message += " (";
                    message += dumpResult.error;
                    message += ')';
                }
                showTelemetryMessage(message);
            }
        }
#endif
            break;
        case ActionId::RestartScenario:
            if (sim.result != GameResult::Playing && m_world.canRestart())
            {
                m_world.reset();
                m_baseCameraTarget = {sim.basePos.x - m_screenWidth * 0.5f, sim.basePos.y - m_screenHeight * 0.5f};
                m_introFocus = leftmostGateWorld(sim.mapDefs);
                m_introCameraTarget = {m_introFocus.x - m_screenWidth * 0.5f,
                                       m_introFocus.y - m_screenHeight * 0.5f};
                m_camera.position = m_introCameraTarget;
                m_introTimer = m_introDuration;
                m_introActive = true;
            }
            break;
        case ActionId::SelectSkill1:
        case ActionId::SelectSkill2:
        case ActionId::SelectSkill3:
        case ActionId::SelectSkill4:
        case ActionId::SelectSkill5:
        case ActionId::SelectSkill6:
        case ActionId::SelectSkill7:
        case ActionId::SelectSkill8:
            handleSkillSelect(evt.id);
            break;
        case ActionId::FocusCommander:
            m_camera.position = {sim.commander.pos.x - m_screenWidth * 0.5f,
                                 sim.commander.pos.y - m_screenHeight * 0.5f};
            m_introActive = false;
            m_introTimer = 0.0f;
            break;
        case ActionId::FocusBase:
            m_camera.position = {sim.basePos.x - m_screenWidth * 0.5f, sim.basePos.y - m_screenHeight * 0.5f};
            m_introActive = false;
            m_introTimer = 0.0f;
            break;
        case ActionId::CommanderCastFireBall:
            if (evt.pressed)
            {
                m_world.castFireBall(120.0f, 4.0f);
            }
            break;
        case ActionId::CommanderGuardHold:
            m_world.setCommanderGuard(evt.pressed);
            break;
        case ActionId::CommanderCancelTarget:
            if (evt.pressed)
            {
                m_world.commanderCancelTarget();
            }
            break;
        case ActionId::CommanderFocusTarget:
            if (evt.pressed)
            {
                m_world.commanderFocusTarget();
            }
            break;
        case ActionId::CommanderAttack:
        {
            std::optional<Vec2> targetPos;
            if (evt.pointer && evt.pointer->pressed)
            {
                targetPos = screenToWorld(evt.pointer->x, evt.pointer->y, m_camera);
            }
            m_world.commanderAttack(targetPos);
            break;
        }
        case ActionId::ActivateSkill:
            if (evt.pointer && evt.pointer->pressed)
            {
                Vec2 worldPos = screenToWorld(evt.pointer->x, evt.pointer->y, m_camera);
                m_world.activateSelectedSkill(worldPos);
            }
            break;
        case ActionId::ToggleGameSpeed:
            cycleGameSpeed();
            break;
        case ActionId::QuitGame:
            app.requestQuit();
            break;
        default:
            break;
        }
    }

    // Cursor feedback: crosshair when hovering an enemy during battle
    bool hoverEnemy = false;
    if (frame.pointer.hasPosition)
    {
        Vec2 worldPos = screenToWorld(frame.pointer.x, frame.pointer.y, m_camera);
        const LegacySimulation &sim = m_world.legacy();
        const float pad = 8.0f;
        for (const EnemyUnit &enemy : sim.enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float reach = enemy.radius + pad;
            if (lengthSq(enemy.pos - worldPos) <= reach * reach)
            {
                hoverEnemy = true;
                break;
            }
        }
    }
    static SDL_Cursor *crosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    static SDL_Cursor *arrow = SDL_GetDefaultCursor();
    if (hoverEnemy && !m_cursorCrosshairActive)
    {
        SDL_SetCursor(crosshair);
        m_cursorCrosshairActive = true;
    }
    else if (!hoverEnemy && m_cursorCrosshairActive)
    {
        SDL_SetCursor(arrow);
        m_cursorCrosshairActive = false;
    }
}

void BattleScene::update(double deltaSeconds, GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (!m_initialized)
    {
        return;
    }

    if (m_pausedForCamp)
    {
        m_debugController.update(deltaSeconds);
        return;
    }

    m_debugController.update(deltaSeconds);
    updateDebugToggles();

    if (m_pendingBudgetCheck)
    {
        evaluatePerformanceBudgets(app);
        m_pendingBudgetCheck = false;
    }

    LegacySimulation &sim = m_world.legacy();
    if (!m_resultOverlayActive && sim.result != GameResult::Playing)
    {
        if (!m_resultRecorded && m_campaign)
        {
            m_campaign->recordRunOutcome(sim);
            m_campaign->saveToDisk();
            m_resultRecorded = true;
        }
        m_resultOverlayActive = true;
        m_resultSummary = sim.hud.resultText.empty() ? "RESULT" : sim.hud.resultText;
    }

    if (m_resultOverlayActive)
    {
        return;
    }

    const float debugScale = std::clamp(m_debugController.timeScale(), 0.25f, 4.0f);
    const float userScale = std::clamp(m_userTimeScale, 0.25f, 4.0f);
    const float timeScale = debugScale * userScale;
    m_lastFrameSeconds = deltaSeconds;
    m_accumulator += deltaSeconds * timeScale;
    m_fpsTimer += deltaSeconds;
    ++m_frames;
    if (m_fpsTimer >= 1.0)
    {
        m_currentFps = static_cast<float>(m_frames / m_fpsTimer);
        m_fpsTimer = 0.0;
        m_frames = 0;
    }

    const float dt = sim.config.fixed_dt;
    const double baseInputTimestamp = static_cast<double>(SDL_GetTicks64());
    const Uint64 updateStart = SDL_GetPerformanceCounter();
    const double tickToMs = m_frequency > 0.0 ? 1000.0 / m_frequency : 0.0;
    std::size_t stepIndex = 0;
    bool producedFrame = false;
    double inputMsAccum = 0.0;
    while (m_accumulator >= dt)
    {
        const Uint64 inputStart = SDL_GetPerformanceCounter();
        const double frameTimestamp = baseInputTimestamp +
                                      static_cast<double>(stepIndex) * (static_cast<double>(dt) * 1000.0);
        app.inputMapper().sampleFrame(!m_introActive,
                                      frameTimestamp,
                                      m_inputSequence++,
                                      m_actionBuffer);
        if (const ActionBuffer::Frame *frame = m_actionBuffer.latest())
        {
            handleActionFrame(*frame, app);
        }
        const Uint64 inputEnd = SDL_GetPerformanceCounter();
        if (tickToMs > 0.0)
        {
            inputMsAccum += (inputEnd - inputStart) * tickToMs;
        }
        m_world.step(dt, m_actionBuffer);
        m_accumulator -= dt;
        ++stepIndex;
        producedFrame = true;
    }
    if (!producedFrame)
    {
        const Uint64 inputStart = SDL_GetPerformanceCounter();
        app.inputMapper().sampleFrame(!m_introActive,
                                      baseInputTimestamp,
                                      m_inputSequence++,
                                      m_actionBuffer);
        if (const ActionBuffer::Frame *frame = m_actionBuffer.latest())
        {
            handleActionFrame(*frame, app);
        }
        const Uint64 inputEnd = SDL_GetPerformanceCounter();
        if (tickToMs > 0.0)
        {
            inputMsAccum += (inputEnd - inputStart) * tickToMs;
        }
    }
    const Uint64 updateEnd = SDL_GetPerformanceCounter();
    const double updateMs = (updateEnd - updateStart) * 1000.0 / m_frequency;
    m_lastUpdateMs = updateMs;
    m_framePerf.msUpdate = static_cast<float>(updateMs);
    m_lastInputMs = inputMsAccum;
    m_framePerf.msInput = static_cast<float>(inputMsAccum);
    m_lastStageTimings.updateMs = updateMs;
    m_lastStageTimings.inputMs = inputMsAccum;

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
        Vec2 targetCamera{sim.commander.pos.x - m_screenWidth * 0.5f,
                          sim.commander.pos.y - m_screenHeight * 0.5f};
        const float followFactor = std::clamp(frameSeconds * 6.0f, 0.0f, 1.0f);
        m_camera.position = lerp(m_camera.position, targetCamera, followFactor);
    }

    if (m_eventBus)
    {
        sim.hud.unconsumedEvents = m_eventBus->unconsumedCount();
    }
    else
    {
        sim.hud.unconsumedEvents = 0;
    }

    m_framePerf.fps = m_currentFps;
}

void BattleScene::updateDebugToggles()
{
    if (m_debugController.consumeHudToggle())
    {
        m_showDebugHud = !m_showDebugHud;
    }
    if (m_debugController.consumeTelemetryToggle())
    {
        m_showTelemetryOverlay = !m_showTelemetryOverlay;
    }
}

void BattleScene::initializeDebugBindings(GameApplication &app)
{
    (void)app;
    debug::DebugBindings bindings;
    bindings.simulation = &m_world.legacy();
    bindings.accessor = &m_debugAccessor;
    m_debugController.bindWorld(bindings);
    m_debugController.onConfigReloaded();
}

void BattleScene::DebugSimulationAccessor::markComponentsDirty()
{
    m_scene.m_world.markComponentsDirty();
}

void BattleScene::DebugSimulationAccessor::setEnemySpawnMultiplier(float multiplier)
{
    m_scene.m_world.setEnemySpawnMultiplier(multiplier);
}

float BattleScene::DebugSimulationAccessor::enemySpawnMultiplier() const
{
    return m_scene.m_world.enemySpawnMultiplier();
}

bool BattleScene::DebugSimulationAccessor::skipNextWave()
{
    return m_scene.m_world.skipNextWave();
}

void BattleScene::render(SDL_Renderer *renderer, GameApplication &app)
{
    (void)app;
    if (!m_initialized)
    {
        return;
    }

    const LegacySimulation &sim = m_world.legacy();
    const Uint64 renderStart = SDL_GetPerformanceCounter();

    const Vec2 cameraOffset = m_camera.position;
    Camera renderCamera = m_camera;
    renderCamera.position = cameraOffset;
    const auto &allyPool = m_world.allies();
    const auto &enemyPool = m_world.enemies();
    m_framePerf.entities = static_cast<int>(allyPool.size() + enemyPool.size() + (sim.commander.alive ? 1 : 0));
    const FormationHudStatus &formationHud = m_ui.formationHud();
    const MoraleHudStatus &moraleHud = m_ui.moraleHud();
    const JobHudStatus &jobHud = m_ui.jobHud();
    double hudMs = 0.0;
    RenderStats renderStats{};

    if (renderer)
    {
        const UiView::Dependencies &deps = m_uiView.dependencies();
        if (deps.renderer != renderer || deps.hudFont != &m_hudFont || deps.debugFont != &m_debugFont ||
            deps.screenWidth != m_screenWidth || deps.screenHeight != m_screenHeight)
        {
            UiView::Dependencies updated = deps;
            updated.renderer = renderer;
            updated.hudFont = &m_hudFont;
            updated.debugFont = &m_debugFont;
            updated.screenWidth = m_screenWidth;
            updated.screenHeight = m_screenHeight;
            m_uiView.setDependencies(updated);
        }
    }

    renderWorld(renderer,
                sim,
                &formationHud,
                &moraleHud,
                &jobHud,
                renderCamera,
                m_hudFont,
                m_debugFont,
                m_tileMap,
                m_atlas,
                m_screenWidth,
                m_screenHeight,
                renderStats);

    Uint64 hudSectionStart = 0;
    if (m_frequency > 0.0)
    {
        hudSectionStart = SDL_GetPerformanceCounter();
    }

    UiView::DrawContext::InputDiagnosticsState inputDiagnostics{};
    inputDiagnostics.bufferCapacity = m_actionBuffer.capacity();
    inputDiagnostics.bufferedFrames = m_actionBuffer.size();
    inputDiagnostics.configuredBufferFrames = app.inputMapper().bufferFrames();
    inputDiagnostics.bufferExpiryMs = app.inputMapper().bufferExpiryMs();
    if (const ActionBuffer::Frame *latest = m_actionBuffer.latest())
    {
        inputDiagnostics.hasLatestFrame = true;
        inputDiagnostics.latestSequence = latest->sequence;
        inputDiagnostics.latestDeviceTimestampMs = latest->deviceTimestampMs;
        inputDiagnostics.hasPointerState = true;
        inputDiagnostics.pointerState.hasPosition = latest->pointer.hasPosition;
        inputDiagnostics.pointerState.x = latest->pointer.x;
        inputDiagnostics.pointerState.y = latest->pointer.y;
        inputDiagnostics.pointerState.left = latest->pointer.left;
        inputDiagnostics.pointerState.right = latest->pointer.right;
        inputDiagnostics.pointerState.middle = latest->pointer.middle;
        inputDiagnostics.latestEvents.reserve(latest->events.size());
        for (const ActionEvent &evt : latest->events)
        {
            UiView::DrawContext::InputDiagnosticsState::Event diagEvt;
            diagEvt.id = evt.id;
            diagEvt.value = evt.value;
            diagEvt.pressed = evt.pressed;
            diagEvt.released = evt.released;
            if (evt.pointer)
            {
                diagEvt.hasPointer = true;
                diagEvt.pointerX = evt.pointer->x;
                diagEvt.pointerY = evt.pointer->y;
                diagEvt.pointerPressed = evt.pointer->pressed;
                diagEvt.pointerReleased = evt.pointer->released;
            }
            inputDiagnostics.latestEvents.push_back(diagEvt);
        }
    }

    UiView::DrawContext hudContext{};
    hudContext.simulation = &sim;
    hudContext.formationHud = &formationHud;
    hudContext.moraleHud = &moraleHud;
    hudContext.jobHud = &jobHud;
    hudContext.framePerf = &m_framePerf;
    hudContext.renderStats = &renderStats;
    hudContext.showDebugHud = m_showDebugHud;
    hudContext.performanceFrequency = m_frequency;
    hudContext.hudTimeMs = &hudMs;
    hudContext.inputDiagnostics = &inputDiagnostics;
    hudContext.timeScale = m_userTimeScale;
    hudContext.showSpeedIndicator = m_speedSteps.size() > 1;
    m_uiView.render(hudContext);

    if (renderer && m_debugController.active())
    {
        m_debugController.gatherDisplay(m_debugDisplayState);
        m_debugDisplayState.showTelemetry = m_showTelemetryOverlay;
        m_debugOverlay.render(renderer,
                              m_debugFont,
                              m_debugFont,
                              m_debugDisplayState,
                              m_framePerf,
                              renderStats,
                              m_screenWidth,
                              m_screenHeight);
    }

    if (m_resultOverlayActive)
    {
        renderResultOverlay(renderer, app);
    }

    if (hudSectionStart != 0)
    {
        const Uint64 hudEnd = SDL_GetPerformanceCounter();
        hudMs += (hudEnd - hudSectionStart) * 1000.0 / m_frequency;
    }

    const Uint64 renderEnd = SDL_GetPerformanceCounter();
    const double renderMs = (renderEnd - renderStart) * 1000.0 / m_frequency;
    m_framePerf.msRender = static_cast<float>(renderMs);
    m_framePerf.msHud = static_cast<float>(hudMs);
    m_framePerf.drawCalls = renderStats.drawCalls;
    m_lastStageTimings.renderMs = renderMs;
    m_lastStageTimings.hudMs = hudMs;

    m_perfLogTimer += m_lastFrameSeconds;
    m_updateAccum += m_lastUpdateMs;
    m_renderAccum += renderMs;
    m_entityAccum += static_cast<double>(m_framePerf.entities);
    ++m_perfLogFrames;
    m_pendingBudgetCheck = true;
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

void BattleScene::renderResultOverlay(SDL_Renderer *renderer, GameApplication &app)
{
    (void)app;
    if (!m_resultOverlayActive || !renderer || !m_hudFont.isLoaded())
    {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_Rect overlay{0, 0, m_screenWidth, m_screenHeight};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    std::vector<std::string> lines{
        m_resultSummary.empty() ? std::string("RESULT") : m_resultSummary,
        std::string("[Enter] キャンプへ戻る"),
        std::string("[R] もう一度挑戦"),
        std::string("[Esc] タイトルへ戻る"),
    };
    const TextRenderer &font = m_hudFont;
    const int lineAdvance = font.getLineHeight() + 8;
    int totalHeight = static_cast<int>(lines.size()) * lineAdvance;
    int y = m_screenHeight / 2 - totalHeight / 2;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string &line = lines[i];
        const int textWidth = font.measureText(line);
        const int x = std::max(32, (m_screenWidth - textWidth) / 2);
        SDL_Color color = i == 0 ? SDL_Color{255, 240, 180, 255} : SDL_Color{230, 230, 255, 255};
        font.drawText(renderer, line, x, y, nullptr, color);
        y += lineAdvance;
    }
}

void BattleScene::handleResultOverlayKey(SDL_Keycode key, GameApplication &app, SceneStack &stack)
{
    switch (key)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!m_pausedForCamp)
        {
            m_pausedForCamp = true;
            m_resultOverlayActive = false;
            stack.push(std::make_unique<CampScene>(m_campaign, this));
        }
        break;
    case SDLK_r:
        m_resultOverlayActive = false;
        startRunFromCamp(app);
        break;
    case SDLK_ESCAPE:
        app.requestQuit();
        break;
    default:
        break;
    }
}

void BattleScene::cycleGameSpeed(int direction)
{
    if (m_speedSteps.empty())
    {
        return;
    }
    const int count = static_cast<int>(m_speedSteps.size());
    direction = direction >= 0 ? 1 : -1;
    m_speedIndex = (m_speedIndex + count + direction) % count;
    m_userTimeScale = std::clamp(m_speedSteps[m_speedIndex], 0.25f, 4.0f);
    std::ostringstream oss;
    oss << "Speed x" << std::fixed << std::setprecision(m_userTimeScale >= 2.0f ? 0 : 1) << m_userTimeScale;
    showTelemetryMessage(oss.str());
}

void BattleScene::resetResultState()
{
    m_resultOverlayActive = false;
    m_resultRecorded = false;
    m_resultSummary.clear();
}

void BattleScene::evaluatePerformanceBudgets(GameApplication &app)
{
    (void)app;
    m_framePerf.budgetExceeded = false;
    m_framePerf.budgetStage.clear();
    m_framePerf.budgetSampleMs = 0.0f;
    m_framePerf.budgetTargetMs = 0.0f;

    telemetry::StageTimingSample sample{};
    sample.updateMs = m_lastStageTimings.updateMs;
    sample.renderMs = m_lastStageTimings.renderMs;
    sample.inputMs = m_lastStageTimings.inputMs;
    sample.hudMs = m_lastStageTimings.hudMs;

    if (auto violation = m_budgetMonitor.evaluate(sample))
    {
        raisePerformanceWarning(*violation, app);
    }
}

void BattleScene::raisePerformanceWarning(const telemetry::BudgetViolation &violation, GameApplication &app)
{
    std::string stageLabel = violation.stage;
    std::transform(stageLabel.begin(), stageLabel.end(), stageLabel.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    auto formatMs = [](double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    };

    const std::string warningText = "Performance spike: " + stageLabel + ' ' + formatMs(violation.sampleMs) +
                                    "ms (budget " + formatMs(violation.budgetMs) + "ms)";

    m_framePerf.budgetExceeded = true;
    m_framePerf.budgetStage = stageLabel;
    m_framePerf.budgetSampleMs = static_cast<float>(violation.sampleMs);
    m_framePerf.budgetTargetMs = static_cast<float>(violation.budgetMs);

    LegacySimulation &sim = m_world.legacy();
    HUDState &hud = sim.hud;
    hud.performance.active = true;
    hud.performance.message = warningText;
    hud.performance.timer = std::max(sim.config.telemetry_duration, 1.5f);

    if (m_telemetry)
    {
        TelemetrySink::Payload payload;
        payload.emplace("stage", violation.stage);
        payload.emplace("sample_ms", formatMs(violation.sampleMs));
        payload.emplace("budget_ms", formatMs(violation.budgetMs));
        payload.emplace("tolerance_ms", formatMs(m_performanceBudget.toleranceMs));
        m_telemetry->recordEvent("battle.performance.budget_exceeded", payload);

        const Uint64 now = SDL_GetTicks64();
        if (m_lastBudgetWarningTick == 0 || now - m_lastBudgetWarningTick >= BudgetWarningCooldownMs)
        {
            m_telemetry->requestFrameCapture();
            m_lastBudgetWarningTick = now;
        }
    }

    (void)app;
}

#ifndef KUSOZAKO_SKIP_APP_MAIN
int main(int argc, char **argv)
{
    std::optional<std::filesystem::path> telemetryDir;
    constexpr std::string_view kTelemetryPrefix{"--telemetry-dir="};
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--telemetry-dir" && i + 1 < argc)
        {
            telemetryDir = std::filesystem::path(argv[++i]);
        }
        else if (arg.substr(0, kTelemetryPrefix.size()) == kTelemetryPrefix)
        {
            telemetryDir = std::filesystem::path(std::string(arg.substr(kTelemetryPrefix.size())));
        }
    }

    auto configLoader = std::make_shared<AppConfigLoader>(std::filesystem::absolute("config"));
    GameApplication app(std::move(configLoader));
    if (telemetryDir)
    {
        app.setTelemetryOutputDirectory(*telemetryDir);
    }
    auto campaign = std::make_shared<CampaignState>();
    std::filesystem::path saveDir = std::filesystem::path("save");
    std::filesystem::path savePath = std::filesystem::absolute(saveDir / "campaign_state.json");
    campaign->setSavePath(savePath);
    campaign->loadFromDisk();
    app.sceneStack().push(std::make_unique<TitleScene>(std::move(campaign)));
    return app.run();
}
#endif
void BattleScene::applyAppConfig(GameApplication &app)
{
    if (!m_assetService)
    {
        return;
    }

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

    LegacySimulation &sim = m_world.legacy();
    sim = {};
    sim.config = appConfig.game;
    sim.temperamentConfig = appConfig.temperament;
    sim.chibiPersonalityConfig = appConfig.chibiPersonality;
    sim.chibiAiParams = appConfig.chibiAiParams;
    sim.yunaStats = appConfig.entityCatalog.yuna;
    sim.slimeStats = appConfig.entityCatalog.slime;
    sim.goblinStats = appConfig.entityCatalog.goblin;
    sim.magicianStats = appConfig.entityCatalog.magician;
    sim.batStats = appConfig.entityCatalog.bat;
    sim.toritoriStats = appConfig.entityCatalog.toritori;
    sim.golemStats = appConfig.entityCatalog.golem;
    sim.wallbreakerStats = appConfig.entityCatalog.wallbreaker;
    sim.commanderStats = appConfig.entityCatalog.commander;
    sim.mapDefs = appConfig.mapDefs;
    sim.spawnScript = appConfig.spawnScript;
    sim.economyConfig = appConfig.economy;
    sim.formationDefaults = appConfig.game.formationDefaults;
    sim.formationAlignTimer = 0.0f;
    sim.formationDefenseMul = 1.0f;
    if (appConfig.stageConfig)
    {
        sim.configureStage(*appConfig.stageConfig);
    }
    else
    {
        sim.clearStageConfiguration();
    }
    m_speedSteps = sim.stage.speed.steps;
    if (m_speedSteps.empty())
    {
        m_speedSteps = {1.0f};
    }
    m_speedIndex = 0;
    m_userTimeScale = std::clamp(m_speedSteps[m_speedIndex], 0.25f, 4.0f);
    if (appConfig.mission && appConfig.mission->mode != MissionMode::None)
    {
        sim.hasMission = true;
        sim.missionConfig = *appConfig.mission;
    }
    else
    {
        sim.hasMission = false;
    }

    m_world.setTelemetrySink(m_telemetry);
    m_world.setEventBus(m_eventBus);
    m_ui.setTelemetrySink(m_telemetry);
    m_ui.setEventBus(m_eventBus);
    m_ui.bindSimulation(&m_world.legacy());

    if (m_tileMap.width > 0 && m_tileMap.height > 0)
    {
        m_world.setWorldBounds(static_cast<float>(m_tileMap.width * m_tileMap.tileWidth),
                               static_cast<float>(m_tileMap.height * m_tileMap.tileHeight));
    }
    else
    {
        m_world.setWorldBounds(static_cast<float>(m_screenWidth), static_cast<float>(m_screenHeight));
    }

    std::vector<SkillDef> skillDefs = appConfig.skills.empty() ? buildDefaultSkills() : appConfig.skills;
    m_world.configureSkills(skillDefs);
    applyCampaignModifiers(app);
    initializeRun(app);
    resetResultState();

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

    UiView::Dependencies uiDeps;
    uiDeps.renderer = app.renderer();
    uiDeps.hudFont = &m_hudFont;
    uiDeps.debugFont = &m_debugFont;
    uiDeps.screenWidth = m_screenWidth;
    uiDeps.screenHeight = m_screenHeight;
    m_uiView.setDependencies(uiDeps);

    initializeDebugBindings(app);
}

void BattleScene::applyCampaignModifiers(GameApplication &app)
{
    if (!m_campaign)
    {
        return;
    }
    m_campaign->applyPersistentUpgrades(app.appConfig(), m_world.legacy());
}

void BattleScene::initializeRun(GameApplication &app)
{
    const AppConfig &appConfig = app.appConfig();
    LegacySimulation &sim = m_world.legacy();
    m_world.reset();
    if (m_campaign)
    {
        m_campaign->applyRunStart(appConfig, sim);
    }

    m_actionBuffer.clear();
    m_actionBuffer.setCapacity(static_cast<std::size_t>(std::max(1, appConfig.input.bufferFrames)));
    m_inputSequence = 0;
    m_haveProcessedSequence = false;
    m_lastProcessedSequence = 0;

    m_camera = {};
    m_baseCameraTarget = {sim.basePos.x - m_screenWidth * 0.5f, sim.basePos.y - m_screenHeight * 0.5f};
    m_introFocus = leftmostGateWorld(sim.mapDefs);
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
    m_performanceBudget = appConfig.game.performance;
    m_budgetMonitor.setBudget(m_performanceBudget);
    m_lastBudgetWarningTick = 0;
    m_lastStageTimings = {};
    m_pendingBudgetCheck = false;
    if (!m_initialized)
    {
        m_showDebugHud = false;
    }
    m_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    m_lastFrameSeconds = 0.0;
    m_lastUpdateMs = 0.0;
}

void BattleScene::startRunFromCamp(GameApplication &app)
{
    applyCampaignModifiers(app);
    initializeRun(app);
    if (m_campaign)
    {
        m_campaign->saveToDisk();
    }
    m_pausedForCamp = false;
    resetResultState();
}

void CampScene::deploy(SceneStack &stack, GameApplication &app)
{
    if (m_battle)
    {
        m_battle->startRunFromCamp(app);
    }
    stack.pop();
}

void BattleScene::onConfigReloaded(GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (!m_initialized)
    {
        return;
    }
    m_screenWidth = app.windowWidth();
    m_screenHeight = app.windowHeight();
    applyAppConfig(app);
}

void BattleScene::showTelemetryMessage(const std::string &message)
{
    if (message.empty())
    {
        return;
    }
    m_world.legacy().pushTelemetry(message);
}

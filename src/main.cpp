#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include "app/GameApplication.h"
#include "app/UiPresenter.h"
#include "assets/AssetManager.h"
#include "config/AppConfig.h"
#include "config/AppConfigLoader.h"
#include "json/JsonUtils.h"
#include "scenes/Scene.h"
#include "scenes/SceneStack.h"
#include "events/EventBus.h"
#include "input/ActionBuffer.h"
#include "input/InputMapper.h"
#include "services/ServiceLocator.h"
#include "telemetry/TelemetrySink.h"
#include "world/ComponentPool.h"
#include "world/FormationUtils.h"
#include "world/LegacySimulation.h"
#include "world/LegacyTypes.h"
#include "world/SkillRuntime.h"
#include "world/WorldState.h"
#include "world/spawn/Spawner.h"
#include "world/spawn/WaveController.h"
#include "world/systems/BehaviorSystem.h"
#include "world/systems/CombatSystem.h"
#include "world/systems/FormationSystem.h"
#include "world/systems/JobAbilitySystem.h"
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
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct RenderStats
{
    int drawCalls = 0;
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

namespace world
{

WorldState::WorldState()
    : m_sim(std::make_unique<LegacySimulation>()),
      m_allies(std::make_unique<ComponentPool<Unit>>()),
      m_enemies(std::make_unique<ComponentPool<EnemyUnit>>()),
      m_walls(std::make_unique<ComponentPool<WallSegment>>()),
      m_captureZones(std::make_unique<ComponentPool<CaptureRuntime>>()),
      m_waveController(std::make_unique<spawn::WaveController>()),
      m_spawner(std::make_unique<spawn::Spawner>())
{
    m_waveController->setSpawner(m_spawner.get());
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
    m_sim->configureSkills(defs);
    markComponentsDirty();
}

void WorldState::reset()
{
    m_sim->reset();
    if (m_spawner)
    {
        m_spawner->clear();
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

systems::SystemContext WorldState::makeSystemContext()
{
    clearSystems();

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
    registerSystem(systems::SystemStage::CommandAndMorale, std::move(formation));
    registerSystem(systems::SystemStage::CommandAndMorale, std::make_unique<systems::MoraleSystem>());
    registerSystem(systems::SystemStage::AiDecision, std::make_unique<systems::BehaviorSystem>());
    registerSystem(systems::SystemStage::Combat, std::make_unique<systems::CombatSystem>());
    registerSystem(systems::SystemStage::StateUpdate, std::make_unique<systems::JobAbilitySystem>());
}

void WorldState::clearSystems()
{
    m_systems.clear();
    m_systemStageOrder.clear();
    m_cachedFormationSystem = nullptr;
}

void WorldState::registerSystem(systems::SystemStage stage, std::unique_ptr<systems::ISystem> system)
{
    if (!system)
    {
        return;
    }
    if (!m_systems.empty())
    {
        const systems::SystemStage lastStage = m_systems.back().stage;
        if (static_cast<std::uint8_t>(stage) < static_cast<std::uint8_t>(lastStage))
        {
            throw std::logic_error("WorldState::registerSystem stage order violation");
        }
    }
    if (auto *formation = dynamic_cast<systems::FormationSystem *>(system.get()))
    {
        m_cachedFormationSystem = formation;
    }
    m_systemStageOrder.push_back(stage);
    m_systems.push_back(SystemEntry{stage, std::move(system)});
}

const std::vector<systems::SystemStage> &WorldState::systemStageOrder() const
{
    return m_systemStageOrder;
}

void WorldState::runSystemsForStage(systems::SystemStage stage,
                                    float dt,
                                    systems::SystemContext &context)
{
    for (auto &entry : m_systems)
    {
        if (entry.stage == stage && entry.system)
        {
            entry.system->update(dt, context);
        }
    }
}

void WorldState::step(float dt, const ActionBuffer &actions)
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
        m_sim->yunas,
        m_sim->enemies,
        m_sim->walls,
        m_sim->gates,
        m_sim->yunaRespawnTimers,
        m_sim->commanderRespawnTimer,
        m_sim->commanderInvulnTimer,
        missionContext,
        actions};
    constexpr std::array<systems::SystemStage, 8> kExecutionOrder{
        systems::SystemStage::InputProcessing,
        systems::SystemStage::CommandAndMorale,
        systems::SystemStage::AiDecision,
        systems::SystemStage::Movement,
        systems::SystemStage::Combat,
        systems::SystemStage::StateUpdate,
        systems::SystemStage::Spawn,
        systems::SystemStage::RenderingPrep,
    };

    for (systems::SystemStage stage : kExecutionOrder)
    {
        switch (stage)
        {
        case systems::SystemStage::StateUpdate:
            m_sim->update(dt);
            runSystemsForStage(stage, dt, context);
            break;
        case systems::SystemStage::Spawn:
        {
            runSystemsForStage(stage, dt, context);

            if (m_sim->spawnEnabled)
            {
                if (m_waveController)
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

                if (m_spawner)
                {
                    if (m_sim->missionMode == MissionMode::Survival &&
                        m_sim->survival.spawnMultiplier > 0.0f)
                    {
                        const float mult = std::max(m_sim->survival.spawnMultiplier, 0.1f);
                        m_spawner->setIntervalModifier([mult](float base) {
                            if (mult <= 0.0f)
                            {
                                return base;
                            }
                            return base / mult;
                        });
                    }
                    else
                    {
                        m_spawner->setIntervalModifier({});
                    }

                    m_spawner->emit(dt, [this](const spawn::SpawnPayload &payload) {
                        m_sim->spawnOneEnemy(payload.position, payload.type);
                    });
                }
            }

            if (m_waveController)
            {
                m_sim->waveScriptComplete = m_waveController->isComplete();
            }
            if (m_spawner)
            {
                m_sim->spawnerIdle = m_spawner->empty();
            }
            break;
        }
        default:
            runSystemsForStage(stage, dt, context);
            break;
        }
    }

    markComponentsDirty();
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
    if (m_jobSystem)
    {
        systems::SystemContext context = makeSystemContext();
        systems::SkillCommand command{m_sim->selectedSkill, worldPos};
        m_jobSystem->triggerSkill(context, command);
    }
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
    if (m_waveController)
    {
        m_waveController->setTelemetrySink(m_telemetry);
    }
    if (auto *formation = formationSystem())
    {
        formation->setTelemetrySink(std::weak_ptr<TelemetrySink>(m_telemetry));
    }
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

void renderScene(SDL_Renderer *renderer, const LegacySimulation &sim, const Camera &camera, const TextRenderer &font,
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
    void handleActionFrame(const ActionBuffer::Frame &frame, GameApplication &app);

    bool m_initialized = false;
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
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    std::shared_ptr<TelemetrySink> m_telemetry;
    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<AssetManager> m_assetService;
    UiPresenter m_ui;
    ActionBuffer m_actionBuffer;
    std::uint64_t m_inputSequence = 0;
    std::uint64_t m_lastProcessedSequence = 0;
    bool m_haveProcessedSequence = false;
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

    m_world.setTelemetrySink(m_telemetry);
    m_world.setEventBus(m_eventBus);
    m_ui.setTelemetrySink(m_telemetry);
    m_ui.setEventBus(m_eventBus);
    m_ui.bindSimulation(&m_world.legacy());

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

    LegacySimulation &sim = m_world.legacy();
    sim = {};
    sim.config = appConfig.game;
    sim.temperamentConfig = appConfig.temperament;
    sim.yunaStats = appConfig.entityCatalog.yuna;
    sim.slimeStats = appConfig.entityCatalog.slime;
    sim.wallbreakerStats = appConfig.entityCatalog.wallbreaker;
    sim.commanderStats = appConfig.entityCatalog.commander;
    sim.mapDefs = appConfig.mapDefs;
    sim.spawnScript = appConfig.spawnScript;
    if (appConfig.mission && appConfig.mission->mode != MissionMode::None)
    {
        sim.hasMission = true;
        sim.missionConfig = *appConfig.mission;
    }
    else
    {
        sim.hasMission = false;
    }

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
    m_world.reset();
    m_actionBuffer.clear();
    m_actionBuffer.setCapacity(static_cast<std::size_t>(std::max(1, appConfig.input.bufferFrames)));
    m_inputSequence = 0;
    m_haveProcessedSequence = false;
    m_lastProcessedSequence = 0;

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
}

void BattleScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    (void)event;
    (void)app;
    (void)stack;
}

void BattleScene::handleActionFrame(const ActionBuffer::Frame &frame, GameApplication &app)
{
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
        case ActionId::ActivateSkill:
            if (evt.pointer && evt.pointer->pressed)
            {
                Vec2 worldPos = screenToWorld(evt.pointer->x, evt.pointer->y, m_camera);
                m_world.activateSelectedSkill(worldPos);
            }
            break;
        case ActionId::QuitGame:
            app.requestQuit();
            break;
        default:
            break;
        }
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

    LegacySimulation &sim = m_world.legacy();

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

    const float dt = sim.config.fixed_dt;
    const double baseInputTimestamp = static_cast<double>(SDL_GetTicks64());
    const Uint64 updateStart = SDL_GetPerformanceCounter();
    std::size_t stepIndex = 0;
    bool producedFrame = false;
    while (m_accumulator >= dt)
    {
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
        m_world.step(dt, m_actionBuffer);
        m_accumulator -= dt;
        ++stepIndex;
        producedFrame = true;
    }
    if (!producedFrame)
    {
        app.inputMapper().sampleFrame(!m_introActive,
                                      baseInputTimestamp,
                                      m_inputSequence++,
                                      m_actionBuffer);
        if (const ActionBuffer::Frame *frame = m_actionBuffer.latest())
        {
            handleActionFrame(*frame, app);
        }
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
        Vec2 targetCamera{sim.commander.pos.x - m_screenWidth * 0.5f,
                          sim.commander.pos.y - m_screenHeight * 0.5f};
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

    const LegacySimulation &sim = m_world.legacy();
    const Uint64 renderStart = SDL_GetPerformanceCounter();

    const Vec2 cameraOffset = m_camera.position;
    Camera renderCamera = m_camera;
    renderCamera.position = cameraOffset;
    const auto &allyPool = m_world.allies();
    const auto &enemyPool = m_world.enemies();
    m_framePerf.entities = static_cast<int>(allyPool.size() + enemyPool.size() + (sim.commander.alive ? 1 : 0));
    renderScene(renderer, sim, renderCamera, m_hudFont, m_debugFont, m_tileMap, m_atlas, m_screenWidth, m_screenHeight,
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

#ifndef KUSOZAKO_SKIP_APP_MAIN
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    auto configLoader = std::make_shared<AppConfigLoader>(std::filesystem::absolute("config"));
    GameApplication app(std::move(configLoader));
    app.sceneStack().push(std::make_unique<BattleScene>());
    return app.run();
}
#endif

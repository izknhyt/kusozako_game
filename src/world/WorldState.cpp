#include "world/WorldState.h"

#include "world/ComponentPool.h"
#include "world/FormationUtils.h"
#include "world/LegacySimulation.h"
#include "world/LegacyTypes.h"
#include "world/SkillRuntime.h"
#include "world/spawn/Spawner.h"
#include "world/spawn/WaveController.h"
#include "world/systems/BehaviorSystem.h"
#include "world/systems/CombatSystem.h"
#include "world/systems/CommanderInputSystem.h"
#include "world/systems/FormationSystem.h"
#include "world/systems/JobAbilitySystem.h"
#include "world/systems/MoraleSystem.h"
#include "world/systems/MovementSystem.h"
#include "world/systems/RenderingPrepSystem.h"
#include "world/systems/SystemContext.h"
#include "events/EventBus.h"
#include "telemetry/TelemetrySink.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// SpawnSystem stub — minimal system that does nothing; the real spawn logic
// is handled by WorldState::runSpawnStage via WaveController/Spawner.
namespace world::systems
{

class SpawnSystem : public ISystem
{
  public:
    void update(float, SystemContext &) override {}
};

} // namespace world::systems

// Free helper functions used by WorldState and scenes
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

void WorldState::commandChibiMove(const std::vector<int> &indices, const Vec2 &targetWorld)
{
    if (!m_sim)
    {
        return;
    }
    bool dirty = false;
    const float arrivalRadius = std::max(m_sim->config.pixels_per_unit * 0.75f, 8.0f);
    for (int idx : indices)
    {
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_sim->yunas.size())
        {
            continue;
        }
        Unit &unit = m_sim->yunas[static_cast<std::size_t>(idx)];
        if (unit.hp <= 0.0f || unit.isNamed)
        {
            continue;
        }
        unit.manualOrder.active = true;
        unit.manualOrder.enemyIndex = -1;
        unit.manualOrder.target = targetWorld;
        unit.manualOrder.arrivalRadiusPx = arrivalRadius;
        unit.manualOrder.holdPosition = true;
        dirty = true;
    }
    if (dirty)
    {
        markComponentsDirty();
    }
}

void WorldState::commandChibiAttack(const std::vector<int> &indices, int enemyIndex)
{
    if (!m_sim)
    {
        return;
    }
    if (enemyIndex < 0 || static_cast<std::size_t>(enemyIndex) >= m_sim->enemies.size())
    {
        return;
    }
    EnemyUnit &target = m_sim->enemies[static_cast<std::size_t>(enemyIndex)];
    if (target.hp <= 0.0f)
    {
        return;
    }
    bool dirty = false;
    for (int idx : indices)
    {
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_sim->yunas.size())
        {
            continue;
        }
        Unit &unit = m_sim->yunas[static_cast<std::size_t>(idx)];
        if (unit.hp <= 0.0f || unit.isNamed)
        {
            continue;
        }
        unit.manualOrder.active = true;
        unit.manualOrder.enemyIndex = enemyIndex;
        unit.manualOrder.target = target.pos;
        unit.manualOrder.holdPosition = false;
        dirty = true;
    }
    if (dirty)
    {
        markComponentsDirty();
    }
}

void WorldState::clearChibiManualOrders(const std::vector<int> &indices)
{
    if (!m_sim)
    {
        return;
    }
    bool dirty = false;
    for (int idx : indices)
    {
        if (idx < 0 || static_cast<std::size_t>(idx) >= m_sim->yunas.size())
        {
            continue;
        }
        Unit &unit = m_sim->yunas[static_cast<std::size_t>(idx)];
        if (!unit.manualOrder.active)
        {
            continue;
        }
        unit.manualOrder.active = false;
        unit.manualOrder.enemyIndex = -1;
        dirty = true;
    }
    if (dirty)
    {
        markComponentsDirty();
    }
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

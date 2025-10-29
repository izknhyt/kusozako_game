#pragma once

#include "input/ActionBuffer.h"
#include "world/ComponentPool.h"
#include "world/FrameAllocator.h"
#include "world/LegacySimulation.h"

#include <memory>

struct CommanderUnit;
struct HUDState;
struct MissionConfig;
struct MissionUIOptions;
struct MissionFailConditions;
enum class MissionMode;
struct RuntimeSkill;
class EventBus;
class TelemetrySink;

namespace world
{

using CaptureRuntime = LegacySimulation::CaptureRuntime;

namespace systems
{

enum class SystemStage : std::uint8_t
{
    InputProcessing = 0,
    CommandAndMorale,
    AiDecision,
    Movement,
    Combat,
    StateUpdate,
    Spawn,
    RenderingPrep,
};

struct MissionContext
{
    bool &hasMission;
    MissionConfig &config;
    MissionMode &mode;
    MissionUIOptions &ui;
    MissionFailConditions &fail;
    float &timer;
    float &victoryCountdown;
};

struct SystemContext
{
    LegacySimulation &simulation;
    EntityRegistry &registry;
    ComponentPool<Unit> &allies;
    ComponentPool<EnemyUnit> &enemies;
    ComponentPool<WallSegment> &walls;
    ComponentPool<CaptureRuntime> &missionZones;

    CommanderUnit &commander;
    HUDState &hud;
    float &baseHp;
    bool &orderActive;
    float &orderTimer;
    bool &waveScriptComplete;
    bool &spawnerIdle;
    float &timeSinceLastEnemySpawn;
    std::vector<RuntimeSkill> &skills;
    int &selectedSkill;
    bool &rallyState;
    float &spawnRateMultiplier;
    float &spawnSlowMultiplier;
    float &spawnSlowTimer;

    std::vector<Unit> &yunaUnits;
    std::vector<EnemyUnit> &enemyUnits;
    std::vector<WallSegment> &wallSegments;
    std::vector<GateRuntime> &gates;
    std::vector<LegacySimulation::PendingRespawn> &yunaRespawnQueue;
    float &commanderRespawnTimer;
    float &commanderInvulnTimer;

    FrameAllocator &frameAllocator;
    MissionContext mission;
    const ActionBuffer &actions;
    std::shared_ptr<EventBus> eventBus;
    std::shared_ptr<TelemetrySink> telemetry;
    bool componentsDirty = false;

    void requestComponentSync()
    {
        componentsDirty = true;
    }
};

class ISystem
{
  public:
    virtual ~ISystem() = default;
    virtual void update(float dt, SystemContext &context) = 0;
};

} // namespace systems

} // namespace world


#pragma once

#include "services/ActionBuffer.h"
#include "world/ComponentPool.h"
#include "world/LegacySimulation.h"

struct CommanderUnit;
struct HUDState;
struct MissionConfig;
struct MissionUIOptions;
struct MissionFailConditions;
enum class MissionMode;

namespace world
{

namespace systems
{

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

    std::vector<Unit> &yunaUnits;
    std::vector<EnemyUnit> &enemyUnits;
    std::vector<WallSegment> &wallSegments;
    std::vector<GateRuntime> &gates;
    std::vector<float> &yunaRespawnQueue;
    float &commanderRespawnTimer;
    float &commanderInvulnTimer;

    MissionContext mission;
    const ActionBuffer &actions;
};

class ISystem
{
  public:
    virtual ~ISystem() = default;
    virtual void update(float dt, SystemContext &context) = 0;
};

} // namespace systems

} // namespace world


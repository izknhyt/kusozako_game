#pragma once

#include "world/ComponentPool.h"

#include <vector>

struct Unit;
struct EnemyUnit;
struct WallSegment;
struct CaptureRuntime;
struct CommanderUnit;
struct HUDState;
struct MissionConfig;
struct MissionUIOptions;
struct MissionFailConditions;
enum class MissionMode;
struct RuntimeSkill;

namespace world
{

struct LegacySimulation;

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

    MissionContext mission;
};

class ISystem
{
  public:
    virtual ~ISystem() = default;
    virtual void update(float dt, SystemContext &context) = 0;
};

} // namespace systems

} // namespace world


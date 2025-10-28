#pragma once

#include "world/FormationUtils.h"
#include "world/LegacyTypes.h"
#include "world/MoraleTypes.h"
#include "world/SkillRuntime.h"

#include <algorithm>
#include <string>
#include <vector>

enum class MissionMode
{
    None,
    Boss,
    Capture,
    Survival,
};

namespace world
{

struct MissionUIOptions
{
    bool showGoalText = false;
    std::string goalText;
    bool showTimer = false;
    bool showBossHpBar = false;
};

struct SurvivalRuntime
{
    float duration = 0.0f;
    float elapsed = 0.0f;
    float spawnMultiplier = 1.0f;
};

struct BossRuntime
{
    float hp = 0.0f;
    float maxHp = 0.0f;
};

struct CaptureRuntime
{
    bool captured = false;
};

struct GameConfig
{
    int base_hp = 0;
    float telemetry_duration = 3.0f;
};

struct CommanderUnit
{
    float hp = 0.0f;
    bool alive = true;
};

struct LegacySimulation
{
    struct RenderQueue
    {
        struct AlignmentHud
        {
            bool active = false;
            float secondsRemaining = 0.0f;
            float progress = 0.0f;
            std::size_t followers = 0;
            std::string label;
        } alignment;

        bool lodActive = false;
        bool skipActors = false;
        int lodFrameCounter = 0;
        std::string telemetryText;
        float telemetryTimer = 0.0f;
        std::string performanceWarningText;
        float performanceWarningTimer = 0.0f;
        std::string spawnWarningText;
        float spawnWarningTimer = 0.0f;
    } renderQueue;

    GameConfig config;
    MissionMode missionMode = MissionMode::None;
    MissionUIOptions missionUI;
    SurvivalRuntime survival;
    BossRuntime boss;
    float missionTimer = 0.0f;
    float baseHp = 0.0f;
    HUDState hud;
    CommanderUnit commander;
    std::vector<int> yunas;
    std::vector<int> enemies;
    std::vector<CaptureRuntime> captureZones;
    int captureGoal = 0;
    int capturedZones = 0;
    float commanderRespawnTimer = 0.0f;
    std::vector<RuntimeSkill> skills;
    int selectedSkill = -1;
    Formation formation = Formation::Swarm;
    ArmyStance stance = ArmyStance::RushNearest;
    ArmyStance defaultStance = ArmyStance::RushNearest;
    bool orderActive = false;
    float orderTimer = 0.0f;
    float orderDuration = 0.0f;

    bool isOrderActive() const { return orderActive; }
    float orderTimeRemaining() const { return std::max(orderTimer, 0.0f); }
    ArmyStance currentOrder() const { return stance; }
};

} // namespace world


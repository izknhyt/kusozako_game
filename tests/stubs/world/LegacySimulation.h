#pragma once

#include "world/FormationUtils.h"
#include "world/LegacyTypes.h"
#include "world/MoraleTypes.h"
#include "world/SkillRuntime.h"
#include "world/StageConfig.h"

#include <algorithm>
#include <string>
#include <vector>

enum class ChibiAction
{
    AssaultEnemy,
    AssaultBase,
    Flee,
    FollowCommander,
    FollowAlly,
    DefendBase,
    Wander
};

inline const char *chibiActionLabel(ChibiAction action)
{
    switch (action)
    {
    case ChibiAction::AssaultEnemy: return "てきにとつげき";
    case ChibiAction::AssaultBase: return "きょてんこうげき";
    case ChibiAction::Flee: return "てきからにげる";
    case ChibiAction::FollowCommander: return "ユウナをおいかける";
    case ChibiAction::FollowAlly: return "みかたをおいかける";
    case ChibiAction::DefendBase: return "きょてんまもる";
    case ChibiAction::Wander:
    default: return "うろうろ";
    }
}

inline const char *chibiActionKey(ChibiAction action)
{
    switch (action)
    {
    case ChibiAction::AssaultEnemy: return "assault_enemy";
    case ChibiAction::AssaultBase: return "assault_base";
    case ChibiAction::Flee: return "flee";
    case ChibiAction::FollowCommander: return "follow_commander";
    case ChibiAction::FollowAlly: return "follow_ally";
    case ChibiAction::DefendBase: return "defend_base";
    case ChibiAction::Wander:
    default: return "wander";
    }
}

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
    float pixels_per_unit = 16.0f;
};

struct CommanderUnit
{
    float hp = 0.0f;
    float mp = 0.0f;
    float mpMax = 0.0f;
    bool alive = true;
};

struct CommanderStats
{
    float radius = 0.0f;
    float speed_u_s = 0.0f;
    float hp = 0.0f;
    float dps = 0.0f;
    float mp = 0.0f;
    float mpRegen = 0.0f;
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
    CommanderStats commanderStats;
    float restartCooldown = 0.0f;
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
    struct StubAllyBase
    {
        std::string id;
        float hp = 0.0f;
        float maxHp = 0.0f;
        bool destroyed = false;
        float auraRadiusPx = 0.0f;
    };
    struct StubEnemyBase
    {
        std::string id;
        float hp = 0.0f;
        float maxHp = 0.0f;
        bool sealed = false;
    };
    struct StageRuntimeState
    {
        bool enabled = false;
        bool dragonDefeated = false;
        StageVictoryRequirements victory{};
        std::vector<StubAllyBase> allyBases;
        std::vector<StubEnemyBase> enemyBases;
    } stage;

    bool isOrderActive() const { return orderActive; }
    float orderTimeRemaining() const { return std::max(orderTimer, 0.0f); }
    ArmyStance currentOrder() const { return stance; }
    StageRuntimeState &stageState() { return stage; }
    const StageRuntimeState &stageState() const { return stage; }
    std::size_t sealedEnemyBases() const
    {
        if (!stage.enabled)
        {
            return 0;
        }
        std::size_t count = 0;
        for (const auto &base : stage.enemyBases)
        {
            if (base.sealed)
            {
                ++count;
            }
        }
        return count;
    }

    float totalAllyBaseMaxHp() const
    {
        float total = 0.0f;
        for (const auto &base : stage.allyBases)
        {
            total += base.maxHp;
        }
        return total;
    }
    void addDynamicHazard(const Vec2 &, float, float, float) {}
    void decayDynamicHazards(float) {}
    void setStageHazardActive(const std::string &, bool) {}
    void appendAiTelemetryCsv(
        const std::array<float, 7> &,
        float,
        float,
        float,
        float,
        int,
        int,
        float,
        float,
        float,
        float,
        float,
        float,
        const std::string &,
        const std::string &,
        const std::string &,
        const std::string &,
        const std::string &)
    {
    }
};

} // namespace world

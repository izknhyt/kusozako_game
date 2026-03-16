#pragma once

#include "config/AppConfig.h"
#include "core/Vec2.h"
#include "telemetry/TelemetrySink.h"
#include "game/AllyLevelingConfig.h"
#include "world/LegacyTypes.h"
#include "world/MoraleTypes.h"
#include "world/SkillRuntime.h"
#include "world/StageConfig.h"

#include <algorithm>
#include <array>
#include <deque>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <type_traits>

// Forward declarations for helper functions shared with the legacy simulation runtime.
Vec2 operator+(const Vec2 &a, const Vec2 &b);
Vec2 operator-(const Vec2 &a, const Vec2 &b);
Vec2 operator*(const Vec2 &a, float s);
Vec2 operator/(const Vec2 &a, float s);
Vec2 &operator+=(Vec2 &a, const Vec2 &b);
Vec2 lerp(const Vec2 &a, const Vec2 &b, float t);
float dot(const Vec2 &a, const Vec2 &b);
float lengthSq(const Vec2 &v);
float length(const Vec2 &v);
Vec2 normalize(const Vec2 &v);

Vec2 tileToWorld(const Vec2 &tile, int tileSize);
Vec2 leftmostGateWorld(const MapDefs &defs);
std::vector<Vec2> computeFormationOffsets(Formation formation, std::size_t count);
const char *stanceLabel(ArmyStance stance);
const char *temperamentBehaviorName(TemperamentBehavior behavior);

namespace world::spawn
{
class WaveController;
}

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

enum class PanicMode
{
    None,
    Flee,
    Tokkou,
    Cling,
    Kaiten,
    RunAround
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
    ChibiAction microAction = ChibiAction::Wander;
    ChibiAction previousMicroAction = ChibiAction::Wander;
    float microActionDecisionTimer = 0.0f;
    float microActionStickyTimer = 0.0f;
    float previousMicroActionTimer = 0.0f;
    ChibiAction baseRole = ChibiAction::Wander;
    bool roleAssigned = false;
    PanicMode panicMode = PanicMode::None;
};

struct WarriorJobRuntime
{
    float stumbleTimer = 0.0f;
};

struct ArcherJobRuntime
{
    bool focusReady = false;
    float holdTimer = 0.0f;
};

struct ShieldJobRuntime
{
    float tauntTimer = 0.0f;
    float selfSlowTimer = 0.0f;
};

struct ManualOrder
{
    bool active = false;
    Vec2 target{0.0f, 0.0f};
    int enemyIndex = -1;
    float arrivalRadiusPx = 12.0f;
    bool holdPosition = true;
};

struct JobRuntimeState
{
    UnitJob job = UnitJob::Warrior;
    float cooldown = 0.0f;
    float endlag = 0.0f;
    WarriorJobRuntime warrior{};
    ArcherJobRuntime archer{};
    ShieldJobRuntime shield{};
};

struct Unit
{
    Vec2 pos;
    float hp = 0.0f;
    float maxHp = 0.0f;
    float radius = 4.0f;
    bool followBySkill = false;
    bool followByStance = false;
    Vec2 formationOffset{0.0f, 0.0f};
    Vec2 desiredVelocity{0.0f, 0.0f};
    bool hasDesiredVelocity = false;
    float attackLockTimer = 0.0f;
    float attackSwingTimer = 0.0f;
    float attackSwingDuration = 0.0f;
    TemperamentState temperament;
    MoraleState moraleState = MoraleState::Stable;
    float moraleTimer = 0.0f;
    float moraleImmunityTimer = 0.0f;
    bool moraleComfortShield = false;
    bool moraleBarrierActive = false;
    float moraleSpeedMultiplier = 1.0f;
    float moraleAccuracyMultiplier = 1.0f;
    float moraleDefenseMultiplier = 1.0f;
    float moraleAttackIntervalMultiplier = 1.0f;
    float moraleIgnoreOrdersChance = 0.0f;
    float moraleDetectionRadiusMultiplier = 1.0f;
    float moraleSpawnDelayMultiplier = 1.0f;
    float moraleRetargetCooldownMultiplier = 1.0f;
    float moraleCommandObeyBonus = 0.0f;
    bool moraleRetreatActive = false;
    float moraleRetreatTimer = 0.0f;
    float moraleRetreatSpeedMultiplier = 1.0f;
    float moraleRetreatHomewardBias = 1.0f;
    float moraleRetreatDuration = 0.0f;
    float moraleRetreatCheckInterval = 0.0f;
    float moraleRetreatCheckChance = 0.0f;
    float moraleRetreatCheckTimer = 0.0f;
    float moraleBarrierLingerTimer = 0.0f;
    float moraleIgnoreOrdersTimer = 0.0f;
    bool moraleIgnoringOrders = false;
    bool moraleLightMesomesoPending = false;
    int braveryAxis = 0;
    int wisdomAxis = 0;
    bool forcePanic = false;
    bool panicTokkouActive = false;
    float panicTokkouTimer = 0.0f;
    bool panicClingActive = false;
    float panicClingAuraTimer = 0.0f;
    bool panicKaitenActive = false;
    float panicKaitenTimer = 0.0f;
    float panicKaitenGuardTimer = 0.0f;
    Vec2 panicKaitenGuardDirection{1.0f, 0.0f};
    bool panicRunActive = false;
    float panicRunDashTimer = 0.0f;
    float panicRunJitterTimer = 0.0f;
    float panicRunRepathTimer = 0.0f;
    float panicRunAuraTimer = 0.0f;
    Vec2 panicRunDirection{0.0f, 0.0f};
    Vec2 panicRunJitterDirection{0.0f, 0.0f};
    bool hpRetreatActive = false;
    Vec2 lastVelocity{0.0f, 0.0f};
    float facingX = 1.0f;
    JobRuntimeState job{};
    bool respawnAllowed = true;
    std::string name;
    bool isNamed = false;
    int level = 1;
    float namedSkillCooldown = 0.0f;

    ManualOrder manualOrder{};
};

struct CommanderUnit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 12.0f;
    float mp = 0.0f;
    float mpMax = 0.0f;
    float mpRegen = 0.0f;
    bool alive = true;
    Vec2 moveIntent{0.0f, 0.0f};
    bool hasMoveIntent = false;
    bool guardActive = false;
    float attackLockTimer = 0.0f;
    float attackSwingTimer = 0.0f;
    float attackSwingDuration = 0.0f;
    Vec2 lastVelocity{0.0f, 0.0f};
    Vec2 lastMoveDir{1.0f, 0.0f};
    int attackTargetIndex = -1;
    int focusTargetIndex = -1;
};

struct EnemyUnit
{
    Vec2 pos;
    Vec2 laneOffset{0.0f, 0.0f};
    float hp = 0.0f;
    float maxHp = 0.0f;
    float radius = 0.0f;
    float attackRangePx = 0.0f;
    EnemyArchetype type = EnemyArchetype::Slime;
    float speedPx = 0.0f;
    float dpsUnit = 0.0f;
    float dpsBase = 0.0f;
    float dpsWall = 0.0f;
    bool noOverlap = false;
    Vec2 tauntTarget{0.0f, 0.0f};
    float tauntTimer = 0.0f;
    int focusUnitIndex = -1;
    float focusTimer = 0.0f;
    float breathCooldown = 0.0f;
    float breathTimer = 0.0f;
    float attackLockTimer = 0.0f;
    float attackSwingTimer = 0.0f;
    float attackSwingDuration = 0.0f;
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

namespace world
{

std::string normalizeTelemetry(const std::string &text);

struct StageAllyBaseState
{
    std::string id;
    Vec2 pos;
    float hp = 0.0f;
    float maxHp = 0.0f;
    float baseMaxHp = 0.0f;
    float auraRadiusPx = 0.0f;
    bool destroyed = false;
    bool destroyedDefault = false;
};

struct StageEnemyBaseState
{
    std::string id;
    Vec2 pos;
    float hp = 0.0f;
    float maxHp = 0.0f;
    static constexpr float kHpFlashDuration = 0.35f;
    float hpFlashTimer = 0.0f;
    float radiusPx = 0.0f;
    float ratePerSecond = 0.0f;
    float rateMax = 0.0f;
    std::unordered_map<std::string, float> weights;
    bool sealed = false;
    bool sealedDefault = false;
    float spawnAccumulator = 0.0f;
};

struct StageHazardState
{
    std::string id;
    Vec2 pos;
    float radius = 0.0f;
    float weight = 1.0f;
    bool active = true;
    bool activeDefault = true;
    std::string triggerEnemyBase;
};

struct StageRuntimeState
{
    bool enabled = false;
    StageAllyFactoryConfig allyFactory{};
    StageBaseSealBehavior sealBehavior{};
    StageGlobalCaps globalCaps{};
    StageVictoryRequirements victory{};
    StageSpeedConfig speed{};
    bool dragonDefeated = false;
    std::vector<StageAllyBaseState> allyBases;
    std::vector<StageEnemyBaseState> enemyBases;
    std::vector<StageHazardState> hazards;
};

struct LegacySimulation
{
    enum class SpawnOrigin
    {
        Natural,
        Respawn,
        Reinforcement
    };

    struct PendingRespawn
    {
        float timer = 0.0f;
        UnitJob job = UnitJob::Warrior;
    };

    GameConfig config;
    TemperamentConfig temperamentConfig;
    ChibiPersonalityConfig chibiPersonalityConfig;
    ChibiAiParams chibiAiParams;
    EntityStats yunaStats;
    EntityStats slimeStats;
    EntityStats goblinStats;
    EntityStats magicianStats;
    EntityStats batStats;
    EntityStats toritoriStats;
    EntityStats golemStats;
    WallbreakerStats wallbreakerStats;
    CommanderStats commanderStats;
    std::unordered_map<std::string, int> namedLevels;
    float meanChibiLevel = 1.0f;
    CommanderUnit commander;
    MapDefs mapDefs;
    SpawnScript spawnScript;
    StageRuntimeState stage;
    EconomyConfig economyConfig;
    struct EconomyState
    {
        int mana = 0;
        int cap = 0;
        int tokens = 0;
        float tokenBonus = 0.0f; // e.g., 0.05 for +5%
        float gainMultiplier = 1.0f; // meta mana gain multiplier
    } economy;
    std::vector<Unit> yunas;
    std::deque<UnitJob> jobHistory;
    std::size_t jobHistoryLimit = 32;
    std::vector<PendingRespawn> yunaRespawns;
    std::deque<UnitJob> reinforcementJobs;
    std::deque<UnitJob> spawnTelemetryWindow;
    std::array<std::uint64_t, UnitJobCount> spawnTelemetryTotals{};
    std::uint64_t spawnTelemetryTotal = 0;
    bool aiTelemetryHeaderWritten = false;
    bool aiChecklistHeaderWritten = false;
    std::weak_ptr<TelemetrySink> telemetry;
    void recordHazardTelemetry(const char *event, const std::string &id, const Vec2 &pos, float radius, float weight);
    std::vector<EnemyUnit> enemies;
    std::vector<WallSegment> walls;
    std::vector<GateRuntime> gates;
    std::vector<RuntimeSkill> skills;
    struct DeathFx
    {
        Vec2 position{0.0f, 0.0f};
        float timer = 0.0f;
        float duration = 0.0f;
        float facingX = 1.0f;
        bool commander = false;
    };
    std::vector<DeathFx> deathFx;
    struct DynamicHazard
    {
        Vec2 pos;
        float radius = 0.0f;
        float weight = 1.0f;
        float timer = 0.0f;
    };
    std::vector<DynamicHazard> dynamicHazards;
    Vec2 worldMin{0.0f, 0.0f};
    Vec2 worldMax{1280.0f, 720.0f};
    float spawnTimer = 0.0f;
    float yunaSpawnTimer = 0.0f;
    float simTime = 0.0f;
    float timeSinceLastEnemySpawn = 0.0f;
    float restartCooldown = 0.0f;
    int statChibiDeaths = 0;
    int statEnemyKills = 0;
    float baseHp = 0.0f;
    bool spawnEnabled = true;
    bool disableYunaSpawns = false;
    bool debugForceBaseTarget = false;
    bool debugForceBaseContact = false;
    GameResult result = GameResult::Playing;
    HUDState hud;
    static constexpr float kCommanderHudHpLagSpeed = 120.0f;
    static constexpr float kCommanderHudHealFlashDuration = 0.2f;
    float commanderHudPrevHp = 0.0f;
    float commanderHudHpLag = 0.0f;
    float commanderHudHealFlashTimer = 0.0f;
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
    bool dragonSpawned = false;
    bool golemSpawned = false;

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

    struct RenderColor
    {
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;
    };

    struct RenderQueue
    {
        struct AllySprite
        {
            Vec2 position{0.0f, 0.0f};
            float radius = 0.0f;
            bool commander = false;
            UnitJob job = UnitJob::Warrior;
            std::uint8_t alpha = 255;
            MoraleState morale = MoraleState::Stable;
            bool named = false;
            std::string name;
            const TemperamentDefinition *temperamentDefinition = nullptr;
            TemperamentBehavior temperamentBehavior = TemperamentBehavior::Wander;
            bool temperamentMimicActive = false;
            TemperamentBehavior temperamentMimicBehavior = TemperamentBehavior::Wander;
            std::size_t unitIndex = 0;
            bool hasUnitIndex = false;
            int braveryAxis = 0;
            int wisdomAxis = 0;
            bool panicActive = false;
            bool forcePanic = false;
            float panicTimer = 0.0f;
            bool panicTokkou = false;
            bool panicCling = false;
            bool panicKaiten = false;
            bool panicRunAround = false;
            float panicBranchTimer = 0.0f;
            ChibiAction action = ChibiAction::Wander;
            float facingX = 1.0f;
            bool moving = false;
            bool attacking = false;
            float attackTimer = 0.0f;
            float attackDuration = 0.0f;
        };

        struct EnemySprite
        {
            Vec2 position{0.0f, 0.0f};
            float radius = 0.0f;
            EnemyArchetype type = EnemyArchetype::Slime;
            float hp = 0.0f;
            float maxHp = 0.0f;
        };

        struct WallSprite
        {
            Vec2 position{0.0f, 0.0f};
            float radius = 0.0f;
        };

        struct AlignmentHud
        {
            bool active = false;
            float secondsRemaining = 0.0f;
            float progress = 0.0f;
            std::size_t followers = 0;
            std::string label;
        } alignment;

        struct MoraleIcon
        {
            Vec2 position{0.0f, 0.0f};
            float radius = 0.0f;
            MoraleState state = MoraleState::Stable;
            bool commander = false;
            std::size_t unitIndex = 0;
        };

        struct DeathFx
        {
            Vec2 position{0.0f, 0.0f};
            float timer = 0.0f;
            float duration = 0.0f;
            float facingX = 1.0f;
            bool commander = false;
        };

        struct Projectile
        {
            Vec2 position{0.0f, 0.0f};
            float radius = 4.0f;
        };

        struct EffectSprite
        {
            Vec2 position{0.0f, 0.0f};
            float radius = 0.0f;
            std::uint8_t r = 255;
            std::uint8_t g = 255;
            std::uint8_t b = 255;
            std::uint8_t a = 180;
            bool cone = false;
            Vec2 dir{1.0f, 0.0f};
            float length = 0.0f;
            float nearWidth = 0.0f;
            float farWidth = 0.0f;
        };

        bool lodActive = false;
        bool skipActors = false;
        int lodFrameCounter = 0;
        std::vector<AllySprite> allies;
        std::vector<EnemySprite> enemies;
        std::vector<WallSprite> walls;
        std::vector<MoraleIcon> moraleIcons;
        std::vector<DeathFx> deathFx;
        std::vector<Projectile> projectiles;
        std::vector<EffectSprite> effects;
        std::string telemetryText;
        float telemetryTimer = 0.0f;
        std::string performanceWarningText;
        float performanceWarningTimer = 0.0f;
        std::string spawnWarningText;
        float spawnWarningTimer = 0.0f;

        void clearDynamic()
        {
            allies.clear();
            enemies.clear();
            walls.clear();
            moraleIcons.clear();
            deathFx.clear();
            projectiles.clear();
            effects.clear();
        }
    } renderQueue;

    struct Projectile
    {
        Vec2 pos;
        Vec2 vel;
        float radius = 4.0f;
        float damage = 0.0f;
        float life = 0.0f;
    };
    std::vector<Projectile> projectiles;
    struct Effect
    {
        Vec2 pos{0.0f, 0.0f};
        float radius = 0.0f;
        float r = 255.0f;
        float g = 255.0f;
        float b = 255.0f;
        float a = 200.0f;
        float ttl = 0.0f;
        bool cone = false;
        Vec2 dir{1.0f, 0.0f};
        float length = 0.0f;
        float nearWidth = 0.0f;
        float farWidth = 0.0f;
    };
    std::vector<Effect> effects;

    struct SpawnBudgetState
    {
        std::size_t totalDeferred = 0;
    } spawnBudgetState;

    struct ActionTelemetryState
    {
        std::array<float, static_cast<std::size_t>(ChibiAction::Wander) + 1> unitTime{};
        float panicUnitTime = 0.0f;
        float totalUnitTime = 0.0f;
        float timer = 0.0f;
        float boidRadiusAccum = 0.0f;
        int boidRadiusSamples = 0;
        int boidRadiusExceed = 0;
        int tokkouActive = 0;
        int tokkouPeak = 0;
        float clingUnitTime = 0.0f;
        float kaitenUnitTime = 0.0f;
        float runUnitTime = 0.0f;
        float hazardCountAccum = 0.0f;
        int hazardSamples = 0;
        float aoeAvoidUnitTime = 0.0f;
        float aoeAvoidMagnitudeAccum = 0.0f;
        int aoeAvoidMagnitudeSamples = 0;
    } actionTelemetry;

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
    FormationAlignmentConfig formationDefaults;
    float formationAlignTimer = 0.0f;
    float formationDefenseMul = 1.0f;
    int selectedSkill = 0;
    bool rallyState = false;
    float spawnRateMultiplier = 1.0f;
    float spawnRateBaseMultiplier = 1.0f;
    float baseDamageReduction = 0.0f;
    float baseAuraRegenPerSecond = 0.0f;
    float moraleSpawnMultiplier = 1.0f;
    float spawnSlowMultiplier = 1.0f;
    float spawnSlowTimer = 0.0f;
    float commanderFireballCooldown = 0.0f;
    float commanderMp = 0.0f;
    float commanderMpMax = 0.0f;
    struct MoraleSummary
    {
        float averageSpeedMul = 1.0f;
        float averageAccuracyMul = 1.0f;
        float averageDefenseMul = 1.0f;
        std::size_t panicCount = 0;
        std::size_t mesomesoCount = 0;
        MoraleState commanderState = MoraleState::Stable;
        bool rallySuppressed = false;
    } moraleSummary;
    float commanderRespawnTimer = 0.0f;
    float commanderInvulnTimer = 0.0f;

    bool waveScriptComplete = false;
    bool spawnerIdle = true;
    bool yunaRespawnsEnabled = false;

    Vec2 basePos;
    Vec2 yunaSpawnPos;

    std::uint64_t frameCounter = 0;
    std::size_t frameCapturePending = 0;
    std::uint64_t frameCaptureBatch = 0;
    std::size_t frameCaptureIndex = 0;

    void captureFrameSnapshot(TelemetrySink &sink);
    std::filesystem::path telemetryDebugDirectory(const TelemetrySink &sink) const;

    void updateProjectiles(float dt)
    {
        if (projectiles.empty())
        {
            return;
        }
        std::vector<Projectile> next;
        next.reserve(projectiles.size());
        for (Projectile &p : projectiles)
        {
            if (p.life <= 0.0f)
            {
                continue;
            }
            p.life -= dt;
            p.pos += p.vel * dt;
            bool hit = false;
            for (EnemyUnit &enemy : enemies)
            {
                if (enemy.hp <= 0.0f)
                {
                    continue;
                }
                const float combined = p.radius + enemy.radius;
                if (lengthSq(enemy.pos - p.pos) <= combined * combined)
                {
                    enemy.hp -= p.damage;
                    hit = true;
                    break;
                }
            }
            if (!hit && p.life > 0.0f)
            {
                next.push_back(p);
            }
        }
        projectiles.swap(next);
    }

    void updateCommanderResources(float dt)
    {
        if (commanderFireballCooldown > 0.0f)
        {
            commanderFireballCooldown = std::max(0.0f, commanderFireballCooldown - dt);
        }
        if (commander.alive && commander.mpRegen > 0.0f && commander.mpMax > 0.0f)
        {
            commander.mp = std::min(commander.mpMax, commander.mp + commander.mpRegen * dt);
        }
    }

    struct SpawnHistoryDumpResult
    {
        bool success = false;
        std::filesystem::path path;
        std::string error;
    };

    SpawnHistoryDumpResult dumpSpawnHistory(const spawn::WaveController &controller) const;

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

    void clearStageConfiguration()
    {
        stage = {};
        dynamicHazards.clear();
        dragonSpawned = false;
        golemSpawned = false;
    }

    void configureStage(const StageConfig &cfg)
    {
        stage.enabled = true;
        stage.allyFactory = cfg.allyFactory;
        stage.sealBehavior = cfg.sealBehavior;
        stage.globalCaps = cfg.globalCaps;
        stage.victory = cfg.victory;
        stage.victory.requireDragon = true;
        stage.victory.requireAllEnemyBases = true;
        stage.speed = cfg.speed;
        stage.allyBases.clear();
        stage.enemyBases.clear();
        dynamicHazards.clear();
        stage.hazards.clear();
        dragonSpawned = false;
        golemSpawned = false;

        const int tileSize = mapDefs.tile_size > 0 ? mapDefs.tile_size : config.pixels_per_unit;

        for (const StageAllyBaseConfig &baseCfg : cfg.allyBases)
        {
            StageAllyBaseState base;
            base.id = baseCfg.id;
            base.maxHp = baseCfg.hp;
            base.hp = baseCfg.hp;
            base.baseMaxHp = baseCfg.hp;
            base.auraRadiusPx = baseCfg.auraRadiusPx;
            base.destroyed = baseCfg.sealedByDefault;
            base.destroyedDefault = baseCfg.sealedByDefault;
            if (base.destroyed)
            {
                base.hp = 0.0f;
            }
            base.pos = tileToWorld(baseCfg.position, tileSize);
            stage.allyBases.push_back(base);
        }
        // 標準の単一拠点運用でも、常に allyBases に1件は入れておく
        if (stage.allyBases.empty())
        {
            StageAllyBaseState base;
            base.id = "main";
            base.maxHp = static_cast<float>(config.base_hp);
            base.hp = base.maxHp;
            base.baseMaxHp = base.maxHp;
            base.auraRadiusPx = 128.0f;
            base.destroyed = false;
            base.destroyedDefault = false;
            base.pos = tileToWorld(mapDefs.base_tile, tileSize);
            stage.allyBases.push_back(base);
        }

        for (const StageEnemyBaseConfig &baseCfg : cfg.enemyBases)
        {
            StageEnemyBaseState base;
            base.id = baseCfg.id;
            base.pos = tileToWorld(baseCfg.position, tileSize);
            base.maxHp = baseCfg.hp;
            base.hp = baseCfg.hp;
            base.radiusPx = baseCfg.radiusPx;
            base.ratePerSecond = baseCfg.ratePerSecond;
            base.rateMax = baseCfg.rateMax;
            base.weights = baseCfg.weights;
            base.sealed = baseCfg.sealedByDefault;
            base.sealedDefault = baseCfg.sealedByDefault;
            base.spawnAccumulator = 0.0f;
            stage.enemyBases.push_back(base);
        }
        for (const StageHazardConfig &hazardCfg : cfg.hazards)
        {
            StageHazardState hazard;
            hazard.id = hazardCfg.id;
            hazard.pos = tileToWorld(hazardCfg.position, tileSize);
            hazard.radius = hazardCfg.radiusPx;
            hazard.weight = hazardCfg.weight;
            hazard.active = hazardCfg.enabled;
            hazard.activeDefault = hazardCfg.enabled;
            hazard.triggerEnemyBase = hazardCfg.triggerEnemyBase;
            stage.hazards.push_back(hazard);
        }
        stage.dragonDefeated = false;
        missionVictoryCountdown = -1.0f;
    }

    void addDynamicHazard(const Vec2 &pos, float radius, float weight, float duration)
    {
        if (radius <= 0.0f || duration <= 0.0f)
        {
            return;
        }
        dynamicHazards.push_back({pos, radius, weight, duration});
        recordHazardTelemetry("world.hazard.dynamic", "", pos, radius, weight);
    }

    void decayDynamicHazards(float dt)
    {
        if (dynamicHazards.empty())
        {
            return;
        }
        for (DynamicHazard &hazard : dynamicHazards)
        {
            hazard.timer = std::max(0.0f, hazard.timer - dt);
        }
        dynamicHazards.erase(
            std::remove_if(dynamicHazards.begin(), dynamicHazards.end(),
                           [](const DynamicHazard &hazard) { return hazard.timer <= 0.0f; }),
            dynamicHazards.end());
    }

    void setStageHazardActive(const std::string &id, bool active)
    {
        if (!stage.enabled)
        {
            return;
        }
        for (StageHazardState &hazard : stage.hazards)
        {
            if (hazard.id == id)
            {
                hazard.active = active;
                if (active)
                {
                    pushTelemetry(std::string("Hazard active: ") + hazard.id);
                    recordHazardTelemetry("world.hazard.stage_activated", hazard.id, hazard.pos, hazard.radius, hazard.weight);
                }
                else
                {
                    recordHazardTelemetry("world.hazard.stage_deactivated", hazard.id, hazard.pos, hazard.radius, hazard.weight);
                }
                break;
            }
        }
    }

    void activateHazardsForBase(const std::string &baseId)
    {
        if (!stage.enabled)
        {
            return;
        }
        for (StageHazardState &hazard : stage.hazards)
        {
            if (!hazard.triggerEnemyBase.empty() && hazard.triggerEnemyBase == baseId && !hazard.active)
            {
                hazard.active = true;
                pushTelemetry(std::string("Hazard active: ") + hazard.id);
                recordHazardTelemetry("world.hazard.stage_activated", hazard.id, hazard.pos, hazard.radius, hazard.weight);
            }
        }
    }

    StageAllyBaseState *findAllyBase(const std::string &id)
    {
        if (!stage.enabled)
        {
            return nullptr;
        }
        for (StageAllyBaseState &base : stage.allyBases)
        {
            if (base.id == id)
            {
                return &base;
            }
        }
        return nullptr;
    }

    StageEnemyBaseState *findEnemyBase(const std::string &id)
    {
        if (!stage.enabled)
        {
            return nullptr;
        }
        for (StageEnemyBaseState &base : stage.enemyBases)
        {
            if (base.id == id)
            {
                return &base;
            }
        }
        return nullptr;
    }

    std::size_t sealedEnemyBases() const
    {
        if (!stage.enabled)
        {
            return 0;
        }
        return std::count_if(stage.enemyBases.begin(), stage.enemyBases.end(),
                             [](const StageEnemyBaseState &base) { return base.sealed; });
    }

    bool allEnemyBasesSealed() const
    {
        if (!stage.enabled)
        {
            return false;
        }
        if (stage.enemyBases.empty())
        {
            return true;
        }
        return sealedEnemyBases() == stage.enemyBases.size();
    }

    bool sealEnemyBase(const std::string &id)
    {
        if (StageEnemyBaseState *base = findEnemyBase(id))
        {
            return sealEnemyBase(*base);
        }
        return false;
    }

    bool sealEnemyBase(StageEnemyBaseState &base)
    {
        if (base.sealed)
        {
            return false;
        }
        base.sealed = true;
        base.spawnAccumulator = 0.0f;
        base.hp = 0.0f;
        base.hpFlashTimer = 0.0f;
        pushTelemetry(std::string("Enemy base sealed: ") + (base.id.empty() ? "base" : base.id));
        activateHazardsForBase(base.id);
        maybeDeclareStageVictory();
        return true;
    }

    StageRuntimeState &stageState() { return stage; }
    const StageRuntimeState &stageState() const { return stage; }

    bool areAllAllyBasesDestroyed() const
    {
        if (!stage.enabled || stage.allyBases.empty())
        {
            return false;
        }
        return std::all_of(stage.allyBases.begin(), stage.allyBases.end(),
                           [](const StageAllyBaseState &base) { return base.destroyed; });
    }

    float totalAllyBaseMaxHp() const
    {
        float total = 0.0f;
        for (const StageAllyBaseState &base : stage.allyBases)
        {
            total += base.maxHp;
        }
        return total;
    }

    void damageAllyBase(StageAllyBaseState &base, float amount)
    {
        if (!stage.enabled || amount <= 0.0f || base.destroyed)
        {
            return;
        }
        const float mitigation = std::clamp(baseDamageReduction, 0.0f, 0.5f);
        float effectiveDamage = amount * (1.0f - mitigation);
        // 最低1ダメージは通す
        if (effectiveDamage > 0.0f && effectiveDamage < 1.0f)
        {
            effectiveDamage = 1.0f;
        }
        if (effectiveDamage <= 0.0f)
        {
            return;
        }
        const float delta = std::min(effectiveDamage, base.hp);
        base.hp = std::max(0.0f, base.hp - delta);
        baseHp = std::max(0.0f, baseHp - delta);
        if (base.hp <= 0.0f && !base.destroyed)
        {
            base.destroyed = true;
            std::string name = base.id.empty() ? "Camp" : base.id;
            pushStageBanner(std::string("Ally base lost: ") + name);
            pushTip("Defend remaining bases!");
            checkCommanderLossCondition();
        }
    }

    bool damageEnemyBase(const std::string &id, float amount)
    {
        if (StageEnemyBaseState *base = findEnemyBase(id))
        {
            damageEnemyBase(*base, amount);
            return true;
        }
        return false;
    }

    void damageEnemyBase(StageEnemyBaseState &base, float amount)
    {
        if (!stage.enabled || amount <= 0.0f || base.sealed)
        {
            return;
        }
        const float before = base.hp;
        base.hp = std::max(0.0f, base.hp - amount);
        if (base.hp < before)
        {
            base.hpFlashTimer = StageEnemyBaseState::kHpFlashDuration;
        }
        if (base.hp <= 0.0f)
        {
            std::string name = base.id.empty() ? "Enemy base" : base.id;
            const bool newlySealed = !base.sealed;
            sealEnemyBase(base);
            if (newlySealed)
            {
                pushStageBanner(std::string("Enemy base sealed: ") + name);
                std::ostringstream oss;
                oss << "Enemy bases sealed " << sealedEnemyBases() << '/' << stage.enemyBases.size();
                pushTip(oss.str());
            }
        }
    }

    void resetEconomyState()
    {
        economy.cap = economyConfig.baseCap;
        economy.mana = 0;
        economy.tokens = 0;
        economy.tokenBonus = economyConfig.tokenBonus;
        economy.gainMultiplier = std::max(0.0f, 1.0f + economyConfig.gainBonus);
        statChibiDeaths = 0;
        statEnemyKills = 0;
        updateEconomyHud();
    }

    void updateEconomyHud()
    {
        std::string recommendation;
        if (economy.cap > 0)
        {
            if (economy.mana >= economy.cap)
            {
                recommendation = "Return to camp to spend mana";
            }
            else
            {
                recommendation = "Seal bases to gain mana";
            }
        }
        setEconomyHud(economy.mana, economy.cap, economy.tokens, recommendation);
    }

    int economyRewardForType(EnemyArchetype type) const
    {
        auto lookup = [this](const std::string &key) -> int {
            auto it = economyConfig.enemyRewards.find(key);
            if (it != economyConfig.enemyRewards.end())
            {
                return it->second;
            }
            return 0;
        };
        switch (type)
        {
        case EnemyArchetype::Goblin: return lookup("goblin");
        case EnemyArchetype::Magician: return lookup("magician");
        case EnemyArchetype::Bat: return lookup("bat");
        case EnemyArchetype::Toritori: return lookup("toritori");
        case EnemyArchetype::Golem: return lookup("golem");
        case EnemyArchetype::Wallbreaker: return lookup("wallbreaker");
        case EnemyArchetype::Boss: return lookup("boss");
        case EnemyArchetype::Slime:
        default: return lookup("slime");
        }
    }

    void grantMana(float amount)
    {
        if (amount <= 0.0f || economy.cap <= 0)
        {
            return;
        }
        const float tokenMul = 1.0f + std::max(0, economy.tokens) * std::max(economy.tokenBonus, 0.0f);
        const float totalMul = std::max(0.0f, economy.gainMultiplier) * std::max(tokenMul, 0.0f);
        int delta = static_cast<int>(std::round(amount * totalMul));
        if (delta <= 0)
        {
            return;
        }
        economy.mana = std::min(economy.cap, economy.mana + delta);
        updateEconomyHud();
    }

    void rewardEnemyKill(EnemyArchetype type)
    {
        const int reward = economyRewardForType(type);
        if (reward > 0)
        {
            ++statEnemyKills;
            grantMana(static_cast<float>(reward));
        }
    }

    bool stageVictoryReady() const
    {
        if (!stage.enabled)
        {
            return false;
        }
        if (stage.victory.requireAllEnemyBases)
        {
            if (stage.enemyBases.empty() || !allEnemyBasesSealed())
            {
                return false;
            }
        }
        if (stage.victory.requireDragon && !stage.dragonDefeated)
        {
            return false;
        }
        return true;
    }

    void maybeDeclareStageVictory()
    {
        if (result != GameResult::Playing)
        {
            return;
        }
        if (stageVictoryReady())
        {
            setResult(GameResult::Victory, "Victory");
        }
    }

    void handleEnemyDefeated(const EnemyUnit &enemy)
    {
        rewardEnemyKill(enemy.type);
        if (!stage.enabled)
        {
            return;
        }
        if (enemy.type == EnemyArchetype::Boss)
        {
            stage.dragonDefeated = true;
            maybeDeclareStageVictory();
        }
    }

    void checkCommanderLossCondition()
    {
        if (!stage.enabled)
        {
            return;
        }
        if (areAllAllyBasesDestroyed() && !commander.alive)
        {
            setResult(GameResult::Defeat, "Defeat");
        }
    }

    bool updateStageEnemySpawns(float dt)
    {
        if (!stage.enabled)
        {
            return false;
        }
        // Safety: clear any legacy boss that may exist before the new spawn trigger fires.
        if (!dragonSpawned)
        {
            const auto before = enemies.size();
            enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const EnemyUnit &enemy) {
                              return enemy.type == EnemyArchetype::Boss;
                          }),
                          enemies.end());
            if (enemies.size() != before)
            {
                boss = {};
            }
        }
        bool spawned = false;

        const std::size_t sealedBases = sealedEnemyBases();
        auto chooseSpawnBase = [&]() -> const StageEnemyBaseState * {
            if (stage.enemyBases.empty())
            {
                return nullptr;
            }
            // Prefer any unsealed base; if none, fall back to the first for safety.
            std::vector<const StageEnemyBaseState *> openBases;
            openBases.reserve(stage.enemyBases.size());
            for (const StageEnemyBaseState &base : stage.enemyBases)
            {
                if (!base.sealed)
                {
                    openBases.push_back(&base);
                }
            }
            if (!openBases.empty())
            {
                std::uniform_int_distribution<std::size_t> dist(0, openBases.size() - 1);
                return openBases[dist(rng)];
            }
            return &stage.enemyBases.front();
        };
        auto spawnNearBase = [&](const StageEnemyBaseState &base) -> Vec2 {
            // Spawn slightly offset from the base center to feel like emerging from that base.
            const float baseRadius = base.radiusPx > 0.0f ? base.radiusPx : 96.0f;
            std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
            const float angle = angleDist(rng);
            const float offset = baseRadius * 0.6f; // close but not overlapping the center
            Vec2 dir{std::cos(angle), std::sin(angle)};
            return base.pos + dir * offset;
        };
        if (!golemSpawned && (simTime >= 150.0f || sealedBases >= 1))
        {
            Vec2 spawnPos = basePos;
            if (const StageEnemyBaseState *base = chooseSpawnBase())
            {
                spawnPos = spawnNearBase(*base);
            }
            spawnOneEnemy(spawnPos, EnemyArchetype::Golem);
            golemSpawned = true;
            spawned = true;
            pushTelemetry("Golem has appeared!");
        }
        if (!dragonSpawned && (simTime >= 300.0f || sealedBases >= 2))
        {
            Vec2 dragonSpawnPos = tileToWorld(missionConfig.boss.tile, mapDefs.tile_size);
            if (stage.enabled)
            {
                if (const StageEnemyBaseState *base = chooseSpawnBase())
                {
                    dragonSpawnPos = spawnNearBase(*base);
                }
            }
            if (spawnMissionBossAt(dragonSpawnPos))
            {
                dragonSpawned = true;
                spawned = true;
                pushTelemetry("Dragon has appeared!");
            }
            else
            {
                pushTelemetry("Dragon spawn failed (missing mission config)");
            }
        }

        const int enemyCap = stage.globalCaps.enemies > 0 ? stage.globalCaps.enemies : std::numeric_limits<int>::max();
        int currentEnemies = static_cast<int>(enemies.size());
        if (currentEnemies >= enemyCap)
        {
            return spawned;
        }

        auto pickType = [this](const StageEnemyBaseState &base) {
            float total = 0.0f;
            for (const auto &kv : base.weights)
            {
                total += std::max(kv.second, 0.0f);
            }
            if (total <= 0.0f)
            {
                return EnemyArchetype::Slime;
            }
            std::uniform_real_distribution<float> dist(0.0f, total);
            float needle = dist(rng);
            for (const auto &kv : base.weights)
            {
                const float weight = std::max(kv.second, 0.0f);
                if (weight <= 0.0f)
                {
                    continue;
                }
                if (needle <= weight)
                {
                    return enemyTypeFromString(kv.first);
                }
                needle -= weight;
            }
            return EnemyArchetype::Slime;
        };

        for (StageEnemyBaseState &base : stage.enemyBases)
        {
            if (base.sealed)
            {
                continue;
            }
            const float targetRate = base.ratePerSecond > 0.0f ? base.ratePerSecond : base.rateMax;
            if (targetRate <= 0.0f)
            {
                continue;
            }
            base.spawnAccumulator += targetRate * dt;
            const int maxSpawnsThisTick = enemyCap - currentEnemies;
            int spawnsPerformed = 0;
            while (base.spawnAccumulator >= 1.0f && currentEnemies < enemyCap)
            {
                EnemyArchetype type = pickType(base);
                spawnOneEnemy(base.pos, type);
                base.spawnAccumulator -= 1.0f;
                ++currentEnemies;
                ++spawnsPerformed;
                spawned = true;
            }
            if (spawnsPerformed >= maxSpawnsThisTick || currentEnemies >= enemyCap)
            {
                break;
            }
        }
        if (spawned)
        {
            maybeDeclareStageVictory();
        }
        return spawned;
    }

    void regenAllyBases(float dt)
    {
        if (baseAuraRegenPerSecond <= 0.0f || dt <= 0.0f)
        {
            return;
        }
        const bool stageEnabled = stage.enabled && !stage.allyBases.empty();
        const float maxHpTotal = stageEnabled ? totalAllyBaseMaxHp() : static_cast<float>(config.base_hp);
        if (maxHpTotal <= 0.0f)
        {
            return;
        }
        const float perBase = baseAuraRegenPerSecond * dt;
        if (perBase <= 0.0f)
        {
            return;
        }
        if (stageEnabled)
        {
            for (StageAllyBaseState &base : stage.allyBases)
            {
                if (base.destroyed || base.hp >= base.maxHp)
                {
                    continue;
                }
                const float delta = std::min(base.maxHp - base.hp, perBase);
                if (delta <= 0.0f)
                {
                    continue;
                }
                base.hp = std::min(base.maxHp, base.hp + delta);
                baseHp = std::min(maxHpTotal, baseHp + delta);
            }
        }
        else
        {
            const float missing = std::max(0.0f, maxHpTotal - baseHp);
            if (missing <= 0.0f)
            {
                return;
            }
            const float delta = std::min(missing, perBase);
            if (delta > 0.0f)
            {
                baseHp = std::min(maxHpTotal, baseHp + delta);
            }
        }
    }

    void setTelemetrySink(std::weak_ptr<TelemetrySink> sink)
    {
        telemetry = std::move(sink);
    }

    void setCommanderGuard(bool active)
    {
        commander.guardActive = active;
    }

    bool spawnProjectile(const Vec2 &origin, const Vec2 &target, float damage, float speed, float radius)
    {
        if (damage <= 0.0f || speed <= 0.0f || radius <= 0.0f)
        {
            return false;
        }
        Vec2 dir = target - origin;
        const float lenSq = lengthSq(dir);
        if (lenSq <= 0.0001f)
        {
            return false;
        }
        dir = dir / std::sqrt(lenSq);
        Projectile p;
        p.pos = origin;
        p.vel = dir * speed;
        p.radius = radius;
        p.damage = damage;
        p.life = length(target - origin) / speed;
        projectiles.push_back(p);
        return true;
    }

    bool canRestart() const { return restartCooldown <= 0.0f; }

    void reset()
    {
        simTime = 0.0f;
        timeSinceLastEnemySpawn = 0.0f;
        restartCooldown = 0.0f;
        yunaSpawnTimer = 0.0f;
        yunas.clear();
        jobHistory.clear();
        const int desiredHistory = std::max(config.jobSpawn.historyLimit, config.jobSpawn.pity.repeatLimit);
        jobHistoryLimit = desiredHistory > 0 ? static_cast<std::size_t>(desiredHistory) : 1;
        yunaRespawns.clear();
        reinforcementJobs.clear();
        spawnTelemetryWindow.clear();
        spawnTelemetryTotals.fill(0);
        spawnTelemetryTotal = 0;
        spawnBudgetState = {};
        statChibiDeaths = 0;
        statEnemyKills = 0;
        yunaRespawnsEnabled = false;
        disableYunaSpawns = false;
        dragonSpawned = false;
        golemSpawned = false;
        enemies.clear();
        walls.clear();
        for (StageAllyBaseState &base : stage.allyBases)
        {
            base.hp = base.maxHp;
            base.destroyed = base.destroyedDefault;
            if (base.destroyed)
            {
                base.hp = 0.0f;
            }
        }
        for (StageEnemyBaseState &base : stage.enemyBases)
        {
            base.sealed = base.sealedDefault;
            base.spawnAccumulator = 0.0f;
            base.hp = base.sealed ? 0.0f : base.maxHp;
        }
        spawnEnabled = true;
        result = GameResult::Playing;
        if (stage.enabled && !stage.allyBases.empty())
        {
            float totalHp = 0.0f;
            for (const StageAllyBaseState &base : stage.allyBases)
            {
                totalHp += base.maxHp;
            }
            baseHp = totalHp;
            stage.dragonDefeated = false;
        }
        else
        {
            baseHp = static_cast<float>(config.base_hp);
        }
        hud = {};
        resetEconomyState();
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
        commander.mpMax = std::max(commanderStats.mp, 0.0f);
        commander.mp = commander.mpMax;
        commander.mpRegen = commanderStats.mpRegen;
        commanderHudPrevHp = commander.hp;
        commanderHudHpLag = commander.hp;
        commanderHudHealFlashTimer = 0.0f;
        commander.pos = yunaSpawnPos;
        commander.lastVelocity = {0.0f, 0.0f};
        commander.lastMoveDir = {1.0f, 0.0f};
        commander.attackTargetIndex = -1;
        commander.focusTargetIndex = -1;
        commander.alive = true;
        commanderRespawnTimer = 0.0f;
        commanderInvulnTimer = 0.0f;
        spawnRateMultiplier = std::max(spawnRateBaseMultiplier, 0.1f);
        moraleSpawnMultiplier = 1.0f;
        spawnSlowMultiplier = 1.0f;
        spawnSlowTimer = 0.0f;
        commanderFireballCooldown = 0.0f;
        rallyState = false;
        stance = defaultStance;
        formation = Formation::Swarm;
        formationAlignTimer = 0.0f;
        formationDefenseMul = 1.0f;
        selectedSkill = 0;
        moraleSummary = {};
        for (RuntimeSkill &skill : skills)
        {
            skill.cooldownRemaining = 0.0f;
            skill.activeTimer = 0.0f;
        }
        waveScriptComplete = false;
        spawnerIdle = true;
        rebuildGates();
        initializeMissionState();

        // Minimal named allies (no respawn)
        const float tileSize = mapDefs.tile_size > 0 ? static_cast<float>(mapDefs.tile_size) : 32.0f;
        spawnNamedUnit("Milly", UnitJob::Archer, commander.pos + Vec2{tileSize * 2.0f, -tileSize * 0.5f});
        spawnNamedUnit("Mary", UnitJob::Shield, commander.pos + Vec2{tileSize * 2.2f, tileSize * 0.5f});
        spawnNamedUnit("Coco", UnitJob::Warrior, commander.pos + Vec2{tileSize * 2.4f, 0.0f});

        // 初期敵配置: 各敵拠点にスライム/ゴブリンを5体ずつ
        for (const StageEnemyBaseState &base : stage.enemyBases)
        {
            const Vec2 pos = base.pos;
            for (int i = 0; i < 5; ++i)
            {
                spawnOneEnemy(pos, EnemyArchetype::Slime);
                spawnOneEnemy(pos, EnemyArchetype::Goblin);
            }
        }
    }

    bool castFireBall(float rangePx, float damage)
    {
        if (!commander.alive || rangePx <= 0.0f || damage <= 0.0f)
        {
            return false;
        }
        if (commanderFireballCooldown > 0.0f)
        {
            return false;
        }
        const float mpCost = 1.0f;
        if (commander.mp < mpCost)
        {
            return false;
        }
        const float rangeSq = rangePx * rangePx;
        EnemyUnit *best = nullptr;
        float bestDist = rangeSq;
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float distSq = lengthSq(enemy.pos - commander.pos);
            if (distSq <= bestDist)
            {
                bestDist = distSq;
                best = &enemy;
            }
        }
        if (!best)
        {
            return false;
        }
        Vec2 dir = best->pos - commander.pos;
        const float lenSq = lengthSq(dir);
        if (lenSq <= 0.0001f)
        {
            return false;
        }
        dir = dir / std::sqrt(lenSq);
        const float speed = 160.0f;
        Projectile p;
        p.pos = commander.pos;
        p.vel = dir * speed;
        p.radius = 5.0f;
        p.damage = damage;
        p.life = rangePx / speed;
        projectiles.push_back(p);
        commander.mp = std::max(0.0f, commander.mp - mpCost);
        commanderFireballCooldown = 0.0f;
        return true;
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
        if (!yunaRespawnsEnabled)
        {
            return;
        }
        PendingRespawn pending;
        pending.timer = computeChibiRespawnTime(overkillRatio);
        pending.job = chooseSpawnJob();
        yunaRespawns.push_back(pending);
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
                    spawnRateMultiplier = std::max(spawnRateBaseMultiplier, 0.1f);
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
                commander.mpMax = std::max(commanderStats.mp, 0.0f);
                commander.mp = commander.mpMax;
                commanderFireballCooldown = 0.0f;
                commander.pos = yunaSpawnPos;
                commanderInvulnTimer = config.commander_respawn.invuln;
                setChibiForcePanic(false);
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
        // どの性格でも、ごく低確率で「うろうろ」「てきからにげる」を割り当てる
        {
            std::uniform_real_distribution<float> tinyRoll(0.0f, 1.0f);
            const float roll = tinyRoll(rng);
            if (roll < 0.005f)
            {
                state.currentBehavior = TemperamentBehavior::Wander;
            }
            else if (roll < 0.010f)
            {
                state.currentBehavior = TemperamentBehavior::FleeNearest;
            }
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
        state.microAction = ChibiAction::Wander;
        state.previousMicroAction = ChibiAction::Wander;
        state.microActionDecisionTimer = 0.0f;
        state.microActionStickyTimer = 0.0f;

        std::uniform_int_distribution<int> axis(-2, 2);
        yuna.braveryAxis = axis(rng);
        yuna.wisdomAxis = axis(rng);
    }

    float panicThresholdForAxis(int axis) const
    {
        if (chibiPersonalityConfig.panicThresholds.empty())
        {
            return 0.35f;
        }
        const int maxIndex = static_cast<int>(chibiPersonalityConfig.panicThresholds.size()) - 1;
        const int index = std::clamp(axis + 2, 0, maxIndex);
        return chibiPersonalityConfig.panicThresholds[static_cast<std::size_t>(index)];
    }

    float panicMinimumDurationForAxis(int axis) const
    {
        if (chibiPersonalityConfig.panicMinimumSeconds.empty())
        {
            return 1.2f;
        }
        const int maxIndex = static_cast<int>(chibiPersonalityConfig.panicMinimumSeconds.size()) - 1;
        const int index = std::clamp(axis + 2, 0, maxIndex);
        return chibiPersonalityConfig.panicMinimumSeconds[static_cast<std::size_t>(index)];
    }

    float panicThresholdFor(const Unit &unit) const
    {
        return panicThresholdForAxis(unit.braveryAxis);
    }

    float panicMinimumDurationFor(const Unit &unit) const
    {
        return panicMinimumDurationForAxis(unit.braveryAxis);
    }

    void setChibiForcePanic(bool active)
    {
        for (Unit &yuna : yunas)
        {
            yuna.forcePanic = active;
            if (active)
            {
                yuna.temperament.panicTimer =
                    std::max(yuna.temperament.panicTimer, panicMinimumDurationFor(yuna));
            }
        }
    }

    void resetUnitMorale(Unit &unit)
    {
        unit.moraleState = MoraleState::Stable;
        unit.moraleTimer = 0.0f;
        unit.moraleImmunityTimer = 0.0f;
        unit.moraleComfortShield = false;
        unit.moraleBarrierActive = false;
        unit.moraleBarrierLingerTimer = 0.0f;
        unit.moraleSpeedMultiplier = std::max(config.morale.stable.speed, 0.01f);
        unit.moraleAccuracyMultiplier = std::max(config.morale.stable.accuracy, 0.01f);
        unit.moraleDefenseMultiplier = std::max(config.morale.stable.defense, 0.01f);
        unit.moraleAttackIntervalMultiplier = std::max(config.morale.stable.attackInterval, 0.01f);
        unit.moraleIgnoreOrdersChance = std::clamp(config.morale.stableBehavior.ignoreOrdersChance, 0.0f, 1.0f);
        unit.moraleDetectionRadiusMultiplier =
            std::max(config.morale.stableBehavior.detectionRadiusMultiplier, 0.0f);
        unit.moraleSpawnDelayMultiplier =
            std::max(config.morale.stableBehavior.spawnDelayMultiplier, 1.0f);
        unit.moraleRetargetCooldownMultiplier =
            std::max(config.morale.stableBehavior.retargetCooldownMultiplier, 0.01f);
        unit.moraleCommandObeyBonus =
            std::clamp(config.morale.stableBehavior.commandObeyBonus, 0.0f, 1.0f);
        unit.moraleRetreatActive = false;
        unit.moraleRetreatTimer = 0.0f;
        unit.moraleRetreatSpeedMultiplier = std::max(config.morale.stableBehavior.retreat.speedMultiplier, 0.0f);
        unit.moraleRetreatHomewardBias = std::clamp(config.morale.stableBehavior.retreat.homewardBias, 0.0f, 1.0f);
        unit.moraleRetreatDuration = config.morale.stableBehavior.retreat.enabled
                                          ? std::max(config.morale.stableBehavior.retreat.duration, 0.0f)
                                          : 0.0f;
        unit.moraleRetreatCheckInterval = 0.0f;
        unit.moraleRetreatCheckChance = 0.0f;
        unit.moraleRetreatCheckTimer = 0.0f;
        unit.moraleIgnoreOrdersTimer = 0.0f;
        unit.moraleIgnoringOrders = false;
        unit.moraleLightMesomesoPending = false;
    }

    void initializeJobState(Unit &unit, UnitJob job)
    {
        unit.job = {};
        unit.job.job = job;
        float baseCooldown = 0.0f;
        switch (job)
        {
        case UnitJob::Warrior:
            baseCooldown = config.warriorJob.cooldown;
            break;
        case UnitJob::Archer:
            baseCooldown = config.archerJob.cooldown;
            break;
        case UnitJob::Shield:
            baseCooldown = config.shieldJob.cooldown;
            break;
        }
        if (baseCooldown > 0.0f)
        {
            std::uniform_real_distribution<float> roll(0.0f, baseCooldown);
            unit.job.cooldown = roll(rng);
        }
        else
        {
            unit.job.cooldown = 0.0f;
        }
        unit.job.endlag = 0.0f;
    }

    void recordSpawnSelection(UnitJob job)
    {
        if (jobHistoryLimit == 0)
        {
            return;
        }
        jobHistory.push_back(job);
        while (jobHistory.size() > jobHistoryLimit)
        {
            jobHistory.pop_front();
        }
    }

    const char *spawnOriginLabel(SpawnOrigin origin) const
    {
        switch (origin)
        {
        case SpawnOrigin::Natural:
            return "natural";
        case SpawnOrigin::Respawn:
            return "respawn";
        case SpawnOrigin::Reinforcement:
            return "reinforcement";
        }
        return "natural";
    }

    void emitSpawnDistributionTelemetry()
    {
        if (spawnTelemetryTotal == 0)
        {
            return;
        }

        TelemetrySink::Payload payload;
        payload.emplace("total_spawns", std::to_string(spawnTelemetryTotal));
        for (UnitJob job : AllUnitJobs)
        {
            payload.emplace(std::string("total_") + unitJobToString(job),
                            std::to_string(spawnTelemetryTotals[unitJobIndex(job)]));
        }

        std::array<std::uint64_t, UnitJobCount> windowCounts{};
        bool haveWindow = false;
        const int windowSize = config.jobSpawn.telemetryWindow;
        if (windowSize > 0 && !spawnTelemetryWindow.empty())
        {
            haveWindow = true;
            for (UnitJob recent : spawnTelemetryWindow)
            {
                windowCounts[unitJobIndex(recent)] += 1;
            }
            payload.emplace("window",
                            std::to_string(std::min<std::size_t>(spawnTelemetryWindow.size(),
                                                                 static_cast<std::size_t>(windowSize))));
            for (UnitJob job : AllUnitJobs)
            {
                payload.emplace(std::string("window_") + unitJobToString(job),
                                std::to_string(windowCounts[unitJobIndex(job)]));
            }
        }

        bool emitted = false;
        if (auto sink = telemetry.lock())
        {
            sink->recordEvent("world.spawn.distribution", payload);
            emitted = true;
        }

        if (!emitted && haveWindow)
        {
            std::ostringstream oss;
            oss << "Spawn mix (" << spawnTelemetryWindow.size() << ") ";
            bool first = true;
            for (UnitJob job : AllUnitJobs)
            {
                if (!first)
                {
                    oss << ", ";
                }
                first = false;
                oss << unitJobToString(job) << ':' << windowCounts[unitJobIndex(job)];
            }
            pushTelemetry(oss.str());
        }
    }

    void recordSpawnTelemetry(UnitJob job, SpawnOrigin origin)
    {
        spawnTelemetryTotals[unitJobIndex(job)] += 1;
        ++spawnTelemetryTotal;

        const int windowSize = config.jobSpawn.telemetryWindow;
        if (windowSize > 0)
        {
            spawnTelemetryWindow.push_back(job);
            while (spawnTelemetryWindow.size() > static_cast<std::size_t>(windowSize))
            {
                spawnTelemetryWindow.pop_front();
            }
        }

        if (auto sink = telemetry.lock())
        {
            TelemetrySink::Payload payload;
            payload.emplace("job", unitJobToString(job));
            payload.emplace("origin", spawnOriginLabel(origin));
            payload.emplace("total_spawns", std::to_string(spawnTelemetryTotal));
            sink->recordEvent("world.spawn.job", payload);
        }

        if (windowSize <= 0 ||
            (windowSize > 0 &&
             spawnTelemetryTotal % static_cast<std::uint64_t>(windowSize) == 0))
        {
            emitSpawnDistributionTelemetry();
        }
    }

    UnitJob chooseSpawnJob()
    {
        std::array<float, UnitJobCount> weights = config.jobSpawn.weights;
        const JobSpawnPity &pity = config.jobSpawn.pity;
        if (pity.repeatLimit > 0 && jobHistory.size() >= static_cast<std::size_t>(pity.repeatLimit))
        {
            const std::size_t limit = static_cast<std::size_t>(pity.repeatLimit);
            const std::size_t start = jobHistory.size() - limit;
            bool allSame = true;
            UnitJob baseline = jobHistory.back();
            for (std::size_t i = start; i < jobHistory.size(); ++i)
            {
                if (jobHistory[i] != baseline)
                {
                    allSame = false;
                    break;
                }
            }
            if (allSame)
            {
                for (UnitJob job : AllUnitJobs)
                {
                    if (job != baseline)
                    {
                        weights[unitJobIndex(job)] *= pity.unseenBoost;
                    }
                }
            }
        }

        float total = 0.0f;
        for (float w : weights)
        {
            if (w > 0.0f)
            {
                total += w;
            }
        }
        if (total <= 0.0f)
        {
            total = static_cast<float>(weights.size());
            for (float &w : weights)
            {
                w = 1.0f;
            }
        }

        std::uniform_real_distribution<float> pickDist(0.0f, total);
        float pick = pickDist(rng);
        UnitJob selected = UnitJob::Warrior;
        for (UnitJob job : AllUnitJobs)
        {
            const float w = weights[unitJobIndex(job)];
            if (w <= 0.0f)
            {
                continue;
            }
            if (pick <= w)
            {
                selected = job;
                break;
            }
            pick -= w;
        }

        recordSpawnSelection(selected);
        return selected;
    }

    void spawnYunaUnit(UnitJob job, SpawnOrigin origin)
    {
        Unit yuna;
        yuna.pos = yunaSpawnPos;
        yuna.pos.y += scatterY(rng);
        yuna.hp = yunaStats.hp;
        yuna.maxHp = yunaStats.hp;
        yuna.radius = yunaStats.radius;
        yuna.level = 1;
        if (meanChibiLevel > 0.0f)
        {
            // 平均と1の距離を上限に加えたレンジを正規分布でサンプリングする
            const float mean = std::max(1.0f, meanChibiLevel);
            const float maxRange = mean + std::max(0.0f, mean - 1.0f); // = 2*mean-1
            const int maxLevel = std::max(1, static_cast<int>(std::round(maxRange)));
            const float stddev = std::max(1.0f, (maxRange - 1.0f) * 0.3f); // 3σ程度で上限付近
            std::normal_distribution<float> levelDist(mean, stddev);
            const int sampled = static_cast<int>(std::round(levelDist(rng)));
            yuna.level = std::clamp(sampled, 1, maxLevel);
        }
        const AllyLevelingParams params = kCommanderLevelingParams;
        const int levelDelta = std::max(yuna.level - 1, 0);
        const float hpBonus = params.hpPerLevel * 0.5f * static_cast<float>(levelDelta);
        yuna.maxHp = yunaStats.hp + hpBonus;
        yuna.hp = yuna.maxHp;
        resetUnitMorale(yuna);
        yuna.moraleLightMesomesoPending = !commander.alive &&
                                         config.morale.spawnWhileLeaderDown.applyLightMesomeso;
        initializeJobState(yuna, job);
        yunas.push_back(yuna);
        assignTemperament(yunas.back());
        clampToWorld(yunas.back().pos, yunas.back().radius);
        yunas.back().forcePanic = !commander.alive;
        if (yunas.back().forcePanic)
        {
            yunas.back().temperament.panicTimer =
                std::max(yunas.back().temperament.panicTimer, panicMinimumDurationFor(yunas.back()));
        }
        recordSpawnTelemetry(job, origin);
    }

    void spawnNamedUnit(const std::string &displayName, UnitJob job, const Vec2 &worldPos)
    {
        spawnYunaUnit(job, SpawnOrigin::Reinforcement);
        if (yunas.empty())
        {
            return;
        }
        Unit &unit = yunas.back();
        unit.pos = worldPos;
        unit.respawnAllowed = false;
        unit.name = displayName;
        unit.isNamed = true;
        // レベル補正（上限なし）
        int level = 1;
        if (auto it = namedLevels.find(displayName); it != namedLevels.end())
        {
            level = std::max(it->second, 1);
        }
        unit.level = level;
        unit.namedSkillCooldown = 0.0f;
        unit.maxHp = commanderStats.hp;
        unit.hp = unit.maxHp;
        unit.radius = std::max(unit.radius, commanderStats.radius * 0.6f); // 少し大きめに
        unit.temperament = TemperamentState{};
        unit.braveryAxis = 0;
        unit.wisdomAxis = 0;
        unit.forcePanic = false;
        unit.temperament.baseRole = ChibiAction::FollowCommander;
        unit.temperament.microAction = ChibiAction::FollowCommander;
        unit.temperament.roleAssigned = true;
        const float delta = static_cast<float>(std::max(level - 1, 0));
        const AllyLevelingParams params = kCommanderLevelingParams;
        unit.maxHp = commanderStats.hp + params.hpPerLevel * delta;
        unit.hp = unit.maxHp;
    }

    int namedLevel(const std::string &name) const
    {
        auto it = namedLevels.find(name);
        if (it != namedLevels.end())
        {
            return std::max(it->second, 1);
        }
        return 1;
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
    }

    void initializeMissionState()
    {
        missionTimer = 0.0f;
        missionVictoryCountdown = -1.0f;
        boss = {};
        hasMission = false;
        captureZones.clear();
        capturedZones = 0;
        captureGoal = 0;
        survival = {};
        disabledGates.clear();

        // Legacy mission flowは無効化（ドラゴン出現はステージロジックで管理）
        missionMode = MissionMode::None;
        missionUI = {};
        missionFail = {};
        captureZones.clear();
        captureGoal = 0;
        survival = {};
    }

    bool spawnMissionBossAt(const Vec2 &world)
    {
        if (missionConfig.boss.hp <= 0.0f)
        {
            return false;
        }
        EnemyUnit bossUnit;
        bossUnit.type = EnemyArchetype::Boss;
        bossUnit.pos = world;
        bossUnit.hp = missionConfig.boss.hp;
        bossUnit.maxHp = bossUnit.hp;
        bossUnit.radius = missionConfig.boss.radius_px > 0.0f ? missionConfig.boss.radius_px : 32.0f;
        bossUnit.speedPx = missionConfig.boss.speed_u_s * config.pixels_per_unit;
        bossUnit.dpsUnit = slimeStats.dps;
        bossUnit.dpsBase = slimeStats.dps;
        bossUnit.dpsWall = slimeStats.dps;
        bossUnit.noOverlap = missionConfig.boss.noOverlap;
        enemies.push_back(bossUnit);
        timeSinceLastEnemySpawn = 0.0f;
        return true;
    }

    bool spawnMissionBoss()
    {
        return spawnMissionBossAt(tileToWorld(missionConfig.boss.tile, mapDefs.tile_size));
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
        // Legacy boss mechanics are disabled; dragon behavior is handled elsewhere.
        (void)dt;
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
        // Mission flow is disabled; boss mechanics are driven by stage spawns.
        (void)dt;
    }

    void scheduleCommanderRespawn(float penaltyMultiplier, float bonusSeconds, float overkillRatio)
    {
        if (commander.alive)
        {
            const float facingX = commander.lastMoveDir.x < 0.0f ? -1.0f : 1.0f;
            deathFx.push_back({commander.pos, 1.0f, 1.0f, facingX, true});
        }
        commander.alive = false;
        commander.hp = 0.0f;
        const float penalty = std::max(1.0f, penaltyMultiplier);
        const float bonus = std::max(0.0f, bonusSeconds);
        float respawnTime = computeCommanderRespawnTime(overkillRatio);
        respawnTime = std::max(config.commander_respawn.floor, respawnTime * penalty - bonus);
        commanderRespawnTimer = respawnTime;
        commanderInvulnTimer = 0.0f;
        const int autoReinforceCount = std::max(0, config.commander_auto_reinforce);
        if (autoReinforceCount > 0)
        {
            for (int i = 0; i < autoReinforceCount; ++i)
            {
                reinforcementJobs.push_back(chooseSpawnJob());
            }
        }
        rallyState = false;
        for (Unit &yuna : yunas)
        {
            yuna.followBySkill = false;
            yuna.followByStance = false;
        }
        hud.resultText = "Commander Down";
        hud.resultTimer = config.telemetry_duration;
        setChibiForcePanic(true);
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
        addDynamicHazard(commander.pos, def.radius, 1.0f, 1.5f);
        pushTelemetry("Self Destruct!");
    }

    void applyRallyState(bool enabled, const SkillDef &def, const Vec2 &worldTarget)
    {
        rallyState = enabled;
        if (enabled)
        {
            const float radiusSq = def.radius * def.radius;
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
        {
            const float surgeMul = skill.def.multiplier > 0.0f ? skill.def.multiplier : 1.0f;
            spawnRateMultiplier = std::max(spawnRateBaseMultiplier * surgeMul, 0.1f);
            skill.activeTimer = skill.def.duration;
            skill.cooldownRemaining = skill.def.cooldown;
            pushTelemetry("Spawn surge");
            break;
        }
        case SkillType::Detonate:
            detonateCommander(skill.def);
            skill.cooldownRemaining = skill.def.cooldown;
            break;
        case SkillType::Hazard:
        {
            const float hazardDuration = skill.def.hazardDuration > 0.0f
                                             ? skill.def.hazardDuration
                                             : (skill.def.duration > 0.0f ? skill.def.duration : 2.0f);
            addDynamicHazard(worldTarget, skill.def.radius, skill.def.hazardWeight, hazardDuration);
            skill.cooldownRemaining = skill.def.cooldown;
            break;
        }
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

    bool isOrderActive() const { return orderActive; }

    float orderTimeRemaining() const { return std::max(orderTimer, 0.0f); }

    ArmyStance currentOrder() const { return stance; }

    void pushTelemetry(const std::string &text)
    {
        hud.telemetryText = normalizeTelemetry(text);
        hud.telemetryTimer = config.telemetry_duration;
    }

    void appendAiTelemetryCsv(
        const std::array<float, static_cast<std::size_t>(ChibiAction::Wander) + 1> &ratios,
        float panicRatio,
        float unitSeconds,
        float avgRadius,
        float exceedRatio,
        int tokkouActive,
        int tokkouPeak,
        float clingRatio,
        float kaitenRatio,
        float runRatio,
        float hazardAvg,
        float aoeRatio,
        float aoeMagnitude,
        const std::string &tokkouUnits,
        const std::string &clingUnits,
        const std::string &kaitenUnits,
        const std::string &runUnits,
        const std::string &panicUnits);

    void pushStageBanner(const std::string &text, float duration = 0.0f)
    {
        const float display = duration > 0.0f ? duration : config.telemetry_duration;
        hud.stageBanner.text = normalizeTelemetry(text);
        hud.stageBanner.timer = display;
    }

    void pushTip(const std::string &text, float duration = 0.0f)
    {
        const float display = duration > 0.0f ? duration : config.telemetry_duration;
        hud.tip.text = normalizeTelemetry(text);
        hud.tip.timer = display;
    }

    void setEconomyHud(int currency, int cap, int tokens, std::string recommended)
    {
        hud.economy.currency = currency;
        hud.economy.cap = cap;
        hud.economy.tokens = tokens;
        hud.economy.recommended = std::move(recommended);
    }

    void handleSpawnDeferral(int deferredCount);

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

        hud.resultStats.available = true;
        hud.resultStats.result = r;
        hud.resultStats.durationSeconds = simTime;
        hud.resultStats.chibiDeaths = statChibiDeaths;
        hud.resultStats.chibiSurvivors = static_cast<int>(yunas.size());
        hud.resultStats.enemyKills = statEnemyKills;
        hud.resultStats.manaEarned = economy.mana;
        hud.resultStats.manaCap = economy.cap;
        const float tokenMul = 1.0f + std::max(0, economy.tokens) * std::max(economy.tokenBonus, 0.0f);
        const float totalMul = std::max(0.0f, economy.gainMultiplier) * std::max(tokenMul, 0.0f);
        hud.resultStats.manaBonusPercent = static_cast<int>(std::round(std::max(totalMul - 1.0f, 0.0f) * 100.0f));
        hud.resultStats.basesSealed = static_cast<int>(sealedEnemyBases());
        hud.resultStats.basesTotal = static_cast<int>(stage.enemyBases.size());
    }

    void update(float dt)
    {
        auto sink = telemetry.lock();
        if (sink && sink->consumeFrameCaptureRequest())
        {
            frameCapturePending = 5;
            ++frameCaptureBatch;
            frameCaptureIndex = 0;
        }

        ++frameCounter;
        simTime += dt;
        if (timeSinceLastEnemySpawn < 10000.0f)
        {
            timeSinceLastEnemySpawn += dt;
        }
        if (restartCooldown > 0.0f)
        {
            restartCooldown = std::max(0.0f, restartCooldown - dt);
        }

        updateSkillTimers(dt);
        updateYunaSpawn(dt);
        updateCommanderRespawn(dt);
        updateWalls(dt);
        updateMission(dt);
        regenAllyBases(dt);
        updateProjectiles(dt);
        updateCommanderResources(dt);
        if (stage.enabled)
        {
            for (StageEnemyBaseState &base : stage.enemyBases)
            {
                if (base.hpFlashTimer > 0.0f)
                {
                    base.hpFlashTimer = std::max(0.0f, base.hpFlashTimer - dt);
                }
            }
        }
        if (commanderHudPrevHp <= 0.0f && commander.hp > 0.0f)
        {
            commanderHudPrevHp = commander.hp;
            commanderHudHpLag = commander.hp;
        }
        if (commander.alive)
        {
            if (commander.hp > commanderHudPrevHp + 0.01f)
            {
                commanderHudHealFlashTimer = kCommanderHudHealFlashDuration;
            }
            if (commander.hp < commanderHudPrevHp - 0.01f)
            {
                commanderHudHpLag = std::max(commanderHudHpLag, commanderHudPrevHp);
            }
            if (commanderHudHpLag < commander.hp)
            {
                commanderHudHpLag = commander.hp;
            }
            else
            {
                commanderHudHpLag = std::max(commander.hp, commanderHudHpLag - kCommanderHudHpLagSpeed * dt);
            }
            if (commanderHudHealFlashTimer > 0.0f)
            {
                commanderHudHealFlashTimer = std::max(0.0f, commanderHudHealFlashTimer - dt);
            }
        }
        else
        {
            commanderHudHpLag = 0.0f;
            commanderHudHealFlashTimer = 0.0f;
        }
        commanderHudPrevHp = commander.hp;

        if (frameCapturePending > 0)
        {
            if (!sink)
            {
                sink = telemetry.lock();
            }
            if (sink)
            {
                captureFrameSnapshot(*sink);
                if (frameCapturePending > 0)
                {
                    --frameCapturePending;
                }
            }
            else
            {
                frameCapturePending = 0;
            }
        }
    }

    void spawnOneEnemy(Vec2 gatePos, EnemyArchetype type)
    {
        EnemyUnit enemy;
        enemy.pos = gatePos;
        enemy.pos.y += gateJitter(rng);
        {
            // 進路を少しだけずらして行列のピッタリ重なりを防ぐ
            std::uniform_real_distribution<float> lane(-12.0f, 12.0f);
            enemy.laneOffset = {lane(rng), lane(rng)};
        }
        enemy.type = type;
        enemy.attackRangePx = 0.0f;
        if (type == EnemyArchetype::Wallbreaker)
        {
            enemy.hp = wallbreakerStats.hp;
            enemy.maxHp = enemy.hp;
            enemy.radius = wallbreakerStats.radius;
            enemy.speedPx = wallbreakerStats.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = wallbreakerStats.dps_unit;
            enemy.dpsBase = wallbreakerStats.dps_base;
            enemy.dpsWall = wallbreakerStats.dps_wall;
            enemy.noOverlap = wallbreakerStats.ignoreKnockback;
            enemy.attackRangePx = enemy.radius;
        }
        else if (type == EnemyArchetype::Boss)
        {
            enemy.hp = missionConfig.boss.hp > 0.0f ? missionConfig.boss.hp : 500.0f;
            enemy.maxHp = enemy.hp;
            enemy.radius = missionConfig.boss.radius_px > 0.0f ? missionConfig.boss.radius_px : 32.0f;
            enemy.speedPx = missionConfig.boss.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = slimeStats.dps;
            enemy.dpsBase = slimeStats.dps;
            enemy.dpsWall = slimeStats.dps;
            enemy.noOverlap = missionConfig.boss.noOverlap;
            enemy.attackRangePx = enemy.radius;
            enemy.breathCooldown = 10.0f;
            enemy.breathTimer = 1.5f; // 少し間を置いてから初回ブレス
        }
        else if (type == EnemyArchetype::Golem)
        {
            auto applyEntityStats = [&](const EntityStats &stats) {
                enemy.hp = stats.hp;
                enemy.maxHp = enemy.hp;
                enemy.radius = stats.radius;
                enemy.speedPx = stats.speed_u_s * config.pixels_per_unit;
                enemy.dpsUnit = stats.dps;
                enemy.dpsBase = stats.dps;
                enemy.dpsWall = stats.dps;
                enemy.attackRangePx = stats.attackRangePx > 0.0f ? stats.attackRangePx : stats.radius;
            };
            applyEntityStats(golemStats);
        }
        else
        {
            auto applyEntityStats = [&](const EntityStats &stats) {
                enemy.hp = stats.hp;
                enemy.maxHp = enemy.hp;
                enemy.radius = stats.radius;
                enemy.speedPx = stats.speed_u_s * config.pixels_per_unit;
                enemy.dpsUnit = stats.dps;
                enemy.dpsBase = stats.dps;
                enemy.dpsWall = stats.dps;
                enemy.attackRangePx = stats.attackRangePx > 0.0f ? stats.attackRangePx : stats.radius;
            };
            switch (type)
            {
            case EnemyArchetype::Goblin: applyEntityStats(goblinStats); break;
            case EnemyArchetype::Magician: applyEntityStats(magicianStats); break;
            case EnemyArchetype::Bat: applyEntityStats(batStats); break;
            case EnemyArchetype::Toritori: applyEntityStats(toritoriStats); break;
            case EnemyArchetype::Golem: applyEntityStats(golemStats); break;
            case EnemyArchetype::Slime:
            default: applyEntityStats(slimeStats); break;
            }
        }
        enemy.focusUnitIndex = -1;
        enemy.focusTimer = 0.0f;
        enemies.push_back(enemy);
        timeSinceLastEnemySpawn = 0.0f;
    }

    void updateYunaSpawn(float dt)
    {
        if (!spawnEnabled || disableYunaSpawns)
        {
            return;
        }
        // すべての味方拠点が破壊されたら湧きを止める
        if (stage.enabled)
        {
            bool allyBaseAlive = false;
            for (const StageAllyBaseState &base : stage.allyBases)
            {
                if (!base.destroyed)
                {
                    allyBaseAlive = true;
                    break;
                }
            }
            if (!allyBaseAlive)
            {
                return;
            }
        }
        const float rateMultiplier = std::max(spawnRateMultiplier, 0.1f);
        const float slowMultiplier = std::max(std::max(spawnSlowMultiplier, moraleSpawnMultiplier), 0.1f);
        const bool stageEnabled = stage.enabled;
        int allyCap = config.yuna_max;
        if (stageEnabled && stage.allyFactory.cap > 0)
        {
            allyCap = stage.allyFactory.cap;
        }
        if (stageEnabled && stage.globalCaps.chibi > 0)
        {
            allyCap = std::min(allyCap, stage.globalCaps.chibi);
        }
        yunaSpawnTimer -= dt;
        const float minInterval = 0.1f;
        float baseInterval = config.yuna_interval;
        if (stageEnabled && stage.allyFactory.ratePerSecond > 0.0f)
        {
            baseInterval = 1.0f / stage.allyFactory.ratePerSecond;
        }
        const float spawnInterval = std::max(minInterval, (baseInterval / rateMultiplier) * slowMultiplier);
        const float respawnRetryInterval = spawnInterval;
        while (yunaSpawnTimer <= 0.0f)
        {
            if (static_cast<int>(yunas.size()) < allyCap)
            {
                spawnYunaUnit(chooseSpawnJob(), SpawnOrigin::Natural);
                yunaSpawnTimer += spawnInterval;
            }
            else
            {
                // If capped, reset to a full interval so we don't backlog and burst-spawn later.
                yunaSpawnTimer = spawnInterval;
                break;
            }
        }

        const float respawnRate = rateMultiplier / slowMultiplier;
        for (PendingRespawn &pending : yunaRespawns)
        {
            pending.timer -= dt * respawnRate;
        }
        std::vector<PendingRespawn> remainingRespawns;
        remainingRespawns.reserve(yunaRespawns.size());
        for (PendingRespawn &pending : yunaRespawns)
        {
            if (pending.timer <= 0.0f && static_cast<int>(yunas.size()) < allyCap)
            {
                spawnYunaUnit(pending.job, SpawnOrigin::Respawn);
            }
            else
            {
                if (pending.timer < 0.0f)
                {
                    pending.timer = 0.0f;
                }
                if (pending.timer <= 0.0f && static_cast<int>(yunas.size()) >= allyCap)
                {
                    // Capに阻まれたリスポーンは再試行まで少し待つ
                    pending.timer = respawnRetryInterval;
                }
                remainingRespawns.push_back(pending);
            }
        }
        yunaRespawns.swap(remainingRespawns);

        while (!reinforcementJobs.empty() && static_cast<int>(yunas.size()) < allyCap)
        {
            UnitJob job = reinforcementJobs.front();
            reinforcementJobs.pop_front();
            spawnYunaUnit(job, SpawnOrigin::Reinforcement);
        }
    }

    template <typename Container>
    void collectRaidTargets(Container &targets) const
    {
        static_assert(std::is_same_v<typename Container::value_type, Vec2>,
                      "collectRaidTargets expects Vec2 container");
        targets.clear();
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
    }

    std::vector<Vec2> collectRaidTargets() const
    {
        std::vector<Vec2> targets;
        collectRaidTargets(targets);
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
};

} // namespace world

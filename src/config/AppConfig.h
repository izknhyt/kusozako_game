#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Vec2.h"
#include "world/MoraleTypes.h"
#include "world/StageConfig.h"

struct RespawnSettings
{
    float base = 5.0f;
    float scale = 5.0f;
    float k = 1.0f;
    float floor = 0.0f;
    float invuln = 0.0f;
};

struct MoraleRetreatConfig
{
    bool enabled = false;
    float duration = 0.0f;
    float speedMultiplier = 1.0f;
    float homewardBias = 1.0f;
};

struct MoraleRetreatCheckConfig
{
    float interval = 0.0f;
    float chance = 0.0f;
};

struct MoraleBehaviorConfig
{
    float ignoreOrdersChance = 0.0f;
    float detectionRadiusMultiplier = 1.0f;
    float spawnDelayMultiplier = 1.0f;
    float retargetCooldownMultiplier = 1.0f;
    float commandObeyBonus = 0.0f;
    MoraleRetreatConfig retreat{};
};

struct MoraleStateConfig
{
    float duration = 0.0f;
    MoraleModifiers modifiers{};
    MoraleBehaviorConfig behavior{};
    MoraleRetreatCheckConfig retreatCheck{};
};

struct MoraleSpawnWhileLeaderDownConfig
{
    bool applyLightMesomeso = false;
    float duration = 0.0f;
};

struct MoraleConfig
{
    float leaderDownWindow = 3.0f;
    float comfortZoneRadius = 96.0f;
    float reviveBarrier = 4.0f;
    float reviveBarrierLinger = 0.0f;
    float detectionRadius = 256.0f;
    MoraleModifiers stable{};
    MoraleBehaviorConfig stableBehavior{};
    MoraleModifiers leaderDown{};
    MoraleBehaviorConfig leaderDownBehavior{};
    MoraleStateConfig panic{};
    MoraleStateConfig mesomeso{};
    MoraleStateConfig recovering{};
    MoraleStateConfig shielded{};
    MoraleStateConfig spawnLightInjury{};
    MoraleSpawnWhileLeaderDownConfig spawnWhileLeaderDown{};
};

enum class UnitJob : std::uint8_t
{
    Warrior = 0,
    Archer = 1,
    Shield = 2
};

constexpr std::size_t UnitJobCount = 3;

inline constexpr std::array<UnitJob, UnitJobCount> AllUnitJobs{
    UnitJob::Warrior,
    UnitJob::Archer,
    UnitJob::Shield};

inline std::size_t unitJobIndex(UnitJob job)
{
    return static_cast<std::size_t>(job);
}

inline const char *unitJobToString(UnitJob job)
{
    switch (job)
    {
    case UnitJob::Warrior:
        return "warrior";
    case UnitJob::Archer:
        return "archer";
    case UnitJob::Shield:
        return "shield";
    }
    return "warrior";
}

std::optional<UnitJob> unitJobFromString(const std::string &id);

struct JobCommonConfig
{
    float fizzleChance = 0.0f;
    float endlagSeconds = 0.0f;
    float projectileSpeedMin = 0.0f;
    float projectileSpeedMax = 0.0f;
};

struct WarriorJobConfig
{
    std::string skillId;
    float cooldown = 0.0f;
    float accuracyMultiplier = 1.0f;
    float stumbleSeconds = 0.0f;
};

struct ArcherJobConfig
{
    std::string skillId;
    float cooldown = 0.0f;
    float critBonus = 0.0f;
    float holdSeconds = 0.0f;
};

struct ShieldJobConfig
{
    std::string skillId;
    float cooldown = 0.0f;
    float radiusUnits = 0.0f;
    float durationSeconds = 0.0f;
    float selfSlowMultiplier = 1.0f;
};

struct JobSpawnPity
{
    int repeatLimit = 0;
    float unseenBoost = 1.0f;
};

struct JobSpawnConfig
{
    std::array<float, UnitJobCount> weights{1.0f, 1.0f, 1.0f};
    JobSpawnPity pity{};
    int historyLimit = 32;
    int telemetryWindow = 10;
    std::string weightsAssetPath;
    bool hasInlineWeights = false;

    float weight(UnitJob job) const { return weights[unitJobIndex(job)]; }
    void setWeight(UnitJob job, float value) { weights[unitJobIndex(job)] = value; }
};

struct FormationAlignmentConfig
{
    float alignDuration = 0.0f;
    float defenseMultiplier = 1.0f;
};

struct PerformanceBudgetConfig
{
    float updateMs = 6.0f;
    float renderMs = 8.0f;
    float inputMs = 1.5f;
    float hudMs = 2.0f;
    float toleranceMs = 0.5f;
};

struct SpawnBudgetConfig
{
    int maxPerFrame = 8;
    std::string warningText = "Spawn queue delayed";
};

struct GameConfig
{
    float fixed_dt = 1.0f / 60.0f;
    float pixels_per_unit = 16.0f;
    int base_hp = 300;
    Vec2 base_aabb{32.0f, 32.0f};
    float gate_radius = 28.0f;
    float gate_hp = 200.0f;
    float yuna_interval = 0.75f;
    int yuna_max = 200;
    Vec2 yuna_offset_px{48.0f, 0.0f};
    float yuna_scatter_y = 16.0f;
    float victory_grace = 5.0f;
    float telemetry_duration = 3.0f;
    float restart_delay = 2.0f;
    std::string enemy_script = "assets/spawn_level1.json";
    std::string map_path = "assets/maps/level1.tmx";
    int rng_seed = 1337;
    int lod_threshold_entities = 0;
    int lod_skip_draw_every = 1;
    std::string mission_path;
    std::string formations_path;
    std::string morale_path;
    std::string jobs_path;
    std::string spawn_weights_path;
    RespawnSettings yuna_respawn{5.0f, 5.0f, 1.0f, 0.0f, 2.0f};
    RespawnSettings commander_respawn{8.0f, 5.0f, 2.0f, 12.0f, 2.0f};
    int commander_auto_reinforce = 0;
    FormationAlignmentConfig formationDefaults{};
    MoraleConfig morale{};
    JobCommonConfig jobCommon{};
    WarriorJobConfig warriorJob{};
    ArcherJobConfig archerJob{};
    ShieldJobConfig shieldJob{};
    JobSpawnConfig jobSpawn{};
    PerformanceBudgetConfig performance{};
    SpawnBudgetConfig spawnBudget{};
};

struct EntityStats
{
    float radius = 4.0f;
    float speed_u_s = 1.8f;
    float hp = 10.0f;
    float dps = 3.0f;
    float attackRangePx = 0.0f;
    std::string spritePrefix;
};

struct CommanderStats
{
    Vec2 aabb{24.0f, 24.0f};
    float radius = 12.0f;
    float speed_u_s = 1.6f;
    float hp = 60.0f;
    float dps = 15.0f;
    float mp = 30.0f;
    float mpRegen = 0.25f;
    std::string spritePrefix;
};

struct WallbreakerStats
{
    float radius = 12.0f;
    float speed_u_s = 1.0f;
    float hp = 60.0f;
    float dps_wall = 15.0f;
    float dps_unit = 5.0f;
    float dps_base = 5.0f;
    bool ignoreKnockback = true;
    float preferWallRadiusPx = 256.0f;
    std::string spritePrefix;
};

struct EntityCatalog
{
    CommanderStats commander;
    EntityStats yuna;
    EntityStats slime;
    EntityStats goblin;
    EntityStats magician;
    EntityStats bat;
    EntityStats toritori;
    EntityStats golem;
    WallbreakerStats wallbreaker;
};

enum class EnemyArchetype
{
    Slime,
    Goblin,
    Magician,
    Bat,
    Toritori,
    Golem,
    Wallbreaker,
    Boss
};

enum class ArmyStance
{
    RushNearest,
    PushForward,
    FollowLeader,
    DefendBase
};

enum class Formation
{
    Swarm,
    Wedge,
    Line,
    Ring
};

enum class SkillType
{
    ToggleFollow,
    MakeWall,
    SpawnRate,
    Detonate,
    Hazard
};

struct SkillDef
{
    std::string id;
    std::string displayName;
    SkillType type = SkillType::ToggleFollow;
    int hotkey = 1;
    float cooldown = 0.0f;
    float mana = 0.0f;
    float radius = 0.0f;
    float duration = 0.0f;
    int lenTiles = 0;
    float hpPerSegment = 0.0f;
    float multiplier = 1.0f;
    float damage = 0.0f;
    float respawnPenalty = 1.0f;
    float spawnSlowMult = 1.0f;
    float spawnSlowDuration = 0.0f;
    float respawnBonusPerHit = 0.0f;
    float respawnBonusCap = 0.0f;
    float hazardWeight = 1.0f;
    float hazardDuration = 0.0f;
};

struct TemperamentRange
{
    float min = 0.0f;
    float max = 0.0f;
};

struct TemperamentFollowCatchup
{
    float distance = 0.0f;
    float duration = 0.0f;
    float multiplier = 1.0f;
};

struct EconomyConfig
{
    int baseCap = 100;
    float tokenBonus = 0.10f;
    float gainBonus = 0.0f; // flat multiplier bonus (e.g., 0.05 = +5%)
    std::unordered_map<std::string, int> enemyRewards;
};

struct CampUpgradeEntry
{
    std::string id;
    std::string label;
    std::string type;
    int maxLevel = 1;
    std::vector<int> costs;
    float delta = 0.0f;
};

struct TrainingStep
{
    float delta = 0.0f;
    int cost = 0;
};

struct TrainingRepeatable
{
    bool enabled = false;
    float delta = 0.0f;
    int maxLevel = 0;
    float baseCost = 0.0f;
    float costGrowth = 1.0f;
};

struct TrainingEntry
{
    std::string id;
    std::string label;
    std::vector<TrainingStep> steps;
    TrainingRepeatable repeatable{};
};

struct StrategyOption
{
    std::string id;
    std::string label;
};

struct StrategyCharacter
{
    std::string id;
    std::string label;
    std::vector<StrategyOption> options;
    std::string defaultOption;
};

struct MetaShopItem
{
    std::string id;
    std::string label;
    std::string type;
    int maxLevel = 1;
    int baseCost = 0;
    int perLevelCost = 0;
    float delta = 0.0f;
    float gainBonus = 0.0f;
};

struct TemperamentChargeDash
{
    float duration = 0.0f;
    float multiplier = 1.0f;
};

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

struct TemperamentDefinition
{
    std::string id;
    std::string label;
    TemperamentBehavior behavior = TemperamentBehavior::Wander;
    float spawnRate = 0.0f;
    float homeRadius = 0.0f;
    float avoidEnemyRadius = 0.0f;
    TemperamentRange cryPauseEvery{0.0f, 0.0f};
    float cryPauseDuration = 0.0f;
    float panicOnHit = 0.0f;
    std::vector<std::string> targetTags;
    TemperamentRange mimicEvery{0.0f, 0.0f};
    TemperamentRange mimicDuration{0.0f, 0.0f};
    std::vector<TemperamentBehavior> mimicPool;
    TemperamentBehavior mimicDefault = TemperamentBehavior::Wander;
};

struct TemperamentConfig
{
    float orderDuration = 10.0f;
    float fearRadius = 160.0f;
    TemperamentFollowCatchup followCatchup{160.0f, 0.5f, 1.2f};
    TemperamentRange wanderTurnInterval{1.5f, 2.5f};
    TemperamentRange sleepEvery{8.0f, 12.0f};
    float sleepDuration = 0.6f;
    TemperamentChargeDash chargeDash{0.2f, 1.2f};
    std::vector<TemperamentDefinition> definitions;
    std::vector<float> cumulativeWeights;
};

struct ChibiTokkouConfig
{
    int braveryMin = 0;
    int braveryMax = 2;
    int wisdomMin = -2;
    int wisdomMax = 2;
    float duration = 0.0f;
    float attackMultiplier = 1.0f;
    float speedMultiplier = 1.0f;
    float cooldownMultiplier = 1.0f;
    float takenMultiplier = 1.0f;
    int maxSimultaneous = 0;
};

struct ChibiClingConfig
{
    int braveryMax = 0;
    int wisdomMin = 0;
    float rearDegrees = 150.0f;
    float radiusMin = 36.0f;
    float radiusMax = 56.0f;
    float exitHpBonus = 0.0f;
    float exitAuraSeconds = 0.0f;
};

struct ChibiKaitenConfig
{
    int wisdomMin = 1;
    float hpSafe = 0.85f;
    float minStaySeconds = 4.0f;
    float guardBiasSeconds = 2.0f;
    float exitHpBonus = 0.25f;
    float damageMultiplier = 0.8f;
};

struct ChibiRunAroundConfig
{
    int wisdomMax = -1;
    float dashSeconds = 0.8f;
    float jitterSeconds = 0.2f;
    float repathSeconds = 0.5f;
    float exitHpBonus = 0.2f;
    float exitAuraSeconds = 2.0f;
    float damageMultiplier = 0.9f;
};

struct ChibiPersonalityConfig
{
    std::vector<float> panicThresholds;
    std::vector<float> panicMinimumSeconds;
    ChibiTokkouConfig tokkou;
    ChibiClingConfig cling;
    ChibiKaitenConfig kaiten;
    ChibiRunAroundConfig runAround;
    float auraAttackMultiplier = 1.05f;
    float auraDamageMultiplier = 0.90f;
    float auraRegenPerSecond = 1.0f;
};

struct ChibiAiActionConfig
{
    float baseScore = 0.0f;
    float bonusScore = 0.0f;
    float rangePixels = 0.0f;
    float braveryWeight = 0.0f;
    float wisdomWeight = 0.0f;
    bool preferFarRange = false;
};

struct ChibiAiParams
{
    float tickSeconds = 0.5f;
    float hysteresisMultiplier = 1.2f;
    float hysteresisDuration = 0.4f;
    float cohesionRadius = 96.0f;
    float cohesionStrength = 0.2f;
    float cohesionStrengthLow = 0.2f;
    float cohesionStrengthHigh = 0.2f;
    float separationRadius = 18.0f;
    float separationStrength = 0.4f;
    float separationStrengthLow = 0.4f;
    float separationStrengthHigh = 0.4f;
    float aoeAvoidRadius = 0.0f;
    float aoeAvoidStrength = 0.0f;
    float aoeAvoidFalloff = 1.0f;
    float aoeAvoidMultiplierLow = 0.85f;
    float aoeAvoidMultiplierHigh = 1.30f;
    float orderRushBonus = 0.5f;
    float orderPushBonus = 0.4f;
    float orderFollowBonus = 1.0f;
    float orderDefendBonus = 0.6f;
    float orderPenalty = 0.2f;
    float followerBonus = 0.8f;
    // Multiplier for cohesion/separation when unit is on aggressive roles (AssaultEnemy/AssaultBase)
    float aggressiveCohesionMultiplier = 0.0f;
    // Multiplier for cohesion/separation while following commander/base (jitter抑制用)
    float followCohesionMultiplier = 0.0f;
    std::unordered_map<std::string, ChibiAiActionConfig> actions;
};

struct MapDefs
{
    int tile_size = 16;
    Vec2 base_tile{70.0f, 22.0f};
    Vec2 spawn_tile_yuna{69.0f, 22.0f};
    std::unordered_map<std::string, Vec2> gate_tiles;
};

struct SpawnSet
{
    std::string gate;
    int count = 0;
    float interval = 0.3f;
    std::string typeId = "slime";
    EnemyArchetype type = EnemyArchetype::Slime;
};

struct Wave
{
    float time = 0.0f;
    std::vector<SpawnSet> sets;
    std::string telemetry;
    std::vector<std::string> activateHazards;
    std::vector<std::string> deactivateHazards;
};

struct SpawnScript
{
    float y_jitter = 0.0f;
    std::unordered_map<std::string, Vec2> gate_tiles;
    std::vector<Wave> waves;
};

enum class MissionMode
{
    None,
    Boss,
    Capture,
    Survival
};

struct MissionUIOptions
{
    bool showGoalText = false;
    bool showTimer = false;
    bool showBossHpBar = false;
    std::string goalText;
};

struct MissionFailConditions
{
    bool baseHpZero = true;
};

struct MissionBossMechanic
{
    float period = 0.0f;
    float windup = 0.0f;
    float radius = 0.0f;
    float damage = 0.0f;
};

struct MissionBossConfig
{
    std::string id;
    Vec2 tile{};
    float hp = 0.0f;
    float speed_u_s = 0.0f;
    float radius_px = 0.0f;
    bool noOverlap = false;
    MissionBossMechanic slam;
};

struct MissionCaptureAction
{
    std::string disableGate;
    std::string telemetry;
};

struct MissionCaptureZone
{
    std::string id;
    Vec2 tile{};
    float radius_px = 0.0f;
    float capture_s = 0.0f;
    float decay_s = 0.0f;
    MissionCaptureAction onCapture;
};

struct MissionSurvivalElite
{
    float time = 0.0f;
    std::string gate;
    std::string typeId = "slime";
    EnemyArchetype type = EnemyArchetype::Slime;
};

struct MissionSurvivalConfig
{
    float duration = 0.0f;
    float pacingStep = 0.0f;
    float pacingMultiplier = 1.0f;
    std::vector<MissionSurvivalElite> elites;
};

struct MissionConfig
{
    MissionMode mode = MissionMode::None;
    MissionUIOptions ui;
    MissionFailConditions fail;
    MissionBossConfig boss;
    std::vector<MissionCaptureZone> captureZones;
    MissionSurvivalConfig survival;
    struct
    {
        bool bossDown = false;
        int requireCaptured = 0;
        float surviveTime = 0.0f;
    } win;
};

struct RendererConfig
{
    std::string backend = "auto";
    bool srgb = true;
    bool allowHiDpi = true;
    bool pixelSnap = true;
    bool integerZoomOnly = true;
    float pixelsPerUnit = 16.0f;
    int lodThresholdEntities = 0;
    int lodSkipDrawEvery = 1;
};

struct TelemetryOptions
{
    std::string outputDirectory{"build/debug_dumps"};
    std::uintmax_t rotationBytes = 10ull * 1024ull * 1024ull;
    std::size_t maxFiles = 8;
    std::uintmax_t textureMemoryWarningBytes = 150ull * 1024ull * 1024ull;
};

struct InputBindings
{
    std::string focusBase{"Tab"};
    std::string focusCommander{"Space"};
    std::string overview;
    std::unordered_map<std::string, std::string> cameraMove;
    std::vector<std::string> summonMode{"1", "2", "3", "4"};
    std::string restart;
    std::string guard;
    std::string fireBall;
    std::vector<std::string> attack{"MouseLeft", "J"};
    std::string targetCancel{"MouseRight"};
    std::string targetFocus{"T"};
    std::vector<std::string> commanderMoveUp{"W", "Up"};
    std::vector<std::string> commanderMoveDown{"S", "Down"};
    std::vector<std::string> commanderMoveLeft{"A", "Left"};
    std::vector<std::string> commanderMoveRight{"D", "Right"};
    std::vector<std::string> orderRushNearest{"F1"};
    std::vector<std::string> orderPushForward{"F2"};
    std::vector<std::string> orderFollowLeader{"F3"};
    std::vector<std::string> orderDefendBase{"F4"};
    std::string toggleDebugHud{"F10"};
    std::string toggleDebugOverlay{"Ctrl+F5"};
    std::string reloadConfig{"F9"};
    std::string dumpSpawnHistory{"Shift+F10"};
    std::string quit{"Escape"};
    std::string toggleSpeed{"F8"};
    std::string formationPrevious{"Z"};
    std::string formationNext{"X"};
    std::string skillActivate{"F"};
    int bufferFrames = 4;
    float bufferExpiryMs = 80.0f;
};

struct AppConfig
{
    TelemetryOptions telemetry{};
    RendererConfig renderer;
    InputBindings input;
    GameConfig game;
    EntityCatalog entityCatalog;
    MapDefs mapDefs;
    TemperamentConfig temperament;
    ChibiPersonalityConfig chibiPersonality;
    ChibiAiParams chibiAiParams;
    SpawnScript spawnScript;
    std::optional<MissionConfig> mission;
    std::optional<StageConfig> stageConfig;
    EconomyConfig economy;
    std::vector<CampUpgradeEntry> campUpgrades;
    std::vector<TrainingEntry> trainingEntries;
    std::vector<StrategyCharacter> strategyCharacters;
    std::vector<MetaShopItem> metaShopItems;
    std::vector<SkillDef> skills;
    std::string atlasPath = "assets/atlas.json";
    std::string stageConfigPath = "assets/stage1_config.json";
};

std::vector<SkillDef> buildDefaultSkills();

EnemyArchetype enemyTypeFromString(const std::string &typeId);

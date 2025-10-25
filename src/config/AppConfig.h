#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Vec2.h"

struct RespawnSettings
{
    float base = 5.0f;
    float scale = 5.0f;
    float k = 1.0f;
    float floor = 0.0f;
    float invuln = 0.0f;
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
    RespawnSettings yuna_respawn{5.0f, 5.0f, 1.0f, 0.0f, 2.0f};
    RespawnSettings commander_respawn{8.0f, 5.0f, 2.0f, 12.0f, 2.0f};
    int commander_auto_reinforce = 0;
};

struct FormationAlignmentConfig
{
    float alignDuration = 0.0f;
    float defenseMultiplier = 1.0f;
};

struct EntityStats
{
    float radius = 4.0f;
    float speed_u_s = 1.8f;
    float hp = 10.0f;
    float dps = 3.0f;
    std::string spritePrefix;
};

struct CommanderStats
{
    Vec2 aabb{24.0f, 24.0f};
    float radius = 12.0f;
    float speed_u_s = 1.6f;
    float hp = 60.0f;
    float dps = 15.0f;
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
    WallbreakerStats wallbreaker;
};

enum class EnemyArchetype
{
    Slime,
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
    Detonate
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

struct InputBindings
{
    std::string focusBase{"Tab"};
    std::string focusCommander{"Space"};
    std::string overview;
    std::unordered_map<std::string, std::string> cameraMove;
    std::vector<std::string> summonMode{"1", "2", "3", "4"};
    std::string restart;
    std::vector<std::string> commanderMoveUp{"W", "Up"};
    std::vector<std::string> commanderMoveDown{"S", "Down"};
    std::vector<std::string> commanderMoveLeft{"A", "Left"};
    std::vector<std::string> commanderMoveRight{"D", "Right"};
    std::vector<std::string> orderRushNearest{"F1"};
    std::vector<std::string> orderPushForward{"F2"};
    std::vector<std::string> orderFollowLeader{"F3"};
    std::vector<std::string> orderDefendBase{"F4"};
    std::string toggleDebugHud{"F10"};
    std::string quit{"Escape"};
    std::string formationPrevious{"Z"};
    std::string formationNext{"X"};
    std::string skillActivate{"MouseRight"};
    int bufferFrames = 4;
    float bufferExpiryMs = 80.0f;
};

struct AppConfig
{
    RendererConfig renderer;
    InputBindings input;
    GameConfig game;
    FormationAlignmentConfig formations;
    EntityCatalog entityCatalog;
    MapDefs mapDefs;
    TemperamentConfig temperament;
    SpawnScript spawnScript;
    std::optional<MissionConfig> mission;
    std::vector<SkillDef> skills;
    std::string atlasPath = "assets/atlas.json";
};

std::vector<SkillDef> buildDefaultSkills();

EnemyArchetype enemyTypeFromString(const std::string &typeId);


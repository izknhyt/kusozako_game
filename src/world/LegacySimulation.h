#pragma once

#include "config/AppConfig.h"
#include "core/Vec2.h"
#include "world/LegacyTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
std::vector<Vec2> computeFormationOffsets(Formation formation, std::size_t count);
const char *stanceLabel(ArmyStance stance);

struct RuntimeSkill
{
    SkillDef def;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
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
};

struct Unit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 4.0f;
    bool followBySkill = false;
    bool followByStance = false;
    bool effectiveFollower = false;
    Vec2 formationOffset{0.0f, 0.0f};
    Vec2 desiredVelocity{0.0f, 0.0f};
    bool hasDesiredVelocity = false;
    TemperamentState temperament;
};

struct CommanderUnit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 12.0f;
    bool alive = true;
    Vec2 moveIntent{0.0f, 0.0f};
    bool hasMoveIntent = false;
};

struct EnemyUnit
{
    Vec2 pos;
    float hp = 0.0f;
    float radius = 0.0f;
    EnemyArchetype type = EnemyArchetype::Slime;
    float speedPx = 0.0f;
    float dpsUnit = 0.0f;
    float dpsBase = 0.0f;
    float dpsWall = 0.0f;
    bool noOverlap = false;
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

struct LegacySimulation
{
{
    GameConfig config;
    TemperamentConfig temperamentConfig;
    EntityStats yunaStats;
    EntityStats slimeStats;
    WallbreakerStats wallbreakerStats;
    CommanderStats commanderStats;
    CommanderUnit commander;
    MapDefs mapDefs;
    SpawnScript spawnScript;
    std::vector<Unit> yunas;
    std::vector<EnemyUnit> enemies;
    std::vector<WallSegment> walls;
    std::vector<GateRuntime> gates;
    std::vector<RuntimeSkill> skills;
    Vec2 worldMin{0.0f, 0.0f};
    Vec2 worldMax{1280.0f, 720.0f};
    float spawnTimer = 0.0f;
    float yunaSpawnTimer = 0.0f;
    float simTime = 0.0f;
    float timeSinceLastEnemySpawn = 0.0f;
    float restartCooldown = 0.0f;
    float baseHp = 0.0f;
    bool spawnEnabled = true;
    GameResult result = GameResult::Playing;
    HUDState hud;
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
    float spawnSlowMultiplier = 1.0f;
    float spawnSlowTimer = 0.0f;
    float commanderRespawnTimer = 0.0f;
    float commanderInvulnTimer = 0.0f;
    int reinforcementQueue = 0;

    bool waveScriptComplete = false;
    bool spawnerIdle = true;

    Vec2 basePos;
    Vec2 yunaSpawnPos;
    std::vector<float> yunaRespawnTimers;

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

    void reset()
    {
        simTime = 0.0f;
        timeSinceLastEnemySpawn = 0.0f;
        restartCooldown = 0.0f;
        yunaSpawnTimer = 0.0f;
        yunas.clear();
        enemies.clear();
        walls.clear();
        spawnEnabled = true;
        result = GameResult::Playing;
        baseHp = static_cast<float>(config.base_hp);
        hud = {};
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
        commander.pos = yunaSpawnPos;
        commander.alive = true;
        commanderRespawnTimer = 0.0f;
        commanderInvulnTimer = 0.0f;
        reinforcementQueue = 0;
        spawnRateMultiplier = 1.0f;
        spawnSlowMultiplier = 1.0f;
        spawnSlowTimer = 0.0f;
        rallyState = false;
        stance = defaultStance;
        formation = Formation::Swarm;
        formationAlignTimer = 0.0f;
        formationDefenseMul = 1.0f;
        selectedSkill = 0;
        for (RuntimeSkill &skill : skills)
        {
            skill.cooldownRemaining = 0.0f;
            skill.activeTimer = 0.0f;
        }
        yunaRespawnTimers.clear();
        waveScriptComplete = false;
        spawnerIdle = true;
        rebuildGates();
        initializeMissionState();
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
        yunaRespawnTimers.push_back(computeChibiRespawnTime(overkillRatio));
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
                        spawnRateMultiplier = 1.0f;
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
                commander.pos = yunaSpawnPos;
                commanderInvulnTimer = config.commander_respawn.invuln;
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
    }

    void spawnYunaUnit()
    {
        Unit yuna;
        yuna.pos = yunaSpawnPos;
        yuna.pos.y += scatterY(rng);
        yuna.hp = yunaStats.hp;
        yuna.radius = yunaStats.radius;
        yunas.push_back(yuna);
        assignTemperament(yunas.back());
        clampToWorld(yunas.back().pos, yunas.back().radius);
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
        captureZones.clear();
        capturedZones = 0;
        captureGoal = 0;
        survival = {};
        disabledGates.clear();

        if (!hasMission)
        {
            missionMode = MissionMode::None;
            missionUI = {};
            missionFail = {};
            return;
        }

        missionMode = missionConfig.mode;
        missionUI = missionConfig.ui;
        missionFail = missionConfig.fail;

        if (missionMode == MissionMode::Boss)
        {
            spawnMissionBoss();
        }
        if (missionMode == MissionMode::Capture)
        {
            for (const MissionCaptureZone &zone : missionConfig.captureZones)
            {
                CaptureRuntime runtime;
                runtime.config = zone;
                runtime.worldPos = tileToWorld(zone.tile, mapDefs.tile_size);
                captureZones.push_back(runtime);
            }
            captureGoal = missionConfig.win.requireCaptured > 0
                              ? missionConfig.win.requireCaptured
                              : static_cast<int>(captureZones.size());
        }
        if (missionMode == MissionMode::Survival)
        {
            survival.duration = missionConfig.survival.duration > 0.0f ? missionConfig.survival.duration : missionConfig.win.surviveTime;
            survival.spawnMultiplier = 1.0f;
            survival.pacingTimer = missionConfig.survival.pacingStep;
            survival.elites = missionConfig.survival.elites;
            survival.nextElite = 0;
        }
    }

    void spawnMissionBoss()
    {
        if (missionConfig.boss.hp <= 0.0f)
        {
            return;
        }
        Vec2 world = tileToWorld(missionConfig.boss.tile, mapDefs.tile_size);
        EnemyUnit bossUnit;
        bossUnit.type = EnemyArchetype::Boss;
        bossUnit.pos = world;
        bossUnit.hp = missionConfig.boss.hp;
        bossUnit.radius = missionConfig.boss.radius_px > 0.0f ? missionConfig.boss.radius_px : 32.0f;
        bossUnit.speedPx = missionConfig.boss.speed_u_s * config.pixels_per_unit;
        bossUnit.dpsUnit = slimeStats.dps;
        bossUnit.dpsBase = slimeStats.dps;
        bossUnit.dpsWall = slimeStats.dps;
        bossUnit.noOverlap = missionConfig.boss.noOverlap;
        enemies.push_back(bossUnit);
        boss.active = true;
        boss.hp = bossUnit.hp;
        boss.maxHp = bossUnit.hp;
        boss.speedPx = bossUnit.speedPx;
        boss.radius = bossUnit.radius;
        boss.mechanic = missionConfig.boss.slam;
        boss.cycleTimer = boss.mechanic.period;
        boss.windupTimer = 0.0f;
        boss.inWindup = false;
        timeSinceLastEnemySpawn = 0.0f;
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
        if (!boss.active)
        {
            return;
        }
        EnemyUnit *bossEnemy = nullptr;
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.type == EnemyArchetype::Boss)
            {
                bossEnemy = &enemy;
                break;
            }
        }
        if (!bossEnemy)
        {
            boss.active = false;
            if (missionVictoryCountdown < 0.0f)
            {
                missionVictoryCountdown = std::max(config.victory_grace, 5.0f);
                pushTelemetry("Boss defeated!");
            }
            return;
        }
        boss.hp = bossEnemy->hp;
        bossEnemy->speedPx = boss.speedPx;
        if (boss.mechanic.period > 0.0f)
        {
            boss.cycleTimer -= dt;
            if (!boss.inWindup && boss.mechanic.windup > 0.0f && boss.cycleTimer <= boss.mechanic.windup)
            {
                boss.inWindup = true;
                boss.windupTimer = boss.mechanic.windup;
            }
            if (boss.inWindup)
            {
                boss.windupTimer -= dt;
                if (boss.windupTimer <= 0.0f)
                {
                    performBossSlam(*bossEnemy);
                    boss.inWindup = false;
                    boss.cycleTimer = boss.mechanic.period;
                }
            }
            else if (boss.mechanic.windup <= 0.0f && boss.cycleTimer <= 0.0f)
            {
                performBossSlam(*bossEnemy);
                boss.cycleTimer = boss.mechanic.period;
            }
            else if (boss.cycleTimer <= 0.0f)
            {
                boss.cycleTimer = boss.mechanic.period;
            }
        }
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
        if (missionMode == MissionMode::None)
        {
            return;
        }
        missionTimer += dt;
        switch (missionMode)
        {
        case MissionMode::Boss:
            updateBossMechanics(dt);
            break;
        case MissionMode::Capture:
            updateCaptureMission(dt);
            break;
        case MissionMode::Survival:
            updateSurvivalMission(dt);
            break;
        case MissionMode::None:
            break;
        }
        if (missionVictoryCountdown >= 0.0f)
        {
            missionVictoryCountdown = std::max(0.0f, missionVictoryCountdown - dt);
            if (missionVictoryCountdown <= 0.0f)
            {
                setResult(GameResult::Victory, "Victory");
            }
        }
    }

    void scheduleCommanderRespawn(float penaltyMultiplier, float bonusSeconds, float overkillRatio)
    {
        commander.alive = false;
        commander.hp = 0.0f;
        const float penalty = std::max(1.0f, penaltyMultiplier);
        const float bonus = std::max(0.0f, bonusSeconds);
        float respawnTime = computeCommanderRespawnTime(overkillRatio);
        respawnTime = std::max(config.commander_respawn.floor, respawnTime * penalty - bonus);
        commanderRespawnTimer = respawnTime;
        commanderInvulnTimer = 0.0f;
        reinforcementQueue += config.commander_auto_reinforce;
        rallyState = false;
        for (Unit &yuna : yunas)
        {
            yuna.followBySkill = false;
            yuna.followByStance = false;
            yuna.effectiveFollower = false;
        }
        hud.resultText = "Commander Down";
        hud.resultTimer = config.telemetry_duration;
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
        pushTelemetry("Self Destruct!");
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
            spawnRateMultiplier = skill.def.multiplier;
            skill.activeTimer = skill.def.duration;
            skill.cooldownRemaining = skill.def.cooldown;
            pushTelemetry("Spawn surge");
            break;
        case SkillType::Detonate:
            detonateCommander(skill.def);
            skill.cooldownRemaining = skill.def.cooldown;
            break;
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
    }

    void update(float dt)
    {
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
    }

    void spawnOneEnemy(Vec2 gatePos, EnemyArchetype type)
    {
        EnemyUnit enemy;
        enemy.pos = gatePos;
        enemy.pos.y += gateJitter(rng);
        enemy.type = type;
        if (type == EnemyArchetype::Wallbreaker)
        {
            enemy.hp = wallbreakerStats.hp;
            enemy.radius = wallbreakerStats.radius;
            enemy.speedPx = wallbreakerStats.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = wallbreakerStats.dps_unit;
            enemy.dpsBase = wallbreakerStats.dps_base;
            enemy.dpsWall = wallbreakerStats.dps_wall;
            enemy.noOverlap = wallbreakerStats.ignoreKnockback;
        }
        else if (type == EnemyArchetype::Boss)
        {
            enemy.hp = missionConfig.boss.hp > 0.0f ? missionConfig.boss.hp : 500.0f;
            enemy.radius = missionConfig.boss.radius_px > 0.0f ? missionConfig.boss.radius_px : 32.0f;
            enemy.speedPx = missionConfig.boss.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = slimeStats.dps;
            enemy.dpsBase = slimeStats.dps;
            enemy.dpsWall = slimeStats.dps;
            enemy.noOverlap = missionConfig.boss.noOverlap;
        }
        else
        {
            enemy.hp = slimeStats.hp;
            enemy.radius = slimeStats.radius;
            enemy.speedPx = slimeStats.speed_u_s * config.pixels_per_unit;
            enemy.dpsUnit = slimeStats.dps;
            enemy.dpsBase = slimeStats.dps;
            enemy.dpsWall = slimeStats.dps;
        }
        enemies.push_back(enemy);
        timeSinceLastEnemySpawn = 0.0f;
    }

    void updateYunaSpawn(float dt)
    {
        if (!spawnEnabled)
        {
            return;
        }
        const float rateMultiplier = std::max(spawnRateMultiplier, 0.1f);
        const float slowMultiplier = std::max(spawnSlowMultiplier, 0.1f);
        yunaSpawnTimer -= dt;
        const float minInterval = 0.1f;
        const float spawnInterval = std::max(minInterval, (config.yuna_interval / rateMultiplier) * slowMultiplier);
        while (yunaSpawnTimer <= 0.0f)
        {
            if (static_cast<int>(yunas.size()) < config.yuna_max)
            {
                spawnYunaUnit();
                yunaSpawnTimer += spawnInterval;
            }
            else
            {
                yunaSpawnTimer = 0.0f;
                break;
            }
        }

        const float respawnRate = rateMultiplier / slowMultiplier;
        for (float &timer : yunaRespawnTimers)
        {
            timer -= dt * respawnRate;
        }
        std::vector<float> remainingRespawns;
        remainingRespawns.reserve(yunaRespawnTimers.size());
        for (float timer : yunaRespawnTimers)
        {
            if (timer <= 0.0f && static_cast<int>(yunas.size()) < config.yuna_max)
            {
                spawnYunaUnit();
            }
            else
            {
                remainingRespawns.push_back(std::max(timer, 0.0f));
            }
        }
        yunaRespawnTimers.swap(remainingRespawns);

        while (reinforcementQueue > 0 && static_cast<int>(yunas.size()) < config.yuna_max)
        {
            spawnYunaUnit();
            --reinforcementQueue;
        }
    }

    std::vector<Vec2> collectRaidTargets() const
    {
        std::vector<Vec2> targets;
        targets.reserve(captureZones.size() + spawnScript.gate_tiles.size() + mapDefs.gate_tiles.size());
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

} // namespace world

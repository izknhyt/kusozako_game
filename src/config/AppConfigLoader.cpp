#include "config/AppConfigLoader.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

#include "assets/AssetManager.h"
#include "json/JsonUtils.h"
#include "input/InputMapper.h"

namespace fs = std::filesystem;

namespace
{

constexpr int kAppSchemaVersion = 1;
constexpr int kInputSchemaVersion = 1;
constexpr int kRendererSchemaVersion = 1;
constexpr int kMoraleSchemaVersion = 3;
constexpr int kJobsSchemaVersion = 1;
constexpr int kFormationsSchemaVersion = 1;
constexpr int kSpawnWeightsSchemaVersion = 1;

struct ParseContext
{
    AssetManager &assets;
    std::vector<AppConfigLoadError> *errors = nullptr;
};

AppConfigLoadError makeError(const fs::path &path, std::string message)
{
    AppConfigLoadError error;
    error.file = path.lexically_normal().string();
    error.message = std::move(message);
    return error;
}

std::optional<json::JsonValue> readLocalJson(const fs::path &path, std::vector<AppConfigLoadError> &errors)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        errors.push_back(makeError(path, "Failed to open JSON"));
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    auto parsed = json::parseJson(buffer.str());
    if (!parsed)
    {
        errors.push_back(makeError(path, "Failed to parse JSON"));
        return std::nullopt;
    }
    return parsed;
}

bool validateSchema(const json::JsonValue &root, int expected, const fs::path &path, std::vector<AppConfigLoadError> &errors)
{
    const json::JsonValue *schemaValue = json::getObjectField(root, "schema_version");
    if (!schemaValue || schemaValue->type != json::JsonValue::Type::Number)
    {
        errors.push_back(makeError(path, "Missing schema_version"));
        return false;
    }
    const int schema = static_cast<int>(schemaValue->number);
    if (schema != expected)
    {
        errors.push_back(makeError(path, "schema_version mismatch"));
        return false;
    }
    return true;
}

struct ParsedJobs
{
    JobCommonConfig common{};
    WarriorJobConfig warrior{};
    ArcherJobConfig archer{};
    ShieldJobConfig shield{};
};

std::optional<ParsedJobs> parseJobsConfig(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load jobs config"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    const json::JsonValue &root = *jsonDoc.get();
    ParsedJobs jobs;

    auto resolvedPath = fs::path(ctx.assets.resolvePath(path));
    if (!validateSchema(root, kJobsSchemaVersion, resolvedPath, *ctx.errors))
    {
        return std::nullopt;
    }

    if (const json::JsonValue *common = json::getObjectField(root, "jobsCommon"))
    {
        jobs.common.fizzleChance = json::getNumber(*common, "fizzle", jobs.common.fizzleChance);
        jobs.common.endlagSeconds = json::getNumber(*common, "endlagSec", jobs.common.endlagSeconds);
        jobs.common.projectileSpeedMin =
            json::getNumber(*common, "projSpeedMin", jobs.common.projectileSpeedMin);
        jobs.common.projectileSpeedMax =
            json::getNumber(*common, "projSpeedMax", jobs.common.projectileSpeedMax);
    }

    const json::JsonValue *jobsObj = json::getObjectField(root, "jobs");
    if (!jobsObj)
    {
        ctx.errors->push_back({path, "Missing jobs definitions"});
        return std::nullopt;
    }

    if (const json::JsonValue *warrior = json::getObjectField(*jobsObj, "warrior"))
    {
        jobs.warrior.skillId = json::getString(*warrior, "skill", jobs.warrior.skillId);
        jobs.warrior.cooldown = json::getNumber(*warrior, "cd", jobs.warrior.cooldown);
        jobs.warrior.accuracyMultiplier =
            json::getNumber(*warrior, "accMul", jobs.warrior.accuracyMultiplier);
        jobs.warrior.stumbleSeconds =
            json::getNumber(*warrior, "stumbleSec", jobs.warrior.stumbleSeconds);
    }
    else
    {
        ctx.errors->push_back({path, "Missing warrior job definition"});
        return std::nullopt;
    }

    if (const json::JsonValue *archer = json::getObjectField(*jobsObj, "archer"))
    {
        jobs.archer.skillId = json::getString(*archer, "skill", jobs.archer.skillId);
        jobs.archer.cooldown = json::getNumber(*archer, "cd", jobs.archer.cooldown);
        jobs.archer.critBonus = json::getNumber(*archer, "critBonus", jobs.archer.critBonus);
        jobs.archer.holdSeconds = json::getNumber(*archer, "holdSec", jobs.archer.holdSeconds);
    }
    else
    {
        ctx.errors->push_back({path, "Missing archer job definition"});
        return std::nullopt;
    }

    if (const json::JsonValue *shield = json::getObjectField(*jobsObj, "shield"))
    {
        jobs.shield.skillId = json::getString(*shield, "skill", jobs.shield.skillId);
        jobs.shield.cooldown = json::getNumber(*shield, "cd", jobs.shield.cooldown);
        jobs.shield.radiusUnits = json::getNumber(*shield, "radius_m", jobs.shield.radiusUnits);
        jobs.shield.durationSeconds =
            json::getNumber(*shield, "durSec", jobs.shield.durationSeconds);
        jobs.shield.selfSlowMultiplier =
            json::getNumber(*shield, "selfSlowMul", jobs.shield.selfSlowMultiplier);
    }
    else
    {
        ctx.errors->push_back({path, "Missing shield job definition"});
        return std::nullopt;
    }

    if (jobs.shield.selfSlowMultiplier < 0.0f)
    {
        jobs.shield.selfSlowMultiplier = 0.0f;
    }

    return jobs;
}

bool applyJobSpawnWeights(const json::JsonValue &weights, JobSpawnConfig &config)
{
    if (weights.type != json::JsonValue::Type::Object)
    {
        return false;
    }
    bool any = false;
    for (const auto &entry : weights.object)
    {
        if (const auto job = unitJobFromString(entry.first))
        {
            if (entry.second.type == json::JsonValue::Type::Number)
            {
                config.setWeight(*job, static_cast<float>(entry.second.number));
                any = true;
            }
        }
    }
    return any;
}

bool parseJobSpawnSection(const json::JsonValue &section, JobSpawnConfig &config, bool allowInlineWeights)
{
    bool parsed = false;
    if (const json::JsonValue *weights = json::getObjectField(section, "weights"))
    {
        parsed = applyJobSpawnWeights(*weights, config) || parsed;
    }
    if (const json::JsonValue *weights = json::getObjectField(section, "jobWeights"))
    {
        parsed = applyJobSpawnWeights(*weights, config) || parsed;
    }
    if (allowInlineWeights && section.type == json::JsonValue::Type::Object)
    {
        parsed = applyJobSpawnWeights(section, config) || parsed;
    }
    if (const json::JsonValue *pity = json::getObjectField(section, "pity"))
    {
        config.pity.repeatLimit =
            std::max(0, json::getInt(*pity, "repeatLimit", config.pity.repeatLimit));
        config.pity.unseenBoost = json::getNumber(*pity, "unseenBoost", config.pity.unseenBoost);
        if (config.pity.unseenBoost < 1.0f)
        {
            config.pity.unseenBoost = 1.0f;
        }
        parsed = true;
    }

    int history = config.historyLimit;
    history = json::getInt(section, "history_limit", history);
    history = json::getInt(section, "historyLimit", history);
    history = json::getInt(section, "jobHistoryLimit", history);
    config.historyLimit = history;

    int telemetry = config.telemetryWindow;
    telemetry = json::getInt(section, "telemetry_window", telemetry);
    telemetry = json::getInt(section, "telemetryWindow", telemetry);
    telemetry = json::getInt(section, "jobTelemetryWindow", telemetry);
    config.telemetryWindow = telemetry;

    return parsed;
}

std::optional<GameConfig> parseGameConfig(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load game config"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    const json::JsonValue &jsonRoot = *jsonDoc.get();
    GameConfig cfg;
    cfg.fixed_dt = json::getNumber(jsonRoot, "fixed_dt", cfg.fixed_dt);
    cfg.pixels_per_unit = json::getNumber(jsonRoot, "pixels_per_unit", cfg.pixels_per_unit);
    if (const json::JsonValue *base = json::getObjectField(jsonRoot, "base"))
    {
        cfg.base_hp = json::getInt(*base, "hp", cfg.base_hp);
        std::vector<float> aabb = json::getNumberArray(*base, "aabb_px");
        if (aabb.size() >= 2)
        {
            cfg.base_aabb = {aabb[0], aabb[1]};
        }
    }
    if (const json::JsonValue *gates = json::getObjectField(jsonRoot, "enemy_gate"))
    {
        cfg.gate_radius = json::getNumber(*gates, "radius_px", cfg.gate_radius);
        cfg.gate_hp = json::getNumber(*gates, "hp", cfg.gate_hp);
    }
    const json::JsonValue *spawn = json::getObjectField(jsonRoot, "spawn");
    if (spawn)
    {
        cfg.yuna_interval = json::getNumber(*spawn, "yuna_interval_s", cfg.yuna_interval);
        cfg.yuna_max = json::getInt(*spawn, "yuna_max", cfg.yuna_max);
        std::vector<float> offset = json::getNumberArray(*spawn, "yuna_offset_px");
        if (offset.size() >= 2)
        {
            cfg.yuna_offset_px = {offset[0], offset[1]};
        }
        cfg.yuna_scatter_y = json::getNumber(*spawn, "yuna_scatter_y_px", cfg.yuna_scatter_y);
    }

    bool hasInlineSpawnWeights = false;
    if (spawn)
    {
        hasInlineSpawnWeights = parseJobSpawnSection(*spawn, cfg.jobSpawn, false) || hasInlineSpawnWeights;
        if (const json::JsonValue *jobs = json::getObjectField(*spawn, "jobs"))
        {
            hasInlineSpawnWeights =
                parseJobSpawnSection(*jobs, cfg.jobSpawn, true) || hasInlineSpawnWeights;
        }
        cfg.jobSpawn.weightsAssetPath =
            json::getString(*spawn, "weights_path", cfg.jobSpawn.weightsAssetPath);
        cfg.spawn_weights_path = json::getString(*spawn, "weights_path", cfg.spawn_weights_path);
        if (const json::JsonValue *budget = json::getObjectField(*spawn, "budget"))
        {
            int maxPerFrame = cfg.spawnBudget.maxPerFrame;
            maxPerFrame = json::getInt(*budget, "max_per_frame", maxPerFrame);
            maxPerFrame = json::getInt(*budget, "maxPerFrame", maxPerFrame);
            cfg.spawnBudget.maxPerFrame = std::max(0, maxPerFrame);

            std::string warning = json::getString(*budget, "warning_text", cfg.spawnBudget.warningText);
            warning = json::getString(*budget, "warningText", warning);
            cfg.spawnBudget.warningText = warning;
        }
    }
    if (const json::JsonValue *jobSection = json::getObjectField(jsonRoot, "spawn_config"))
    {
        hasInlineSpawnWeights = parseJobSpawnSection(*jobSection, cfg.jobSpawn, true) || hasInlineSpawnWeights;
    }
    if (const json::JsonValue *jobSection = json::getObjectField(jsonRoot, "spawn_jobs"))
    {
        hasInlineSpawnWeights = parseJobSpawnSection(*jobSection, cfg.jobSpawn, true) || hasInlineSpawnWeights;
    }
    cfg.spawn_weights_path = json::getString(jsonRoot, "spawn_weights", cfg.spawn_weights_path);
    cfg.jobSpawn.hasInlineWeights = hasInlineSpawnWeights;

    if (cfg.jobSpawn.historyLimit < 0)
    {
        cfg.jobSpawn.historyLimit = 0;
    }
    if (cfg.jobSpawn.pity.repeatLimit > 0 && cfg.jobSpawn.historyLimit < cfg.jobSpawn.pity.repeatLimit)
    {
        cfg.jobSpawn.historyLimit = cfg.jobSpawn.pity.repeatLimit;
    }
    if (cfg.jobSpawn.telemetryWindow < 0)
    {
        cfg.jobSpawn.telemetryWindow = 0;
    }
    if (const json::JsonValue *respawn = json::getObjectField(jsonRoot, "respawn"))
    {
        if (const json::JsonValue *chibi = json::getObjectField(*respawn, "chibi"))
        {
            cfg.yuna_respawn.base = json::getNumber(*chibi, "base_s", cfg.yuna_respawn.base);
            cfg.yuna_respawn.scale = json::getNumber(*chibi, "scale_s", cfg.yuna_respawn.scale);
            cfg.yuna_respawn.k = json::getNumber(*chibi, "k", cfg.yuna_respawn.k);
            cfg.yuna_respawn.invuln = json::getNumber(*chibi, "invuln_s", cfg.yuna_respawn.invuln);
        }
        if (const json::JsonValue *commander = json::getObjectField(*respawn, "commander"))
        {
            cfg.commander_respawn.base = json::getNumber(*commander, "base_s", cfg.commander_respawn.base);
            cfg.commander_respawn.scale = json::getNumber(*commander, "scale_s", cfg.commander_respawn.scale);
            cfg.commander_respawn.k = json::getNumber(*commander, "k", cfg.commander_respawn.k);
            cfg.commander_respawn.floor = json::getNumber(*commander, "floor_s", cfg.commander_respawn.floor);
            cfg.commander_respawn.invuln = json::getNumber(*commander, "invuln_s", cfg.commander_respawn.invuln);
        }
    }
    if (const json::JsonValue *victory = json::getObjectField(jsonRoot, "victory"))
    {
        cfg.victory_grace = json::getNumber(*victory, "grace_period_s", cfg.victory_grace);
    }
    if (const json::JsonValue *result = json::getObjectField(jsonRoot, "result"))
    {
        cfg.telemetry_duration = json::getNumber(*result, "telemetry_duration_s", cfg.telemetry_duration);
        cfg.restart_delay = json::getNumber(*result, "restart_delay_s", cfg.restart_delay);
    }
    if (const json::JsonValue *onDeath = json::getObjectField(jsonRoot, "on_commander_death"))
    {
        cfg.commander_auto_reinforce = json::getInt(*onDeath, "auto_reinforce_chibi", cfg.commander_auto_reinforce);
    }
    cfg.enemy_script = json::getString(jsonRoot, "enemy_script", cfg.enemy_script);
    cfg.map_path = json::getString(jsonRoot, "map", cfg.map_path);
    cfg.rng_seed = json::getInt(jsonRoot, "rng_seed", cfg.rng_seed);
    if (const json::JsonValue *lod = json::getObjectField(jsonRoot, "lod"))
    {
        cfg.lod_threshold_entities = json::getInt(*lod, "threshold_entities", cfg.lod_threshold_entities);
        cfg.lod_skip_draw_every = std::max(1, json::getInt(*lod, "skip_draw_every", cfg.lod_skip_draw_every));
    }
    cfg.mission_path = json::getString(jsonRoot, "mission", cfg.mission_path);
    cfg.formations_path = json::getString(jsonRoot, "formations_config", cfg.formations_path);
    cfg.morale_path = json::getString(jsonRoot, "morale_config", cfg.morale_path);
    cfg.jobs_path = json::getString(jsonRoot, "jobs_config", cfg.jobs_path);
    if (const json::JsonValue *performance = json::getObjectField(jsonRoot, "performance"))
    {
        const json::JsonValue *budget = json::getObjectField(*performance, "budget");
        const json::JsonValue *limits = budget ? budget : performance;
        auto clampBudget = [](float value) {
            if (std::isfinite(value) && value >= 0.0f)
            {
                return value;
            }
            return 0.0f;
        };
        cfg.performance.updateMs = clampBudget(json::getNumber(*limits, "update_ms", cfg.performance.updateMs));
        cfg.performance.renderMs = clampBudget(json::getNumber(*limits, "render_ms", cfg.performance.renderMs));
        cfg.performance.inputMs = clampBudget(json::getNumber(*limits, "input_ms", cfg.performance.inputMs));
        cfg.performance.hudMs = clampBudget(json::getNumber(*limits, "hud_ms", cfg.performance.hudMs));
        cfg.performance.toleranceMs = clampBudget(json::getNumber(*performance, "tolerance_ms", cfg.performance.toleranceMs));
    }
    return cfg;
}

std::optional<MoraleConfig> parseMoraleConfig(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load morale config"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    const json::JsonValue &root = *jsonDoc.get();
    fs::path resolved = ctx.assets.resolvePath(path);
    if (!validateSchema(root, kMoraleSchemaVersion, resolved, *ctx.errors))
    {
        return std::nullopt;
    }

    auto readModifiers = [](const json::JsonValue &obj, MoraleModifiers modifiers) {
        modifiers.speed = json::getNumber(obj, "speed", modifiers.speed);
        modifiers.attackInterval = json::getNumber(obj, "attack_interval_mul", modifiers.attackInterval);
        modifiers.accuracy = json::getNumber(obj, "accuracy", modifiers.accuracy);
        modifiers.defense = json::getNumber(obj, "defense", modifiers.defense);
        if (modifiers.speed <= 0.0f) modifiers.speed = 0.01f;
        if (modifiers.attackInterval <= 0.0f) modifiers.attackInterval = 0.01f;
        if (modifiers.accuracy <= 0.0f) modifiers.accuracy = 0.01f;
        if (modifiers.defense <= 0.0f) modifiers.defense = 0.01f;
        return modifiers;
    };

    auto readRetreat = [](const json::JsonValue &obj, MoraleRetreatConfig retreat) {
        retreat.enabled = json::getBool(obj, "enabled", retreat.enabled);
        retreat.duration = json::getNumber(obj, "duration_s", retreat.duration);
        if (retreat.duration < 0.0f)
        {
            retreat.duration = 0.0f;
        }
        retreat.speedMultiplier = json::getNumber(obj, "speed_mul", retreat.speedMultiplier);
        if (!std::isfinite(retreat.speedMultiplier) || retreat.speedMultiplier < 0.0f)
        {
            retreat.speedMultiplier = 1.0f;
        }
        retreat.homewardBias = json::getNumber(obj, "homeward_bias", retreat.homewardBias);
        retreat.homewardBias = std::clamp(retreat.homewardBias, 0.0f, 1.0f);
        return retreat;
    };

    auto readBehavior = [&](const json::JsonValue &obj, MoraleBehaviorConfig behavior) {
        behavior.ignoreOrdersChance = json::getNumber(obj, "ignore_orders_chance", behavior.ignoreOrdersChance);
        behavior.ignoreOrdersChance = std::clamp(behavior.ignoreOrdersChance, 0.0f, 1.0f);
        behavior.detectionRadiusMultiplier =
            json::getNumber(obj, "detection_radius_mul", behavior.detectionRadiusMultiplier);
        if (!std::isfinite(behavior.detectionRadiusMultiplier) || behavior.detectionRadiusMultiplier < 0.0f)
        {
            behavior.detectionRadiusMultiplier = 1.0f;
        }
        behavior.spawnDelayMultiplier = json::getNumber(obj, "spawn_delay_mul", behavior.spawnDelayMultiplier);
        if (!std::isfinite(behavior.spawnDelayMultiplier) || behavior.spawnDelayMultiplier < 0.0f)
        {
            behavior.spawnDelayMultiplier = 1.0f;
        }
        behavior.retargetCooldownMultiplier =
            json::getNumber(obj, "retarget_cd_mul", behavior.retargetCooldownMultiplier);
        if (!std::isfinite(behavior.retargetCooldownMultiplier) || behavior.retargetCooldownMultiplier <= 0.0f)
        {
            behavior.retargetCooldownMultiplier = 1.0f;
        }
        behavior.commandObeyBonus = json::getNumber(obj, "command_obey_bonus", behavior.commandObeyBonus);
        behavior.commandObeyBonus = std::clamp(behavior.commandObeyBonus, 0.0f, 1.0f);
        if (const json::JsonValue *retreat = json::getObjectField(obj, "retreat"))
        {
            behavior.retreat = readRetreat(*retreat, behavior.retreat);
        }
        else
        {
            behavior.retreat.enabled = false;
        }
        return behavior;
    };

    auto readRetreatCheck = [](const json::JsonValue &obj, MoraleRetreatCheckConfig config) {
        config.interval = json::getNumber(obj, "interval_s", config.interval);
        if (!std::isfinite(config.interval) || config.interval < 0.0f)
        {
            config.interval = 0.0f;
        }
        config.chance = json::getNumber(obj, "chance", config.chance);
        config.chance = std::clamp(config.chance, 0.0f, 1.0f);
        return config;
    };

    auto readState = [&](const json::JsonValue &obj, MoraleStateConfig state) {
        state.duration = json::getNumber(obj, "duration_s", state.duration);
        if (state.duration < 0.0f)
        {
            state.duration = 0.0f;
        }
        state.modifiers = readModifiers(obj, state.modifiers);
        state.behavior = readBehavior(obj, state.behavior);
        if (const json::JsonValue *retreatCheck = json::getObjectField(obj, "retreat_check"))
        {
            state.retreatCheck = readRetreatCheck(*retreatCheck, state.retreatCheck);
        }
        else
        {
            state.retreatCheck = {};
        }
        return state;
    };

    MoraleConfig config;
    config.leaderDownWindow = json::getNumber(root, "leader_down_window_s", config.leaderDownWindow);
    config.comfortZoneRadius = json::getNumber(root, "comfort_zone_radius_px", config.comfortZoneRadius);
    config.reviveBarrier = json::getNumber(root, "revive_barrier_s", config.reviveBarrier);
    if (config.leaderDownWindow < 0.0f)
    {
        config.leaderDownWindow = 0.0f;
    }
    if (config.comfortZoneRadius < 0.0f)
    {
        config.comfortZoneRadius = 0.0f;
    }
    if (config.reviveBarrier < 0.0f)
    {
        config.reviveBarrier = 0.0f;
    }
    config.reviveBarrierLinger = json::getNumber(root, "revive_barrier_linger_s", config.reviveBarrierLinger);
    if (config.reviveBarrierLinger < 0.0f)
    {
        config.reviveBarrierLinger = 0.0f;
    }
    config.detectionRadius = json::getNumber(root, "detection_radius_px", config.detectionRadius);
    if (config.detectionRadius < 0.0f)
    {
        config.detectionRadius = 0.0f;
    }

    if (const json::JsonValue *stable = json::getObjectField(root, "stable"))
    {
        config.stable = readModifiers(*stable, config.stable);
        config.stableBehavior = readBehavior(*stable, config.stableBehavior);
    }
    if (const json::JsonValue *leader = json::getObjectField(root, "leader_down"))
    {
        config.leaderDown = readModifiers(*leader, config.leaderDown);
        config.leaderDownBehavior = readBehavior(*leader, config.leaderDownBehavior);
    }
    if (const json::JsonValue *states = json::getObjectField(root, "states"))
    {
        if (const json::JsonValue *panic = json::getObjectField(*states, "panic"))
        {
            config.panic = readState(*panic, config.panic);
        }
        if (const json::JsonValue *meso = json::getObjectField(*states, "mesomeso"))
        {
            config.mesomeso = readState(*meso, config.mesomeso);
        }
        if (const json::JsonValue *recovering = json::getObjectField(*states, "recovering"))
        {
            config.recovering = readState(*recovering, config.recovering);
        }
        if (const json::JsonValue *shielded = json::getObjectField(*states, "shielded"))
        {
            config.shielded = readState(*shielded, config.shielded);
        }
    }

    if (const json::JsonValue *spawn = json::getObjectField(root, "spawn_light_injury"))
    {
        config.spawnLightInjury = readState(*spawn, config.spawnLightInjury);
    }

    if (const json::JsonValue *spawnWindow = json::getObjectField(root, "spawn_while_leader_down"))
    {
        config.spawnWhileLeaderDown.applyLightMesomeso =
            json::getBool(*spawnWindow, "apply_light_mesomeso", config.spawnWhileLeaderDown.applyLightMesomeso);
        config.spawnWhileLeaderDown.duration =
            json::getNumber(*spawnWindow, "duration_s", config.spawnWhileLeaderDown.duration);
        if (!std::isfinite(config.spawnWhileLeaderDown.duration) || config.spawnWhileLeaderDown.duration < 0.0f)
        {
            config.spawnWhileLeaderDown.duration = 0.0f;
        }
    }

    return config;
}

std::optional<FormationAlignmentConfig> parseFormationConfig(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load formation config"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    FormationAlignmentConfig config;
    const json::JsonValue &root = *jsonDoc.get();
    fs::path resolved = ctx.assets.resolvePath(path);
    if (!validateSchema(root, kFormationsSchemaVersion, resolved, *ctx.errors))
    {
        return std::nullopt;
    }
    config.alignDuration = std::max(0.0f, json::getNumber(root, "align_duration_s", config.alignDuration));
    config.defenseMultiplier = json::getNumber(root, "defense_multiplier", config.defenseMultiplier);
    if (config.defenseMultiplier <= 0.0f)
    {
        config.defenseMultiplier = 1.0f;
    }
    return config;
}

std::optional<JobSpawnConfig> parseSpawnWeights(ParseContext &ctx, const std::string &path,
                                                const JobSpawnConfig &defaults)
{
    if (path.empty())
    {
        return defaults;
    }

    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load spawn weights"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    const json::JsonValue &root = *jsonDoc.get();
    fs::path resolved = ctx.assets.resolvePath(path);
    if (!validateSchema(root, kSpawnWeightsSchemaVersion, resolved, *ctx.errors))
    {
        return std::nullopt;
    }

    JobSpawnConfig config = defaults;
    parseJobSpawnSection(root, config, true);
    config.weightsAssetPath = path;
    return config;
}

std::optional<EntityCatalog> parseEntityCatalog(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load entity catalog"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }
    EntityCatalog catalog;
    catalog.commander.spritePrefix = "commander";
    catalog.yuna.spritePrefix = "yuna_front";
    catalog.slime.spritePrefix = "slime_walk";
    catalog.wallbreaker.spritePrefix = "elite_wallbreaker";

    const json::JsonValue &root = *jsonDoc.get();
    const json::JsonValue *commander = json::getObjectField(root, "commander");
    const json::JsonValue *yuna = json::getObjectField(root, "yuna");
    const json::JsonValue *slime = json::getObjectField(root, "slime");
    if (!commander || !yuna || !slime)
    {
        ctx.errors->push_back({path, "Missing commander/yuna/slime definitions"});
        return std::nullopt;
    }

    std::vector<float> commanderAabb = json::getNumberArray(*commander, "aabb_px");
    if (commanderAabb.size() >= 2)
    {
        catalog.commander.aabb = {commanderAabb[0], commanderAabb[1]};
    }
    catalog.commander.radius = json::getNumber(*commander, "r_px", catalog.commander.radius);
    catalog.commander.speed_u_s = json::getNumber(*commander, "speed_u_s", catalog.commander.speed_u_s);
    catalog.commander.hp = json::getNumber(*commander, "hp", catalog.commander.hp);
    catalog.commander.dps = json::getNumber(*commander, "dps", catalog.commander.dps);
    catalog.commander.spritePrefix = json::getString(*commander, "sprite_prefix", catalog.commander.spritePrefix);

    catalog.yuna.radius = json::getNumber(*yuna, "r_px", catalog.yuna.radius);
    catalog.yuna.speed_u_s = json::getNumber(*yuna, "speed_u_s", catalog.yuna.speed_u_s);
    catalog.yuna.hp = json::getNumber(*yuna, "hp", catalog.yuna.hp);
    catalog.yuna.dps = json::getNumber(*yuna, "dps", catalog.yuna.dps);
    catalog.yuna.spritePrefix = json::getString(*yuna, "sprite_prefix", catalog.yuna.spritePrefix);

    catalog.slime.radius = json::getNumber(*slime, "r_px", catalog.slime.radius);
    catalog.slime.speed_u_s = json::getNumber(*slime, "speed_u_s", catalog.slime.speed_u_s);
    catalog.slime.hp = json::getNumber(*slime, "hp", catalog.slime.hp);
    catalog.slime.dps = json::getNumber(*slime, "dps", catalog.slime.dps);
    catalog.slime.spritePrefix = json::getString(*slime, "sprite_prefix", catalog.slime.spritePrefix);

    if (const json::JsonValue *elite = json::getObjectField(root, "elite_wallbreaker"))
    {
        catalog.wallbreaker.radius = json::getNumber(*elite, "r_px", catalog.wallbreaker.radius);
        catalog.wallbreaker.speed_u_s = json::getNumber(*elite, "speed_u_s", catalog.wallbreaker.speed_u_s);
        catalog.wallbreaker.hp = json::getNumber(*elite, "hp", catalog.wallbreaker.hp);
        if (const json::JsonValue *dps = json::getObjectField(*elite, "dps"))
        {
            catalog.wallbreaker.dps_wall = json::getNumber(*dps, "wall", catalog.wallbreaker.dps_wall);
            catalog.wallbreaker.dps_unit = json::getNumber(*dps, "unit", catalog.wallbreaker.dps_unit);
            catalog.wallbreaker.dps_base = json::getNumber(*dps, "base", catalog.wallbreaker.dps_base);
        }
        if (const json::JsonValue *traits = json::getObjectField(*elite, "traits"))
        {
            catalog.wallbreaker.ignoreKnockback = json::getBool(*traits, "ignore_knockback", catalog.wallbreaker.ignoreKnockback);
            catalog.wallbreaker.preferWallRadiusPx = json::getNumber(*traits, "prefer_wall_radius_px", catalog.wallbreaker.preferWallRadiusPx);
        }
        catalog.wallbreaker.spritePrefix = json::getString(*elite, "sprite_prefix", catalog.wallbreaker.spritePrefix);
    }

    return catalog;
}

std::optional<MapDefs> parseMapDefs(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load map defs"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    MapDefs defs;
    const json::JsonValue &root = *jsonDoc.get();
    defs.tile_size = json::getInt(root, "tile_size_px", defs.tile_size);
    std::vector<float> baseTile = json::getNumberArray(root, "base_tile");
    if (baseTile.size() >= 2)
    {
        defs.base_tile = {baseTile[0], baseTile[1]};
    }
    std::vector<float> spawnTile = json::getNumberArray(root, "spawn_tile_yuna");
    if (spawnTile.size() >= 2)
    {
        defs.spawn_tile_yuna = {spawnTile[0], spawnTile[1]};
    }
    if (const json::JsonValue *gates = json::getObjectField(root, "gate_tiles"))
    {
        for (const auto &kv : gates->object)
        {
            const json::JsonValue &arr = kv.second;
            if (arr.type == json::JsonValue::Type::Array && arr.array.size() >= 2 &&
                arr.array[0].type == json::JsonValue::Type::Number && arr.array[1].type == json::JsonValue::Type::Number)
            {
                defs.gate_tiles[kv.first] = {static_cast<float>(arr.array[0].number), static_cast<float>(arr.array[1].number)};
            }
        }
    }
    return defs;
}

TemperamentRange readRangeValue(const json::JsonValue &value, TemperamentRange fallback)
{
    TemperamentRange range = fallback;
    if (value.type == json::JsonValue::Type::Array && !value.array.empty())
    {
        if (value.array[0].type == json::JsonValue::Type::Number)
        {
            range.min = static_cast<float>(value.array[0].number);
        }
        if (value.array.size() > 1 && value.array[1].type == json::JsonValue::Type::Number)
        {
            range.max = static_cast<float>(value.array[1].number);
        }
        else
        {
            range.max = range.min;
        }
    }
    else if (value.type == json::JsonValue::Type::Number)
    {
        range.min = static_cast<float>(value.number);
        range.max = range.min;
    }
    return range;
}

TemperamentRange getRangeField(const json::JsonValue &obj, const std::string &key, TemperamentRange fallback)
{
    if (const json::JsonValue *value = json::getObjectField(obj, key))
    {
        return readRangeValue(*value, fallback);
    }
    return fallback;
}

std::optional<TemperamentBehavior> temperamentBehaviorFromString(const std::string &id)
{
    if (id == "CHARGE_NEAREST") return TemperamentBehavior::ChargeNearest;
    if (id == "FLEE_NEAREST") return TemperamentBehavior::FleeNearest;
    if (id == "FOLLOW_YUNA") return TemperamentBehavior::FollowYuna;
    if (id == "RAID_GATE") return TemperamentBehavior::RaidGate;
    if (id == "HOMEBOUND") return TemperamentBehavior::Homebound;
    if (id == "WANDER") return TemperamentBehavior::Wander;
    if (id == "DOZE") return TemperamentBehavior::Doze;
    if (id == "GUARD_BASE") return TemperamentBehavior::GuardBase;
    if (id == "TARGET_TAG") return TemperamentBehavior::TargetTag;
    if (id == "MIMIC") return TemperamentBehavior::Mimic;
    return std::nullopt;
}

std::optional<TemperamentConfig> parseTemperamentConfig(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load temperament config"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    TemperamentConfig config;
    const json::JsonValue &root = *jsonDoc.get();
    config.orderDuration = json::getNumber(root, "order_duration_s", config.orderDuration);
    config.fearRadius = json::getNumber(root, "fear_radius_px", config.fearRadius);
    if (const json::JsonValue *follow = json::getObjectField(root, "follow_catchup"))
    {
        config.followCatchup.distance = json::getNumber(*follow, "distance_px", config.followCatchup.distance);
        config.followCatchup.distance = json::getNumber(*follow, "dist_px", config.followCatchup.distance);
        config.followCatchup.duration = json::getNumber(*follow, "duration_s", config.followCatchup.duration);
        config.followCatchup.duration = json::getNumber(*follow, "dur_s", config.followCatchup.duration);
        config.followCatchup.multiplier = json::getNumber(*follow, "multiplier", config.followCatchup.multiplier);
        config.followCatchup.multiplier = json::getNumber(*follow, "mult", config.followCatchup.multiplier);
    }
    if (const json::JsonValue *wander = json::getObjectField(root, "wander"))
    {
        config.wanderTurnInterval = getRangeField(*wander, "turn_every_s", config.wanderTurnInterval);
    }
    else if (const json::JsonValue *wanderInterval = json::getObjectField(root, "wander_turn_interval_s"))
    {
        config.wanderTurnInterval = readRangeValue(*wanderInterval, config.wanderTurnInterval);
    }
    if (const json::JsonValue *sleep = json::getObjectField(root, "sleep"))
    {
        config.sleepEvery = getRangeField(*sleep, "every_s", config.sleepEvery);
        config.sleepDuration = json::getNumber(*sleep, "duration_s", config.sleepDuration);
    }
    else
    {
        if (const json::JsonValue *sleepEvery = json::getObjectField(root, "sleep_every_s"))
        {
            config.sleepEvery = readRangeValue(*sleepEvery, config.sleepEvery);
        }
        config.sleepDuration = json::getNumber(root, "sleep_dur_s", config.sleepDuration);
    }
    if (const json::JsonValue *dash = json::getObjectField(root, "charge_dash"))
    {
        config.chargeDash.duration = json::getNumber(*dash, "duration_s", config.chargeDash.duration);
        config.chargeDash.duration = json::getNumber(*dash, "dur_s", config.chargeDash.duration);
        config.chargeDash.multiplier = json::getNumber(*dash, "multiplier", config.chargeDash.multiplier);
        config.chargeDash.multiplier = json::getNumber(*dash, "mult", config.chargeDash.multiplier);
    }

    const json::JsonValue *definitions = json::getObjectField(root, "definitions");
    if (!definitions)
    {
        definitions = json::getObjectField(root, "temperaments");
    }
    if (!definitions)
    {
        ctx.errors->push_back({path, "Missing temperament definitions"});
        return std::nullopt;
    }

    config.definitions.clear();
    config.cumulativeWeights.clear();
    float cumulative = 0.0f;
    for (const auto &kv : definitions->object)
    {
        const json::JsonValue &value = kv.second;
        if (value.type != json::JsonValue::Type::Object)
        {
            continue;
        }
        TemperamentDefinition def;
        def.id = kv.first;
        def.label = json::getString(value, "label", def.id);
        def.spawnRate = json::getNumber(value, "spawn_rate", def.spawnRate);
        def.homeRadius = json::getNumber(value, "home_radius_px", def.homeRadius);
        def.homeRadius = json::getNumber(value, "home_r_px", def.homeRadius);
        def.avoidEnemyRadius = json::getNumber(value, "avoid_enemy_radius_px", def.avoidEnemyRadius);
        def.avoidEnemyRadius = json::getNumber(value, "avoid_enemy_r_px", def.avoidEnemyRadius);
        if (const json::JsonValue *behavior = json::getObjectField(value, "behavior"))
        {
            if (behavior->type == json::JsonValue::Type::String)
            {
                auto mapped = temperamentBehaviorFromString(behavior->string);
                if (mapped)
                {
                    def.behavior = *mapped;
                }
            }
        }
        if (const json::JsonValue *cry = json::getObjectField(value, "cry"))
        {
            def.cryPauseEvery = getRangeField(*cry, "every_s", def.cryPauseEvery);
            def.cryPauseDuration = json::getNumber(*cry, "pause_s", def.cryPauseDuration);
        }
        else
        {
            if (const json::JsonValue *cryEvery = json::getObjectField(value, "cry_pause_every_s"))
            {
                def.cryPauseEvery = readRangeValue(*cryEvery, def.cryPauseEvery);
            }
            def.cryPauseDuration = json::getNumber(value, "cry_pause_dur_s", def.cryPauseDuration);
        }
        def.panicOnHit = json::getNumber(value, "panic_on_hit", def.panicOnHit);
        def.panicOnHit = json::getNumber(value, "panic_on_hit_s", def.panicOnHit);
        auto appendTargetTags = [&def](const json::JsonValue *targets) {
            if (!targets)
            {
                return;
            }
            if (targets->type == json::JsonValue::Type::Array)
            {
                for (const auto &entry : targets->array)
                {
                    if (entry.type == json::JsonValue::Type::String)
                    {
                        def.targetTags.push_back(entry.string);
                    }
                }
            }
        };
        appendTargetTags(json::getObjectField(value, "target_tags"));
        appendTargetTags(json::getObjectField(value, "target_tag"));
        if (def.targetTags.empty())
        {
            if (const json::JsonValue *singleTag = json::getObjectField(value, "target_tag"))
            {
                if (singleTag->type == json::JsonValue::Type::String)
                {
                    def.targetTags.push_back(singleTag->string);
                }
            }
        }
        if (const json::JsonValue *mimic = json::getObjectField(value, "mimic"))
        {
            def.mimicEvery = getRangeField(*mimic, "every_s", def.mimicEvery);
            def.mimicDuration = getRangeField(*mimic, "duration_s", def.mimicDuration);
            if (const json::JsonValue *pool = json::getObjectField(*mimic, "pool"))
            {
                if (pool->type == json::JsonValue::Type::Array)
                {
                    for (const auto &entry : pool->array)
                    {
                        if (entry.type == json::JsonValue::Type::String)
                        {
                            auto mapped = temperamentBehaviorFromString(entry.string);
                            if (mapped)
                            {
                                def.mimicPool.push_back(*mapped);
                            }
                        }
                    }
                }
            }
            if (const json::JsonValue *defValue = json::getObjectField(*mimic, "default"))
            {
                if (defValue->type == json::JsonValue::Type::String)
                {
                    auto mapped = temperamentBehaviorFromString(defValue->string);
                    if (mapped)
                    {
                        def.mimicDefault = *mapped;
                    }
                }
            }
        }

        cumulative += std::max(def.spawnRate, 0.0f);
        config.cumulativeWeights.push_back(cumulative);
        config.definitions.push_back(std::move(def));
    }

    return config;
}

std::optional<SpawnScript> parseSpawnScript(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load spawn script"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    SpawnScript script;
    const json::JsonValue &root = *jsonDoc.get();
    script.y_jitter = json::getNumber(root, "y_jitter_px", 0.0f);
    if (const json::JsonValue *gates = json::getObjectField(root, "gates"))
    {
        if (gates->type == json::JsonValue::Type::Object)
        {
            for (const auto &kv : gates->object)
            {
                const json::JsonValue &entry = kv.second;
                if (entry.type != json::JsonValue::Type::Object)
                {
                    continue;
                }
                SpawnSet set;
                set.gate = kv.first;
                set.count = json::getInt(entry, "count", set.count);
                set.interval = json::getNumber(entry, "interval_s", set.interval);
                set.typeId = json::getString(entry, "type", set.typeId);
                set.type = enemyTypeFromString(set.typeId);
                script.gate_tiles[set.gate] = {0.0f, 0.0f};
                script.waves.push_back({0.0f, {set}, json::getString(entry, "telemetry", "")});
            }
        }
    }
    if (const json::JsonValue *waves = json::getObjectField(root, "waves"))
    {
        if (waves->type == json::JsonValue::Type::Array)
        {
            script.waves.clear();
            for (const auto &waveValue : waves->array)
            {
                if (waveValue.type != json::JsonValue::Type::Object)
                {
                    continue;
                }
                Wave wave;
                wave.time = json::getNumber(waveValue, "time_s", wave.time);
                wave.telemetry = json::getString(waveValue, "telemetry", wave.telemetry);
                if (const json::JsonValue *sets = json::getObjectField(waveValue, "sets"))
                {
                    if (sets->type == json::JsonValue::Type::Array)
                    {
                        for (const auto &setValue : sets->array)
                        {
                            if (setValue.type != json::JsonValue::Type::Object)
                            {
                                continue;
                            }
                            SpawnSet set;
                            set.gate = json::getString(setValue, "gate", "");
                            set.count = json::getInt(setValue, "count", set.count);
                            set.interval = json::getNumber(setValue, "interval_s", set.interval);
                            set.typeId = json::getString(setValue, "type", set.typeId);
                            set.type = enemyTypeFromString(set.typeId);
                            wave.sets.push_back(set);
                        }
                    }
                }
                script.waves.push_back(wave);
            }
        }
    }
    if (const json::JsonValue *gates = json::getObjectField(root, "gate_tiles"))
    {
        if (gates->type == json::JsonValue::Type::Object)
        {
            for (const auto &kv : gates->object)
            {
                const json::JsonValue &arr = kv.second;
                if (arr.type == json::JsonValue::Type::Array && arr.array.size() >= 2 &&
                    arr.array[0].type == json::JsonValue::Type::Number && arr.array[1].type == json::JsonValue::Type::Number)
                {
                    script.gate_tiles[kv.first] = {static_cast<float>(arr.array[0].number), static_cast<float>(arr.array[1].number)};
                }
            }
        }
    }
    return script;
}

std::optional<MissionConfig> parseMissionConfig(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load mission config"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    MissionConfig mission;
    const json::JsonValue &root = *jsonDoc.get();
    std::string mode = json::getString(root, "mode", "");
    if (mode == "boss")
    {
        mission.mode = MissionMode::Boss;
    }
    else if (mode == "capture")
    {
        mission.mode = MissionMode::Capture;
    }
    else if (mode == "survival")
    {
        mission.mode = MissionMode::Survival;
    }
    else
    {
        mission.mode = MissionMode::None;
    }
    if (const json::JsonValue *ui = json::getObjectField(root, "ui"))
    {
        mission.ui.showGoalText = json::getBool(*ui, "show_goal_text", mission.ui.showGoalText);
        mission.ui.showTimer = json::getBool(*ui, "show_timer", mission.ui.showTimer);
        mission.ui.showBossHpBar = json::getBool(*ui, "show_boss_hp", mission.ui.showBossHpBar);
        mission.ui.goalText = json::getString(*ui, "goal_text", mission.ui.goalText);
    }
    if (const json::JsonValue *fail = json::getObjectField(root, "fail"))
    {
        mission.fail.baseHpZero = json::getBool(*fail, "base_hp_zero", mission.fail.baseHpZero);
    }
    if (const json::JsonValue *boss = json::getObjectField(root, "boss"))
    {
        mission.boss.id = json::getString(*boss, "id", mission.boss.id);
        std::vector<float> tile = json::getNumberArray(*boss, "tile");
        if (tile.size() >= 2)
        {
            mission.boss.tile = {tile[0], tile[1]};
        }
        mission.boss.hp = json::getNumber(*boss, "hp", mission.boss.hp);
        mission.boss.speed_u_s = json::getNumber(*boss, "speed_u_s", mission.boss.speed_u_s);
        mission.boss.radius_px = json::getNumber(*boss, "radius_px", mission.boss.radius_px);
        mission.boss.noOverlap = json::getBool(*boss, "no_overlap", mission.boss.noOverlap);
        if (const json::JsonValue *slam = json::getObjectField(*boss, "slam"))
        {
            mission.boss.slam.period = json::getNumber(*slam, "period_s", mission.boss.slam.period);
            mission.boss.slam.windup = json::getNumber(*slam, "windup_s", mission.boss.slam.windup);
            mission.boss.slam.radius = json::getNumber(*slam, "radius_px", mission.boss.slam.radius);
            mission.boss.slam.damage = json::getNumber(*slam, "damage", mission.boss.slam.damage);
        }
    }
    if (const json::JsonValue *capture = json::getObjectField(root, "capture"))
    {
        if (const json::JsonValue *zones = json::getObjectField(*capture, "zones"))
        {
            if (zones->type == json::JsonValue::Type::Array)
            {
                for (const auto &zoneValue : zones->array)
                {
                    if (zoneValue.type != json::JsonValue::Type::Object)
                    {
                        continue;
                    }
                    MissionCaptureZone zone;
                    zone.id = json::getString(zoneValue, "id", zone.id);
                    std::vector<float> tile = json::getNumberArray(zoneValue, "tile");
                    if (tile.size() >= 2)
                    {
                        zone.tile = {tile[0], tile[1]};
                    }
                    zone.radius_px = json::getNumber(zoneValue, "radius_px", zone.radius_px);
                    zone.capture_s = json::getNumber(zoneValue, "capture_s", zone.capture_s);
                    zone.decay_s = json::getNumber(zoneValue, "decay_s", zone.decay_s);
                    if (const json::JsonValue *onCapture = json::getObjectField(zoneValue, "on_capture"))
                    {
                        zone.onCapture.disableGate = json::getString(*onCapture, "disable_gate", zone.onCapture.disableGate);
                        zone.onCapture.telemetry = json::getString(*onCapture, "telemetry", zone.onCapture.telemetry);
                    }
                    mission.captureZones.push_back(zone);
                }
            }
        }
        mission.win.requireCaptured = json::getInt(*capture, "require_captured", mission.win.requireCaptured);
    }
    if (const json::JsonValue *survival = json::getObjectField(root, "survival"))
    {
        mission.survival.duration = json::getNumber(*survival, "duration_s", mission.survival.duration);
        mission.survival.pacingStep = json::getNumber(*survival, "pacing_step_s", mission.survival.pacingStep);
        mission.survival.pacingMultiplier = json::getNumber(*survival, "pacing_multiplier", mission.survival.pacingMultiplier);
        if (const json::JsonValue *elites = json::getObjectField(*survival, "elites"))
        {
            if (elites->type == json::JsonValue::Type::Array)
            {
                for (const auto &eliteValue : elites->array)
                {
                    if (eliteValue.type != json::JsonValue::Type::Object)
                    {
                        continue;
                    }
                    MissionSurvivalElite elite;
                    elite.time = json::getNumber(eliteValue, "time_s", elite.time);
                    elite.gate = json::getString(eliteValue, "gate", elite.gate);
                    elite.typeId = json::getString(eliteValue, "type", elite.typeId);
                    elite.type = enemyTypeFromString(elite.typeId);
                    mission.survival.elites.push_back(elite);
                }
            }
        }
        mission.win.surviveTime = json::getNumber(*survival, "win_survive_s", mission.win.surviveTime);
    }
    mission.win.bossDown = json::getBool(root, "win_boss_down", mission.win.bossDown);
    return mission;
}

std::optional<std::vector<SkillDef>> parseSkillCatalog(ParseContext &ctx, const std::string &path)
{
    AssetManager::AssetLoadStatus status;
    auto jsonDoc = ctx.assets.acquireJson(path);
    status = jsonDoc.status();
    if (!jsonDoc.get())
    {
        auto &errors = *ctx.errors;
        if (!status.message.empty())
        {
            errors.push_back({path, status.message});
        }
        else
        {
            errors.push_back({path, "Failed to load skill catalog"});
        }
        return std::nullopt;
    }
    if (!status.ok && !status.message.empty())
    {
        ctx.errors->push_back({path, status.message});
    }

    auto parseSkill = [](const std::string &idHint, const json::JsonValue &value) -> std::optional<SkillDef> {
        if (value.type != json::JsonValue::Type::Object)
        {
            return std::nullopt;
        }

        auto getNumberOpt = [](const json::JsonValue &obj, const std::string &key) -> std::optional<float> {
            if (const json::JsonValue *v = json::getObjectField(obj, key))
            {
                if (v->type == json::JsonValue::Type::Number)
                {
                    return static_cast<float>(v->number);
                }
            }
            return std::nullopt;
        };
        auto getIntOpt = [](const json::JsonValue &obj, const std::string &key) -> std::optional<int> {
            if (const json::JsonValue *v = json::getObjectField(obj, key))
            {
                if (v->type == json::JsonValue::Type::Number)
                {
                    return static_cast<int>(v->number);
                }
            }
            return std::nullopt;
        };

        SkillDef def;
        def.id = json::getString(value, "id", idHint);
        if (def.id.empty())
        {
            def.id = idHint;
        }
        def.displayName = json::getString(value, "name", def.id);
        if (def.displayName.empty())
        {
            def.displayName = def.id;
        }

        std::string type = json::getString(value, "type", "toggle_follow");
        if (type == "make_wall")
        {
            def.type = SkillType::MakeWall;
        }
        else if (type == "spawn_rate")
        {
            def.type = SkillType::SpawnRate;
        }
        else if (type == "detonate")
        {
            def.type = SkillType::Detonate;
        }
        else
        {
            def.type = SkillType::ToggleFollow;
        }

        if (auto hotkey = getIntOpt(value, "hotkey"))
        {
            def.hotkey = *hotkey;
        }
        if (auto hotkey = getIntOpt(value, "key"))
        {
            def.hotkey = *hotkey;
        }
        if (auto cooldown = getNumberOpt(value, "cooldown_s"))
        {
            def.cooldown = *cooldown;
        }
        if (auto mana = getNumberOpt(value, "mana"))
        {
            def.mana = *mana;
        }
        if (auto radius = getNumberOpt(value, "radius_px"))
        {
            def.radius = *radius;
        }
        if (auto duration = getNumberOpt(value, "duration_s"))
        {
            def.duration = *duration;
        }
        else if (auto duration = getNumberOpt(value, "life_s"))
        {
            def.duration = *duration;
        }
        if (auto lenTiles = getIntOpt(value, "len_tiles"))
        {
            def.lenTiles = *lenTiles;
        }
        if (auto hpPerSegment = getNumberOpt(value, "hp_per_segment"))
        {
            def.hpPerSegment = *hpPerSegment;
        }
        if (auto multiplier = getNumberOpt(value, "multiplier"))
        {
            def.multiplier = *multiplier;
        }
        else if (auto multiplier = getNumberOpt(value, "mult"))
        {
            def.multiplier = *multiplier;
        }
        if (auto damage = getNumberOpt(value, "damage"))
        {
            def.damage = *damage;
        }
        if (auto penalty = getNumberOpt(value, "respawn_penalty"))
        {
            def.respawnPenalty = *penalty;
        }
        else if (auto penalty = getNumberOpt(value, "respawn_penalty_ratio"))
        {
            def.respawnPenalty = *penalty;
        }
        if (auto slowMult = getNumberOpt(value, "spawn_slow_multiplier"))
        {
            def.spawnSlowMult = *slowMult;
        }
        if (auto slowDuration = getNumberOpt(value, "spawn_slow_duration_s"))
        {
            def.spawnSlowDuration = *slowDuration;
        }
        if (const json::JsonValue *spawnSlow = json::getObjectField(value, "spawn_slow"))
        {
            if (auto slowMult = getNumberOpt(*spawnSlow, "multiplier"))
            {
                def.spawnSlowMult = *slowMult;
            }
            else if (auto slowMult = getNumberOpt(*spawnSlow, "mult"))
            {
                def.spawnSlowMult = *slowMult;
            }
            if (auto slowDuration = getNumberOpt(*spawnSlow, "duration_s"))
            {
                def.spawnSlowDuration = *slowDuration;
            }
        }
        if (auto bonus = getNumberOpt(value, "respawn_bonus_per_hit"))
        {
            def.respawnBonusPerHit = *bonus;
        }
        else if (auto bonus = getNumberOpt(value, "respawn_bonus_per_hit_s"))
        {
            def.respawnBonusPerHit = *bonus;
        }
        if (auto bonusCap = getNumberOpt(value, "respawn_bonus_cap"))
        {
            def.respawnBonusCap = *bonusCap;
        }
        else if (auto bonusCap = getNumberOpt(value, "respawn_bonus_cap_s"))
        {
            def.respawnBonusCap = *bonusCap;
        }

        if (def.id.empty())
        {
            return std::nullopt;
        }
        return def;
    };

    std::vector<SkillDef> defs;
    const json::JsonValue &root = *jsonDoc.get();
    if (root.type == json::JsonValue::Type::Array)
    {
        for (const auto &value : root.array)
        {
            if (auto def = parseSkill("", value))
            {
                defs.push_back(*def);
            }
        }
    }
    else if (root.type == json::JsonValue::Type::Object)
    {
        for (const auto &kv : root.object)
        {
            if (auto def = parseSkill(kv.first, kv.second))
            {
                defs.push_back(*def);
            }
        }
    }
    else
    {
        ctx.errors->push_back({path, "Expected skill catalog object or array"});
        return std::nullopt;
    }

    if (defs.empty())
    {
        ctx.errors->push_back({path, "No valid skill definitions"});
        return std::nullopt;
    }

    return defs;
}

RendererConfig parseRendererConfig(const json::JsonValue &root)
{
    RendererConfig cfg;
    cfg.backend = json::getString(root, "backend", cfg.backend);
    cfg.srgb = json::getBool(root, "srgb", cfg.srgb);
    cfg.allowHiDpi = json::getBool(root, "allow_hidpi", cfg.allowHiDpi);
    cfg.pixelSnap = json::getBool(root, "pixel_snap", cfg.pixelSnap);
    cfg.integerZoomOnly = json::getBool(root, "integer_zoom_only", cfg.integerZoomOnly);
    cfg.pixelsPerUnit = json::getNumber(root, "pixels_per_unit", cfg.pixelsPerUnit);
    if (const json::JsonValue *lod = json::getObjectField(root, "lod"))
    {
        cfg.lodThresholdEntities = json::getInt(*lod, "threshold_entities", cfg.lodThresholdEntities);
        cfg.lodSkipDrawEvery = std::max(1, json::getInt(*lod, "skip_draw_every", cfg.lodSkipDrawEvery));
    }
    return cfg;
}

InputBindings parseInputBindings(const json::JsonValue &root, std::vector<AppConfigLoadError> &errors, const fs::path &path)
{
    InputBindings bindings;
    bindings.focusBase = json::getString(root, "FocusBase", bindings.focusBase);
    bindings.focusCommander = json::getString(root, "FocusCommander", bindings.focusCommander);
    bindings.overview = json::getString(root, "Overview", bindings.overview);
    if (const json::JsonValue *move = json::getObjectField(root, "CameraMove"))
    {
        for (const auto &kv : move->object)
        {
            if (kv.second.type == json::JsonValue::Type::String)
            {
                bindings.cameraMove[kv.first] = kv.second.string;
            }
        }
    }
    if (const json::JsonValue *summon = json::getObjectField(root, "SummonMode"))
    {
        if (summon->type == json::JsonValue::Type::Array)
        {
            for (const auto &entry : summon->array)
            {
                if (entry.type == json::JsonValue::Type::String)
                {
                    bindings.summonMode.push_back(entry.string);
                }
            }
        }
    }
    bindings.restart = json::getString(root, "Restart", bindings.restart);
    bindings.toggleDebugHud = json::getString(root, "ToggleDebugHud", bindings.toggleDebugHud);
    bindings.toggleDebugOverlay = json::getString(root, "ToggleDebugOverlay", bindings.toggleDebugOverlay);
    bindings.reloadConfig = json::getString(root, "ReloadConfig", bindings.reloadConfig);
    bindings.dumpSpawnHistory = json::getString(root, "DumpSpawnHistory", bindings.dumpSpawnHistory);
    bindings.quit = json::getString(root, "Quit", bindings.quit);
    bindings.formationPrevious = json::getString(root, "FormationPrevious", bindings.formationPrevious);
    bindings.formationNext = json::getString(root, "FormationNext", bindings.formationNext);
    bindings.skillActivate = json::getString(root, "SkillActivate", bindings.skillActivate);
    if (const json::JsonValue *commander = json::getObjectField(root, "CommanderMove"))
    {
        auto assignKeys = [](const json::JsonValue *value, std::vector<std::string> &out) {
            if (!value)
            {
                return;
            }
            out.clear();
            if (value->type == json::JsonValue::Type::Array)
            {
                for (const auto &entry : value->array)
                {
                    if (entry.type == json::JsonValue::Type::String)
                    {
                        out.push_back(entry.string);
                    }
                }
            }
            else if (value->type == json::JsonValue::Type::String)
            {
                out.push_back(value->string);
            }
        };
        assignKeys(json::getObjectField(*commander, "Up"), bindings.commanderMoveUp);
        assignKeys(json::getObjectField(*commander, "Down"), bindings.commanderMoveDown);
        assignKeys(json::getObjectField(*commander, "Left"), bindings.commanderMoveLeft);
        assignKeys(json::getObjectField(*commander, "Right"), bindings.commanderMoveRight);
    }
    if (const json::JsonValue *orders = json::getObjectField(root, "Orders"))
    {
        auto assignOrder = [](const json::JsonValue *value, std::vector<std::string> &out) {
            if (!value)
            {
                return;
            }
            out.clear();
            if (value->type == json::JsonValue::Type::Array)
            {
                for (const auto &entry : value->array)
                {
                    if (entry.type == json::JsonValue::Type::String)
                    {
                        out.push_back(entry.string);
                    }
                }
            }
            else if (value->type == json::JsonValue::Type::String)
            {
                out.push_back(value->string);
            }
        };
        assignOrder(json::getObjectField(*orders, "RushNearest"), bindings.orderRushNearest);
        assignOrder(json::getObjectField(*orders, "PushForward"), bindings.orderPushForward);
        assignOrder(json::getObjectField(*orders, "FollowLeader"), bindings.orderFollowLeader);
        assignOrder(json::getObjectField(*orders, "DefendBase"), bindings.orderDefendBase);
    }
    bindings.bufferFrames = std::max(1, json::getInt(root, "buffer_frames", bindings.bufferFrames));
    bindings.bufferExpiryMs = static_cast<float>(json::getNumber(root, "buffer_expiry_ms", bindings.bufferExpiryMs));

    auto validateKey = [&errors, &path](const std::string &value, const char *label) {
        if (!value.empty() && !InputMapper::isValidKeyBinding(value))
        {
            std::string message = "Invalid ";
            message += label;
            message += " binding: ";
            message += value;
            errors.push_back(makeError(path, std::move(message)));
        }
    };

    validateKey(bindings.toggleDebugHud, "ToggleDebugHud");
    validateKey(bindings.toggleDebugOverlay, "ToggleDebugOverlay");
    validateKey(bindings.reloadConfig, "ReloadConfig");
    validateKey(bindings.dumpSpawnHistory, "DumpSpawnHistory");

    return bindings;
}

} // namespace

AppConfigLoader::AppConfigLoader(std::filesystem::path configRoot)
{
    if (!configRoot.empty())
    {
        m_configRoot = std::move(configRoot);
    }
    else
    {
        m_configRoot = fs::path("config");
    }
    m_configRoot = m_configRoot.lexically_normal();
}

AppConfig AppConfigLoader::loadFallback() const
{
    AppConfig config;
    config.skills = buildDefaultSkills();
    return config;
}

void AppConfigLoader::trackFile(const std::string &logicalName, const std::filesystem::path &path)
{
    std::error_code ec;
    auto timestamp = fs::last_write_time(path, ec);
    TrackedFile tracked;
    tracked.path = path;
    tracked.timestamp = ec ? std::filesystem::file_time_type{} : timestamp;
    m_trackedFiles[logicalName] = tracked;
}

AppConfigLoadResult AppConfigLoader::load(AssetManager &assets)
{
    AppConfigLoadResult result;
    result.config = loadFallback();
    result.success = false;

    std::vector<AppConfigLoadError> errors;

    const fs::path appPath = m_configRoot / "app.json";
    trackFile("config/app.json", appPath);
    auto appJson = readLocalJson(appPath, errors);
    if (!appJson)
    {
        result.errors = std::move(errors);
        return result;
    }
    if (!validateSchema(*appJson, kAppSchemaVersion, appPath, errors))
    {
        result.errors = std::move(errors);
        return result;
    }

    TelemetryOptions telemetryOptions = result.config.telemetry;
    if (const json::JsonValue *telemetryObj = json::getObjectField(*appJson, "telemetry"))
    {
        std::string output = json::getString(*telemetryObj, "output_dir", telemetryOptions.outputDirectory);
        output = json::getString(*telemetryObj, "outputDir", output);
        if (!output.empty())
        {
            telemetryOptions.outputDirectory = output;
        }

        const double rotationMb = json::getNumber(*telemetryObj, "rotation_mb", 0.0);
        if (rotationMb > 0.0)
        {
            telemetryOptions.rotationBytes = static_cast<std::uintmax_t>(rotationMb * 1024.0 * 1024.0);
        }
        const double rotationBytesValue =
            json::getNumber(*telemetryObj, "rotation_bytes", static_cast<double>(telemetryOptions.rotationBytes));
        if (rotationBytesValue > 0.0)
        {
            telemetryOptions.rotationBytes = static_cast<std::uintmax_t>(rotationBytesValue);
        }

        int maxFiles = json::getInt(*telemetryObj, "max_files", static_cast<int>(telemetryOptions.maxFiles));
        maxFiles = json::getInt(*telemetryObj, "maxFiles", maxFiles);
        if (maxFiles > 0)
        {
            telemetryOptions.maxFiles = static_cast<std::size_t>(maxFiles);
        }

        double warningMb = json::getNumber(*telemetryObj, "texture_warning_mb", -1.0);
        warningMb = json::getNumber(*telemetryObj, "textureWarningMB", warningMb);
        if (warningMb >= 0.0)
        {
            telemetryOptions.textureMemoryWarningBytes =
                static_cast<std::uintmax_t>(warningMb * 1024.0 * 1024.0);
        }

        double warningBytes = json::getNumber(*telemetryObj, "texture_warning_bytes", -1.0);
        warningBytes = json::getNumber(*telemetryObj, "textureWarningBytes", warningBytes);
        if (warningBytes >= 0.0)
        {
            telemetryOptions.textureMemoryWarningBytes = static_cast<std::uintmax_t>(warningBytes);
        }
    }

    const json::JsonValue *assetsObj = json::getObjectField(*appJson, "assets");
    std::string gamePath = "assets/game.json";
    std::string entitiesPath = "assets/entities.json";
    std::string mapDefsPath = "assets/map_defs.json";
    std::string temperamentPath = "assets/ai_temperaments.json";
    std::string jobsPath = "assets/jobs.json";
    std::string skillsPath = "assets/skills.json";
    std::string formationsPath = "assets/formations.json";
    std::string atlasPath = "assets/atlas.json";
    std::string moralePath = "assets/morale.json";
    std::string spawnWeightsPath = "assets/spawn_weights.json";
    if (assetsObj)
    {
        gamePath = json::getString(*assetsObj, "game", gamePath);
        entitiesPath = json::getString(*assetsObj, "entities", entitiesPath);
        mapDefsPath = json::getString(*assetsObj, "map_defs", mapDefsPath);
        temperamentPath = json::getString(*assetsObj, "temperaments", temperamentPath);
        jobsPath = json::getString(*assetsObj, "jobs", jobsPath);
        skillsPath = json::getString(*assetsObj, "skills", skillsPath);
        formationsPath = json::getString(*assetsObj, "formations", formationsPath);
        atlasPath = json::getString(*assetsObj, "atlas", atlasPath);
        moralePath = json::getString(*assetsObj, "morale", moralePath);
        spawnWeightsPath = json::getString(*assetsObj, "spawn_weights", spawnWeightsPath);
    }

    const fs::path rendererPath = m_configRoot / "renderer.json";
    trackFile("config/renderer.json", rendererPath);
    auto rendererJson = readLocalJson(rendererPath, errors);
    if (!rendererJson)
    {
        result.errors = std::move(errors);
        return result;
    }
    if (!validateSchema(*rendererJson, kRendererSchemaVersion, rendererPath, errors))
    {
        result.errors = std::move(errors);
        return result;
    }

    const fs::path inputPath = m_configRoot / "input.json";
    trackFile("config/input.json", inputPath);
    auto inputJson = readLocalJson(inputPath, errors);
    if (!inputJson)
    {
        result.errors = std::move(errors);
        return result;
    }
    if (!validateSchema(*inputJson, kInputSchemaVersion, inputPath, errors))
    {
        result.errors = std::move(errors);
        return result;
    }

    ParseContext ctx{assets, &errors};

    auto gameCfg = parseGameConfig(ctx, gamePath);
    if (!gameCfg)
    {
        result.errors = std::move(errors);
        return result;
    }
    auto entities = parseEntityCatalog(ctx, entitiesPath);
    if (!entities)
    {
        result.errors = std::move(errors);
        return result;
    }
    auto mapDefs = parseMapDefs(ctx, mapDefsPath);
    if (!mapDefs)
    {
        result.errors = std::move(errors);
        return result;
    }
    auto temperament = parseTemperamentConfig(ctx, temperamentPath);
    if (!temperament)
    {
        result.errors = std::move(errors);
        return result;
    }
    auto jobs = parseJobsConfig(ctx, jobsPath);
    if (!jobs)
    {
        result.errors = std::move(errors);
        return result;
    }
    auto morale = parseMoraleConfig(ctx, moralePath);
    if (!morale)
    {
        result.errors = std::move(errors);
        return result;
    }
    gameCfg->morale = *morale;
    auto spawnWeights = parseSpawnWeights(ctx, spawnWeightsPath, gameCfg->jobSpawn);
    if (!spawnWeights)
    {
        result.errors = std::move(errors);
        return result;
    }
    gameCfg->jobSpawn = *spawnWeights;
    if (gameCfg->jobSpawn.historyLimit < 0)
    {
        gameCfg->jobSpawn.historyLimit = 0;
    }
    if (gameCfg->jobSpawn.pity.repeatLimit > 0 &&
        gameCfg->jobSpawn.historyLimit < gameCfg->jobSpawn.pity.repeatLimit)
    {
        gameCfg->jobSpawn.historyLimit = gameCfg->jobSpawn.pity.repeatLimit;
    }
    if (gameCfg->jobSpawn.telemetryWindow < 0)
    {
        gameCfg->jobSpawn.telemetryWindow = 0;
    }
    gameCfg->jobCommon = jobs->common;
    gameCfg->warriorJob = jobs->warrior;
    gameCfg->archerJob = jobs->archer;
    gameCfg->shieldJob = jobs->shield;

    auto spawnScript = parseSpawnScript(ctx, gameCfg->enemy_script);
    if (!spawnScript)
    {
        result.errors = std::move(errors);
        return result;
    }
    std::optional<MissionConfig> mission;
    if (!gameCfg->mission_path.empty())
    {
        mission = parseMissionConfig(ctx, gameCfg->mission_path);
        if (!mission)
        {
            errors.push_back({gameCfg->mission_path, "Failed to parse mission config"});
        }
    }
    auto skills = parseSkillCatalog(ctx, skillsPath);
    if (!skills)
    {
        skills = buildDefaultSkills();
        errors.push_back({skillsPath, "Failed to parse skills, using defaults"});
    }
    auto formations = parseFormationConfig(ctx, formationsPath);
    if (!formations)
    {
        result.errors = std::move(errors);
        return result;
    }
    gameCfg->formationDefaults = *formations;

    trackFile("assets/game", assets.resolvePath(gamePath));
    trackFile("assets/entities", assets.resolvePath(entitiesPath));
    trackFile("assets/map_defs", assets.resolvePath(mapDefsPath));
    trackFile("assets/temperaments", assets.resolvePath(temperamentPath));
    trackFile("assets/jobs", assets.resolvePath(jobsPath));
    trackFile("assets/spawn", assets.resolvePath(gameCfg->enemy_script));
    trackFile("assets/morale", assets.resolvePath(moralePath));
    trackFile("assets/skills", assets.resolvePath(skillsPath));
    trackFile("assets/formations", assets.resolvePath(formationsPath));
    if (!spawnWeightsPath.empty())
    {
        trackFile("assets/spawn_weights", assets.resolvePath(spawnWeightsPath));
    }
    if (!gameCfg->mission_path.empty())
    {
        trackFile("assets/mission", assets.resolvePath(gameCfg->mission_path));
    }
    trackFile("assets/atlas", assets.resolvePath(atlasPath));

    result.config = loadFallback();
    result.config.telemetry = telemetryOptions;
    result.config.renderer = parseRendererConfig(*rendererJson);
    result.config.input = parseInputBindings(*inputJson, errors, inputPath);
    gameCfg->jobs_path = jobsPath;
    gameCfg->morale_path = moralePath;
    gameCfg->formations_path = formationsPath;
    gameCfg->spawn_weights_path = spawnWeightsPath;
    if (gameCfg->jobSpawn.weightsAssetPath.empty())
    {
        gameCfg->jobSpawn.weightsAssetPath = spawnWeightsPath;
    }
    result.config.game = *gameCfg;
    result.config.entityCatalog = *entities;
    result.config.mapDefs = *mapDefs;
    result.config.temperament = *temperament;
    result.config.spawnScript = *spawnScript;
    result.config.game.morale = *morale;
    result.config.mission = mission;
    result.config.skills = *skills;
    result.config.atlasPath = atlasPath;

    result.errors = std::move(errors);
    result.success = result.errors.empty();
    return result;
}

std::vector<std::string> AppConfigLoader::detectChangedFiles()
{
    std::vector<std::string> changed;
    for (auto &kv : m_trackedFiles)
    {
        std::error_code ec;
        const auto ts = fs::last_write_time(kv.second.path, ec);
        if (ec)
        {
            if (kv.second.timestamp != fs::file_time_type{})
            {
                kv.second.timestamp = fs::file_time_type{};
                changed.push_back(kv.first);
            }
            continue;
        }
        if (kv.second.timestamp != ts)
        {
            kv.second.timestamp = ts;
            changed.push_back(kv.first);
        }
    }
    return changed;
}

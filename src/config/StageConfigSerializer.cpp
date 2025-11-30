#include "config/StageConfigSerializer.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

json::JsonValue makeNumber(double v)
{
    json::JsonValue j;
    j.type = json::JsonValue::Type::Number;
    j.number = v;
    return j;
}

json::JsonValue makeBool(bool v)
{
    json::JsonValue j;
    j.type = json::JsonValue::Type::Bool;
    j.boolean = v;
    return j;
}

json::JsonValue makeString(const std::string &s)
{
    json::JsonValue j;
    j.type = json::JsonValue::Type::String;
    j.string = s;
    return j;
}

json::JsonValue makeArray(std::initializer_list<json::JsonValue> values)
{
    json::JsonValue j;
    j.type = json::JsonValue::Type::Array;
    j.array.insert(j.array.end(), values.begin(), values.end());
    return j;
}

json::JsonValue makeObject(std::initializer_list<std::pair<std::string, json::JsonValue>> values)
{
    json::JsonValue j;
    j.type = json::JsonValue::Type::Object;
    for (const auto &kv : values)
    {
        j.object.emplace(kv.first, kv.second);
    }
    return j;
}

json::JsonValue serializeBases(const std::vector<StageEnemyBaseConfig> &bases)
{
    json::JsonValue arr;
    arr.type = json::JsonValue::Type::Array;
    for (const auto &base : bases)
    {
        json::JsonValue weights;
        weights.type = json::JsonValue::Type::Object;
        for (const auto &kv : base.weights)
        {
            weights.object.emplace(kv.first, makeNumber(kv.second));
        }

        json::JsonValue obj = makeObject({
            {"id", makeString(base.id)},
            {"hp", makeNumber(base.hp)},
            {"radius_px", makeNumber(base.radiusPx)},
            {"rate_per_s", makeNumber(base.ratePerSecond)},
            {"rate_max", makeNumber(base.rateMax)},
            {"sealed_by_default", makeBool(base.sealedByDefault)},
            {"pos", makeArray({makeNumber(base.position.x), makeNumber(base.position.y)})},
            {"weights", std::move(weights)},
        });
        arr.array.push_back(std::move(obj));
    }
    return arr;
}

json::JsonValue serializeAllyBases(const std::vector<StageAllyBaseConfig> &bases)
{
    json::JsonValue arr;
    arr.type = json::JsonValue::Type::Array;
    for (const auto &base : bases)
    {
        json::JsonValue obj = makeObject({
            {"id", makeString(base.id)},
            {"hp", makeNumber(base.hp)},
            {"aura_radius_px", makeNumber(base.auraRadiusPx)},
            {"sealed_by_default", makeBool(base.sealedByDefault)},
            {"pos", makeArray({makeNumber(base.position.x), makeNumber(base.position.y)})},
        });
        arr.array.push_back(std::move(obj));
    }
    return arr;
}

json::JsonValue serializeHazards(const std::vector<StageHazardConfig> &hazards)
{
    json::JsonValue arr;
    arr.type = json::JsonValue::Type::Array;
    for (const auto &hazard : hazards)
    {
        json::JsonValue obj = makeObject({
            {"id", makeString(hazard.id)},
            {"radius_px", makeNumber(hazard.radiusPx)},
            {"weight", makeNumber(hazard.weight)},
            {"enabled", makeBool(hazard.enabled)},
            {"trigger_enemy_base", makeString(hazard.triggerEnemyBase)},
            {"pos", makeArray({makeNumber(hazard.position.x), makeNumber(hazard.position.y)})},
        });
        arr.array.push_back(std::move(obj));
    }
    return arr;
}

} // namespace

json::JsonValue stageConfigToJson(const StageConfig &config)
{
    json::JsonValue root;
    root.type = json::JsonValue::Type::Object;
    root.object.emplace("schema_version", makeNumber(config.schemaVersion));
    root.object.emplace("ally_factory",
                        makeObject({{"rate_per_s", makeNumber(config.allyFactory.ratePerSecond)},
                                    {"cap", makeNumber(config.allyFactory.cap)}}));
    root.object.emplace("on_base_sealed",
                        makeObject({{"stop_spawn", makeBool(config.sealBehavior.stopSpawn)},
                                    {"visual_only", makeBool(config.sealBehavior.visualOnly)},
                                    {"no_collision", makeBool(config.sealBehavior.removeCollision)}}));
    root.object.emplace("global_caps",
                        makeObject({{"enemies", makeNumber(config.globalCaps.enemies)},
                                    {"chibi", makeNumber(config.globalCaps.chibi)}}));
    root.object.emplace("victory",
                        makeObject({{"require_dragon", makeBool(config.victory.requireDragon)},
                                    {"require_all_bases", makeBool(config.victory.requireAllEnemyBases)}}));

    json::JsonValue speed = makeObject({{"hp_bar_fast_interval", makeNumber(config.speed.hpBarFastInterval)}});
    json::JsonValue steps;
    steps.type = json::JsonValue::Type::Array;
    for (float v : config.speed.steps)
    {
        steps.array.push_back(makeNumber(v));
    }
    speed.object.emplace("steps", std::move(steps));
    root.object.emplace("speed", std::move(speed));

    root.object.emplace("ally_bases", serializeAllyBases(config.allyBases));
    root.object.emplace("enemy_bases", serializeBases(config.enemyBases));
    root.object.emplace("hazards", serializeHazards(config.hazards));

    return root;
}

SaveFileResult saveStageConfig(const StageConfig &config, const std::filesystem::path &path,
                               const SaveFileOptions &options)
{
    const auto json = stageConfigToJson(config);
    const std::string text = json::serializeJson(json, true, 2);
    return atomicWriteFile(path, text, options);
}

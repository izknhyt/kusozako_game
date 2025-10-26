#include "config/AppConfig.h"

#include <algorithm>
#include <optional>

EnemyArchetype enemyTypeFromString(const std::string &typeId)
{
    if (typeId == "elite_wallbreaker" || typeId == "wallbreaker" || typeId == "wall_breaker")
    {
        return EnemyArchetype::Wallbreaker;
    }
    if (typeId == "boss")
    {
        return EnemyArchetype::Boss;
    }
    return EnemyArchetype::Slime;
}

std::optional<UnitJob> unitJobFromString(const std::string &id)
{
    if (id == "warrior")
    {
        return UnitJob::Warrior;
    }
    if (id == "archer")
    {
        return UnitJob::Archer;
    }
    if (id == "shield")
    {
        return UnitJob::Shield;
    }
    return std::nullopt;
}

std::vector<SkillDef> buildDefaultSkills()
{
    std::vector<SkillDef> defs;

    SkillDef rally;
    rally.id = "rally";
    rally.displayName = "Rally";
    rally.type = SkillType::ToggleFollow;
    rally.hotkey = 1;
    rally.cooldown = 3.0f;
    rally.radius = 160.0f;
    defs.push_back(rally);

    SkillDef wall;
    wall.id = "wall";
    wall.displayName = "Wall";
    wall.type = SkillType::MakeWall;
    wall.hotkey = 2;
    wall.cooldown = 15.0f;
    wall.lenTiles = 8;
    wall.duration = 20.0f;
    wall.hpPerSegment = 10.0f;
    defs.push_back(wall);

    SkillDef surge;
    surge.id = "surge";
    surge.displayName = "Surge";
    surge.type = SkillType::SpawnRate;
    surge.hotkey = 3;
    surge.cooldown = 40.0f;
    surge.multiplier = 1.5f;
    surge.duration = 20.0f;
    defs.push_back(surge);

    SkillDef selfDestruct;
    selfDestruct.id = "self_destruct";
    selfDestruct.displayName = "Self Destruct";
    selfDestruct.type = SkillType::Detonate;
    selfDestruct.hotkey = 4;
    selfDestruct.cooldown = 60.0f;
    selfDestruct.radius = 128.0f;
    selfDestruct.damage = 80.0f;
    selfDestruct.respawnPenalty = 2.0f;
    selfDestruct.spawnSlowMult = 2.0f;
    selfDestruct.spawnSlowDuration = 10.0f;
    selfDestruct.respawnBonusPerHit = 0.5f;
    selfDestruct.respawnBonusCap = 6.0f;
    defs.push_back(selfDestruct);

    return defs;
}


#include "world/systems/RenderingPrepSystem.h"

#include "world/FormationUtils.h"

#include <algorithm>
#include <cstdint>

namespace world::systems
{

namespace
{

std::uint8_t crowdAlphaForNeighborCount(int neighbors)
{
    if (neighbors >= 4)
    {
        return static_cast<std::uint8_t>(255 * 0.3f);
    }
    return 255;
}

} // namespace

void RenderingPrepSystem::update(float, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    auto &queue = sim.renderQueue;

    queue.clearDynamic();

    queue.telemetryText = context.hud.telemetryText;
    queue.telemetryTimer = context.hud.telemetryTimer;
    if (context.hud.performance.active && context.hud.performance.timer > 0.0f)
    {
        queue.performanceWarningText = context.hud.performance.message;
        queue.performanceWarningTimer = context.hud.performance.timer;
    }
    else
    {
        queue.performanceWarningText.clear();
        queue.performanceWarningTimer = 0.0f;
    }

    const std::size_t allyCount = sim.yunas.size();
    const std::size_t enemyCount = sim.enemies.size();
    const std::size_t commanderCount = sim.commander.alive ? 1u : 0u;
    const std::size_t entityCount = allyCount + enemyCount + commanderCount;

    const bool lodEnabled = sim.config.lod_threshold_entities > 0 &&
                            entityCount >= static_cast<std::size_t>(sim.config.lod_threshold_entities);
    queue.lodActive = lodEnabled;
    ++queue.lodFrameCounter;
    if (queue.lodFrameCounter < 0)
    {
        queue.lodFrameCounter = 1;
    }
    queue.skipActors = queue.lodActive && sim.config.lod_skip_draw_every > 1 &&
                       (queue.lodFrameCounter % sim.config.lod_skip_draw_every != 0);

    queue.allies.reserve(allyCount + commanderCount);
    queue.moraleIcons.reserve(allyCount + commanderCount);
    queue.enemies.reserve(enemyCount);
    queue.walls.reserve(sim.walls.size());

    if (sim.commander.alive)
    {
        LegacySimulation::RenderQueue::AllySprite commanderSprite;
        commanderSprite.position = sim.commander.pos;
        commanderSprite.radius = sim.commander.radius;
        commanderSprite.commander = true;
        commanderSprite.job = UnitJob::Warrior;
        commanderSprite.alpha = 255;
        commanderSprite.morale = sim.moraleSummary.commanderState;
        commanderSprite.hasUnitIndex = false;
        queue.allies.push_back(commanderSprite);

        LegacySimulation::RenderQueue::MoraleIcon commanderIcon;
        commanderIcon.position = sim.commander.pos;
        commanderIcon.radius = sim.commander.radius;
        commanderIcon.state = sim.moraleSummary.commanderState;
        commanderIcon.commander = true;
        commanderIcon.unitIndex = 0;
        queue.moraleIcons.push_back(commanderIcon);
    }

    std::vector<std::uint8_t> yunaAlpha(allyCount, 255);
    if (allyCount > 1)
    {
        const float crowdRadiusSq = 32.0f * 32.0f;
        for (std::size_t i = 0; i < allyCount; ++i)
        {
            int neighbors = 0;
            for (std::size_t j = 0; j < allyCount; ++j)
            {
                if (i == j)
                {
                    continue;
                }
                if (lengthSq(sim.yunas[i].pos - sim.yunas[j].pos) <= crowdRadiusSq)
                {
                    ++neighbors;
                    if (neighbors >= 4)
                    {
                        break;
                    }
                }
            }
            yunaAlpha[i] = crowdAlphaForNeighborCount(neighbors);
        }
    }

    std::size_t followerCount = 0;
    for (std::size_t i = 0; i < allyCount; ++i)
    {
        const Unit &yuna = sim.yunas[i];
        if (yuna.effectiveFollower)
        {
            ++followerCount;
        }

        LegacySimulation::RenderQueue::AllySprite allySprite;
        allySprite.position = yuna.pos;
        allySprite.radius = yuna.radius;
        allySprite.commander = false;
        allySprite.job = yuna.job.job;
        allySprite.alpha = yunaAlpha[i];
        allySprite.morale = yuna.moraleState;
        allySprite.temperamentDefinition = yuna.temperament.definition;
        allySprite.temperamentBehavior = yuna.temperament.currentBehavior;
        allySprite.temperamentMimicActive = yuna.temperament.mimicActive;
        allySprite.temperamentMimicBehavior = yuna.temperament.mimicBehavior;
        allySprite.unitIndex = i;
        allySprite.hasUnitIndex = true;
        queue.allies.push_back(allySprite);

        LegacySimulation::RenderQueue::MoraleIcon icon;
        icon.position = yuna.pos;
        icon.radius = yuna.radius;
        icon.state = yuna.moraleState;
        icon.commander = false;
        icon.unitIndex = i;
        queue.moraleIcons.push_back(icon);
    }

    std::sort(queue.allies.begin(), queue.allies.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.position.y == rhs.position.y)
        {
            return lhs.position.x < rhs.position.x;
        }
        return lhs.position.y < rhs.position.y;
    });

    queue.enemies.clear();
    queue.enemies.reserve(enemyCount);
    for (const EnemyUnit &enemy : sim.enemies)
    {
        LegacySimulation::RenderQueue::EnemySprite sprite;
        sprite.position = enemy.pos;
        sprite.radius = enemy.radius;
        sprite.type = enemy.type;
        queue.enemies.push_back(sprite);
    }
    std::sort(queue.enemies.begin(), queue.enemies.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.position.y == rhs.position.y)
        {
            return lhs.position.x < rhs.position.x;
        }
        return lhs.position.y < rhs.position.y;
    });

    queue.walls.clear();
    queue.walls.reserve(sim.walls.size());
    for (const WallSegment &wall : sim.walls)
    {
        LegacySimulation::RenderQueue::WallSprite sprite;
        sprite.position = wall.pos;
        sprite.radius = wall.radius;
        queue.walls.push_back(sprite);
    }

    queue.alignment = {};
    if (context.hud.alignment.active)
    {
        queue.alignment.active = true;
        queue.alignment.secondsRemaining = std::max(context.hud.alignment.secondsRemaining, 0.0f);
        queue.alignment.progress = std::clamp(context.hud.alignment.progress, 0.0f, 1.0f);
        queue.alignment.followers = context.hud.alignment.followers;
        queue.alignment.label = context.hud.alignment.label.empty() ? formationLabel(sim.formation)
                                                                   : context.hud.alignment.label;
    }
    else if (sim.formationAlignTimer > 0.0f)
    {
        queue.alignment.active = true;
        queue.alignment.secondsRemaining = sim.formationAlignTimer;
        queue.alignment.followers = followerCount;
        queue.alignment.progress = 0.0f;
        if (sim.formationDefaults.duration > 0.0f)
        {
            const float clamped = std::clamp(sim.formationDefaults.duration - sim.formationAlignTimer, 0.0f,
                                             sim.formationDefaults.duration);
            queue.alignment.progress = clamped / sim.formationDefaults.duration;
        }
        queue.alignment.label = formationLabel(sim.formation);
    }
}

} // namespace world::systems


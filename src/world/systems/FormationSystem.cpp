#include "world/systems/FormationSystem.h"

#include "events/EventBus.h"
#include "telemetry/TelemetrySink.h"
#include "world/FormationUtils.h"

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace world::systems
{

namespace
{

constexpr std::size_t kFollowLimit = 30;
constexpr float kFollowerSnapDistSq = 16.0f;

float computeAlignmentProgress(const CommanderUnit &commander, const std::vector<Unit *> &followers)
{
    if (!commander.alive || followers.empty())
    {
        return 0.0f;
    }
    const float snapDist = std::sqrt(kFollowerSnapDistSq);
    if (snapDist <= 0.0f)
    {
        return 0.0f;
    }
    float total = 0.0f;
    for (const Unit *unit : followers)
    {
        if (!unit)
        {
            continue;
        }
        const Vec2 desired = commander.pos + unit->formationOffset;
        const float dist = length(desired - unit->pos);
        const float normalized = std::clamp(1.0f - dist / snapDist, 0.0f, 1.0f);
        total += normalized;
    }
    return followers.empty() ? 0.0f : std::clamp(total / static_cast<float>(followers.size()), 0.0f, 1.0f);
}

} // namespace

FormationSystem::FormationSystem()
    : m_state(FormationAlignmentState::Idle),
      m_progress(0.0f),
      m_lastFormation(Formation::Swarm),
      m_lastProgressSent(-1.0f),
      m_lastStateSent(FormationAlignmentState::Idle),
      m_lastFollowerCount(0)
{
}

void FormationSystem::setEventBus(std::weak_ptr<EventBus> bus)
{
    m_eventBus = std::move(bus);
}

void FormationSystem::setTelemetrySink(std::weak_ptr<TelemetrySink> sink)
{
    m_telemetry = std::move(sink);
}

void FormationSystem::cycleFormation(int direction, LegacySimulation &simulation)
{
    static const std::array<Formation, 4> order{Formation::Swarm, Formation::Wedge, Formation::Line, Formation::Ring};
    auto it = std::find(order.begin(), order.end(), simulation.formation);
    if (it == order.end())
    {
        simulation.formation = Formation::Swarm;
    }
    else
    {
        int index = static_cast<int>(std::distance(order.begin(), it));
        index = (index + direction + static_cast<int>(order.size())) % static_cast<int>(order.size());
        simulation.formation = order[static_cast<std::size_t>(index)];
    }
    m_lastFormation = simulation.formation;
    m_state = FormationAlignmentState::Aligning;
    m_progress = 0.0f;
    emitFormationChanged(simulation.formation);
    emitFormationProgress(simulation.formation, m_state, m_progress, 0);
}

void FormationSystem::issueOrder(ArmyStance stance, LegacySimulation &simulation)
{
    simulation.stance = stance;
    simulation.orderActive = true;
    simulation.orderTimer = simulation.orderDuration;

    const std::string message = std::string("Order: ") + stanceLabel(simulation.stance);
    if (auto bus = m_eventBus.lock())
    {
        EventContext ctx;
        ctx.payload = message;
        bus->dispatch("hud.telemetry", ctx);
    }
    if (auto telemetry = m_telemetry.lock())
    {
        TelemetrySink::Payload payload{{"type", "order"}, {"label", stanceLabel(simulation.stance)}};
        telemetry->recordEvent("hud.telemetry", payload);
    }
    simulation.hud.telemetryText = message;
    simulation.hud.telemetryTimer = simulation.config.telemetry_duration;
}

void FormationSystem::reset(const LegacySimulation &simulation)
{
    m_state = FormationAlignmentState::Idle;
    m_progress = 0.0f;
    m_lastFormation = simulation.formation;
    m_lastProgressSent = -1.0f;
    m_lastStateSent = FormationAlignmentState::Idle;
    m_lastFollowerCount = 0;
}

void FormationSystem::update(float, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    auto &yunas = context.yunaUnits;

    std::vector<Unit *> followers;
    followers.reserve(kFollowLimit);

    for (Unit &unit : yunas)
    {
        unit.followByStance = false;
        unit.effectiveFollower = false;
    }

    if (context.orderActive && sim.stance == ArmyStance::FollowLeader && commander.alive)
    {
        std::vector<std::pair<float, Unit *>> distances;
        distances.reserve(yunas.size());
        for (Unit &unit : yunas)
        {
            distances.emplace_back(lengthSq(unit.pos - commander.pos), &unit);
        }
        std::sort(distances.begin(), distances.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        const std::size_t take = std::min<std::size_t>(kFollowLimit, distances.size());
        for (std::size_t i = 0; i < take; ++i)
        {
            distances[i].second->followByStance = true;
        }
    }

    std::vector<std::pair<float, Unit *>> skillFollowers;
    skillFollowers.reserve(yunas.size());
    for (Unit &unit : yunas)
    {
        if (unit.followBySkill)
        {
            skillFollowers.emplace_back(lengthSq(unit.pos - commander.pos), &unit);
        }
    }
    std::sort(skillFollowers.begin(), skillFollowers.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    for (auto &entry : skillFollowers)
    {
        if (followers.size() >= kFollowLimit)
        {
            break;
        }
        entry.second->effectiveFollower = true;
        followers.push_back(entry.second);
    }
    if (followers.size() < kFollowLimit)
    {
        for (Unit &unit : yunas)
        {
            if (followers.size() >= kFollowLimit)
            {
                break;
            }
            if (!unit.followBySkill && unit.followByStance)
            {
                unit.effectiveFollower = true;
                followers.push_back(&unit);
            }
        }
    }

    const auto formationOffsets = computeFormationOffsets(sim.formation, followers.size());
    for (std::size_t i = 0; i < followers.size(); ++i)
    {
        followers[i]->formationOffset = formationOffsets[i];
    }

    if (!commander.alive || followers.empty())
    {
        m_state = FormationAlignmentState::Idle;
        m_progress = 0.0f;
    }
    else
    {
        m_progress = computeAlignmentProgress(commander, followers);
        m_state = m_progress >= 0.99f ? FormationAlignmentState::Locked : FormationAlignmentState::Aligning;
    }

    if (m_lastFormation != sim.formation)
    {
        m_lastFormation = sim.formation;
        m_lastProgressSent = -1.0f;
    }

    const bool stateChanged = m_state != m_lastStateSent;
    const bool progressChanged = std::fabs(m_progress - m_lastProgressSent) > 0.01f;
    const bool followerChanged = followers.size() != m_lastFollowerCount;
    if (stateChanged || progressChanged || followerChanged)
    {
        emitFormationProgress(sim.formation, m_state, m_progress, followers.size());
        m_lastProgressSent = m_progress;
        m_lastStateSent = m_state;
        m_lastFollowerCount = followers.size();
    }
}

void FormationSystem::emitFormationChanged(Formation formation)
{
    FormationChangedEvent payload{formation, formationLabel(formation)};
    if (auto bus = m_eventBus.lock())
    {
        EventContext ctx;
        ctx.payload = payload;
        bus->dispatch(FormationChangedEventName, ctx);
    }
    if (auto telemetry = m_telemetry.lock())
    {
        TelemetrySink::Payload data{{"type", "formation"}, {"label", payload.label}};
        telemetry->recordEvent("hud.telemetry", data);
    }
}

void FormationSystem::emitFormationProgress(Formation formation, FormationAlignmentState state, float progress,
                                            std::size_t followers)
{
    FormationProgressEvent payload{formation, state, std::clamp(progress, 0.0f, 1.0f), followers};
    if (auto bus = m_eventBus.lock())
    {
        EventContext ctx;
        ctx.payload = payload;
        bus->dispatch(FormationProgressEventName, ctx);
    }
}

} // namespace world::systems


#include "world/systems/FormationSystem.h"

#include "events/EventBus.h"
#include "telemetry/TelemetrySink.h"
#include "world/FormationUtils.h"

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <type_traits>

namespace world::systems
{

namespace
{

constexpr std::size_t kFollowLimit = 30;
constexpr float kFollowerSnapDistSq = 16.0f;

template <typename Container>
float computeAlignmentProgress(const CommanderUnit &commander, const Container &followers)
{
    static_assert(std::is_same_v<typename Container::value_type, Unit *>,
                  "followers container must hold Unit pointers");
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

const char *alignmentStateLabel(FormationAlignmentState state)
{
    switch (state)
    {
    case FormationAlignmentState::Idle:
        return "idle";
    case FormationAlignmentState::Aligning:
        return "aligning";
    case FormationAlignmentState::Locked:
        return "locked";
    }
    return "unknown";
}

} // namespace

FormationSystem::FormationSystem()
    : m_state(FormationAlignmentState::Idle),
      m_progress(0.0f),
      m_lastFormation(Formation::Swarm),
      m_lastProgressSent(-1.0f),
      m_lastStateSent(FormationAlignmentState::Idle),
      m_lastFollowerCount(0),
      m_lastSecondsRemaining(-1.0f),
      m_lastCountdownActive(false),
      m_lastCountdownSeconds(-1.0f),
      m_lastCountdownProgress(-1.0f),
      m_lastCountdownFollowers(0)
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
    simulation.formationAlignTimer = std::max(0.0f, simulation.formationDefaults.alignDuration);
    simulation.formationDefenseMul =
        simulation.formationDefaults.defenseMultiplier > 0.0f ? simulation.formationDefaults.defenseMultiplier : 1.0f;
    if (simulation.formationAlignTimer <= 0.0f)
    {
        simulation.formationDefenseMul = 1.0f;
    }
    m_lastSecondsRemaining = simulation.formationAlignTimer;
    emitFormationChanged(simulation.formation);
    emitFormationProgress(simulation.formation, m_state, m_progress, simulation.formationAlignTimer, 0);
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
    m_lastSecondsRemaining = simulation.formationAlignTimer;
    m_lastCountdownActive = false;
    m_lastCountdownSeconds = simulation.formationAlignTimer;
    m_lastCountdownProgress = 0.0f;
    m_lastCountdownFollowers = 0;
    m_lastCountdownLabel.clear();
}

void FormationSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    auto &yunas = context.yunaUnits;

    if (sim.formationAlignTimer > 0.0f)
    {
        sim.formationAlignTimer = std::max(0.0f, sim.formationAlignTimer - dt);
        if (sim.formationAlignTimer <= 0.0f)
        {
            sim.formationDefenseMul = 1.0f;
        }
    }
    const float secondsRemaining = std::max(sim.formationAlignTimer, 0.0f);

    FrameAllocator::Allocator<Unit *> followerAlloc(context.frameAllocator);
    std::vector<Unit *, FrameAllocator::Allocator<Unit *>> followers(followerAlloc);
    followers.reserve(kFollowLimit);

    for (Unit &unit : yunas)
    {
        unit.followByStance = false;
        unit.effectiveFollower = false;
    }

    if (context.orderActive && sim.stance == ArmyStance::FollowLeader && commander.alive)
    {
        using DistanceEntry = std::pair<float, Unit *>;
        FrameAllocator::Allocator<DistanceEntry> distanceAlloc(context.frameAllocator);
        std::vector<DistanceEntry, FrameAllocator::Allocator<DistanceEntry>> distances(distanceAlloc);
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

    FrameAllocator::Allocator<std::pair<float, Unit *>> skillAlloc(context.frameAllocator);
    std::vector<std::pair<float, Unit *>, FrameAllocator::Allocator<std::pair<float, Unit *>>> skillFollowers(
        skillAlloc);
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
        m_progress = 0.0f;
        m_state = secondsRemaining > 0.0f ? FormationAlignmentState::Aligning : FormationAlignmentState::Idle;
    }
    else
    {
        m_progress = computeAlignmentProgress(commander, followers);
        if (secondsRemaining > 0.0f)
        {
            m_state = FormationAlignmentState::Aligning;
        }
        else
        {
            m_state = m_progress >= 0.99f ? FormationAlignmentState::Locked : FormationAlignmentState::Aligning;
        }
    }

    if (m_lastFormation != sim.formation)
    {
        m_lastFormation = sim.formation;
        m_lastProgressSent = -1.0f;
    }

    const bool stateChanged = m_state != m_lastStateSent;
    const bool progressChanged = std::fabs(m_progress - m_lastProgressSent) > 0.01f;
    const bool followerChanged = followers.size() != m_lastFollowerCount;
    const bool timerChanged = std::fabs(secondsRemaining - m_lastSecondsRemaining) > 0.01f;
    if (stateChanged || progressChanged || followerChanged || timerChanged)
    {
        emitFormationProgress(sim.formation, m_state, m_progress, secondsRemaining, followers.size());
        m_lastProgressSent = m_progress;
        m_lastStateSent = m_state;
        m_lastFollowerCount = followers.size();
        m_lastSecondsRemaining = secondsRemaining;
    }

    context.requestComponentSync();
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
                                            float secondsRemaining, std::size_t followers)
{
    FormationProgressEvent payload;
    payload.formation = formation;
    payload.state = state;
    payload.progress = std::clamp(progress, 0.0f, 1.0f);
    payload.secondsRemaining = std::max(secondsRemaining, 0.0f);
    payload.followers = followers;
    const std::string label = formationLabel(formation);
    if (auto bus = m_eventBus.lock())
    {
        EventContext ctx;
        ctx.payload = payload;
        bus->dispatch(FormationProgressEventName, ctx);
    }
    if (auto telemetry = m_telemetry.lock())
    {
        TelemetrySink::Payload data{{"formation", label}, {"state", alignmentStateLabel(state)}};
        std::ostringstream progressStream;
        progressStream << std::fixed << std::setprecision(2) << payload.progress;
        data.emplace("progress", progressStream.str());
        std::ostringstream timerStream;
        timerStream << std::fixed << std::setprecision(2) << payload.secondsRemaining;
        data.emplace("remaining_s", timerStream.str());
        data.emplace("followers", std::to_string(payload.followers));
        telemetry->recordEvent("formation.progress", data);
    }

    const bool active = state == FormationAlignmentState::Aligning && payload.secondsRemaining > 0.0f;
    emitFormationCountdown(formation, active, payload.secondsRemaining, payload.progress, payload.followers, label);
}

void FormationSystem::emitFormationCountdown(Formation formation, bool active, float secondsRemaining, float progress,
                                             std::size_t followers, const std::string &label)
{
    (void)formation;
    const float clampedSeconds = std::max(secondsRemaining, 0.0f);
    const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
    const bool changed = m_lastCountdownActive != active ||
                         std::fabs(m_lastCountdownSeconds - clampedSeconds) > 0.05f ||
                         std::fabs(m_lastCountdownProgress - clampedProgress) > 0.01f ||
                         m_lastCountdownFollowers != followers ||
                         m_lastCountdownLabel != label;
    if (!changed)
    {
        return;
    }

    m_lastCountdownActive = active;
    m_lastCountdownSeconds = clampedSeconds;
    m_lastCountdownProgress = clampedProgress;
    m_lastCountdownFollowers = followers;
    m_lastCountdownLabel = label;

    FormationCountdownEvent countdown;
    countdown.active = active;
    countdown.label = label;
    countdown.secondsRemaining = clampedSeconds;
    countdown.progress = clampedProgress;
    countdown.followers = followers;

    if (auto bus = m_eventBus.lock())
    {
        EventContext ctx;
        ctx.payload = countdown;
        bus->dispatch(FormationCountdownEventName, ctx);
    }
    if (auto telemetry = m_telemetry.lock())
    {
        TelemetrySink::Payload payload{{"formation", label}, {"active", active ? "true" : "false"}};
        std::ostringstream timerStream;
        timerStream << std::fixed << std::setprecision(2) << clampedSeconds;
        payload.emplace("remaining_s", timerStream.str());
        std::ostringstream progressStream;
        progressStream << std::fixed << std::setprecision(2) << clampedProgress;
        payload.emplace("progress", progressStream.str());
        payload.emplace("followers", std::to_string(followers));
        telemetry->recordEvent("hud.alignment", payload);
    }
}

} // namespace world::systems

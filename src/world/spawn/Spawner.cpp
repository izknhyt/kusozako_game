#include "world/spawn/Spawner.h"

#include <algorithm>
#include <utility>

namespace world::spawn
{

Spawner::Spawner() = default;

void Spawner::setBudget(const SpawnBudget &budget)
{
    m_budget = budget;
    if (m_budget.maxPerFrame < 0)
    {
        m_budget.maxPerFrame = 0;
    }
}

void Spawner::setGateChecks(GateCheck disabledCheck, GateCheck destroyedCheck)
{
    m_disabledCheck = std::move(disabledCheck);
    m_destroyedCheck = std::move(destroyedCheck);
}

void Spawner::setIntervalModifier(IntervalModifier modifier)
{
    m_intervalModifier = std::move(modifier);
}

void Spawner::clear()
{
    m_queues.clear();
    m_indexByGate.clear();
}

Spawner::GateQueue &Spawner::ensureQueue(const std::string &gateId)
{
    const auto found = m_indexByGate.find(gateId);
    if (found != m_indexByGate.end())
    {
        return m_queues[found->second];
    }

    GateQueue queue;
    queue.gateId = gateId;
    m_queues.push_back(queue);
    m_indexByGate[gateId] = m_queues.size() - 1;
    return m_queues.back();
}

void Spawner::enqueue(const SpawnRequest &request)
{
    if (request.count <= 0)
    {
        return;
    }

    GateQueue &queue = ensureQueue(request.gateId);
    ActiveSpawn spawn;
    spawn.position = request.position;
    spawn.type = request.type;
    spawn.remaining = request.count;
    spawn.interval = request.interval;
    spawn.timer = 0.0f;
    queue.spawns.push_back(spawn);
}

bool Spawner::empty() const
{
    for (const GateQueue &queue : m_queues)
    {
        if (!queue.spawns.empty())
        {
            return false;
        }
    }
    return true;
}

Spawner::EmitResult Spawner::emit(float dt, const SpawnCallback &callback)
{
    EmitResult result;
    if (!callback)
    {
        return result;
    }

    bool budgetHit = false;
    for (GateQueue &queue : m_queues)
    {
        const bool destroyed = m_destroyedCheck && m_destroyedCheck(queue.gateId);
        if (destroyed)
        {
            queue.spawns.clear();
            continue;
        }

        const bool disabled = m_disabledCheck && m_disabledCheck(queue.gateId);
        for (ActiveSpawn &spawn : queue.spawns)
        {
            if (spawn.remaining <= 0)
            {
                continue;
            }

            if (disabled)
            {
                spawn.timer = std::max(spawn.timer, 0.0f);
                continue;
            }

            spawn.timer -= dt;
            while (spawn.remaining > 0 && spawn.timer <= 0.0f)
            {
                if (m_budget.maxPerFrame > 0 && result.emitted >= m_budget.maxPerFrame)
                {
                    spawn.timer = 0.0f;
                    budgetHit = true;
                    break;
                }

                SpawnPayload payload{queue.gateId, spawn.position, spawn.type};
                callback(payload);
                ++result.emitted;
                --spawn.remaining;

                if (spawn.remaining <= 0)
                {
                    spawn.timer = 0.0f;
                    break;
                }

                float nextInterval = spawn.interval;
                if (m_intervalModifier)
                {
                    const float modified = m_intervalModifier(nextInterval);
                    if (modified > 0.0f)
                    {
                        nextInterval = modified;
                    }
                }
                spawn.timer += nextInterval;
            }

            if (budgetHit)
            {
                break;
            }
        }

        queue.spawns.erase(
            std::remove_if(queue.spawns.begin(), queue.spawns.end(), [](const ActiveSpawn &s) { return s.remaining <= 0; }),
            queue.spawns.end());

        if (budgetHit)
        {
            break;
        }
    }

    for (std::size_t i = 0; i < m_queues.size();)
    {
        GateQueue &queue = m_queues[i];
        const bool destroyed = m_destroyedCheck && m_destroyedCheck(queue.gateId);
        if (destroyed || queue.spawns.empty())
        {
            m_indexByGate.erase(queue.gateId);
            if (i + 1 < m_queues.size())
            {
                std::swap(m_queues[i], m_queues.back());
                m_indexByGate[m_queues[i].gateId] = i;
            }
            m_queues.pop_back();
            continue;
        }
        ++i;
    }

    if (budgetHit)
    {
        for (const GateQueue &queue : m_queues)
        {
            const bool destroyed = m_destroyedCheck && m_destroyedCheck(queue.gateId);
            if (destroyed)
            {
                continue;
            }
            const bool disabled = m_disabledCheck && m_disabledCheck(queue.gateId);
            if (disabled)
            {
                continue;
            }
            for (const ActiveSpawn &spawn : queue.spawns)
            {
                if (spawn.remaining > 0 && spawn.timer <= 0.0f)
                {
                    result.deferred += spawn.remaining;
                }
            }
        }
    }

    return result;
}

} // namespace world::spawn


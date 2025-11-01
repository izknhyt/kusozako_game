#include "world/spawn/WaveController.h"

#include <utility>

#include "events/EventBus.h"
#include "telemetry/TelemetrySink.h"

namespace world::spawn
{
namespace
{
Vec2 tileToWorld(const Vec2 &tile, int tileSize)
{
    return {tile.x * tileSize + tileSize * 0.5f, tile.y * tileSize + tileSize * 0.5f};
}
} // namespace

WaveController::WaveController() = default;

void WaveController::setSpawner(Spawner *spawner)
{
    m_spawner = spawner;
}

void WaveController::setEventBus(std::shared_ptr<EventBus> bus)
{
    m_eventBus = bus;
}

void WaveController::setTelemetrySink(std::shared_ptr<TelemetrySink> sink)
{
    m_telemetry = sink;
}

void WaveController::setSpawnScript(const SpawnScript &script, const MapDefs &map)
{
    m_script = script;
    m_map = map;
    reset();
}

void WaveController::reset()
{
    m_nextWave = 0;
    m_history.clear();
}

std::vector<std::string> WaveController::advance(float currentTime)
{
    std::vector<std::string> announcements;
    if (!m_spawner)
    {
        return announcements;
    }

    while (m_nextWave < m_script.waves.size() && currentTime >= m_script.waves[m_nextWave].time)
    {
        const Wave &wave = m_script.waves[m_nextWave];
        for (const SpawnSet &set : wave.sets)
        {
            auto worldPos = resolveGateWorld(set.gate);
            if (!worldPos)
            {
                continue;
            }

            SpawnRequest request;
            request.gateId = set.gate;
            request.position = *worldPos;
            request.type = set.type;
            request.count = set.count;
            request.interval = set.interval;
            m_spawner->enqueue(request);
        }

        if (!wave.telemetry.empty())
        {
            announcements.push_back(wave.telemetry);
        }
        recordHistory(m_nextWave, wave, currentTime);
        notifyWave(m_nextWave, wave);
        ++m_nextWave;
    }

    return announcements;
}

bool WaveController::triggerNextWave(float currentTime, std::vector<std::string> &announcements)
{
    if (!m_spawner || m_nextWave >= m_script.waves.size())
    {
        return false;
    }

    const Wave &wave = m_script.waves[m_nextWave];
    for (const SpawnSet &set : wave.sets)
    {
        auto worldPos = resolveGateWorld(set.gate);
        if (!worldPos)
        {
            continue;
        }

        SpawnRequest request;
        request.gateId = set.gate;
        request.position = *worldPos;
        request.type = set.type;
        request.count = set.count;
        request.interval = set.interval;
        m_spawner->enqueue(request);
    }

    if (!wave.telemetry.empty())
    {
        announcements.push_back(wave.telemetry);
    }

    recordHistory(m_nextWave, wave, currentTime);
    notifyWave(m_nextWave, wave);
    ++m_nextWave;
    return true;
}

bool WaveController::isComplete() const
{
    return m_nextWave >= m_script.waves.size();
}

std::optional<Vec2> WaveController::resolveGateWorld(const std::string &gateId) const
{
    if (gateId.empty())
    {
        return std::nullopt;
    }

    if (const auto scriptGate = m_script.gate_tiles.find(gateId); scriptGate != m_script.gate_tiles.end())
    {
        return tileToWorld(scriptGate->second, m_map.tile_size);
    }
    if (const auto mapGate = m_map.gate_tiles.find(gateId); mapGate != m_map.gate_tiles.end())
    {
        return tileToWorld(mapGate->second, m_map.tile_size);
    }
    return std::nullopt;
}

void WaveController::notifyWave(std::size_t index, const Wave &wave) const
{
    if (auto bus = m_eventBus.lock())
    {
        EventContext context;
        context.payload = WaveEvent{index, wave.time, wave.telemetry};
        bus->dispatch("world.wave.started", context);
    }

    if (auto telemetry = m_telemetry.lock())
    {
        TelemetrySink::Payload payload{{"wave", std::to_string(index)}, {"time", std::to_string(wave.time)}};
        if (!wave.telemetry.empty())
        {
            payload.emplace("telemetry", wave.telemetry);
        }
        telemetry->recordEvent("world.wave.started", payload);
    }
}

std::vector<WaveController::WaveHistoryEntry> WaveController::historySnapshot() const
{
    return std::vector<WaveHistoryEntry>(m_history.begin(), m_history.end());
}

void WaveController::setHistoryLimit(std::size_t limit)
{
    m_historyLimit = limit == 0 ? 0 : limit;
    if (m_historyLimit == 0)
    {
        m_history.clear();
        return;
    }
    while (m_history.size() > m_historyLimit)
    {
        m_history.pop_front();
    }
}

void WaveController::recordHistory(std::size_t index, const Wave &wave, float triggerTime)
{
    if (m_historyLimit == 0)
    {
        return;
    }

    WaveHistoryEntry entry;
    entry.index = index;
    entry.scheduledTime = wave.time;
    entry.triggerTime = triggerTime;
    entry.wallClock = std::chrono::system_clock::now();
    entry.telemetry = wave.telemetry;
    entry.sets = wave.sets;
    m_history.push_back(std::move(entry));
    while (m_history.size() > m_historyLimit)
    {
        m_history.pop_front();
    }
}

} // namespace world::spawn

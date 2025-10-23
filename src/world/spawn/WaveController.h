#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/AppConfig.h"
#include "core/Vec2.h"
#include "world/spawn/Spawner.h"

class EventBus;
class TelemetrySink;

namespace world::spawn
{

struct WaveEvent
{
    std::size_t index = 0;
    float time = 0.0f;
    std::string telemetry;
};

class WaveController
{
  public:
    WaveController();

    void setSpawner(Spawner *spawner);
    void setEventBus(std::shared_ptr<EventBus> bus);
    void setTelemetrySink(std::shared_ptr<TelemetrySink> sink);

    void setSpawnScript(const SpawnScript &script, const MapDefs &map);
    void reset();

    std::vector<std::string> advance(float currentTime);

    bool isComplete() const;

  private:
    std::optional<Vec2> resolveGateWorld(const std::string &gateId) const;
    void notifyWave(std::size_t index, const Wave &wave) const;

    Spawner *m_spawner = nullptr;
    SpawnScript m_script;
    MapDefs m_map;
    std::size_t m_nextWave = 0;
    std::weak_ptr<EventBus> m_eventBus;
    std::weak_ptr<TelemetrySink> m_telemetry;
};

} // namespace world::spawn


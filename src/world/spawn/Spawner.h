#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/AppConfig.h"
#include "core/Vec2.h"

namespace world::spawn
{

struct SpawnRequest
{
    std::string gateId;
    Vec2 position{};
    EnemyArchetype type = EnemyArchetype::Slime;
    int count = 0;
    float interval = 0.3f;
};

struct SpawnPayload
{
    std::string gateId;
    Vec2 position{};
    EnemyArchetype type = EnemyArchetype::Slime;
};

struct SpawnBudget
{
    int maxPerFrame = 8;
};

class Spawner
{
  public:
    using GateCheck = std::function<bool(const std::string &)>;
    using SpawnCallback = std::function<void(const SpawnPayload &)>;
    using IntervalModifier = std::function<float(float)>;

    struct EmitResult
    {
        int emitted = 0;
        int deferred = 0;
    };

    Spawner();

    void setBudget(const SpawnBudget &budget);
    void setGateChecks(GateCheck disabledCheck, GateCheck destroyedCheck);
    void setIntervalModifier(IntervalModifier modifier);

    void clear();
    void enqueue(const SpawnRequest &request);
    EmitResult emit(float dt, const SpawnCallback &callback);

    bool empty() const;

  private:
    struct ActiveSpawn
    {
        Vec2 position{};
        EnemyArchetype type = EnemyArchetype::Slime;
        int remaining = 0;
        float interval = 0.3f;
        float timer = 0.0f;
    };

    struct GateQueue
    {
        std::string gateId;
        std::vector<ActiveSpawn> spawns;
    };

    GateQueue &ensureQueue(const std::string &gateId);

    SpawnBudget m_budget{};
    GateCheck m_disabledCheck;
    GateCheck m_destroyedCheck;
    IntervalModifier m_intervalModifier;
    std::vector<GateQueue> m_queues;
    std::unordered_map<std::string, std::size_t> m_indexByGate;
};

} // namespace world::spawn


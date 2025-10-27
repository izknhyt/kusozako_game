#include "world/spawn/Spawner.h"

#include <iostream>
#include <string>
#include <vector>

using world::spawn::SpawnBudget;
using world::spawn::SpawnPayload;
using world::spawn::SpawnRequest;
using world::spawn::Spawner;

namespace
{

bool testSingleQueueDeferral()
{
    Spawner spawner;
    SpawnBudget budget;
    budget.maxPerFrame = 2;
    spawner.setBudget(budget);

    SpawnRequest request;
    request.gateId = "gate_a";
    request.count = 3;
    request.interval = 0.0f;
    spawner.enqueue(request);

    std::vector<SpawnPayload> emitted;
    const auto callback = [&emitted](const SpawnPayload &payload) { emitted.push_back(payload); };

    auto result = spawner.emit(0.0f, callback);
    if (result.emitted != 2)
    {
        std::cerr << "Expected 2 spawns in first frame, got " << result.emitted << '\n';
        return false;
    }
    if (result.deferred != 1)
    {
        std::cerr << "Expected 1 deferred spawn in first frame, got " << result.deferred << '\n';
        return false;
    }

    result = spawner.emit(0.0f, callback);
    if (result.emitted != 1)
    {
        std::cerr << "Expected 1 spawn in second frame, got " << result.emitted << '\n';
        return false;
    }
    if (result.deferred != 0)
    {
        std::cerr << "Expected 0 deferred spawns in second frame, got " << result.deferred << '\n';
        return false;
    }

    if (emitted.size() != 3)
    {
        std::cerr << "Expected total of 3 spawns, got " << emitted.size() << '\n';
        return false;
    }

    return true;
}

bool testMultiGateDeferral()
{
    Spawner spawner;
    SpawnBudget budget;
    budget.maxPerFrame = 2;
    spawner.setBudget(budget);

    SpawnRequest gateA;
    gateA.gateId = "gate_a";
    gateA.count = 2;
    gateA.interval = 0.0f;
    spawner.enqueue(gateA);

    SpawnRequest gateB;
    gateB.gateId = "gate_b";
    gateB.count = 2;
    gateB.interval = 0.0f;
    spawner.enqueue(gateB);

    std::vector<SpawnPayload> emitted;
    const auto callback = [&emitted](const SpawnPayload &payload) { emitted.push_back(payload); };

    auto result = spawner.emit(0.0f, callback);
    if (result.emitted != 2)
    {
        std::cerr << "Expected 2 spawns across gates in first frame, got " << result.emitted << '\n';
        return false;
    }
    if (result.deferred != 2)
    {
        std::cerr << "Expected 2 deferred spawns across gates, got " << result.deferred << '\n';
        return false;
    }

    result = spawner.emit(0.0f, callback);
    if (result.emitted != 2)
    {
        std::cerr << "Expected remaining 2 spawns in second frame, got " << result.emitted << '\n';
        return false;
    }
    if (result.deferred != 0)
    {
        std::cerr << "Expected 0 deferred spawns in second frame of multi-gate test, got " << result.deferred << '\n';
        return false;
    }

    if (emitted.size() != 4)
    {
        std::cerr << "Expected total of 4 spawns across gates, got " << emitted.size() << '\n';
        return false;
    }

    return true;
}

} // namespace

int main()
{
    if (!testSingleQueueDeferral())
    {
        return 1;
    }
    if (!testMultiGateDeferral())
    {
        return 1;
    }

    std::cout << "Spawner tests passed" << std::endl;
    return 0;
}


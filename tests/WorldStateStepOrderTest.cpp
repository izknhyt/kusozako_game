#include "TestSystem.h"

#include "input/ActionBuffer.h"
#include "world/WorldState.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{

using Stage = world::systems::SystemStage;

void reportMismatch(const char *label,
                    const std::vector<Stage> &expected,
                    const std::vector<Stage> &actual)
{
    std::cerr << label << " mismatch. Expected:";
    for (Stage stage : expected)
    {
        std::cerr << ' ' << static_cast<int>(stage);
    }
    std::cerr << " | Actual:";
    for (Stage stage : actual)
    {
        std::cerr << ' ' << static_cast<int>(stage);
    }
    std::cerr << '\n';
}

} // namespace

int main()
{
    bool success = true;

    {
        world::WorldState world;
        const std::vector<Stage> expectedDefault{
            Stage::InputProcessing,
            Stage::CommandAndMorale,
            Stage::CommandAndMorale,
            Stage::AiDecision,
            Stage::Movement,
            Stage::Combat,
            Stage::StateUpdate,
            Stage::Spawn,
            Stage::RenderingPrep,
        };
        if (world.systemStageOrder() != expectedDefault)
        {
            reportMismatch("Default stage order", expectedDefault, world.systemStageOrder());
            success = false;
        }
    }

    {
        world::WorldState world;
        world.clearSystems();
        std::vector<Stage> log;
        world.registerSystem(Stage::AiDecision, std::make_unique<TestSystem>(&log, Stage::AiDecision));
        bool threw = false;
        try
        {
            world.registerSystem(Stage::CommandAndMorale, std::make_unique<TestSystem>(&log, Stage::CommandAndMorale));
        }
        catch (const std::logic_error &)
        {
            threw = true;
        }
        if (!threw)
        {
            std::cerr << "Expected stage order violation to throw" << '\n';
            success = false;
        }
    }

    {
        world::WorldState world;
        world.clearSystems();
        std::vector<Stage> callLog;
        auto registerLogger = [&](Stage stage) {
            world.registerSystem(stage, std::make_unique<TestSystem>(&callLog, stage));
        };

        registerLogger(Stage::InputProcessing);
        registerLogger(Stage::CommandAndMorale);
        registerLogger(Stage::CommandAndMorale);
        registerLogger(Stage::AiDecision);
        registerLogger(Stage::Movement);
        registerLogger(Stage::Combat);
        registerLogger(Stage::StateUpdate);
        registerLogger(Stage::Spawn);
        registerLogger(Stage::RenderingPrep);

        ActionBuffer actions;
        world.step(0.016f, actions);

        const std::vector<Stage> expectedOrder{
            Stage::InputProcessing,
            Stage::CommandAndMorale,
            Stage::CommandAndMorale,
            Stage::AiDecision,
            Stage::Movement,
            Stage::Combat,
            Stage::StateUpdate,
            Stage::Spawn,
            Stage::RenderingPrep,
        };

        if (callLog != expectedOrder)
        {
            reportMismatch("Stage invocation order", expectedOrder, callLog);
            success = false;
        }

        if (world.systemStageOrder() != expectedOrder)
        {
            reportMismatch("Configured stage order", expectedOrder, world.systemStageOrder());
            success = false;
        }
    }

    {
        world::WorldState world;
        world::LegacySimulation &sim = world.legacy();
        sim.commander.alive = true;
        sim.commander.pos = {0.0f, 0.0f};
        sim.commanderStats.speed_u_s = 4.0f;
        sim.config.pixels_per_unit = 10.0f;

        ActionBuffer actions;
        std::array<float, static_cast<std::size_t>(AxisId::Count)> axes{};
        axes[static_cast<std::size_t>(AxisId::CommanderMoveX)] = 1.0f;
        axes[static_cast<std::size_t>(AxisId::CommanderMoveY)] = 0.0f;
        actions.pushFrame(1, 0.0, axes, {}, PointerState{});

        const float dt = 0.5f;
        world.step(dt, actions);

        const float expectedCommanderX = sim.commanderStats.speed_u_s * sim.config.pixels_per_unit * dt;
        if (std::fabs(sim.commander.pos.x - expectedCommanderX) > 0.001f ||
            std::fabs(sim.commander.pos.y) > 0.001f)
        {
            std::cerr << "Commander movement mismatch" << '\n';
            success = false;
        }
        if (sim.commander.hasMoveIntent)
        {
            std::cerr << "Commander move intent not cleared" << '\n';
            success = false;
        }
    }

    {
        world::WorldState world;
        world::LegacySimulation &sim = world.legacy();
        sim.commander.alive = true;
        sim.commander.pos = {0.0f, 0.0f};
        sim.yunaStats.speed_u_s = 2.0f;
        sim.config.pixels_per_unit = 5.0f;

        world.markComponentsDirty();

        sim.yunas.clear();
        Unit follower;
        follower.pos = {100.0f, 0.0f};
        follower.radius = 4.0f;
        follower.effectiveFollower = true;
        follower.formationOffset = {0.0f, 0.0f};
        sim.yunas.push_back(follower);

        ActionBuffer actions;
        const float dt = 0.25f;
        world.step(dt, actions);

        const float expectedDelta = sim.yunaStats.speed_u_s * sim.config.pixels_per_unit * dt;
        const float expectedPosX = 100.0f - expectedDelta;
        if (sim.yunas.empty() || std::fabs(sim.yunas.front().pos.x - expectedPosX) > 0.01f)
        {
            std::cerr << "Follower movement mismatch" << '\n';
            success = false;
        }
        if (!sim.yunas.empty() && sim.yunas.front().hasDesiredVelocity)
        {
            std::cerr << "Follower intent not cleared" << '\n';
            success = false;
        }
    }

    return success ? 0 : 1;
}


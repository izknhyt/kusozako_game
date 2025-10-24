#include "TestSystem.h"

#include "services/ActionBuffer.h"
#include "world/WorldState.h"

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
            Stage::CommandAndMorale,
            Stage::CommandAndMorale,
            Stage::AiDecision,
            Stage::Combat,
            Stage::StateUpdate,
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

    return success ? 0 : 1;
}


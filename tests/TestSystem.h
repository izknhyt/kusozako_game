#pragma once

#include "world/systems/SystemContext.h"

#include <vector>

class TestSystem : public world::systems::ISystem
{
  public:
    TestSystem(std::vector<world::systems::SystemStage> *log, world::systems::SystemStage stage)
        : m_log(log), m_stage(stage)
    {
    }

    void update(float, world::systems::SystemContext &) override
    {
        if (m_log)
        {
            m_log->push_back(m_stage);
        }
    }

  private:
    std::vector<world::systems::SystemStage> *m_log;
    world::systems::SystemStage m_stage;
};


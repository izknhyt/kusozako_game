#pragma once

#include "core/Vec2.h"
#include "world/systems/SystemContext.h"

#include <array>
#include <vector>

struct RuntimeSkill;

namespace world::systems
{

struct SkillCommand
{
    int index = -1;
    Vec2 worldTarget{};
};

class JobAbilitySystem : public ISystem
{
  public:
    JobAbilitySystem() = default;

    void update(float dt, SystemContext &context) override;
    void triggerSkill(SystemContext &context, const SkillCommand &command);

  private:
    void toggleRally(SystemContext &context, RuntimeSkill &skill, const SkillCommand &command);
    void activateSpawnRate(SystemContext &context, RuntimeSkill &skill);

    struct JobHudSnapshot
    {
        std::array<std::size_t, UnitJobCount> total{};
        std::array<std::size_t, UnitJobCount> ready{};
        std::array<float, UnitJobCount> maxCooldown{};
        std::array<float, UnitJobCount> maxEndlag{};
        std::array<float, UnitJobCount> specialTimer{};
        std::array<bool, UnitJobCount> specialActive{};
        struct Skill
        {
            float cooldown = 0.0f;
            float active = 0.0f;
            bool toggled = false;
        };
        std::vector<Skill> skills;
    } m_lastHud;
    bool m_hudInitialized = false;
};

} // namespace world::systems


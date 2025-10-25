#pragma once

#include "core/Vec2.h"
#include "world/systems/SystemContext.h"

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
};

} // namespace world::systems


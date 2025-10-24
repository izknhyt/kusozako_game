#pragma once

#include "input/ActionBuffer.h"
#include "world/Entity.h"
#include "world/LegacySimulation.h"
#include "world/systems/SystemContext.h"

#include <memory>
#include <vector>

struct GameConfig;
struct SkillDef;
struct Vec2;
struct TileMap;
struct TemperamentConfig;
struct CommanderStats;
struct EntityStats;
struct WallbreakerStats;
struct MissionConfig;
struct MissionUIState;
struct ActiveSpawn;
struct SurvivalState;
struct BossState;
struct CaptureState;
struct HUDState;
struct SpawnScript;
struct MapDefs;
enum class ArmyStance;

class EventBus;
class TelemetrySink;

namespace world
{

template <typename T>
class ComponentPool;

namespace spawn
{
class WaveController;
class Spawner;
} // namespace spawn

namespace systems
{
class ISystem;
class FormationSystem;
} // namespace systems

class WorldState
{
  public:
    WorldState();
    WorldState(WorldState &&other) noexcept;
    WorldState &operator=(WorldState &&other) noexcept;
    ~WorldState();

    WorldState(const WorldState &) = delete;
    WorldState &operator=(const WorldState &) = delete;

    LegacySimulation &legacy();
    const LegacySimulation &legacy() const;

    void setWorldBounds(float width, float height);
    void configureSkills(const std::vector<SkillDef> &defs);
    void reset();
    void step(float dt, const ActionBuffer &actions);
    void issueOrder(ArmyStance stance);
    void cycleFormation(int direction);
    void selectSkillByHotkey(int hotkey);
    void activateSelectedSkill(const Vec2 &worldPos);

    void setEventBus(std::shared_ptr<EventBus> bus);
    void setTelemetrySink(std::shared_ptr<TelemetrySink> sink);

    bool canRestart() const;

    ComponentPool<Unit> &allies();
    const ComponentPool<Unit> &allies() const;
    ComponentPool<EnemyUnit> &enemies();
    const ComponentPool<EnemyUnit> &enemies() const;
    ComponentPool<WallSegment> &walls();
    const ComponentPool<WallSegment> &walls() const;
    ComponentPool<CaptureRuntime> &missionZones();
    const ComponentPool<CaptureRuntime> &missionZones() const;

    void markComponentsDirty();
    void syncComponents() const;

    void clearSystems();
    void registerSystem(systems::SystemStage stage, std::unique_ptr<systems::ISystem> system);
    const std::vector<systems::SystemStage> &systemStageOrder() const;

  private:
    struct SystemEntry
    {
        systems::SystemStage stage;
        std::unique_ptr<systems::ISystem> system;
    };

    std::unique_ptr<LegacySimulation> m_sim;
    mutable EntityRegistry m_registry;
    mutable std::unique_ptr<ComponentPool<Unit>> m_allies;
    mutable std::unique_ptr<ComponentPool<EnemyUnit>> m_enemies;
    mutable std::unique_ptr<ComponentPool<WallSegment>> m_walls;
    mutable std::unique_ptr<ComponentPool<CaptureRuntime>> m_captureZones;
    mutable bool m_componentsDirty = true;

    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<TelemetrySink> m_telemetry;
    std::unique_ptr<spawn::WaveController> m_waveController;
    std::unique_ptr<spawn::Spawner> m_spawner;
    std::vector<SystemEntry> m_systems;
    std::vector<systems::SystemStage> m_systemStageOrder;
    systems::FormationSystem *m_cachedFormationSystem = nullptr;

    void rebuildMissionComponents() const;
    void initializeSystems();
    void runSystemsForStage(systems::SystemStage stage, float dt, systems::SystemContext &context);
    systems::FormationSystem *formationSystem() const;
};

} // namespace world


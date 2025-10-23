#pragma once

#include "world/Entity.h"

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
struct RuntimeSkill;
struct GateRuntime;
struct ActiveSpawn;
struct WallSegment;
struct Unit;
struct EnemyUnit;
struct CommanderUnit;
struct CaptureRuntime;
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

struct LegacySimulation;

namespace spawn
{
class WaveController;
class Spawner;
} // namespace spawn

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
    void step(float dt, const Vec2 &commanderInput);
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

  private:
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

    void rebuildMissionComponents() const;
};

} // namespace world


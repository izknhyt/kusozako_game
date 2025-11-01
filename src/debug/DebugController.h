#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

namespace world
{
struct LegacySimulation;
}

namespace debug
{

struct DebugBindings
{
    class SimulationAccessor
    {
      public:
        virtual ~SimulationAccessor() = default;
        virtual void markComponentsDirty() = 0;
        virtual void setEnemySpawnMultiplier(float multiplier) = 0;
        virtual float enemySpawnMultiplier() const = 0;
        virtual bool skipNextWave() = 0;
    };

    void *simulation = nullptr; // world::LegacySimulation*
    SimulationAccessor *accessor = nullptr;
};

struct DisplayCategory
{
    std::string name;
    bool active = false;
};

struct DisplayEntry
{
    std::string label;
    std::string value;
    bool selected = false;
    bool command = false;
    bool warning = false;
};

struct DisplayState
{
    bool active = false;
    std::vector<DisplayCategory> categories;
    std::vector<DisplayEntry> entries;
    std::string footer;
    std::string help;
    std::string toast;
    bool showTelemetry = false;
};

class Parameter;

struct Category
{
    std::string name;
    std::vector<std::unique_ptr<Parameter>> parameters;
    Category();
    Category(const Category &) = delete;
    Category &operator=(const Category &) = delete;
    Category(Category &&) noexcept;
    Category &operator=(Category &&) noexcept;
    ~Category();
};

class DebugController
{
  public:
    DebugController();

    void bindWorld(const DebugBindings &bindings);
    void onConfigReloaded();

    void toggle();
    bool active() const { return m_active; }
    void handleActionToggle();
    void handleEvent(const SDL_Event &event);
    void update(double dt);

    float timeScale() const { return m_timeScale; }
    void gatherDisplay(DisplayState &state) const;

    bool consumeHudToggle();
    bool consumeTelemetryToggle();

  private:
    DebugBindings m_bindings;
    DebugBindings::SimulationAccessor *m_accessor = nullptr;
    world::LegacySimulation *m_simulation = nullptr;
    bool m_active = false;
    float m_timeScale = 1.0f;
    float m_defaultTimeScale = 1.0f;
    float m_enemySpawnMultiplier = 1.0f;
    float m_defaultEnemySpawnMultiplier = 1.0f;

    bool m_pendingHudToggle = false;
    bool m_pendingTelemetryToggle = false;

    std::string m_toastMessage;
    double m_toastTimer = 0.0;

    std::vector<Category> m_categories;
    int m_activeCategory = 0;
    int m_activeParameter = 0;

    float m_baseYunaInterval = 0.0f;
    int m_baseYunaMax = 0;
    float m_baseYunaHp = 0.0f;
    float m_baseEnemyHp = 0.0f;
    float m_baseCommanderHp = 0.0f;
    float m_baseLeaderDownWindow = 0.0f;
    float m_baseOrderDuration = 0.0f;
    float m_baseMimicCooldown = 0.0f;

    void rebuildBaseValues();
    void rebuildParameters();
    void ensureSelectionValid();
    void selectCategory(int index);
    void nextParameter(bool reverse);
    void adjustParameter(bool increase, bool largeStep);
    void resetParameter();
    void triggerCommand();

    Parameter *currentParameter();
    const Parameter *currentParameter() const;

    void setToast(const std::string &message);
    void applyEnemySpawnMultiplier();
};

} // namespace debug

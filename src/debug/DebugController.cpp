#include "debug/DebugController.h"

#include "world/LegacySimulation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace debug
{

class Parameter
{
  public:
    explicit Parameter(std::string label) : m_label(std::move(label)) {}
    virtual ~Parameter() = default;

    const std::string &label() const { return m_label; }
    virtual std::string valueString() const = 0;
    virtual void increase(bool largeStep) = 0;
    virtual void decrease(bool largeStep) = 0;
    virtual void reset() = 0;
    virtual bool isCommand() const { return false; }
    virtual bool activate() { return false; }

  private:
    std::string m_label;
};

namespace
{

constexpr float kMinMultiplier = 0.1f;
constexpr float kMaxMultiplier = 5.0f;

class NumericParameter : public Parameter
{
  public:
    using Getter = std::function<float()>;
    using Setter = std::function<void(float)>;
    using Formatter = std::function<std::string(float)>;

    NumericParameter(std::string label,
                     Getter getter,
                     Setter setter,
                     float defaultValue,
                     float minValue,
                     float maxValue,
                     float smallStep,
                     float largeStep,
                     Formatter formatter = Formatter{})
        : Parameter(std::move(label)),
          m_getter(std::move(getter)),
          m_setter(std::move(setter)),
          m_default(defaultValue),
          m_min(minValue),
          m_max(maxValue),
          m_smallStep(smallStep),
          m_largeStep(largeStep),
          m_formatter(std::move(formatter))
    {
    }

    std::string valueString() const override
    {
        const float value = m_getter();
        if (m_formatter)
        {
            return m_formatter(value);
        }
        std::ostringstream oss;
        oss.setf(std::ios::fixed, std::ios::floatfield);
        oss.precision(2);
        oss << value;
        return oss.str();
    }

    void increase(bool largeStep) override
    {
        applyDelta(largeStep ? m_largeStep : m_smallStep);
    }

    void decrease(bool largeStep) override
    {
        applyDelta(largeStep ? -m_largeStep : -m_smallStep);
    }

    void reset() override
    {
        m_setter(std::clamp(m_default, m_min, m_max));
    }

  private:
    void applyDelta(float delta)
    {
        float value = m_getter();
        value = std::clamp(value + delta, m_min, m_max);
        m_setter(value);
    }

    Getter m_getter;
    Setter m_setter;
    float m_default;
    float m_min;
    float m_max;
    float m_smallStep;
    float m_largeStep;
    Formatter m_formatter;
};

class CommandParameter : public Parameter
{
  public:
    using Action = std::function<void()>;
    using Status = std::function<std::string()>;

    CommandParameter(std::string label, Action action, Status status = Status{})
        : Parameter(std::move(label)), m_action(std::move(action)), m_status(std::move(status))
    {
    }

    std::string valueString() const override
    {
        if (m_status)
        {
            return m_status();
        }
        return std::string();
    }

    void increase(bool) override {}

    void decrease(bool) override {}

    void reset() override {}

    bool isCommand() const override { return true; }

    bool activate() override
    {
        if (m_action)
        {
            m_action();
            return true;
        }
        return false;
    }

  private:
    Action m_action;
    Status m_status;
};

std::string formatSeconds(float seconds)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(2);
    oss << seconds << "s";
    return oss.str();
}

std::string formatMultiplier(float value)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(2);
    oss << "×" << value;
    return oss.str();
}

std::string formatInteger(float value)
{
    std::ostringstream oss;
    oss << static_cast<int>(std::round(value));
    return oss.str();
}

} // namespace

Category::Category() = default;
Category::Category(Category &&) noexcept = default;
Category &Category::operator=(Category &&) noexcept = default;
Category::~Category() = default;

DebugController::DebugController() = default;

void DebugController::bindWorld(const DebugBindings &bindings)
{
    m_bindings = bindings;
    m_accessor = bindings.accessor;
    m_simulation = static_cast<world::LegacySimulation *>(bindings.simulation);
    rebuildBaseValues();
    rebuildParameters();
    ensureSelectionValid();
}

void DebugController::onConfigReloaded()
{
    rebuildBaseValues();
    rebuildParameters();
    ensureSelectionValid();
}

void DebugController::toggle()
{
    m_active = !m_active;
    if (m_active)
    {
        rebuildBaseValues();
        rebuildParameters();
        ensureSelectionValid();
        setToast("Debug mode enabled");
    }
    else
    {
        setToast("Debug mode disabled");
    }
}

void DebugController::handleActionToggle()
{
    toggle();
}

void DebugController::handleEvent(const SDL_Event &event)
{
    if (!m_active)
    {
        return;
    }

    if (event.type != SDL_KEYDOWN || event.key.repeat != 0)
    {
        return;
    }

    const SDL_Keymod mods = static_cast<SDL_Keymod>(event.key.keysym.mod);
    const bool shift = (mods & KMOD_SHIFT) != 0;
    const bool ctrl = (mods & KMOD_CTRL) != 0;

    switch (event.key.keysym.sym)
    {
    case SDLK_F6:
        if (ctrl)
        {
            m_pendingHudToggle = true;
        }
        else
        {
            selectCategory(0);
            nextParameter(shift);
        }
        break;
    case SDLK_F7:
        if (ctrl)
        {
            m_pendingTelemetryToggle = true;
        }
        else
        {
            selectCategory(1);
            nextParameter(shift);
        }
        break;
    case SDLK_F8:
        if (ctrl)
        {
            selectCategory(3);
            nextParameter(shift);
        }
        else
        {
            selectCategory(2);
            nextParameter(shift);
        }
        break;
    case SDLK_PAGEUP:
        adjustParameter(true, ctrl);
        break;
    case SDLK_PAGEDOWN:
        adjustParameter(false, ctrl);
        break;
    case SDLK_HOME:
        if (ctrl)
        {
            m_timeScale = m_defaultTimeScale;
            setToast("Time scale reset");
        }
        else
        {
            resetParameter();
        }
        break;
    case SDLK_END:
        if (ctrl && m_accessor && m_accessor->skipNextWave())
        {
            setToast("Wave skipped");
        }
        break;
    case SDLK_RETURN:
        if (ctrl)
        {
            resetParameter();
        }
        else
        {
            triggerCommand();
        }
        break;
    case SDLK_TAB:
        nextParameter(shift);
        break;
    default: break;
    }
}

void DebugController::update(double dt)
{
    if (m_toastTimer > 0.0)
    {
        m_toastTimer = std::max(0.0, m_toastTimer - dt);
        if (m_toastTimer <= 0.0)
        {
            m_toastMessage.clear();
        }
    }
}

void DebugController::gatherDisplay(DisplayState &state) const
{
    state.active = m_active;
    state.categories.clear();
    state.entries.clear();
    state.toast = m_toastMessage;
    state.showTelemetry = m_pendingTelemetryToggle;

    for (std::size_t i = 0; i < m_categories.size(); ++i)
    {
        DisplayCategory category;
        category.name = m_categories[i].name;
        category.active = static_cast<int>(i) == m_activeCategory;
        state.categories.push_back(std::move(category));
    }

    if (m_activeCategory >= 0 && m_activeCategory < static_cast<int>(m_categories.size()))
    {
        const auto &category = m_categories[static_cast<std::size_t>(m_activeCategory)];
        for (std::size_t i = 0; i < category.parameters.size(); ++i)
        {
            const Parameter &param = *category.parameters[i];
            DisplayEntry entry;
            entry.label = param.label();
            entry.value = param.valueString();
            entry.selected = static_cast<int>(i) == m_activeParameter;
            entry.command = param.isCommand();
            state.entries.push_back(std::move(entry));
        }
    }

    state.footer = "PageUp/PageDown: adjust  Ctrl+PageUp/PageDown: coarse  Ctrl+Enter/Home: reset";
    state.help = "F6/F7/F8: select category  Ctrl+F6: HUD  Ctrl+F7: Telemetry  Ctrl+F8: System";
}

bool DebugController::consumeHudToggle()
{
    if (m_pendingHudToggle)
    {
        m_pendingHudToggle = false;
        return true;
    }
    return false;
}

bool DebugController::consumeTelemetryToggle()
{
    if (m_pendingTelemetryToggle)
    {
        m_pendingTelemetryToggle = false;
        return true;
    }
    return false;
}

void DebugController::rebuildBaseValues()
{
    if (!m_simulation)
    {
        return;
    }

    m_baseYunaInterval = m_simulation->config.yuna_interval;
    m_baseYunaMax = m_simulation->config.yuna_max;
    m_baseYunaHp = m_simulation->yunaStats.hp;
    m_baseEnemyHp = m_simulation->slimeStats.hp;
    m_baseCommanderHp = m_simulation->commanderStats.hp;
    m_baseLeaderDownWindow = m_simulation->config.morale.leaderDownWindow;
    m_baseOrderDuration = m_simulation->temperamentConfig.orderDuration;
    m_baseMimicCooldown = 0.0f;
    for (const auto &def : m_simulation->temperamentConfig.definitions)
    {
        if (def.behavior == TemperamentBehavior::Mimic)
        {
            m_baseMimicCooldown = (def.mimicEvery.min + def.mimicEvery.max) * 0.5f;
            break;
        }
    }
    if (m_accessor)
    {
        m_enemySpawnMultiplier = m_accessor->enemySpawnMultiplier();
        m_defaultEnemySpawnMultiplier = m_enemySpawnMultiplier;
    }
    else
    {
        m_enemySpawnMultiplier = 1.0f;
        m_defaultEnemySpawnMultiplier = 1.0f;
    }
}

void DebugController::rebuildParameters()
{
    m_categories.clear();

    if (!m_simulation)
    {
        return;
    }

    auto makeSpawnCategory = [this]() {
        Category category;
        category.name = "Spawn";

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Ally Spawn Interval (s)",
            [this]() { return m_simulation->config.yuna_interval; },
            [this](float value) {
                m_simulation->config.yuna_interval = value;
                m_simulation->yunaSpawnTimer = std::min(m_simulation->yunaSpawnTimer, value);
            },
            m_baseYunaInterval,
            0.1f,
            10.0f,
            0.1f,
            0.5f,
            formatSeconds));

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Ally Max Units",
            [this]() { return static_cast<float>(m_simulation->config.yuna_max); },
            [this](float value) {
                int capped = std::clamp(static_cast<int>(std::round(value)), 1, 1000);
                m_simulation->config.yuna_max = capped;
            },
            static_cast<float>(m_baseYunaMax),
            10.0f,
            500.0f,
            5.0f,
            25.0f,
            formatInteger));

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Enemy Spawn Multiplier",
            [this]() { return m_enemySpawnMultiplier; },
            [this](float value) {
                m_enemySpawnMultiplier = std::clamp(value, 0.1f, 5.0f);
                applyEnemySpawnMultiplier();
            },
            m_defaultEnemySpawnMultiplier,
            0.1f,
            5.0f,
            0.1f,
            0.5f,
            formatMultiplier));

        return category;
    };

    auto makeStatsCategory = [this]() {
        Category category;
        category.name = "Stats";

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Ally HP Multiplier",
            [this]() { return m_simulation->yunaStats.hp / std::max(m_baseYunaHp, 0.01f); },
            [this](float value) {
                value = std::clamp(value, kMinMultiplier, kMaxMultiplier);
                const float newHp = m_baseYunaHp * value;
                if (newHp > 0.0f)
                {
                    for (auto &unit : m_simulation->yunas)
                    {
                        unit.hp = newHp;
                    }
                    m_simulation->yunaStats.hp = newHp;
                }
            },
            1.0f,
            kMinMultiplier,
            kMaxMultiplier,
            0.1f,
            0.5f,
            formatMultiplier));

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Enemy HP Multiplier",
            [this]() { return m_simulation->slimeStats.hp / std::max(m_baseEnemyHp, 0.01f); },
            [this](float value) {
                value = std::clamp(value, kMinMultiplier, kMaxMultiplier);
                const float newHp = m_baseEnemyHp * value;
                if (newHp > 0.0f)
                {
                    for (auto &enemy : m_simulation->enemies)
                    {
                        enemy.hp = newHp;
                    }
                    m_simulation->slimeStats.hp = newHp;
                }
            },
            1.0f,
            kMinMultiplier,
            kMaxMultiplier,
            0.1f,
            0.5f,
            formatMultiplier));

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Commander HP Multiplier",
            [this]() { return m_simulation->commanderStats.hp / std::max(m_baseCommanderHp, 0.01f); },
            [this](float value) {
                value = std::clamp(value, kMinMultiplier, kMaxMultiplier);
                const float newHp = m_baseCommanderHp * value;
                if (newHp > 0.0f)
                {
                    m_simulation->commanderStats.hp = newHp;
                    if (m_simulation->commander.alive)
                    {
                        m_simulation->commander.hp = newHp;
                    }
                }
            },
            1.0f,
            kMinMultiplier,
            kMaxMultiplier,
            0.1f,
            0.5f,
            formatMultiplier));

        return category;
    };

    auto makeMoraleCategory = [this]() {
        Category category;
        category.name = "Morale";

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Leader Down Window (s)",
            [this]() { return m_simulation->config.morale.leaderDownWindow; },
            [this](float value) {
                m_simulation->config.morale.leaderDownWindow = std::clamp(value, 0.0f, 30.0f);
            },
            m_baseLeaderDownWindow,
            0.0f,
            30.0f,
            0.5f,
            2.0f,
            formatSeconds));

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Order Duration (s)",
            [this]() { return m_simulation->temperamentConfig.orderDuration; },
            [this](float value) {
                m_simulation->temperamentConfig.orderDuration = std::clamp(value, 1.0f, 30.0f);
            },
            m_baseOrderDuration,
            1.0f,
            30.0f,
            0.5f,
            2.0f,
            formatSeconds));

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Mimic Cooldown (s)",
            [this]() { return m_baseMimicCooldown > 0.0f ? m_baseMimicCooldown : 0.0f; },
            [this](float value) {
                if (value <= 0.0f)
                {
                    return;
                }
                for (auto &def : m_simulation->temperamentConfig.definitions)
                {
                    if (def.behavior == TemperamentBehavior::Mimic)
                    {
                        def.mimicEvery.min = value;
                        def.mimicEvery.max = value;
                    }
                }
            },
            m_baseMimicCooldown > 0.0f ? m_baseMimicCooldown : 6.0f,
            0.5f,
            30.0f,
            0.5f,
            2.0f,
            formatSeconds));

        return category;
    };

    auto makeSystemCategory = [this]() {
        Category category;
        category.name = "System";

        category.parameters.push_back(std::make_unique<NumericParameter>(
            "Time Scale",
            [this]() { return m_timeScale; },
            [this](float value) {
                m_timeScale = std::clamp(value, 0.25f, 4.0f);
            },
            m_defaultTimeScale,
            0.25f,
            4.0f,
            0.1f,
            0.5f,
            formatMultiplier));

        category.parameters.push_back(std::make_unique<CommandParameter>(
            "Heal Commander",
            [this]() {
                if (m_simulation)
                {
                    m_simulation->commander.alive = true;
                    m_simulation->commander.hp = m_simulation->commanderStats.hp;
                    m_simulation->commanderRespawnTimer = 0.0f;
                    m_simulation->commanderInvulnTimer = 0.0f;
                    setToast("Commander healed");
                }
            }));

        category.parameters.push_back(std::make_unique<CommandParameter>(
            "Force Chibi Spawn",
            [this]() {
                if (m_simulation)
                {
                    m_simulation->yunaSpawnTimer = 0.0f;
                    setToast("Spawn timer reset");
                }
            }));

        category.parameters.push_back(std::make_unique<CommandParameter>(
            "Skip Next Wave",
            [this]() {
                if (m_accessor && m_accessor->skipNextWave())
                {
                    setToast("Next wave scheduled");
                }
            }));

        return category;
    };

    m_categories.push_back(makeSpawnCategory());
    m_categories.push_back(makeStatsCategory());
    m_categories.push_back(makeMoraleCategory());
    m_categories.push_back(makeSystemCategory());
}

void DebugController::ensureSelectionValid()
{
    if (m_categories.empty())
    {
        m_activeCategory = 0;
        m_activeParameter = 0;
        return;
    }

    if (m_activeCategory < 0 || m_activeCategory >= static_cast<int>(m_categories.size()))
    {
        m_activeCategory = 0;
    }

    const auto &params = m_categories[static_cast<std::size_t>(m_activeCategory)].parameters;
    if (params.empty())
    {
        m_activeParameter = 0;
        return;
    }
    if (m_activeParameter < 0 || m_activeParameter >= static_cast<int>(params.size()))
    {
        m_activeParameter = 0;
    }
}

void DebugController::selectCategory(int index)
{
    if (index < 0 || index >= static_cast<int>(m_categories.size()))
    {
        return;
    }
    if (m_activeCategory != index)
    {
        m_activeCategory = index;
        m_activeParameter = 0;
    }
    ensureSelectionValid();
}

void DebugController::nextParameter(bool reverse)
{
    if (m_categories.empty())
    {
        return;
    }
    auto &params = m_categories[static_cast<std::size_t>(m_activeCategory)].parameters;
    if (params.empty())
    {
        return;
    }
    if (reverse)
    {
        m_activeParameter = (m_activeParameter - 1 + static_cast<int>(params.size())) % static_cast<int>(params.size());
    }
    else
    {
        m_activeParameter = (m_activeParameter + 1) % static_cast<int>(params.size());
    }
}

void DebugController::adjustParameter(bool increase, bool largeStep)
{
    if (auto *param = currentParameter())
    {
        if (param->isCommand())
        {
            return;
        }
        if (increase)
        {
            param->increase(largeStep);
        }
        else
        {
            param->decrease(largeStep);
        }
        if (m_accessor)
        {
            m_accessor->markComponentsDirty();
        }
    }
}

void DebugController::resetParameter()
{
    if (auto *param = currentParameter())
    {
        param->reset();
        if (m_accessor)
        {
            m_accessor->markComponentsDirty();
        }
    }
}

void DebugController::triggerCommand()
{
    if (auto *param = currentParameter())
    {
        if (param->isCommand() && param->activate())
        {
            if (m_accessor)
            {
                m_accessor->markComponentsDirty();
            }
        }
    }
}

Parameter *DebugController::currentParameter()
{
    if (m_categories.empty())
    {
        return nullptr;
    }
    auto &params = m_categories[static_cast<std::size_t>(m_activeCategory)].parameters;
    if (params.empty())
    {
        return nullptr;
    }
    if (m_activeParameter < 0 || m_activeParameter >= static_cast<int>(params.size()))
    {
        return nullptr;
    }
    return params[static_cast<std::size_t>(m_activeParameter)].get();
}

const Parameter *DebugController::currentParameter() const
{
    if (m_categories.empty())
    {
        return nullptr;
    }
    const auto &params = m_categories[static_cast<std::size_t>(m_activeCategory)].parameters;
    if (params.empty())
    {
        return nullptr;
    }
    if (m_activeParameter < 0 || m_activeParameter >= static_cast<int>(params.size()))
    {
        return nullptr;
    }
    return params[static_cast<std::size_t>(m_activeParameter)].get();
}

void DebugController::setToast(const std::string &message)
{
    m_toastMessage = message;
    m_toastTimer = 2.0;
}

void DebugController::applyEnemySpawnMultiplier()
{
    if (m_accessor)
    {
        m_accessor->setEnemySpawnMultiplier(m_enemySpawnMultiplier);
    }
}

} // namespace debug

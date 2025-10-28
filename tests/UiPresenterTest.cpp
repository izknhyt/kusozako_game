#include "app/UiPresenter.h"
#include "world/LegacySimulation.h"

#include <cstddef>
#include <cmath>
#include <iostream>
#include <memory>

namespace
{

bool almostEqual(float lhs, float rhs, float epsilon = 1e-4f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

class FixedEventBus : public NullEventBus
{
  public:
    explicit FixedEventBus(std::size_t count = 0) : m_count(count) {}

    void setCount(std::size_t count) { m_count = count; }

    std::size_t unconsumedCount() const override { return m_count; }

  private:
    std::size_t m_count = 0;
};

bool testUnconsumedEventWarning()
{
    UiPresenter presenter;
    world::LegacySimulation sim{};

    presenter.bindSimulation(&sim);

    auto bus = std::make_shared<FixedEventBus>();
    presenter.setEventBus(bus);

    bool success = true;

    bus->setCount(9);
    presenter.bindSimulation(&sim);
    if (sim.hud.performance.active)
    {
        std::cerr << "Warning should not activate below threshold" << '\n';
        success = false;
    }

    bus->setCount(10);
    presenter.bindSimulation(&sim);
    if (!sim.hud.performance.active)
    {
        std::cerr << "Warning did not activate at threshold" << '\n';
        success = false;
    }
    if (sim.hud.performance.message != "Events lost 10")
    {
        std::cerr << "Warning message did not include unconsumed count" << '\n';
        success = false;
    }

    const float initialTimer = sim.hud.performance.timer;
    if (!almostEqual(initialTimer, presenter.lastWarningDuration()))
    {
        std::cerr << "Warning timer did not use configured duration" << '\n';
        success = false;
    }

    sim.hud.performance.timer = initialTimer - 0.5f;
    bus->setCount(10);
    presenter.bindSimulation(&sim);
    if (!almostEqual(sim.hud.performance.timer, initialTimer - 0.5f))
    {
        std::cerr << "Warning timer should not reset while message is unchanged" << '\n';
        success = false;
    }

    bus->setCount(12);
    presenter.bindSimulation(&sim);
    if (sim.hud.performance.message != "Events lost 12")
    {
        std::cerr << "Warning message did not refresh when count changed" << '\n';
        success = false;
    }
    if (!almostEqual(sim.hud.performance.timer, presenter.lastWarningDuration()))
    {
        std::cerr << "Warning timer did not reset for new message" << '\n';
        success = false;
    }

    bus->setCount(4);
    presenter.bindSimulation(&sim);
    if (sim.hud.unconsumedEvents != 4)
    {
        std::cerr << "Unconsumed event count did not update when below threshold" << '\n';
        success = false;
    }

    return success;
}

} // namespace

int main()
{
    return testUnconsumedEventWarning() ? 0 : 1;
}


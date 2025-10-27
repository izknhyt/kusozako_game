#pragma once

#include <optional>
#include <string>

#include "config/AppConfig.h"

namespace telemetry
{

struct StageTimingSample
{
    double updateMs = 0.0;
    double renderMs = 0.0;
    double inputMs = 0.0;
    double hudMs = 0.0;
};

struct BudgetViolation
{
    std::string stage;
    double sampleMs = 0.0;
    double budgetMs = 0.0;
};

class PerformanceBudgetMonitor
{
  public:
    PerformanceBudgetMonitor();
    explicit PerformanceBudgetMonitor(PerformanceBudgetConfig budget);

    void setBudget(PerformanceBudgetConfig budget);
    [[nodiscard]] const PerformanceBudgetConfig &budget() const { return m_budget; }

    [[nodiscard]] std::optional<BudgetViolation> evaluate(const StageTimingSample &sample) const;

  private:
    PerformanceBudgetConfig m_budget{};
};

} // namespace telemetry

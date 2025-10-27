#include "telemetry/PerformanceBudgetMonitor.h"

#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace telemetry
{

PerformanceBudgetMonitor::PerformanceBudgetMonitor() = default;

PerformanceBudgetMonitor::PerformanceBudgetMonitor(PerformanceBudgetConfig budget)
    : m_budget(std::move(budget))
{
}

void PerformanceBudgetMonitor::setBudget(PerformanceBudgetConfig budget)
{
    m_budget = std::move(budget);
}

std::optional<BudgetViolation> PerformanceBudgetMonitor::evaluate(const StageTimingSample &sample) const
{
    const float tolerance = std::max(0.0f, m_budget.toleranceMs);
    const double toleranceMs = static_cast<double>(tolerance);
    struct StageInfo
    {
        std::string_view id;
        double sample;
        float budget;
    };
    const std::array<StageInfo, 4> stages{{
        StageInfo{"update", sample.updateMs, m_budget.updateMs},
        StageInfo{"render", sample.renderMs, m_budget.renderMs},
        StageInfo{"input", sample.inputMs, m_budget.inputMs},
        StageInfo{"hud", sample.hudMs, m_budget.hudMs},
    }};

    for (const StageInfo &stage : stages)
    {
        if (!(stage.budget > 0.0f))
        {
            continue;
        }
        if (!std::isfinite(stage.sample))
        {
            continue;
        }
        const double threshold = static_cast<double>(stage.budget) + toleranceMs;
        if (stage.sample > threshold)
        {
            BudgetViolation violation;
            violation.stage = std::string(stage.id);
            violation.sampleMs = stage.sample;
            violation.budgetMs = static_cast<double>(stage.budget);
            return violation;
        }
    }
    return std::nullopt;
}

} // namespace telemetry

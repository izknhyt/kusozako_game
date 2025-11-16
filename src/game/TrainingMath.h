#pragma once

#include "config/AppConfig.h"

#include <algorithm>
#include <cmath>

inline bool trainingIsRepeatable(const TrainingEntry &entry)
{
    return entry.repeatable.enabled;
}

inline int trainingAbsoluteLevel(const TrainingEntry &entry, int progress)
{
    if (trainingIsRepeatable(entry))
    {
        return std::max(progress, 0) + 1;
    }
    return progress;
}

inline int trainingMaxDisplayLevel(const TrainingEntry &entry)
{
    if (trainingIsRepeatable(entry))
    {
        return entry.repeatable.maxLevel;
    }
    return static_cast<int>(entry.steps.size());
}

inline bool trainingIsMaxed(const TrainingEntry &entry, int progress)
{
    if (trainingIsRepeatable(entry))
    {
        if (entry.repeatable.maxLevel <= 0)
        {
            return false;
        }
        const int absolute = trainingAbsoluteLevel(entry, progress);
        return absolute >= entry.repeatable.maxLevel;
    }
    return progress >= static_cast<int>(entry.steps.size());
}

inline int trainingCostAtLevel(const TrainingEntry &entry, int progress)
{
    if (trainingIsRepeatable(entry))
    {
        if (trainingIsMaxed(entry, progress))
        {
            return 0;
        }
        const int absolute = trainingAbsoluteLevel(entry, progress);
        const float baseCost = std::max(0.0f, entry.repeatable.baseCost);
        const float growth = entry.repeatable.costGrowth > 0.0f ? entry.repeatable.costGrowth : 1.0f;
        const float exponent = static_cast<float>(std::max(absolute - 1, 0));
        const float raw = baseCost * std::pow(growth, exponent);
        return std::max(1, static_cast<int>(std::round(raw)));
    }
    if (progress < 0 || progress >= static_cast<int>(entry.steps.size()))
    {
        return 0;
    }
    return entry.steps[static_cast<std::size_t>(progress)].cost;
}

inline float trainingDeltaAtLevel(const TrainingEntry &entry, int progress)
{
    if (trainingIsRepeatable(entry))
    {
        return entry.repeatable.delta;
    }
    if (progress < 0 || progress >= static_cast<int>(entry.steps.size()))
    {
        return 0.0f;
    }
    return entry.steps[static_cast<std::size_t>(progress)].delta;
}

#pragma once

#include <string>

struct FramePerf
{
    float fps = 0.0f;
    float msUpdate = 0.0f;
    float msRender = 0.0f;
    float msInput = 0.0f;
    float msHud = 0.0f;
    int drawCalls = 0;
    int entities = 0;
    bool budgetExceeded = false;
    std::string budgetStage;
    float budgetSampleMs = 0.0f;
    float budgetTargetMs = 0.0f;
};

#include "telemetry/PerformanceBudgetMonitor.h"
#include "telemetry/TelemetrySink.h"

#include <iostream>

namespace
{

class MockTelemetrySink : public TelemetrySink
{
  public:
    void recordEvent(std::string_view, const Payload &) override {}

    void requestFrameCapture() override
    {
        TelemetrySink::requestFrameCapture();
        requested = true;
    }

    bool requested = false;
};

bool assertTrue(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool success = true;

    PerformanceBudgetConfig config;
    config.updateMs = 0.01f;
    config.renderMs = 0.0f;
    config.inputMs = 0.0f;
    config.hudMs = 0.0f;
    config.toleranceMs = 0.0f;

    telemetry::PerformanceBudgetMonitor monitor(config);
    telemetry::StageTimingSample sample;
    sample.updateMs = 0.5;

    auto violation = monitor.evaluate(sample);
    success &= assertTrue(violation.has_value(), "Expected update budget violation to be detected");
    if (violation)
    {
        success &= assertTrue(violation->stage == "update", "Violation should reference the update stage");
    }

    MockTelemetrySink sink;
    if (violation)
    {
        sink.requestFrameCapture();
    }
    success &= assertTrue(sink.requested, "Frame capture should be requested when a violation occurs");

    PerformanceBudgetConfig toleranceConfig;
    toleranceConfig.updateMs = 1.0f;
    toleranceConfig.toleranceMs = 0.5f;
    telemetry::PerformanceBudgetMonitor toleranceMonitor(toleranceConfig);
    telemetry::StageTimingSample toleranceSample;
    toleranceSample.updateMs = 1.2;
    success &= assertTrue(!toleranceMonitor.evaluate(toleranceSample).has_value(),
                          "Timing within tolerance should not trigger a violation");
    toleranceSample.updateMs = 1.7;
    success &= assertTrue(toleranceMonitor.evaluate(toleranceSample).has_value(),
                          "Timing beyond tolerance should trigger a violation");

    return success ? 0 : 1;
}

#pragma once

#include <mutex>

#include "telemetry/TelemetrySink.h"

class ConsoleTelemetrySink : public TelemetrySink
{
  public:
    ConsoleTelemetrySink() = default;

    void recordEvent(std::string_view eventName, const Payload &payload) override;
    void flush() override;

  private:
    std::mutex m_mutex;
};


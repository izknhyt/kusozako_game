#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

class TelemetrySink
{
  public:
    using Payload = std::unordered_map<std::string, std::string>;

    virtual ~TelemetrySink() = default;

    virtual void recordEvent(std::string_view eventName, const Payload &payload) = 0;
    virtual void flush() {}
};

class NullTelemetrySink : public TelemetrySink
{
  public:
    void recordEvent(std::string_view, const Payload &) override {}
};


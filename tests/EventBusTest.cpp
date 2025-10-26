#include "events/EventBus.h"

#include "telemetry/TelemetrySink.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

class RecordingTelemetrySink : public TelemetrySink
{
  public:
    void recordEvent(std::string_view eventName, const Payload &payload) override
    {
        events.emplace_back(std::string(eventName), payload);
    }

    std::vector<std::pair<std::string, Payload>> events;
};

bool testSubscriptionTokenAutoUnsubscribe()
{
    BasicEventBus bus;
    bool handled = false;
    {
        auto token = bus.subscribe("token_test", [&](const EventContext &) { handled = true; });
        EventContext ctx;
        bus.dispatch("token_test", ctx);
        bus.pump();
        if (!handled)
        {
            std::cerr << "Handler did not fire while subscription token was alive" << '\n';
            return false;
        }
    }

    handled = false;
    EventContext ctx;
    bus.dispatch("token_test", ctx);
    bus.pump();
    if (handled)
    {
        std::cerr << "Handler fired after subscription token was destroyed" << '\n';
        return false;
    }

    bus.advanceFrame();
    bus.pump();
    bus.advanceFrame();
    if (bus.unconsumedCount() != 1)
    {
        std::cerr << "Unconsumed event count did not increment after TTL expiry" << '\n';
        return false;
    }

    return true;
}

bool testEventTtlExpiry()
{
    auto telemetry = std::make_shared<RecordingTelemetrySink>();
    BasicEventBus bus(telemetry);

    EventContext ctx;
    bus.dispatch("ttl_test", ctx);
    bus.pump();
    if (bus.unconsumedCount() != 0)
    {
        std::cerr << "Unconsumed count incremented before TTL expired" << '\n';
        return false;
    }

    bus.advanceFrame();
    bus.pump();
    bus.advanceFrame();

    if (bus.unconsumedCount() != 1)
    {
        std::cerr << "Unconsumed count did not reflect expired event" << '\n';
        return false;
    }

    if (telemetry->events.empty())
    {
        std::cerr << "Telemetry did not record expired event" << '\n';
        return false;
    }

    const auto &record = telemetry->events.back();
    if (record.first != "event_bus.no_listener")
    {
        std::cerr << "Unexpected telemetry event name: " << record.first << '\n';
        return false;
    }
    auto it = record.second.find("event");
    if (it == record.second.end() || it->second != "ttl_test")
    {
        std::cerr << "Telemetry payload missing event name" << '\n';
        return false;
    }

    return true;
}

} // namespace

int main()
{
    bool success = true;
    if (!testSubscriptionTokenAutoUnsubscribe())
    {
        success = false;
    }
    if (!testEventTtlExpiry())
    {
        success = false;
    }
    return success ? 0 : 1;
}

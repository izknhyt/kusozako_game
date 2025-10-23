#pragma once

#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "telemetry/TelemetrySink.h"

struct EventContext
{
    std::any payload;
};

class EventBus
{
  public:
    using HandlerId = std::size_t;
    using EventHandler = std::function<void(const EventContext &)>;

    virtual ~EventBus() = default;

    virtual HandlerId subscribe(const std::string &eventName, EventHandler handler) = 0;
    virtual void unsubscribe(const std::string &eventName, HandlerId handlerId) = 0;
    virtual void dispatch(const std::string &eventName, const EventContext &context) = 0;
};

class NullEventBus : public EventBus
{
  public:
    HandlerId subscribe(const std::string &, EventHandler) override { return 0; }
    void unsubscribe(const std::string &, HandlerId) override {}
    void dispatch(const std::string &, const EventContext &) override {}
};

class BasicEventBus : public EventBus
{
  public:
    explicit BasicEventBus(std::shared_ptr<TelemetrySink> telemetry = nullptr);

    void setTelemetrySink(std::shared_ptr<TelemetrySink> telemetry);

    HandlerId subscribe(const std::string &eventName, EventHandler handler) override;
    void unsubscribe(const std::string &eventName, HandlerId handlerId) override;
    void dispatch(const std::string &eventName, const EventContext &context) override;

  private:
    struct HandlerEntry
    {
        HandlerId id = 0;
        EventHandler handler;
    };

    void notifyNoHandlers(const std::string &eventName);
    void notifyHandlerException(const std::string &eventName, const std::string &message);

    std::shared_ptr<TelemetrySink> m_telemetry;
    std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<HandlerEntry>> m_handlers;
    HandlerId m_nextId = 1;
};


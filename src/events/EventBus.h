#pragma once

#include <any>
#include <atomic>
#include <deque>
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

    class SubscriptionToken
    {
      public:
        SubscriptionToken() = default;
        SubscriptionToken(EventBus *bus, std::string eventName, HandlerId handlerId)
            : m_bus(bus), m_eventName(std::move(eventName)), m_handlerId(handlerId)
        {
        }

        SubscriptionToken(const SubscriptionToken &) = delete;
        SubscriptionToken &operator=(const SubscriptionToken &) = delete;

        SubscriptionToken(SubscriptionToken &&other) noexcept
        {
            *this = std::move(other);
        }

        SubscriptionToken &operator=(SubscriptionToken &&other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }
            reset();
            m_bus = other.m_bus;
            m_eventName = std::move(other.m_eventName);
            m_handlerId = other.m_handlerId;
            other.m_bus = nullptr;
            other.m_handlerId = 0;
            other.m_eventName.clear();
            return *this;
        }

        ~SubscriptionToken()
        {
            reset();
        }

        void reset()
        {
            if (!m_bus || m_handlerId == 0)
            {
                m_bus = nullptr;
                m_eventName.clear();
                m_handlerId = 0;
                return;
            }

            EventBus *bus = m_bus;
            std::string eventName = std::move(m_eventName);
            HandlerId handlerId = m_handlerId;
            m_bus = nullptr;
            m_handlerId = 0;
            bus->unsubscribe(eventName, handlerId);
            m_eventName.clear();
        }

        [[nodiscard]] bool valid() const noexcept { return m_bus != nullptr && m_handlerId != 0; }
        explicit operator bool() const noexcept { return valid(); }

        [[nodiscard]] HandlerId handlerId() const noexcept { return m_handlerId; }
        [[nodiscard]] const std::string &eventName() const noexcept { return m_eventName; }

      private:
        EventBus *m_bus = nullptr;
        std::string m_eventName;
        HandlerId m_handlerId = 0;
    };

    virtual ~EventBus() = default;

    virtual SubscriptionToken subscribe(const std::string &eventName, EventHandler handler) = 0;
    virtual void unsubscribe(const std::string &eventName, HandlerId handlerId) = 0;
    virtual void dispatch(const std::string &eventName, const EventContext &context) = 0;
    virtual void pump() = 0;
    virtual void advanceFrame() = 0;
    virtual std::size_t unconsumedCount() const = 0;
};

class NullEventBus : public EventBus
{
  public:
    SubscriptionToken subscribe(const std::string &, EventHandler) override { return {}; }
    void unsubscribe(const std::string &, HandlerId) override {}
    void dispatch(const std::string &, const EventContext &) override {}
    void pump() override {}
    void advanceFrame() override {}
    std::size_t unconsumedCount() const override { return 0; }
};

class BasicEventBus : public EventBus
{
  public:
    explicit BasicEventBus(std::shared_ptr<TelemetrySink> telemetry = nullptr);

    void setTelemetrySink(std::shared_ptr<TelemetrySink> telemetry);

    SubscriptionToken subscribe(const std::string &eventName, EventHandler handler) override;
    void unsubscribe(const std::string &eventName, HandlerId handlerId) override;
    void dispatch(const std::string &eventName, const EventContext &context) override;
    void pump() override;
    void advanceFrame() override;
    std::size_t unconsumedCount() const override;

  private:
    struct HandlerEntry
    {
        HandlerId id = 0;
        EventHandler handler;
    };

    struct QueuedEvent
    {
        std::string name;
        EventContext context;
        std::size_t age = 0;
    };

    struct PendingDelivery
    {
        std::string name;
        EventContext context;
        std::vector<HandlerEntry> handlers;
    };

    void notifyEventExpired(const std::string &eventName);
    void notifyHandlerException(const std::string &eventName, const std::string &message);

    std::shared_ptr<TelemetrySink> m_telemetry;
    std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<HandlerEntry>> m_handlers;
    std::deque<QueuedEvent> m_queue;
    std::atomic<std::size_t> m_unconsumedCount{0};
    HandlerId m_nextId = 1;
};


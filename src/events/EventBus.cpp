#include "events/EventBus.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

BasicEventBus::BasicEventBus(std::shared_ptr<TelemetrySink> telemetry) : m_telemetry(std::move(telemetry)) {}

void BasicEventBus::setTelemetrySink(std::shared_ptr<TelemetrySink> telemetry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_telemetry = std::move(telemetry);
}

EventBus::SubscriptionToken BasicEventBus::subscribe(const std::string &eventName, EventHandler handler)
{
    if (!handler)
    {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const HandlerId id = m_nextId++;
    m_handlers[eventName].push_back(HandlerEntry{id, std::move(handler)});
    return SubscriptionToken(this, eventName, id);
}

void BasicEventBus::unsubscribe(const std::string &eventName, HandlerId handlerId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_handlers.find(eventName);
    if (it == m_handlers.end())
    {
        return;
    }

    auto &entries = it->second;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [handlerId](const HandlerEntry &entry) {
                      return entry.id == handlerId;
                  }),
                  entries.end());
    if (entries.empty())
    {
        m_handlers.erase(it);
    }
}

void BasicEventBus::dispatch(const std::string &eventName, const EventContext &context)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back(QueuedEvent{eventName, context, 0});
}

void BasicEventBus::pump()
{
    std::vector<PendingDelivery> deliveries;
    std::deque<QueuedEvent> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_queue.empty())
        {
            QueuedEvent event = std::move(m_queue.front());
            m_queue.pop_front();

            auto it = m_handlers.find(event.name);
            if (it != m_handlers.end() && !it->second.empty())
            {
                deliveries.push_back(PendingDelivery{event.name, std::move(event.context), it->second});
            }
            else
            {
                pending.push_back(std::move(event));
            }
        }
        m_queue = std::move(pending);
    }

    for (const auto &delivery : deliveries)
    {
        for (const auto &entry : delivery.handlers)
        {
            try
            {
                entry.handler(delivery.context);
            }
            catch (const std::exception &ex)
            {
                notifyHandlerException(delivery.name, ex.what());
            }
            catch (...)
            {
                notifyHandlerException(delivery.name, "unknown");
            }
        }
    }
}

void BasicEventBus::advanceFrame()
{
    std::vector<QueuedEvent> expired;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_queue.begin(); it != m_queue.end();)
        {
            it->age += 1;
            if (it->age >= 2)
            {
                expired.push_back(std::move(*it));
                it = m_queue.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    if (!expired.empty())
    {
        m_unconsumedCount.fetch_add(expired.size(), std::memory_order_relaxed);
        for (const auto &event : expired)
        {
            notifyEventExpired(event.name);
        }
    }
}

std::size_t BasicEventBus::unconsumedCount() const
{
    return m_unconsumedCount.load(std::memory_order_relaxed);
}

void BasicEventBus::notifyEventExpired(const std::string &eventName)
{
    if (!m_telemetry)
    {
        return;
    }
    TelemetrySink::Payload payload{{"event", eventName}};
    m_telemetry->recordEvent("event_bus.no_listener", payload);
}

void BasicEventBus::notifyHandlerException(const std::string &eventName, const std::string &message)
{
    if (!m_telemetry)
    {
        return;
    }
    TelemetrySink::Payload payload{{"event", eventName}, {"reason", "handler_exception"}, {"message", message}};
    m_telemetry->recordEvent("event_bus.warning", payload);
}


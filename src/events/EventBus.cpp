#include "events/EventBus.h"

#include <algorithm>
#include <exception>
#include <utility>

BasicEventBus::BasicEventBus(std::shared_ptr<TelemetrySink> telemetry) : m_telemetry(std::move(telemetry)) {}

void BasicEventBus::setTelemetrySink(std::shared_ptr<TelemetrySink> telemetry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_telemetry = std::move(telemetry);
}

EventBus::HandlerId BasicEventBus::subscribe(const std::string &eventName, EventHandler handler)
{
    if (!handler)
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const HandlerId id = m_nextId++;
    m_handlers[eventName].push_back(HandlerEntry{id, std::move(handler)});
    return id;
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
    std::vector<HandlerEntry> handlers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_handlers.find(eventName);
        if (it != m_handlers.end())
        {
            handlers = it->second;
        }
    }

    if (handlers.empty())
    {
        notifyNoHandlers(eventName);
        return;
    }

    for (const auto &entry : handlers)
    {
        try
        {
            entry.handler(context);
        }
        catch (const std::exception &ex)
        {
            notifyHandlerException(eventName, ex.what());
        }
        catch (...)
        {
            notifyHandlerException(eventName, "unknown");
        }
    }
}

void BasicEventBus::notifyNoHandlers(const std::string &eventName)
{
    if (!m_telemetry)
    {
        return;
    }
    TelemetrySink::Payload payload{{"event", eventName}, {"reason", "no_handlers"}};
    m_telemetry->recordEvent("event_bus.warning", payload);
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


#include "services/ServiceLocator.h"

#include <utility>

ServiceLocator &ServiceLocator::instance()
{
    static ServiceLocator locator;
    return locator;
}

ServiceLocator::ServiceLocator()
{
    auto nullSink = std::make_shared<NullTelemetrySink>();
    Entry &entry = m_entries[std::type_index(typeid(TelemetrySink))];
    entry.fallback = nullSink;
    m_cachedTelemetry = nullSink;
}

void ServiceLocator::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    auto nullSink = std::make_shared<NullTelemetrySink>();
    Entry entry{};
    entry.fallback = nullSink;
    m_entries.emplace(std::type_index(typeid(TelemetrySink)), std::move(entry));
    m_cachedTelemetry = nullSink;
}

std::shared_ptr<TelemetrySink> ServiceLocator::telemetrySink() const
{
    if (auto cached = m_cachedTelemetry.lock())
    {
        return cached;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto cached = m_cachedTelemetry.lock())
    {
        return cached;
    }

    const auto it = m_entries.find(std::type_index(typeid(TelemetrySink)));
    if (it != m_entries.end())
    {
        if (auto instance = std::static_pointer_cast<TelemetrySink>(it->second.instance))
        {
            m_cachedTelemetry = instance;
            return instance;
        }
        if (auto fallback = std::static_pointer_cast<TelemetrySink>(it->second.fallback))
        {
            m_cachedTelemetry = fallback;
            return fallback;
        }
    }

    auto nullSink = std::make_shared<NullTelemetrySink>();
    m_entries[std::type_index(typeid(TelemetrySink))].fallback = nullSink;
    m_cachedTelemetry = nullSink;
    return nullSink;
}

void ServiceLocator::notifyFallback(const std::type_index &type, const std::string &reason) const
{
    TelemetrySink::Payload payload{{"service", type.name()}, {"reason", reason}};
    if (auto telemetry = telemetrySink())
    {
        telemetry->recordEvent("service_locator.fallback", payload);
    }
}

void ServiceLocator::updateTelemetryCacheLocked(const std::shared_ptr<void> &candidate) const
{
    if (candidate)
    {
        if (auto telemetry = std::static_pointer_cast<TelemetrySink>(candidate))
        {
            m_cachedTelemetry = telemetry;
            return;
        }
    }

    const auto it = m_entries.find(std::type_index(typeid(TelemetrySink)));
    if (it != m_entries.end())
    {
        if (auto instance = std::static_pointer_cast<TelemetrySink>(it->second.instance))
        {
            m_cachedTelemetry = instance;
            return;
        }
        if (auto fallback = std::static_pointer_cast<TelemetrySink>(it->second.fallback))
        {
            m_cachedTelemetry = fallback;
            return;
        }
    }

    m_cachedTelemetry.reset();
}


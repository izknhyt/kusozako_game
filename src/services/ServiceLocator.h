#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include "telemetry/TelemetrySink.h"

class ServiceLocator
{
  public:
    static ServiceLocator &instance();

    template <typename Service>
    void registerService(std::shared_ptr<Service> service)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto &entry = m_entries[std::type_index(typeid(Service))];
        entry.instance = std::move(service);
        if constexpr (std::is_base_of_v<TelemetrySink, Service>)
        {
            updateTelemetryCacheLocked(entry.instance);
        }
    }

    template <typename Service>
    void registerFallback(std::shared_ptr<Service> fallback)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto &entry = m_entries[std::type_index(typeid(Service))];
        entry.fallback = std::move(fallback);
        if constexpr (std::is_base_of_v<TelemetrySink, Service>)
        {
            updateTelemetryCacheLocked(entry.fallback);
        }
    }

    template <typename Service>
    std::shared_ptr<Service> getService() const
    {
        std::shared_ptr<Service> instance;
        std::shared_ptr<Service> fallback;
        bool usedFallback = false;
        std::string reason = "unregistered";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_entries.find(std::type_index(typeid(Service)));
            if (it != m_entries.end())
            {
                if (it->second.instance)
                {
                    instance = std::static_pointer_cast<Service>(it->second.instance);
                    reason.clear();
                }
                else if (it->second.fallback)
                {
                    fallback = std::static_pointer_cast<Service>(it->second.fallback);
                    usedFallback = true;
                    reason = "fallback";
                }
                else
                {
                    usedFallback = true;
                    reason = "missing";
                }
            }
            else
            {
                usedFallback = true;
                reason = "unregistered";
            }
        }

        if (usedFallback)
        {
            notifyFallback(std::type_index(typeid(Service)), reason);
        }

        if (instance)
        {
            return instance;
        }
        return fallback;
    }

    template <typename Service>
    void unregisterService()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_entries.find(std::type_index(typeid(Service)));
        if (it != m_entries.end())
        {
            it->second.instance.reset();
            if constexpr (std::is_base_of_v<TelemetrySink, Service>)
            {
                updateTelemetryCacheLocked(nullptr);
            }
            if (!it->second.fallback)
            {
                m_entries.erase(it);
            }
        }
    }

    void clear();

    std::shared_ptr<TelemetrySink> telemetrySink() const;

  private:
    struct Entry
    {
        std::shared_ptr<void> instance;
        std::shared_ptr<void> fallback;
    };

    ServiceLocator();

    void notifyFallback(const std::type_index &type, const std::string &reason) const;
    void updateTelemetryCacheLocked(const std::shared_ptr<void> &candidate) const;

    mutable std::mutex m_mutex;
    mutable std::unordered_map<std::type_index, Entry> m_entries;
    mutable std::weak_ptr<TelemetrySink> m_cachedTelemetry;
};


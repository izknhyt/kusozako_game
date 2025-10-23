#include "telemetry/ConsoleTelemetrySink.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
std::string currentTimestamp()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t time = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
} // namespace

void ConsoleTelemetrySink::recordEvent(std::string_view eventName, const Payload &payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "[telemetry] time=" << currentTimestamp() << " event=" << eventName;
    for (const auto &entry : payload)
    {
        std::cout << ' ' << entry.first << '=' << entry.second;
    }
    std::cout << '\n';
}

void ConsoleTelemetrySink::flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout.flush();
}


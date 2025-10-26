#include "telemetry/FileTelemetrySink.h"

#include "telemetry/ConsoleTelemetrySink.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string makeTimestampedName(std::uint64_t sequence)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    std::ostringstream oss;
    oss << "telemetry_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << '_'
        << std::setw(6) << std::setfill('0') << sequence << ".jsonl";
    return oss.str();
}
} // namespace

FileTelemetrySink::FileTelemetrySink() : FileTelemetrySink(std::make_shared<ConsoleTelemetrySink>()) {}

FileTelemetrySink::FileTelemetrySink(std::shared_ptr<TelemetrySink> fallback)
    : m_fallback(std::move(fallback))
{
    TelemetrySink::setOutputDirectory(fs::path("build") / "debug_dumps");
    TelemetrySink::setRotationThresholdBytes(10ull * 1024ull * 1024ull);
    TelemetrySink::setMaxRetentionFiles(8);
}

void FileTelemetrySink::recordEvent(std::string_view eventName, const Payload &payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureStreamLocked())
    {
        if (m_fallback)
        {
            m_fallback->recordEvent(eventName, payload);
        }
        return;
    }
    writeLineLocked(eventName, payload, true);
}

void FileTelemetrySink::flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stream.is_open())
    {
        m_stream.flush();
    }
    if (m_fallback)
    {
        m_fallback->flush();
    }
}

void FileTelemetrySink::setOutputDirectory(const std::filesystem::path &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    TelemetrySink::setOutputDirectory(path);
    closeStreamLocked();
    m_directoryReady = false;
}

void FileTelemetrySink::setRotationThresholdBytes(std::uintmax_t bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    TelemetrySink::setRotationThresholdBytes(bytes);
    if (shouldRotate())
    {
        rotateLocked();
    }
}

void FileTelemetrySink::setMaxRetentionFiles(std::size_t count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    TelemetrySink::setMaxRetentionFiles(count);
    pruneLogsLocked();
}

void FileTelemetrySink::requestFrameCapture()
{
    TelemetrySink::requestFrameCapture();
    std::lock_guard<std::mutex> lock(m_mutex);
    Payload payload;
    payload.emplace("requested", "true");
    logInternalEventLocked("telemetry.frame_capture.requested", std::move(payload), true, true);
}

bool FileTelemetrySink::ensureOutputDirectoryLocked()
{
    fs::path dir = outputDirectory();
    if (dir.empty())
    {
        dir = fs::path("build") / "debug_dumps";
        TelemetrySink::setOutputDirectory(dir);
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
    {
        std::error_code existsEc;
        if (!fs::exists(dir, existsEc) || existsEc)
        {
            m_directoryReady = false;
            if (m_fallback)
            {
                Payload payload;
                payload.emplace("path", dir.lexically_normal().string());
                payload.emplace("error", ec.message());
                m_fallback->recordEvent("telemetry.directory_unavailable", payload);
            }
            return false;
        }
    }

    m_directoryReady = true;
    return true;
}

bool FileTelemetrySink::ensureStreamLocked()
{
    if (!m_directoryReady && !ensureOutputDirectoryLocked())
    {
        return false;
    }
    if (m_stream.is_open())
    {
        return true;
    }

    const auto path = buildLogFilePath();
    std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        m_directoryReady = false;
        if (m_fallback)
        {
            Payload payload;
            payload.emplace("path", path.lexically_normal().string());
            payload.emplace("error", "failed_to_open");
            m_fallback->recordEvent("telemetry.log_open_failed", payload);
        }
        return false;
    }

    m_currentFile = path;
    m_stream = std::move(stream);
    m_bytesWritten = 0;

    Payload payload;
    payload.emplace("file", m_currentFile.lexically_normal().string());
    payload.emplace("rotation_bytes", std::to_string(rotationThresholdBytes()));
    logInternalEventLocked("telemetry.log.opened", std::move(payload), false, false);
    return true;
}

void FileTelemetrySink::closeStreamLocked()
{
    if (m_stream.is_open())
    {
        m_stream.flush();
        m_stream.close();
    }
    m_currentFile.clear();
    m_bytesWritten = 0;
}

void FileTelemetrySink::rotateLocked()
{
    const auto previous = m_currentFile;
    std::uintmax_t previousSize = 0;
    if (!previous.empty())
    {
        std::error_code sizeEc;
        previousSize = fs::file_size(previous, sizeEc);
        if (sizeEc)
        {
            previousSize = 0;
        }
    }

    closeStreamLocked();
    pruneLogsLocked();

    if (!ensureStreamLocked())
    {
        if (m_fallback)
        {
            Payload payload;
            payload.emplace("previous", previous.lexically_normal().string());
            payload.emplace("error", "rotation_failed");
            m_fallback->recordEvent("telemetry.rotation_failed", payload);
        }
        return;
    }

    Payload payload;
    if (!previous.empty())
    {
        payload.emplace("previous", previous.lexically_normal().string());
    }
    if (previousSize > 0)
    {
        payload.emplace("bytes", std::to_string(previousSize));
    }
    payload.emplace("current", m_currentFile.lexically_normal().string());
    payload.emplace("threshold", std::to_string(rotationThresholdBytes()));
    logInternalEventLocked("telemetry.rotation", std::move(payload), false, false);
}

void FileTelemetrySink::pruneLogsLocked()
{
    const std::size_t maxFiles = maxRetentionFiles();
    if (maxFiles == 0)
    {
        return;
    }

    const fs::path dir = outputDirectory();
    std::error_code iterEc;
    fs::directory_iterator iter(dir, iterEc);
    if (iterEc)
    {
        return;
    }

    std::vector<fs::directory_entry> entries;
    for (const auto &entry : iter)
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const fs::path &path = entry.path();
        if (path.extension() == ".jsonl")
        {
            const std::string filename = path.filename().string();
            if (filename.rfind("telemetry_", 0) == 0)
            {
                entries.push_back(entry);
            }
        }
    }

    if (entries.size() <= maxFiles)
    {
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry &a, const fs::directory_entry &b) {
        std::error_code taEc;
        const auto ta = fs::last_write_time(a.path(), taEc);
        std::error_code tbEc;
        const auto tb = fs::last_write_time(b.path(), tbEc);
        if (!taEc && !tbEc)
        {
            return ta > tb;
        }
        return a.path().filename().string() > b.path().filename().string();
    });

    for (std::size_t i = maxFiles; i < entries.size(); ++i)
    {
        std::error_code removeEc;
        fs::remove(entries[i].path(), removeEc);
        if (removeEc && m_fallback)
        {
            Payload payload;
            payload.emplace("path", entries[i].path().lexically_normal().string());
            payload.emplace("error", removeEc.message());
            m_fallback->recordEvent("telemetry.rotation_prune_failed", payload);
        }
    }
}

void FileTelemetrySink::logInternalEventLocked(std::string_view eventName, Payload payload, bool ensureStream, bool checkRotation)
{
    if (ensureStream && !ensureStreamLocked())
    {
        if (m_fallback)
        {
            m_fallback->recordEvent(eventName, payload);
        }
        return;
    }

    if (m_stream.is_open())
    {
        writeLineLocked(eventName, payload, checkRotation);
    }
    else if (m_fallback)
    {
        m_fallback->recordEvent(eventName, payload);
    }
}

void FileTelemetrySink::writeLineLocked(std::string_view eventName, PayloadView payload, bool checkRotation)
{
    if (!m_stream.is_open())
    {
        return;
    }

    const std::string line = formatEventLine(eventName, payload);
    m_stream << line;
    if (!m_stream.good())
    {
        m_directoryReady = false;
        if (m_fallback)
        {
            Payload errorPayload;
            errorPayload.emplace("file", m_currentFile.lexically_normal().string());
            errorPayload.emplace("error", "write_failed");
            m_fallback->recordEvent("telemetry.write_failed", errorPayload);
        }
        return;
    }

    m_bytesWritten += static_cast<std::uintmax_t>(line.size());
    if (checkRotation && shouldRotate())
    {
        rotateLocked();
    }
}

std::filesystem::path FileTelemetrySink::buildLogFilePath()
{
    const std::uint64_t sequence = ++m_sequence;
    fs::path dir = outputDirectory();
    if (dir.empty())
    {
        dir = fs::path("build") / "debug_dumps";
    }
    return dir / makeTimestampedName(sequence);
}

std::string FileTelemetrySink::escapeJson(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                std::ostringstream hex;
                hex << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch));
                escaped += hex.str();
            }
            else
            {
                escaped.push_back(ch);
            }
            break;
        }
    }
    return escaped;
}

std::string FileTelemetrySink::formatEventLine(std::string_view eventName, PayloadView payload) const
{
    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(payload.size());
    for (const auto &entry : payload)
    {
        entries.emplace_back(entry.first, entry.second);
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    std::ostringstream oss;
    oss << '{' << "\"event\":\"" << escapeJson(eventName) << "\"";
    for (const auto &entry : entries)
    {
        oss << ",\"" << escapeJson(entry.first) << "\":\"" << escapeJson(entry.second) << "\"";
    }
    oss << "}\n";
    return oss.str();
}

bool FileTelemetrySink::shouldRotate() const
{
    return rotationThresholdBytes() > 0 && m_bytesWritten >= rotationThresholdBytes();
}


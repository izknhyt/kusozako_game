#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "telemetry/TelemetrySink.h"

class FileTelemetrySink : public TelemetrySink
{
  public:
    FileTelemetrySink();
    explicit FileTelemetrySink(std::shared_ptr<TelemetrySink> fallback);

    void recordEvent(std::string_view eventName, const Payload &payload) override;
    void flush() override;
    void setOutputDirectory(const std::filesystem::path &path) override;
    void setRotationThresholdBytes(std::uintmax_t bytes) override;
    void setMaxRetentionFiles(std::size_t count) override;
    void requestFrameCapture() override;

  private:
    using PayloadView = const Payload &;

    bool ensureOutputDirectoryLocked();
    bool ensureStreamLocked();
    void closeStreamLocked();
    void rotateLocked();
    void pruneLogsLocked();
    void logInternalEventLocked(std::string_view eventName, Payload payload, bool ensureStream, bool checkRotation);
    void writeLineLocked(std::string_view eventName, PayloadView payload, bool checkRotation);
    std::filesystem::path buildLogFilePath();
    static std::string escapeJson(std::string_view value);
    std::string formatEventLine(std::string_view eventName, PayloadView payload) const;
    bool shouldRotate() const;

    std::mutex m_mutex;
    std::ofstream m_stream;
    std::shared_ptr<TelemetrySink> m_fallback;
    std::filesystem::path m_currentFile;
    std::uintmax_t m_bytesWritten = 0;
    std::uint64_t m_sequence = 0;
    bool m_directoryReady = false;
};


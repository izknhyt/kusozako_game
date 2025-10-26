#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

class TelemetrySink
{
  public:
    using Payload = std::unordered_map<std::string, std::string>;

    virtual ~TelemetrySink() = default;

    virtual void recordEvent(std::string_view eventName, const Payload &payload) = 0;
    virtual void flush() {}

    virtual void setOutputDirectory(const std::filesystem::path &path);
    [[nodiscard]] const std::filesystem::path &outputDirectory() const;

    virtual void setRotationThresholdBytes(std::uintmax_t bytes);
    [[nodiscard]] std::uintmax_t rotationThresholdBytes() const;

    virtual void setMaxRetentionFiles(std::size_t count);
    [[nodiscard]] std::size_t maxRetentionFiles() const;

    virtual void requestFrameCapture();
    [[nodiscard]] bool frameCaptureRequested() const;
    bool consumeFrameCaptureRequest();

  protected:
    std::filesystem::path m_outputDirectory{};
    std::uintmax_t m_rotationThresholdBytes = 0;
    std::size_t m_maxRetentionFiles = 0;

  private:
    std::atomic<bool> m_frameCaptureRequested{false};
};

class NullTelemetrySink : public TelemetrySink
{
  public:
    void recordEvent(std::string_view, const Payload &) override {}
};


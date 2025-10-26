#include "telemetry/TelemetrySink.h"

void TelemetrySink::setOutputDirectory(const std::filesystem::path &path)
{
    m_outputDirectory = path;
}

const std::filesystem::path &TelemetrySink::outputDirectory() const
{
    return m_outputDirectory;
}

void TelemetrySink::setRotationThresholdBytes(std::uintmax_t bytes)
{
    m_rotationThresholdBytes = bytes;
}

std::uintmax_t TelemetrySink::rotationThresholdBytes() const
{
    return m_rotationThresholdBytes;
}

void TelemetrySink::setMaxRetentionFiles(std::size_t count)
{
    m_maxRetentionFiles = count;
}

std::size_t TelemetrySink::maxRetentionFiles() const
{
    return m_maxRetentionFiles;
}

void TelemetrySink::requestFrameCapture()
{
    m_frameCaptureRequested.store(true, std::memory_order_relaxed);
}

bool TelemetrySink::frameCaptureRequested() const
{
    return m_frameCaptureRequested.load(std::memory_order_relaxed);
}

bool TelemetrySink::consumeFrameCaptureRequest()
{
    return m_frameCaptureRequested.exchange(false, std::memory_order_acq_rel);
}

#include "world/LegacySimulation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <ctime>
#include <utility>

#include "telemetry/TelemetrySink.h"
#include "world/spawn/WaveController.h"

namespace
{
std::string_view enemyTypeLabel(EnemyArchetype type)
{
    switch (type)
    {
    case EnemyArchetype::Wallbreaker: return "wallbreaker";
    case EnemyArchetype::Boss: return "boss";
    case EnemyArchetype::Slime:
    default: return "slime";
    }
}

std::string formatFloat(float value, int precision = 3)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

std::string boolString(bool value)
{
    return value ? "true" : "false";
}

std::string sanitizeForTsv(std::string value)
{
    for (char &ch : value)
    {
        if (ch == '\t' || ch == '\n' || ch == '\r')
        {
            ch = ' ';
        }
    }
    return value;
}

std::string formatTimestamp(const std::chrono::system_clock::time_point &tp)
{
    std::time_t raw = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
} // namespace

namespace world
{

std::filesystem::path LegacySimulation::telemetryDebugDirectory(const TelemetrySink &sink) const
{
    auto dir = sink.outputDirectory();
    if (dir.empty())
    {
        dir = std::filesystem::path("build") / "debug_dumps";
    }
    return dir;
}

void LegacySimulation::captureFrameSnapshot(TelemetrySink &sink)
{
    namespace fs = std::filesystem;

    const fs::path baseDir = telemetryDebugDirectory(sink);
    std::error_code dirEc;
    fs::create_directories(baseDir, dirEc);
    if (dirEc)
    {
        TelemetrySink::Payload payload;
        payload.emplace("path", baseDir.lexically_normal().string());
        payload.emplace("error", dirEc.message());
        sink.recordEvent("world.frame_capture.error", payload);
        frameCapturePending = 0;
        return;
    }

    if (frameCaptureBatch == 0)
    {
        frameCaptureBatch = 1;
    }
    ++frameCaptureIndex;

    std::ostringstream filename;
    filename << "frame_capture_" << std::setw(6) << std::setfill('0') << frameCaptureBatch << '_' << std::setw(2)
             << std::setfill('0') << frameCaptureIndex << ".json";
    const fs::path filePath = baseDir / filename.str();

    std::ofstream stream(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        TelemetrySink::Payload payload;
        payload.emplace("path", filePath.lexically_normal().string());
        payload.emplace("error", "open_failed");
        sink.recordEvent("world.frame_capture.error", payload);
        return;
    }

    stream << "{\n";
    stream << "  \"batch\": " << frameCaptureBatch << ",\n";
    stream << "  \"index\": " << frameCaptureIndex << ",\n";
    stream << "  \"frame\": " << frameCounter << ",\n";
    stream << "  \"sim_time\": " << formatFloat(simTime) << ",\n";
    stream << "  \"commander\": {\n";
    stream << "    \"alive\": " << boolString(commander.alive) << ",\n";
    stream << "    \"hp\": " << formatFloat(commander.hp) << ",\n";
    stream << "    \"pos\": {\"x\": " << formatFloat(commander.pos.x) << ", \"y\": " << formatFloat(commander.pos.y)
           << "}\n";
    stream << "  },\n";

    stream << "  \"yunas\": [\n";
    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        const Unit &yuna = yunas[i];
        if (i > 0)
        {
            stream << ",\n";
        }
        stream << "    {\"index\": " << i << ", \"job\": \"" << unitJobToString(yuna.job.job) << "\", \"hp\": "
               << formatFloat(yuna.hp) << ", \"morale\": \"" << moraleStateLabel(yuna.moraleState)
               << "\", \"pos\": {\"x\": " << formatFloat(yuna.pos.x) << ", \"y\": " << formatFloat(yuna.pos.y)
               << "}, \"follow_skill\": " << boolString(yuna.followBySkill) << ", \"follow_stance\": "
               << boolString(yuna.followByStance) << "}";
    }
    if (!yunas.empty())
    {
        stream << '\n';
    }
    stream << "  ],\n";

    stream << "  \"enemies\": [\n";
    for (std::size_t i = 0; i < enemies.size(); ++i)
    {
        const EnemyUnit &enemy = enemies[i];
        if (i > 0)
        {
            stream << ",\n";
        }
        stream << "    {\"index\": " << i << ", \"type\": \"" << enemyTypeLabel(enemy.type)
               << "\", \"hp\": " << formatFloat(enemy.hp) << ", \"pos\": {\"x\": "
               << formatFloat(enemy.pos.x) << ", \"y\": " << formatFloat(enemy.pos.y)
               << "}, \"radius\": " << formatFloat(enemy.radius) << "}";
    }
    if (!enemies.empty())
    {
        stream << '\n';
    }
    stream << "  ],\n";

    stream << "  \"walls\": [\n";
    for (std::size_t i = 0; i < walls.size(); ++i)
    {
        const WallSegment &wall = walls[i];
        if (i > 0)
        {
            stream << ",\n";
        }
        stream << "    {\"index\": " << i << ", \"hp\": " << formatFloat(wall.hp) << ", \"life\": "
               << formatFloat(wall.life) << ", \"pos\": {\"x\": " << formatFloat(wall.pos.x) << ", \"y\": "
               << formatFloat(wall.pos.y) << "}, \"radius\": " << formatFloat(wall.radius) << "}";
    }
    if (!walls.empty())
    {
        stream << '\n';
    }
    stream << "  ]\n";
    stream << "}\n";

    stream.flush();
    if (!stream.good())
    {
        TelemetrySink::Payload payload;
        payload.emplace("path", filePath.lexically_normal().string());
        payload.emplace("error", "write_failed");
        sink.recordEvent("world.frame_capture.error", payload);
        return;
    }

    stream.close();

    std::error_code sizeEc;
    const auto size = fs::file_size(filePath, sizeEc);

    TelemetrySink::Payload payload;
    payload.emplace("file", filePath.lexically_normal().string());
    payload.emplace("frame", std::to_string(frameCounter));
    payload.emplace("batch", std::to_string(frameCaptureBatch));
    payload.emplace("index", std::to_string(frameCaptureIndex));
    payload.emplace("yunas", std::to_string(yunas.size()));
    payload.emplace("enemies", std::to_string(enemies.size()));
    if (!sizeEc)
    {
        payload.emplace("bytes", std::to_string(size));
    }
    sink.recordEvent("world.frame_capture.saved", payload);
}

void LegacySimulation::handleSpawnDeferral(int deferredCount)
{
    if (deferredCount <= 0)
    {
        return;
    }

    spawnBudgetState.totalDeferred += static_cast<std::size_t>(deferredCount);

    std::string warningBase = config.spawnBudget.warningText;
    if (warningBase.empty())
    {
        warningBase = "Spawn queue delayed";
    }

    std::ostringstream oss;
    oss << warningBase << " (" << deferredCount << " deferred";
    if (config.spawnBudget.maxPerFrame > 0)
    {
        oss << ", budget " << config.spawnBudget.maxPerFrame << "/frame";
    }
    oss << ')';
    const std::string warningText = oss.str();

    pushTelemetry(warningText);

    HUDState::SpawnBudgetWarning &hudWarning = hud.spawnBudget;
    hudWarning.active = true;
    hudWarning.message = warningText;
    hudWarning.timer = std::max(config.telemetry_duration, 1.5f);
    hudWarning.lastDeferred = static_cast<std::size_t>(deferredCount);
    hudWarning.totalDeferred += static_cast<std::size_t>(deferredCount);

    if (auto sink = telemetry.lock())
    {
        TelemetrySink::Payload payload;
        payload.emplace("count", std::to_string(deferredCount));
        payload.emplace("total_deferred", std::to_string(hudWarning.totalDeferred));
        if (config.spawnBudget.maxPerFrame > 0)
        {
            payload.emplace("max_per_frame", std::to_string(config.spawnBudget.maxPerFrame));
        }
        payload.emplace("frame", std::to_string(frameCounter));
        sink->recordEvent("battle.spawn.budget_deferred", payload);
    }
}

LegacySimulation::SpawnHistoryDumpResult LegacySimulation::dumpSpawnHistory(const spawn::WaveController &controller) const
{
    SpawnHistoryDumpResult result;
    const auto history = controller.historySnapshot();
    if (history.empty())
    {
        result.error = "no_history";
        return result;
    }

    namespace fs = std::filesystem;
    fs::path baseDir;
    if (auto sink = telemetry.lock())
    {
        baseDir = telemetryDebugDirectory(*sink);
    }
    else
    {
        baseDir = fs::path("build") / "debug_dumps";
    }

    std::error_code dirEc;
    fs::create_directories(baseDir, dirEc);
    if (dirEc)
    {
        result.error = dirEc.message();
        return result;
    }

    const auto now = std::chrono::system_clock::now();
    std::time_t fileTime = std::chrono::system_clock::to_time_t(now);
    std::tm fileTm{};
#if defined(_WIN32)
    localtime_s(&fileTm, &fileTime);
#else
    localtime_r(&fileTime, &fileTm);
#endif
    std::ostringstream filename;
    filename << "spawn_history_" << std::put_time(&fileTm, "%Y%m%d_%H%M%S") << ".tsv";
    fs::path filePath = baseDir / filename.str();

    std::ofstream stream(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        result.error = "open_failed";
        return result;
    }

    stream << "wall_time\tsim_time\twave_index\tscheduled_time\tset_index\tgate\tenemy_type\tenemy_type_id\tcount\tinterval\ttelemetry\n";

    for (const auto &entry : history)
    {
        const std::string wallTime = formatTimestamp(entry.wallClock);
        const std::string telemetryText = sanitizeForTsv(entry.telemetry);
        if (entry.sets.empty())
        {
            stream << wallTime << '\t' << formatFloat(entry.triggerTime) << '\t' << entry.index << '\t' << formatFloat(entry.scheduledTime)
                   << '\t' << -1 << '\t' << "\t\t\t\t" << telemetryText << '\n';
            continue;
        }
        for (std::size_t i = 0; i < entry.sets.size(); ++i)
        {
            const SpawnSet &set = entry.sets[i];
            stream << wallTime << '\t' << formatFloat(entry.triggerTime) << '\t' << entry.index << '\t' << formatFloat(entry.scheduledTime)
                   << '\t' << i << '\t' << sanitizeForTsv(set.gate) << '\t' << enemyTypeLabel(set.type) << '\t'
                   << sanitizeForTsv(set.typeId) << '\t' << set.count << '\t' << formatFloat(set.interval)
                   << '\t' << telemetryText << '\n';
        }
    }

    stream.flush();
    if (!stream.good())
    {
        result.error = "write_failed";
        return result;
    }

    stream.close();
    result.success = true;
    result.path = std::move(filePath);
    return result;
}

} // namespace world


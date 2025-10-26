#include "world/LegacySimulation.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include "telemetry/TelemetrySink.h"

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

} // namespace world


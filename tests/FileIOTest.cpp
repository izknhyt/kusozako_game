#include "assets/FileIO.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

bool writeText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

std::string readText(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool assertTrue(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool success = true;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "kusozako_file_io_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::filesystem::path target = root / "sample.json";
    const std::string original = R"({"value":1})";
    const std::string updated = R"({"value":2})";

    success &= assertTrue(writeText(target, original), "Should prepare initial file");

    SaveFileOptions options;
    options.createBackup = true;
    auto result = atomicWriteFile(target, updated, options);
    success &= assertTrue(result.ok, "Atomic write should succeed");
    success &= assertTrue(std::filesystem::exists(target), "Target should exist");
    success &= assertTrue(readText(target) == updated, "Target contents should match updated text");
    success &= assertTrue(!result.backupPath.empty(), "Backup path should be populated");
    success &= assertTrue(std::filesystem::exists(result.backupPath), "Backup file should exist");
    success &= assertTrue(readText(result.backupPath) == original, "Backup should contain original contents");

    auto second = atomicWriteFile(target, updated, options);
    success &= assertTrue(second.ok, "Second write should succeed even when backup exists");

    std::filesystem::remove_all(root);
    return success ? 0 : 1;
}

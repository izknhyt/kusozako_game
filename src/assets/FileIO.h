#pragma once

#include <filesystem>
#include <string>

struct SaveFileOptions
{
    bool createBackup = true;
    bool keepTemp = false;
};

struct SaveFileResult
{
    bool ok = false;
    std::string error;
    std::filesystem::path writtenPath;
    std::filesystem::path backupPath;
};

SaveFileResult atomicWriteFile(const std::filesystem::path &targetPath, const std::string &contents,
                               const SaveFileOptions &options = {});

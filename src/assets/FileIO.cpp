#include "assets/FileIO.h"

#include <fstream>
#include <random>

SaveFileResult atomicWriteFile(const std::filesystem::path &targetPath, const std::string &contents,
                               const SaveFileOptions &options)
{
    SaveFileResult result;
    result.writtenPath = targetPath;
    std::error_code ec;

    const std::filesystem::path parent = targetPath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent))
    {
        if (!std::filesystem::create_directories(parent, ec) && ec)
        {
            result.error = "Failed to create parent directory: " + ec.message();
            return result;
        }
    }

    std::filesystem::path tempPath = targetPath;
    tempPath += ".tmp";

    {
        std::ofstream tempFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!tempFile.is_open())
        {
            result.error = "Failed to open temp file for write";
            return result;
        }
        tempFile.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!tempFile.good())
        {
            result.error = "Failed to write temp file";
            return result;
        }
    }

    if (options.createBackup && std::filesystem::exists(targetPath))
    {
        std::filesystem::path backupPath = targetPath;
        backupPath += ".bak";
        std::filesystem::copy_options copyOpts = std::filesystem::copy_options::overwrite_existing;
        std::filesystem::copy_file(targetPath, backupPath, copyOpts, ec);
        if (ec)
        {
            result.error = "Failed to create backup: " + ec.message();
            std::filesystem::remove(tempPath);
            return result;
        }
        result.backupPath = backupPath;
    }

    std::filesystem::rename(tempPath, targetPath, ec);
    if (ec)
    {
        result.error = "Failed to move temp file into place: " + ec.message();
        std::filesystem::remove(tempPath);
        return result;
    }

    if (!options.keepTemp)
    {
        std::filesystem::remove(tempPath);
    }

    result.ok = true;
    return result;
}

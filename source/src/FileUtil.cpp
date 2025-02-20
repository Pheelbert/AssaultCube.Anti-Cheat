#include "cube.h"

#include <windows.h>
#include <stdexcept>
#include <string>
#include <algorithm>

#include "FileUtil.h"

namespace PhantiCheat {

  // Helper function to extract the directory from a full path
  std::string getDirectoryFromPath(const std::string& fullPath) {
    size_t lastSlashPos = fullPath.find_last_of("\\/");
    if (lastSlashPos == std::string::npos) {
      return fullPath;  // No directory found, return as is
    }
    return fullPath.substr(0, lastSlashPos);
  }

  // Helper function to normalize path (replace '/' with '\')
  std::string normalizePath(const std::string& path) {
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    return normalized;
  }

  std::string FileUtil::getExecutableDirectory() {
    char buffer[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (length == 0) {
      throw std::runtime_error("Unable to get the executable path.");
    }

    // Extract the directory from the full path to the executable
    std::string exePath(buffer);
    return getDirectoryFromPath(exePath);
  }

  std::string FileUtil::makePathRelativeToExe(const std::string& filePath, const std::string& exeDir) {
    // Normalize paths to ensure consistency in comparison
    std::string normalizedExeDir = normalizePath(exeDir);
    std::string normalizedFilePath = normalizePath(filePath);

    // If the file path starts with the executable directory, remove the prefix
    if (normalizedFilePath.find(normalizedExeDir) == 0) {
      return normalizedFilePath.substr(normalizedExeDir.length() + 1); // Skip the directory and the separator
    }

    // If not relative, just return the original path
    return normalizedFilePath;
  }

  std::string FileUtil::buildAbsolutePath(const std::string& directoryPath, const std::string& filePath) {
    // Normalize the directory path
    std::string normalizedDir = normalizePath(directoryPath);

    // Ensure the directory path ends with a backslash
    if (!normalizedDir.empty() && normalizedDir.back() != '\\') {
      normalizedDir += '\\';
    }

    return normalizedDir + normalizePath(filePath);
  }

}

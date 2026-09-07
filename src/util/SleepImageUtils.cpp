#include "SleepImageUtils.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "CrossPointSettings.h"

namespace {
std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool pathIsDirectory(const std::string& path) {
  auto dir = Storage.open(path.c_str());
  const bool isDirectory = dir && dir.isDirectory();
  if (dir) {
    dir.close();
  }
  return isDirectory;
}
}  // namespace

bool SleepImageUtils::isSleepDirectoryName(const std::string& name) {
  if (name.empty()) {
    return false;
  }

  std::string baseName = name;
  if (!baseName.empty() && baseName.front() == '/') {
    baseName.erase(baseName.begin());
  }
  if (!baseName.empty() && baseName.front() == '.') {
    baseName.erase(baseName.begin());
  }

  const std::string lowerName = toLower(baseName);
  return lowerName == "sleep" || lowerName.rfind("sleep_", 0) == 0;
}

std::vector<std::string> SleepImageUtils::listSleepDirectories() {
  std::vector<std::string> directories;

  auto root = Storage.open("/");
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return directories;
  }

  root.rewindDirectory();

  char name[500];
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    const std::string directoryName{name};
    if (isSleepDirectoryName(directoryName)) {
      directories.push_back("/" + directoryName);
    }
    entry.close();
  }
  root.close();

  std::sort(directories.begin(), directories.end(),
            [](const std::string& left, const std::string& right) { return toLower(left) < toLower(right); });
  return directories;
}

std::vector<std::string> SleepImageUtils::listBmpFiles(const std::string& directoryPath) {
  std::vector<std::string> files;

  auto dir = Storage.open(directoryPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return files;
  }

  dir.rewindDirectory();

  char name[500];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    if (FsHelpers::hasBmpExtension(name)) {
      files.push_back(directoryPath + "/" + name);
    }
    entry.close();
  }
  dir.close();

  std::sort(files.begin(), files.end(),
            [](const std::string& left, const std::string& right) { return toLower(left) < toLower(right); });
  return files;
}

std::vector<std::string> SleepImageUtils::listImageFiles(const std::string& directoryPath) {
  std::vector<std::string> files;

  auto dir = Storage.open(directoryPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return files;
  }

  dir.rewindDirectory();

  char name[500];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    entry.getName(name, sizeof(name));
    const std::string_view fileName{name};
    if (FsHelpers::hasBmpExtension(fileName) || FsHelpers::hasPngExtension(fileName)) {
      files.push_back(directoryPath + "/" + name);
    }
    entry.close();
  }
  dir.close();

  std::sort(files.begin(), files.end(),
            [](const std::string& left, const std::string& right) { return toLower(left) < toLower(right); });
  return files;
}

std::string SleepImageUtils::resolveConfiguredSleepDirectory() {
  if (SETTINGS.sleepDirectory[0] != '\0') {
    const std::string configuredPath = SETTINGS.sleepDirectory;
    if (pathIsDirectory(configuredPath)) {
      return configuredPath;
    }
  }

  char resolvedPath[16];
  if (FsHelpers::resolveRootDirectoryIgnoreCase("/.sleep", resolvedPath, sizeof(resolvedPath))) {
    return resolvedPath;
  }
  if (FsHelpers::resolveRootDirectoryIgnoreCase("/sleep", resolvedPath, sizeof(resolvedPath))) {
    return resolvedPath;
  }

  const auto directories = listSleepDirectories();
  return directories.empty() ? "" : directories.front();
}

std::string SleepImageUtils::getDirectoryLabel(const std::string& directoryPath) {
  if (directoryPath.empty()) {
    return "";
  }

  const size_t separator = directoryPath.find_last_of('/');
  if (separator == std::string::npos || separator + 1 >= directoryPath.size()) {
    return directoryPath;
  }
  return directoryPath.substr(separator + 1);
}

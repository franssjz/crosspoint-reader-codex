#include "SleepScreenCache.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"

namespace {
uint32_t getSourceFileSize(const std::string& sourcePath) {
  HalFile file;
  if (!Storage.openFileForRead("SLC", sourcePath, file)) {
    return 0;
  }
  const uint32_t size = file.fileSize();
  file.close();
  return size;
}
}  // namespace

uint32_t SleepScreenCache::hashKey(const std::string& sourcePath, const uint32_t fileSize) {
  uint32_t hash = 2166136261u;
  for (char c : sourcePath) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  for (int i = 0; i < 4; i++) {
    hash ^= static_cast<uint8_t>((fileSize >> (i * 8)) & 0xFF);
    hash *= 16777619u;
  }
  hash ^= static_cast<uint8_t>(SETTINGS.sleepScreenCoverFilter);
  hash *= 16777619u;
  hash ^= static_cast<uint8_t>(SETTINGS.sleepScreenCoverMode);
  hash *= 16777619u;
  return hash;
}

std::string SleepScreenCache::getCachePath(const std::string& sourcePath, const uint32_t fileSize) {
  char filename[64];
  snprintf(filename, sizeof(filename), "%s/%08x.raw", CACHE_DIR, hashKey(sourcePath, fileSize));
  return std::string(filename);
}

bool SleepScreenCache::load(GfxRenderer& renderer, const std::string& sourcePath) {
  const uint32_t sourceSize = getSourceFileSize(sourcePath);
  if (sourceSize == 0) {
    return false;
  }

  const auto path = getCachePath(sourcePath, sourceSize);
  HalFile file;
  if (!Storage.openFileForRead("SLC", path, file)) {
    return false;
  }

  const uint32_t bufferSize = display.getBufferSize();
  if (file.fileSize() != bufferSize) {
    LOG_ERR("SLC", "Invalid cache size for %s", path.c_str());
    file.close();
    Storage.remove(path.c_str());
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  const int bytesRead = file.read(frameBuffer, bufferSize);
  file.close();

  if (bytesRead != static_cast<int>(bufferSize)) {
    LOG_ERR("SLC", "Incomplete cache read for %s", path.c_str());
    return false;
  }

  LOG_DBG("SLC", "Loaded cache: %s", path.c_str());
  return true;
}

void SleepScreenCache::save(const GfxRenderer& renderer, const std::string& sourcePath) {
  Storage.mkdir(CACHE_DIR);

  const uint32_t sourceSize = getSourceFileSize(sourcePath);
  if (sourceSize == 0) {
    return;
  }

  const auto path = getCachePath(sourcePath, sourceSize);
  HalFile file;
  if (!Storage.openFileForWrite("SLC", path, file)) {
    LOG_ERR("SLC", "Could not open cache file %s", path.c_str());
    return;
  }

  const uint32_t bufferSize = display.getBufferSize();
  const uint8_t* frameBuffer = renderer.getFrameBuffer();
  const size_t bytesWritten = file.write(frameBuffer, bufferSize);
  file.close();

  if (bytesWritten != bufferSize) {
    LOG_ERR("SLC", "Incomplete cache write for %s", path.c_str());
    Storage.remove(path.c_str());
    return;
  }

  LOG_DBG("SLC", "Saved cache: %s", path.c_str());
}

int SleepScreenCache::invalidateAll() {
  auto dir = Storage.open(CACHE_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return 0;
  }

  int count = 0;
  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    file.close();
    const auto fullPath = std::string(CACHE_DIR) + "/" + name;
    if (Storage.remove(fullPath.c_str())) {
      count++;
    }
  }
  dir.close();
  return count;
}

#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <FirmwareManifestJsonParser.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_wifi.h>
// clang-format on

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "version.h"

namespace {
constexpr char firmwareManifestUrl[] = "https://franssjz.github.io/cpr-vcodex/firmware/manifest.json";
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/franssjz/cpr-vcodex/releases/latest";
constexpr char otaCachePath[] = "/.crosspoint/ota-update.bin";

struct ParsedVersion {
  int parts[4] = {0, 0, 0, 0};
  bool parsed = false;
  bool isRc = false;
  bool isDev = false;
};

const char* currentVersionString() {
#ifdef VCODEX_VERSION
  return VCODEX_VERSION;
#else
  return CROSSPOINT_VERSION;
#endif
}

ParsedVersion parseVersion(const char* version) {
  ParsedVersion parsedVersion;
  if (!version) {
    return parsedVersion;
  }

  const char* cursor = version;
  while (*cursor && !std::isdigit(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }

  for (int index = 0; index < 4 && *cursor; ++index) {
    if (!std::isdigit(static_cast<unsigned char>(*cursor))) {
      break;
    }

    int value = 0;
    while (std::isdigit(static_cast<unsigned char>(*cursor))) {
      value = value * 10 + (*cursor - '0');
      ++cursor;
    }

    parsedVersion.parts[index] = value;
    parsedVersion.parsed = true;

    if (*cursor != '.') {
      break;
    }
    ++cursor;
  }

  parsedVersion.isRc = strstr(version, "-rc") != nullptr || strstr(version, ".rc") != nullptr;
  parsedVersion.isDev = strstr(version, "-dev") != nullptr || strstr(version, ".dev") != nullptr;
  return parsedVersion;
}

// The C3 X4/X3 binary is published without a board suffix (firmware.bin and
// <tag>.bin, matching every pre-existing release). Other boards get their own
// asset: firmware-<board>.bin / <tag>-<board>.bin (see ReleaseJsonParser).
bool isC3X4Board() { return board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0; }

void legacyAssetName(char* out, size_t outSize) {
  if (isC3X4Board()) {
    snprintf(out, outSize, "firmware.bin");
    return;
  }
  snprintf(out, outSize, "firmware-%.*s.bin", static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
}

// Stream the response straight into a parser as it arrives. Buffering the whole
// body in a std::string would add a growing allocation on top of the TLS
// session's heap during the fetch; with -fno-exceptions an OOM there aborts.
// fetchUrl handles the verified-https GET, redirects, and User-Agent.
template <typename Parser>
OtaUpdater::OtaUpdaterError performStreamingRequest(const char* url, Parser& parser, size_t& bytesReceived) {
  bytesReceived = 0;
  const bool ok = HttpDownloader::fetchUrl(url, [&parser, &bytesReceived](const uint8_t* data, size_t len) {
    bytesReceived += len;
    parser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  return ok ? OtaUpdater::OK : OtaUpdater::HTTP_ERROR;
}

class WifiPowerSaveGuard {
 public:
  WifiPowerSaveGuard() { esp_wifi_set_ps(WIFI_PS_NONE); }
  ~WifiPowerSaveGuard() { esp_wifi_set_ps(WIFI_PS_MIN_MODEM); }
};

struct FlashProgressCtx {
  OtaUpdater* updater = nullptr;
  OtaUpdater::ProgressCallback callback = nullptr;
  void* callbackCtx = nullptr;
};

void notifyProgress(OtaUpdater::ProgressCallback callback, void* ctx) {
  if (callback) {
    callback(ctx);
  }
}

void onFlashProgress(size_t written, size_t total, void* rawCtx) {
  auto* progressCtx = static_cast<FlashProgressCtx*>(rawCtx);
  if (!progressCtx || !progressCtx->updater) {
    return;
  }

  progressCtx->updater->setProgress(written, total);
  notifyProgress(progressCtx->callback, progressCtx->callbackCtx);
}

OtaUpdater::OtaUpdaterError mapFlashError(firmware_flash::Result result) {
  switch (result) {
    case firmware_flash::Result::OK:
      return OtaUpdater::OK;
    case firmware_flash::Result::OOM:
      return OtaUpdater::OOM_ERROR;
    case firmware_flash::Result::BAD_CHIP:
    case firmware_flash::Result::WRONG_BOARD:
      return OtaUpdater::WRONG_DEVICE_ERROR;
    default:
      return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  size_t bytesReceived = 0;
  OtaUpdaterError manifestResult = NO_UPDATE;

  // The auto-flash manifest only describes the C3 X4/X3 binary, so other boards
  // go straight to the GitHub release and its board-suffixed asset.
  if (isC3X4Board()) {
    FirmwareManifestJsonParser manifestParser;
    LOG_DBG("OTA", "Checking firmware manifest (current: %s)", currentVersionString());
    manifestResult = performStreamingRequest(firmwareManifestUrl, manifestParser, bytesReceived);
    LOG_DBG("OTA", "Manifest response received: %zu bytes total", bytesReceived);
    LOG_DBG("OTA", "Manifest parser result: manifest=%s", manifestParser.foundManifest() ? "yes" : "no");

    if (manifestResult == OK && manifestParser.foundManifest()) {
      latestVersion = manifestParser.getVersion();
      otaUrl = manifestParser.getDownloadUrl();
      otaSize = manifestParser.getFirmwareSize();
      totalSize = otaSize;
      updateAvailable = true;
      LOG_DBG("OTA", "Found update via manifest: tag=%s size=%zu", latestVersion.c_str(), otaSize);
      LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
      return OK;
    }

    if (manifestResult == OK) {
      LOG_ERR("OTA", "Firmware manifest missing version or downloadUrl");
      manifestResult = JSON_PARSE_ERROR;
    }
    LOG_DBG("OTA", "Falling back to latest GitHub release after manifest check failed: %d", manifestResult);
  } else {
    LOG_DBG("OTA", "Checking latest GitHub release (current: %s)", currentVersionString());
  }

  ReleaseJsonParser releaseParser;
  // Each board updates from its own release asset. The parser prefers the
  // tag-named asset (<tag>.bin / <tag>-<board>.bin) and falls back to the legacy
  // name set here; releases without a firmware asset are ignored.
  char assetName[48];
  legacyAssetName(assetName, sizeof(assetName));
  releaseParser.setFirmwareAssetName(assetName);
  const OtaUpdaterError releaseResult = performStreamingRequest(latestReleaseUrl, releaseParser, bytesReceived);
  LOG_DBG("OTA", "Release response received: %zu bytes total", bytesReceived);
  LOG_DBG("OTA", "Release parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (releaseResult != OK) {
    LOG_ERR("OTA", "Release check fetch failed");
    return isC3X4Board() && manifestResult != OK && manifestResult != NO_UPDATE ? manifestResult : releaseResult;
  }

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_INF("OTA", "No %s asset in latest release", assetName);
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update via release: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }

  const auto currentVersion = parseVersion(currentVersionString());
  const auto latest = parseVersion(latestVersion.c_str());
  if (!currentVersion.parsed || !latest.parsed) {
    return false;
  }

  const bool currentPreRelease = currentVersion.isRc || currentVersion.isDev;
  const bool latestPreRelease = latest.isRc || latest.isDev;
  if (currentVersion.isDev && !latestPreRelease) {
    return true;
  }

  for (int index = 0; index < 4; ++index) {
    if (latest.parts[index] != currentVersion.parts[index]) {
      return latest.parts[index] > currentVersion.parts[index];
    }
  }

  if (currentPreRelease != latestPreRelease) {
    return !latestPreRelease && currentPreRelease;
  }

  if (currentVersion.isRc != latest.isRc) {
    return !latest.isRc && currentVersion.isRc;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // The image is staged on the SD card and flashed from there (FirmwareFlasher)
  // rather than streamed into the OTA partition: the download then only holds
  // the TLS session, and validateImageFile() checks magic, checksum, SHA256,
  // chip_id and the embedded board tag before a single sector is erased.
  WifiPowerSaveGuard wifiPowerSaveGuard;
  Storage.mkdir("/.crosspoint");

  LOG_INF("OTA", "Downloading firmware to %s", otaCachePath);
  setProgress(0, otaSize);
  int lastReportedPercent = -1;
  const auto downloadResult = HttpDownloader::downloadToFile(
      otaUrl, otaCachePath, [this, onProgress, ctx, &lastReportedPercent](size_t downloaded, size_t total) {
        const size_t effectiveTotal = total > 0 ? total : otaSize;
        setProgress(downloaded, effectiveTotal);
        if (effectiveTotal > 0) {
          // Fire the callback only on whole-percent change: per-chunk updates wake
          // the render task, whose framebuffer work contends with TLS for heap.
          const int percent =
              static_cast<int>(std::min<size_t>(100, static_cast<uint64_t>(downloaded) * 100 / effectiveTotal));
          if (percent == lastReportedPercent) {
            return;
          }
          lastReportedPercent = percent;
        }
        notifyProgress(onProgress, ctx);
      });

  if (downloadResult != HttpDownloader::OK) {
    LOG_ERR("OTA", "Firmware download failed: %d", downloadResult);
    return downloadResult == HttpDownloader::FILE_ERROR ? INTERNAL_UPDATE_ERROR : HTTP_ERROR;
  }

  LOG_INF("OTA", "Flashing downloaded firmware");
  setProgress(0, otaSize);
  notifyProgress(onProgress, ctx);

  FlashProgressCtx progressCtx{this, onProgress, ctx};
  const auto flashResult = firmware_flash::flashFromSdPath(otaCachePath, onFlashProgress, &progressCtx);
  if (flashResult != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "Firmware flash failed: %s", firmware_flash::resultName(flashResult));
    Storage.remove(otaCachePath);
    return mapFlashError(flashResult);
  }

  Storage.remove(otaCachePath);

  LOG_INF("OTA", "Update completed");
  return OK;
}

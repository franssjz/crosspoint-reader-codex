#include "OtaUpdater.h"

#include <FirmwareManifestJsonParser.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <algorithm>
#include <cctype>
#include <cstring>

#include "esp_http_client.h"
#include "esp_wifi.h"
#include "FirmwareFlasher.h"
#include "HttpDownloader.h"
#include "version.h"

namespace {
constexpr char firmwareManifestUrl[] = "https://franssjz.github.io/cpr-vcodex/firmware/manifest.json";
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/franssjz/cpr-vcodex/releases/latest";
constexpr char otaCachePath[] = "/.crosspoint/ota-update.bin";

/*
 * When esp_crt_bundle.h is included here, Arduino's include path can resolve
 * the wrong header. Keep the upstream streaming OTA implementation but retain
 * the explicit declaration that already worked in CPR-vCodex.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

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

std::string buildUserAgent() { return std::string("CrossPoint-ESP32-") + currentVersionString(); }

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

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  const std::string userAgent = buildUserAgent();
  return esp_http_client_set_header(http_client, "User-Agent", userAgent.c_str());
}

size_t totalBytesReceived = 0;

esp_err_t manifest_event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  totalBytesReceived += event->data_len;
  LOG_DBG("OTA", "HTTP chunk: %d bytes (total: %zu)", event->data_len, totalBytesReceived);
  auto* parser = static_cast<FirmwareManifestJsonParser*>(event->user_data);
  parser->feed(static_cast<const char*>(event->data), event->data_len);
  return ESP_OK;
}

esp_err_t release_event_handler(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  totalBytesReceived += event->data_len;
  LOG_DBG("OTA", "HTTP chunk: %d bytes (total: %zu)", event->data_len, totalBytesReceived);
  auto* parser = static_cast<ReleaseJsonParser*>(event->user_data);
  parser->feed(static_cast<const char*>(event->data), event->data_len);
  return ESP_OK;
}

OtaUpdater::OtaUpdaterError performStreamingRequest(const char* url, esp_err_t (*eventHandler)(esp_http_client_event_t*),
                                                    void* userData, const int bufferSize) {
  esp_http_client_config_t client_config = {
      .url = url,
      .event_handler = eventHandler,
      .buffer_size = bufferSize,
      .buffer_size_tx = 1024,
      .user_data = userData,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .keep_alive_enable = true,
  };

  totalBytesReceived = 0;

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  const std::string userAgent = buildUserAgent();
  esp_err_t esp_err = esp_http_client_set_header(client_handle, "User-Agent", userAgent.c_str());
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    esp_http_client_cleanup(client_handle);
    return OtaUpdater::HTTP_ERROR;
  }

  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  return OtaUpdater::OK;
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

  FirmwareManifestJsonParser manifestParser;
  LOG_DBG("OTA", "Checking firmware manifest (current: %s)", currentVersionString());
  OtaUpdaterError manifestResult = performStreamingRequest(firmwareManifestUrl, manifest_event_handler, &manifestParser, 2048);
  LOG_DBG("OTA", "Manifest response received: %zu bytes total", totalBytesReceived);
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

  ReleaseJsonParser releaseParser;
  LOG_DBG("OTA", "Falling back to latest GitHub release after manifest check failed: %d", manifestResult);
  const OtaUpdaterError releaseResult = performStreamingRequest(latestReleaseUrl, release_event_handler, &releaseParser, 4096);
  LOG_DBG("OTA", "Release response received: %zu bytes total", totalBytesReceived);
  LOG_DBG("OTA", "Release parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (releaseResult != OK) {
    return manifestResult != OK ? manifestResult : releaseResult;
  }

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON fallback");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No OTA firmware asset found in release fallback");
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update via release fallback: tag=%s size=%zu", latestVersion.c_str(), otaSize);
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

  WifiPowerSaveGuard wifiPowerSaveGuard;
  Storage.mkdir("/.crosspoint");

  LOG_INF("OTA", "Downloading firmware to %s", otaCachePath);
  setProgress(0, otaSize);
  int lastReportedPercent = -1;
  const auto downloadResult = HttpDownloader::downloadToFile(
      otaUrl, otaCachePath,
      [this, onProgress, ctx, &lastReportedPercent](size_t downloaded, size_t total) {
        const size_t effectiveTotal = total > 0 ? total : otaSize;
        setProgress(downloaded, effectiveTotal);
        if (effectiveTotal > 0) {
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
    return mapFlashError(flashResult);
  }

  Storage.remove(otaCachePath);

  LOG_INF("OTA", "Update completed");
  return OK;
}

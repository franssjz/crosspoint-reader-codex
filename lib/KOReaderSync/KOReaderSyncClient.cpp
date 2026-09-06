#include "KOReaderSyncClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include "KOReaderCredentialStore.h"

int KOReaderSyncClient::lastHttpCode = 0;
int KOReaderSyncClient::lastEspError = 0;
unsigned KOReaderSyncClient::lastHeapAtFailure = 0;
unsigned KOReaderSyncClient::lastContigHeapAtFailure = 0;
const char* KOReaderSyncClient::lastOperation = "";

namespace {
bool g_keepSessionOpen = false;
esp_http_client_handle_t g_sessionClient = nullptr;

enum class EndpointProfile : uint8_t {
  UNKNOWN = 0,
  LEGACY_ROOT,
  CWA_KOSYNC,
};

EndpointProfile g_lastResolvedProfile = EndpointProfile::UNKNOWN;

// Static buffer for the detail string returned by lastFailureDetail()
char g_failureDetailBuf[160] = {0};
char g_lastResponsePreview[160] = {0};

std::string previewBody(const char* body, const size_t maxLen = 120) {
  if (!body || !*body) {
    return "<empty>";
  }

  std::string preview;
  preview.reserve(maxLen);
  for (const char* p = body; *p && preview.size() < maxLen; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '\r' || c == '\n' || c == '\t') {
      preview.push_back(' ');
    } else if (std::isprint(c)) {
      preview.push_back(static_cast<char>(c));
    } else {
      preview.push_back('?');
    }
  }

  if (strlen(body) > preview.size()) {
    preview += "...";
  }
  return preview;
}

void rememberResponsePreview(const char* body) {
  const std::string preview = previewBody(body, sizeof(g_lastResponsePreview) - 1);
  strncpy(g_lastResponsePreview, preview.c_str(), sizeof(g_lastResponsePreview) - 1);
  g_lastResponsePreview[sizeof(g_lastResponsePreview) - 1] = '\0';
}

void beginRequest(const char* operation) {
  KOReaderSyncClient::lastOperation = operation;
  KOReaderSyncClient::lastEspError = 0;
  KOReaderSyncClient::lastHttpCode = 0;
  KOReaderSyncClient::lastHeapAtFailure = ESP.getFreeHeap();
  KOReaderSyncClient::lastContigHeapAtFailure = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
  g_lastResponsePreview[0] = '\0';
}

// Skip a leading UTF-8 BOM and ASCII whitespace; returns pointer to first content char.
// Used to detect captive-portal HTML responses where first char != '{'.
const char* skipBomAndWhitespace(const char* p) {
  if (!p) {
    return "";
  }
  if (static_cast<unsigned char>(p[0]) == 0xEF && p[1] != '\0' && p[2] != '\0' &&
      static_cast<unsigned char>(p[1]) == 0xBB && static_cast<unsigned char>(p[2]) == 0xBF) {
    p += 3;
  }
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
    p++;
  }
  return p;
}

constexpr char DEVICE_NAME[] = "CrossPoint";
constexpr char DEVICE_ID[] = "crosspoint-reader";

// Small TLS buffers reduce peak handshake memory. KOSync payloads are tiny JSON.
constexpr int HTTP_BUF_SIZE = 1024;
constexpr unsigned TLS_CONTIG_HEAP_TOLERANCE = 0;

bool endsWith(const std::string& value, const char* suffix) {
  const size_t suffixLen = strlen(suffix);
  return value.size() >= suffixLen && value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

bool startsWithIgnoreCase(const char* value, const char* prefix) {
  if (!value) return false;
  const size_t prefixLen = strlen(prefix);
  if (strlen(value) < prefixLen) return false;
  for (size_t i = 0; i < prefixLen; ++i) {
    if (std::tolower(static_cast<unsigned char>(value[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
      return false;
    }
  }
  return true;
}

bool startsWithIgnoreCase(const std::string& value, const char* prefix) {
  return startsWithIgnoreCase(value.c_str(), prefix);
}

bool urlUsesTls(const char* url) { return startsWithIgnoreCase(url, "https://"); }

bool urlUsesTls(const std::string& url) { return urlUsesTls(url.c_str()); }

std::string trimTrailingSlashes(std::string url) {
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url;
}

std::string configuredBaseUrl() { return trimTrailingSlashes(KOREADER_STORE.getBaseUrl()); }

bool configuredEndpointUsesTls() {
  const std::string& customUrl = KOREADER_STORE.getServerUrl();
  if (customUrl.empty()) {
    return true;
  }
  if (customUrl.find("://") == std::string::npos) {
    return false;
  }
  return urlUsesTls(customUrl);
}

std::string rootBaseUrl() {
  std::string root = configuredBaseUrl();
  if (endsWith(root, "/kosync")) {
    root.resize(root.size() - strlen("/kosync"));
  }
  return root;
}

std::string kosyncBaseUrl() {
  std::string base = configuredBaseUrl();
  if (!endsWith(base, "/kosync")) {
    base = rootBaseUrl() + "/kosync";
  }
  return base;
}

std::string buildEndpointUrl(const EndpointProfile profile, const std::string& path) {
  const std::string base = (profile == EndpointProfile::CWA_KOSYNC) ? kosyncBaseUrl() : rootBaseUrl();
  return base + path;
}

int buildCandidateProfiles(EndpointProfile profiles[2]) {
  if (endsWith(configuredBaseUrl(), "/kosync")) {
    profiles[0] = EndpointProfile::CWA_KOSYNC;
    return 1;
  }

  if (g_lastResolvedProfile != EndpointProfile::UNKNOWN) {
    profiles[0] = g_lastResolvedProfile;
    profiles[1] = (g_lastResolvedProfile == EndpointProfile::CWA_KOSYNC) ? EndpointProfile::LEGACY_ROOT
                                                                         : EndpointProfile::CWA_KOSYNC;
    return 2;
  }

  profiles[0] = EndpointProfile::LEGACY_ROOT;
  profiles[1] = EndpointProfile::CWA_KOSYNC;
  return 2;
}

void rememberResolvedProfile(const EndpointProfile profile) {
  if (profile != EndpointProfile::UNKNOWN) {
    g_lastResolvedProfile = profile;
  }
}

void logWifiSnapshot(const char* stage) {
  const wl_status_t status = WiFi.status();
  const int32_t rssi = WiFi.RSSI();
  LOG_DBG("KOSync", "%s: wifi_status=%d rssi=%ld ip=%s", stage, static_cast<int>(status), static_cast<long>(rssi),
          WiFi.localIP().toString().c_str());
}

struct ResponseBuffer {
  char* data = nullptr;
  int len = 0;
  int capacity = 0;

  ~ResponseBuffer() { free(data); }

  bool ensure(int size) {
    if (size <= capacity) return true;
    char* newData = static_cast<char*>(realloc(data, size));
    if (!newData) return false;
    data = newData;
    capacity = size;
    return true;
  }
};

bool responseLooksLikeHtml(const ResponseBuffer* buf) {
  if (!buf || !buf->data || buf->data[0] == '\0') {
    return false;
  }
  return *skipBomAndWhitespace(buf->data) == '<';
}

ResponseBuffer g_sessionResponseBuf;

void clearResponseBuffer(ResponseBuffer* buf) {
  if (!buf) return;
  if (buf->data) {
    free(buf->data);
    buf->data = nullptr;
  }
  buf->len = 0;
  buf->capacity = 0;
}

void resetResponseBuffer(ResponseBuffer* buf) {
  if (!buf) return;
  buf->len = 0;
  if (buf->data) {
    buf->data[0] = '\0';
  }
}

ResponseBuffer* effectiveResponseBuffer(ResponseBuffer* localBuf) {
  return g_keepSessionOpen ? &g_sessionResponseBuf : localBuf;
}

esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf) {
    if (buf->ensure(buf->len + evt->data_len + 1)) {
      memcpy(buf->data + buf->len, evt->data, evt->data_len);
      buf->len += evt->data_len;
      buf->data[buf->len] = '\0';
    } else {
      LOG_ERR("KOSync", "Response buffer allocation failed (%d bytes)", evt->data_len);
    }
  }
  if (evt->event_id == HTTP_EVENT_REDIRECT && buf) {
    // Clear intermediate body so final response accumulates cleanly.
    buf->len = 0;
    if (buf->data) buf->data[0] = '\0';
  }
  return ESP_OK;
}

// Refuse to attempt a TLS handshake when fragmentation makes it doomed.
// Total free heap misleads because fragmentation can leave no block big enough.
bool checkHeapForTls() {
  if (!configuredEndpointUsesTls()) {
    return true;
  }

  const bool hasReusableSession = g_keepSessionOpen && g_sessionClient != nullptr;
  const bool isUpload =
      (KOReaderSyncClient::lastOperation && strcmp(KOReaderSyncClient::lastOperation, "update progress") == 0);
  const unsigned requiredContig = KOReaderSyncClient::MIN_CONTIG_HEAP_FOR_TLS;

  if (isUpload && hasReusableSession) {
    return true;
  }

  if (KOReaderSyncClient::lastContigHeapAtFailure + TLS_CONTIG_HEAP_TOLERANCE < requiredContig) {
    LOG_ERR("KOSync", "Insufficient contiguous heap for TLS: %u available, %u required",
            KOReaderSyncClient::lastContigHeapAtFailure, requiredContig);
    KOReaderSyncClient::lastEspError = ESP_ERR_NO_MEM;
    return false;
  }
  return true;
}

void refreshHeapSnapshot() {
  KOReaderSyncClient::lastHeapAtFailure = ESP.getFreeHeap();
  KOReaderSyncClient::lastContigHeapAtFailure = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DEFAULT);
}

void logTlsAttemptPlan(const char* operation, int attempt) {
  const bool usesTls = configuredEndpointUsesTls();
  const bool isUpload = (operation && strcmp(operation, "update progress") == 0);
  const bool hasReusableSession = g_keepSessionOpen && g_sessionClient != nullptr;
  unsigned requiredContig = 0;
  if (usesTls) {
    requiredContig = (isUpload && hasReusableSession) ? KOReaderSyncClient::MIN_CONTIG_HEAP_FOR_TLS_UPLOAD
                                                      : KOReaderSyncClient::MIN_CONTIG_HEAP_FOR_TLS;
  }
  const char* tlsMode = !usesTls ? "plain" : (isUpload && hasReusableSession) ? "reuse" : "handshake";

  LOG_DBG("KOSync", "%s attempt %d: keep_session=%s reusable=%s tls_mode=%s heap=%u contig=%u need=%u",
          operation ? operation : "request", attempt, g_keepSessionOpen ? "yes" : "no",
          hasReusableSession ? "yes" : "no", tlsMode, KOReaderSyncClient::lastHeapAtFailure,
          KOReaderSyncClient::lastContigHeapAtFailure, requiredContig);
}

void resetSessionClientForRetry() {
  if (g_sessionClient) {
    esp_http_client_cleanup(g_sessionClient);
    g_sessionClient = nullptr;
  }
}

void applyAuthHeaders(esp_http_client_handle_t client) {
  esp_http_client_set_header(client, "Accept", "application/vnd.koreader.v1+json");
  esp_http_client_set_header(client, "x-auth-user", KOREADER_STORE.getUsername().c_str());
  esp_http_client_set_header(client, "x-auth-key", KOREADER_STORE.getMd5Password().c_str());
}

esp_http_client_handle_t createClient(const char* url, ResponseBuffer* buf,
                                      esp_http_client_method_t method = HTTP_METHOD_GET) {
  ResponseBuffer* activeBuf = effectiveResponseBuffer(buf);

  if (g_keepSessionOpen && g_sessionClient) {
    esp_http_client_set_url(g_sessionClient, url);
    esp_http_client_set_method(g_sessionClient, method);
    applyAuthHeaders(g_sessionClient);
    return g_sessionClient;
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.event_handler = httpEventHandler;
  config.user_data = activeBuf;
  config.method = method;
  config.timeout_ms = 5000;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = 512;
  if (urlUsesTls(url)) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }
  config.keep_alive_enable = g_keepSessionOpen;
  config.max_redirection_count = 3;
  config.username = KOREADER_STORE.getUsername().c_str();
  config.password = KOREADER_STORE.getPassword().c_str();
  config.auth_type = HTTP_AUTH_TYPE_BASIC;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return nullptr;

  applyAuthHeaders(client);

  if (g_keepSessionOpen) {
    g_sessionClient = client;
  }

  return client;
}
}  // namespace

static inline bool hasCredentials() {
  if (KOREADER_STORE.hasCredentials()) return true;
  LOG_INF("KOSync", "No credentials configured");
  return false;
}

void KOReaderSyncClient::beginPersistentSession() {
  g_keepSessionOpen = true;
  clearResponseBuffer(&g_sessionResponseBuf);
}

void KOReaderSyncClient::endPersistentSession() {
  g_keepSessionOpen = false;
  if (g_sessionClient) {
    esp_http_client_cleanup(g_sessionClient);
    g_sessionClient = nullptr;
  }
  clearResponseBuffer(&g_sessionResponseBuf);
}

KOReaderSyncClient::Error KOReaderSyncClient::registerUser() {
  if (!hasCredentials()) return NO_CREDENTIALS;

  return registerUser(KOREADER_STORE.getUsername(), KOREADER_STORE.getMd5Password(), KOREADER_STORE.getBaseUrl());
}

KOReaderSyncClient::Error KOReaderSyncClient::registerUser(const std::string& username, const std::string& md5Password,
                                                           const std::string& baseUrl) {
  if (username.empty() || md5Password.empty()) return NO_CREDENTIALS;

  beginRequest("register");
  if (urlUsesTls(baseUrl) && lastContigHeapAtFailure < MIN_CONTIG_HEAP_FOR_TLS) {
    lastEspError = ESP_ERR_NO_MEM;
    return NETWORK_ERROR;
  }

  std::string url = trimTrailingSlashes(baseUrl) + "/users/create/";
  LOG_INF("KOSync", "Registering user: %s (heap: %u, contig: %u)", url.c_str(), lastHeapAtFailure,
          lastContigHeapAtFailure);

  JsonDocument doc;
  doc["username"] = username;
  doc["password"] = md5Password;
  std::string body;
  serializeJson(doc, body);

  ResponseBuffer buf;
  // Registration is deliberately independent from the active sync session:
  // profile editing may target a different account and server.
  ResponseBuffer* activeBuf = &buf;
  resetResponseBuffer(activeBuf);
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.event_handler = httpEventHandler;
  config.user_data = activeBuf;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = 5000;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = 512;
  if (urlUsesTls(url)) config.crt_bundle_attach = esp_crt_bundle_attach;
  config.max_redirection_count = 3;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    lastEspError = ESP_ERR_NO_MEM;
    return NETWORK_ERROR;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_post_field(client, body.c_str(), body.length());

  esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  lastEspError = err;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Register response: %d (err: %s)", httpCode, esp_err_to_name(err));

  if (err != ESP_OK) return NETWORK_ERROR;
  if (httpCode >= 300 && httpCode < 400) return REDIRECT_ERROR;

  if (httpCode == 201) {
    return OK;
  } else if (httpCode == 200) {
    return USER_EXISTS;
  } else if (httpCode == 402) {
    std::string lowerBody = activeBuf->data ? activeBuf->data : "";
    std::transform(lowerBody.begin(), lowerBody.end(), lowerBody.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowerBody.find("already") != std::string::npos) {
      return USER_EXISTS;
    }
    return REGISTRATION_DISABLED;
  } else if (httpCode == 409) {
    return USER_EXISTS;
  }
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  if (!hasCredentials()) return NO_CREDENTIALS;

  beginRequest("auth");

  ResponseBuffer buf;
  ResponseBuffer* activeBuf = effectiveResponseBuffer(&buf);
  EndpointProfile profiles[2] = {EndpointProfile::UNKNOWN, EndpointProfile::UNKNOWN};
  const int profileCount = buildCandidateProfiles(profiles);

  for (int profileIndex = 0; profileIndex < profileCount; profileIndex++) {
    const EndpointProfile profile = profiles[profileIndex];
    const bool hasFallback = profileIndex + 1 < profileCount;
    const std::string url = buildEndpointUrl(profile, "/users/auth");
    esp_err_t err = ESP_FAIL;
    int httpCode = 0;

    LOG_DBG("KOSync", "Authenticating: %s (heap: %u, contig: %u)", url.c_str(), lastHeapAtFailure,
            lastContigHeapAtFailure);

    for (int attempt = 1; attempt <= 3; attempt++) {
      refreshHeapSnapshot();
      logTlsAttemptPlan("auth", attempt);
      if (!checkHeapForTls()) return NETWORK_ERROR;

      resetResponseBuffer(activeBuf);
      esp_http_client_handle_t client = createClient(url.c_str(), &buf);
      if (!client) {
        lastEspError = ESP_ERR_NO_MEM;
        return NETWORK_ERROR;
      }

      logWifiSnapshot("WiFi before auth");
      err = esp_http_client_perform(client);
      httpCode = esp_http_client_get_status_code(client);
      lastHttpCode = httpCode;
      lastEspError = err;
      if (!g_keepSessionOpen) {
        esp_http_client_cleanup(client);
      }

      LOG_DBG("KOSync", "Auth %s -> %d (err: %s) [attempt %d]", url.c_str(), httpCode, esp_err_to_name(err), attempt);
      if (err == ESP_OK && (httpCode < 200 || httpCode >= 300)) {
        rememberResponsePreview(activeBuf->data);
      }

      const bool retryable = (err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_HTTP_EAGAIN);
      if (err == ESP_OK || !retryable || attempt == 3) break;

      resetSessionClientForRetry();
      LOG_ERR("KOSync", "auth failed attempt %d, retrying", attempt);
      logWifiSnapshot("WiFi before auth retry");
      delay(400 * attempt);
    }

    if (err != ESP_OK) {
      if (hasFallback) {
        continue;
      }
      return NETWORK_ERROR;
    }
    if (httpCode >= 300 && httpCode < 400) {
      if (hasFallback) {
        continue;
      }
      return REDIRECT_ERROR;
    }
    if (httpCode >= 200 && httpCode < 300) {
      // Accept empty body or JSON body. Reject non-empty non-JSON (login/captive-portal HTML).
      if (activeBuf->data && activeBuf->data[0] != '\0') {
        const char* first = skipBomAndWhitespace(activeBuf->data);
        if (*first != '\0' && *first != '{') {
          rememberResponsePreview(activeBuf->data);
          if (hasFallback && responseLooksLikeHtml(activeBuf)) {
            continue;
          }
          return INVALID_RESPONSE;
        }
      }
      rememberResolvedProfile(profile);
      return OK;
    }
    if (httpCode == 401) {
      rememberResolvedProfile(profile);
      return AUTH_FAILED;
    }

    if (!hasFallback) {
      return SERVER_ERROR;
    }
  }

  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  if (!hasCredentials()) return NO_CREDENTIALS;

  beginRequest("get progress");
  if (!checkHeapForTls()) return NETWORK_ERROR;

  ResponseBuffer buf;
  ResponseBuffer* activeBuf = effectiveResponseBuffer(&buf);
  EndpointProfile profiles[2] = {EndpointProfile::UNKNOWN, EndpointProfile::UNKNOWN};
  const int profileCount = buildCandidateProfiles(profiles);

  for (int profileIndex = 0; profileIndex < profileCount; profileIndex++) {
    const EndpointProfile profile = profiles[profileIndex];
    const bool hasFallback = profileIndex + 1 < profileCount;
    const std::string url = buildEndpointUrl(profile, "/syncs/progress/" + documentHash);
    esp_err_t err = ESP_FAIL;
    int httpCode = 0;

    LOG_DBG("KOSync", "Getting progress: %s (heap: %u, contig: %u)", url.c_str(), lastHeapAtFailure,
            lastContigHeapAtFailure);

    for (int attempt = 1; attempt <= 3; attempt++) {
      refreshHeapSnapshot();
      logTlsAttemptPlan("get progress", attempt);
      if (!checkHeapForTls()) return NETWORK_ERROR;

      resetResponseBuffer(activeBuf);

      esp_http_client_handle_t client = createClient(url.c_str(), &buf);
      if (!client) {
        lastEspError = ESP_ERR_NO_MEM;
        return NETWORK_ERROR;
      }

      logWifiSnapshot("WiFi before getProgress");
      err = esp_http_client_perform(client);
      httpCode = esp_http_client_get_status_code(client);
      lastHttpCode = httpCode;
      lastEspError = err;
      if (!g_keepSessionOpen) {
        esp_http_client_cleanup(client);
      }

      LOG_DBG("KOSync", "GET %s -> %d (err: %s) [attempt %d]", url.c_str(), httpCode, esp_err_to_name(err), attempt);
      if (err == ESP_OK && (httpCode < 200 || httpCode >= 300)) {
        rememberResponsePreview(activeBuf->data);
      }

      const bool retryable = (err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_HTTP_EAGAIN);
      if (err == ESP_OK || !retryable || attempt == 3) break;

      resetSessionClientForRetry();
      LOG_ERR("KOSync", "getProgress failed attempt %d, retrying", attempt);
      logWifiSnapshot("WiFi before getProgress retry");
      delay(400 * attempt);
    }

    if (err != ESP_OK) {
      if (hasFallback) {
        continue;
      }
      return NETWORK_ERROR;
    }
    if (httpCode >= 300 && httpCode < 400) {
      if (hasFallback) {
        continue;
      }
      return REDIRECT_ERROR;
    }

    // Spring-based KOSync servers use 204 when the document has no progress.
    if (httpCode == 204) {
      LOG_INF("KOSync", "No progress found for %s (HTTP 204)", documentHash.c_str());
      rememberResolvedProfile(profile);
      return NOT_FOUND;
    }

    if (httpCode >= 200 && httpCode < 300 && activeBuf->data) {
      if (responseLooksLikeHtml(activeBuf)) {
        rememberResponsePreview(activeBuf->data);
        if (hasFallback) {
          continue;
        }
        return INVALID_RESPONSE;
      }

      JsonDocument doc;
      const DeserializationError error = deserializeJson(doc, activeBuf->data);

      if (error) {
        LOG_ERR("KOSync", "JSON parse failed: %s", error.c_str());
        return JSON_ERROR;
      }

      // kosync returns HTTP 200 + empty body ("{}") when no progress exists.
      // Treat missing progress field as NOT_FOUND to avoid applying a zeroed position.
      if (doc["progress"].isNull()) {
        LOG_INF("KOSync", "Empty progress payload — treating as not found");
        rememberResolvedProfile(profile);
        return NOT_FOUND;
      }

      outProgress.document = documentHash;
      outProgress.progress = doc["progress"].as<std::string>();
      outProgress.percentage = doc["percentage"].as<float>();
      outProgress.device = doc["device"].as<std::string>();
      outProgress.deviceId = doc["device_id"].as<std::string>();
      outProgress.timestamp = doc["timestamp"].as<int64_t>();
      outProgress.position.reset();
      if (KOREADER_STORE.usesCrossPointSyncServer()) {
        // CrossPoint-specific extension (upstream parity): only crosspoint-sync servers send it.
        const JsonObjectConst pos = doc["position"].as<JsonObjectConst>();
        if (!pos.isNull()) {
          KOReaderRichPosition rich;
          rich.pctQ = pos["pctQ"].as<uint32_t>();
          rich.spineIndex = pos["spine"].as<uint16_t>();
          rich.pageNumber = pos["page"].as<uint16_t>();
          const uint16_t pages = pos["pages"].as<uint16_t>();
          rich.totalPages = pages > 0 ? pages : 1;
          const uint16_t para = pos["para"].as<uint16_t>();
          if (para > 0) rich.paragraphIndex = para;
          rich.xpath = pos["xpath"].as<const char*>() ? pos["xpath"].as<const char*>() : "";
          LOG_DBG("KOSync", "Got rich position: spine=%u page=%u/%u para=%u", rich.spineIndex, rich.pageNumber,
                  rich.totalPages, para);
          outProgress.position = std::move(rich);
        }
      }

      LOG_INF("KOSync", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
      rememberResolvedProfile(profile);
      return OK;
    }

    if (httpCode == 401) {
      rememberResolvedProfile(profile);
      return AUTH_FAILED;
    }
    if (httpCode == 404) {
      LOG_INF("KOSync", "No progress found for %s", documentHash.c_str());
      rememberResolvedProfile(profile);
      return NOT_FOUND;
    }

    if (!hasFallback) {
      return SERVER_ERROR;
    }
  }

  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  if (!hasCredentials()) return NO_CREDENTIALS;

  beginRequest("update progress");
  if (!checkHeapForTls()) return NETWORK_ERROR;

  JsonDocument doc;
  doc["document"] = progress.document;
  if (progress.metadata.has_value()) {
    auto metadata = doc["metadata"].to<JsonObject>();
    metadata["filename"] = progress.metadata->filename;
    metadata["title"] = progress.metadata->title;
    metadata["authors"] = progress.metadata->authors;
  }
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = DEVICE_NAME;
  doc["device_id"] = DEVICE_ID;
  if (progress.position.has_value() && KOREADER_STORE.usesCrossPointSyncServer()) {
    // CrossPoint-specific extension: do not send it to third-party KOSync servers.
    const auto& p = *progress.position;
    auto pos = doc["position"].to<JsonObject>();
    pos["pctQ"] = p.pctQ;
    pos["spine"] = p.spineIndex;
    pos["page"] = p.pageNumber;
    pos["pages"] = p.totalPages;
    if (p.paragraphIndex.has_value()) pos["para"] = *p.paragraphIndex;
    // Server rejects the whole position object if xpath exceeds 120 bytes.
    if (!p.xpath.empty() && p.xpath.size() <= 120) pos["xpath"] = p.xpath;
  }

  std::string body;
  serializeJson(doc, body);
  LOG_INF("KOSync", "PUT body: %s", body.c_str());

  ResponseBuffer buf;
  ResponseBuffer* activeBuf = effectiveResponseBuffer(&buf);
  EndpointProfile profiles[2] = {EndpointProfile::UNKNOWN, EndpointProfile::UNKNOWN};
  const int profileCount = buildCandidateProfiles(profiles);

  for (int profileIndex = 0; profileIndex < profileCount; profileIndex++) {
    const EndpointProfile profile = profiles[profileIndex];
    const bool hasFallback = profileIndex + 1 < profileCount;
    const std::string url = buildEndpointUrl(profile, "/syncs/progress");
    esp_err_t err = ESP_FAIL;
    int httpCode = 0;

    LOG_DBG("KOSync", "Updating progress: %s (heap: %u, contig: %u)", url.c_str(), lastHeapAtFailure,
            lastContigHeapAtFailure);

    for (int attempt = 1; attempt <= 3; attempt++) {
      refreshHeapSnapshot();
      logTlsAttemptPlan("update progress", attempt);
      if (!checkHeapForTls()) return NETWORK_ERROR;

      resetResponseBuffer(activeBuf);

      esp_http_client_handle_t client = createClient(url.c_str(), &buf, HTTP_METHOD_PUT);
      if (!client) {
        lastEspError = ESP_ERR_NO_MEM;
        return NETWORK_ERROR;
      }

      esp_http_client_set_header(client, "Content-Type", "application/json");
      esp_http_client_set_post_field(client, body.c_str(), body.length());

      logWifiSnapshot("WiFi before updateProgress");
      err = esp_http_client_perform(client);
      httpCode = esp_http_client_get_status_code(client);
      lastHttpCode = httpCode;
      lastEspError = err;
      if (!g_keepSessionOpen) {
        esp_http_client_cleanup(client);
      }

      LOG_DBG("KOSync", "PUT %s -> %d (err: %s) [attempt %d]", url.c_str(), httpCode, esp_err_to_name(err), attempt);
      if (err == ESP_OK && (httpCode < 200 || httpCode >= 300)) {
        rememberResponsePreview(activeBuf->data);
      }

      const bool retryable = (err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_HTTP_EAGAIN);
      if (err == ESP_OK || !retryable || attempt == 3) break;

      resetSessionClientForRetry();
      LOG_ERR("KOSync", "updateProgress failed attempt %d, retrying", attempt);
      logWifiSnapshot("WiFi before updateProgress retry");
      delay(400 * attempt);
    }

    if (err != ESP_OK) {
      if (hasFallback) {
        continue;
      }
      return NETWORK_ERROR;
    }
    if (httpCode >= 300 && httpCode < 400) {
      if (hasFallback) {
        continue;
      }
      return REDIRECT_ERROR;
    }
    if (httpCode >= 200 && httpCode < 300) {
      if (activeBuf->data) {
        const char c = *skipBomAndWhitespace(activeBuf->data);
        if (c != '\0' && c != '{') {
          if (hasFallback && c == '<') {
            continue;
          }
          return INVALID_RESPONSE;
        }
      }
      rememberResolvedProfile(profile);
      return OK;
    }
    if (httpCode == 401) {
      rememberResolvedProfile(profile);
      return AUTH_FAILED;
    }
    if (httpCode == 405 && hasFallback) {
      continue;
    }

    if (!hasFallback) {
      return SERVER_ERROR;
    }
  }

  return SERVER_ERROR;
}

bool KOReaderSyncClient::usesKosyncSubdirectory() { return g_lastResolvedProfile == EndpointProfile::CWA_KOSYNC; }

const char* KOReaderSyncClient::lastFailureDetail() {
  const bool isUpload = (lastOperation && strcmp(lastOperation, "update progress") == 0);
  const bool hasReusableSession = g_keepSessionOpen && g_sessionClient != nullptr;
  const unsigned requiredContig =
      (isUpload && hasReusableSession) ? MIN_CONTIG_HEAP_FOR_TLS_UPLOAD : MIN_CONTIG_HEAP_FOR_TLS;

  if (lastEspError == ESP_ERR_NO_MEM && lastHttpCode == 0) {
    snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf),
             "%s: low memory (%u free, %u contig, need %u). Reboot device.", lastOperation, lastHeapAtFailure,
             lastContigHeapAtFailure, requiredContig);
    return g_failureDetailBuf;
  }
  if (lastHttpCode == 0 && lastEspError != 0) {
    if (lastEspError == ESP_ERR_HTTP_CONNECT && KOREADER_STORE.getBaseUrl().rfind("https", 0) == 0) {
      snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf),
               "%s: connect failed — check network, DNS, certs, or TLS 1.2 compat (heap %u/%u contig)", lastOperation,
               lastHeapAtFailure, lastContigHeapAtFailure);
    } else {
      snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf), "%s: %s (heap %u/%u contig)", lastOperation,
               esp_err_to_name(lastEspError), lastHeapAtFailure, lastContigHeapAtFailure);
    }
    return g_failureDetailBuf;
  }
  if (lastHttpCode >= 200 && lastHttpCode < 300 && lastEspError == ESP_OK) {
    snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf),
             "%s: expected JSON but received HTML (captive portal or proxy?)", lastOperation);
    return g_failureDetailBuf;
  }
  if (lastHttpCode != 0) {
    if (lastHttpCode == 404 && lastOperation && strcmp(lastOperation, "update progress") == 0) {
      std::string lowerPreview = g_lastResponsePreview;
      std::transform(lowerPreview.begin(), lowerPreview.end(), lowerPreview.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (lowerPreview.find("book not found") != std::string::npos) {
        snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf),
                 "%s: server does not know this book yet; download it via OPDS first", lastOperation);
        return g_failureDetailBuf;
      }
      snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf),
               "%s: HTTP 404 (upload rejected; server may require book to be known)", lastOperation);
      return g_failureDetailBuf;
    }
    snprintf(g_failureDetailBuf, sizeof(g_failureDetailBuf), "%s: HTTP %d", lastOperation, lastHttpCode);
    return g_failureDetailBuf;
  }
  g_failureDetailBuf[0] = '\0';
  return g_failureDetailBuf;
}

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found";
    case LOW_MEMORY:
      return "Not enough memory for TLS";
    case USER_EXISTS:
      return "Username is already taken";
    case REGISTRATION_DISABLED:
      return "Registration is disabled on this server";
    case REDIRECT_ERROR:
      return "Server redirected (check server URL)";
    case INVALID_RESPONSE:
      return "Unexpected response (check server URL)";
    default:
      return "Unknown error";
  }
}

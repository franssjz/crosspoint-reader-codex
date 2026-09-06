#pragma once
#include <cstdint>
#include <optional>
#include <string>

struct KOReaderMetadata {
  std::string filename;
  std::string title;
  std::string authors;
};

/**
 * Rich CrossPoint position sent alongside progress uploads. Maps 1:1 onto the
 * crosspoint-sync extended `position` object (see crosspoint-sync docs/API.md).
 * It is only transmitted to sync.crosspointreader.com. These fields remain
 * layout-dependent compatibility hints; the standard XPath is the content anchor.
 */
struct KOReaderRichPosition {
  uint32_t pctQ = 0;                       // Percentage quantized 0..1,000,000 (metadata/fallback)
  uint16_t spineIndex = 0;                 // Spine (chapter) index
  uint16_t pageNumber = 0;                 // Page within spine (layout-dependent hint)
  uint16_t totalPages = 1;                 // Spine page count (layout-dependent hint)
  std::optional<uint16_t> paragraphIndex;  // Synthetic 1-based paragraph index
  std::string xpath;                       // KOReader-style xpath (server cap: 120 bytes)
};

/**
 * Progress data from KOReader sync server.
 */
struct KOReaderProgress {
  std::string document;                          // Document hash
  std::string progress;                          // XPath-like progress string
  float percentage;                              // Progress percentage (0.0 to 1.0)
  std::string device;                            // Device name
  std::string deviceId;                          // Device ID
  int64_t timestamp;                             // Unix timestamp of last update
  std::optional<KOReaderMetadata> metadata;      // Optional document metadata
  std::optional<KOReaderRichPosition> position;  // Optional rich position (crosspoint-sync servers only)
};

/**
 * HTTP client for KOReader sync API.
 *
 * Base URL: https://sync.koreader.rocks:443/
 *
 * API Endpoints:
 *   POST /users/create - Register a new user
 *   GET  /users/auth  - Authenticate (validate credentials)
 *   GET  /syncs/progress/:document - Get progress for a document
 *   PUT  /syncs/progress - Update progress for a document
 *
 * Authentication:
 *   x-auth-user: username
 *   x-auth-key: MD5 hash of password
 */
class KOReaderSyncClient {
 public:
  enum Error {
    OK = 0,
    NO_CREDENTIALS,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    JSON_ERROR,
    NOT_FOUND,
    LOW_MEMORY,
    USER_EXISTS,
    REGISTRATION_DISABLED,
    REDIRECT_ERROR,
    INVALID_RESPONSE
  };

  /**
   * Register a new user account with the sync server.
   * Uses credentials already stored in KOReaderCredentialStore.
   * @return OK on success, USER_EXISTS if taken, REGISTRATION_DISABLED if server disallows it
   */
  static Error registerUser();
  static Error registerUser(const std::string& username, const std::string& md5Password, const std::string& baseUrl);
  /** Upstream name for registerUser() (stored credentials). */
  static Error createUser() { return registerUser(); }

  /**
   * Authenticate with the sync server (validate credentials).
   * @return OK on success, error code on failure
   */
  static Error authenticate();

  /**
   * Get reading progress for a document.
   * @param documentHash The document hash (from KOReaderDocumentId)
   * @param outProgress Output: the progress data
   * @return OK on success, NOT_FOUND if no progress exists, error code on failure
   */
  static Error getProgress(const std::string& documentHash, KOReaderProgress& outProgress);

  /**
   * Update reading progress for a document.
   * @param progress The progress data to upload
   * @return OK on success, error code on failure
   */
  static Error updateProgress(const KOReaderProgress& progress);

  /**
   * Keep HTTP/TLS session alive across multiple sync requests (GET/PUT).
   * Intended for KOReaderSyncActivity to reduce repeated handshake churn.
   */
  static void beginPersistentSession();

  /**
   * Close and release any persistent HTTP/TLS session.
   */
  static void endPersistentSession();

  /**
   * True when the last recognized server interaction resolved against the
   * Calibre-Web-Automated `/kosync` endpoint prefix.
   */
  static bool usesKosyncSubdirectory();

  /**
   * Get human-readable error message (short, for status line).
   */
  static const char* errorString(Error error);

  /**
   * Get a detailed diagnostic string for the last failure, combining the error
   * category, the underlying esp_err_t (when applicable), the HTTP status code,
   * and the free heap at the time of failure. Intended for the SYNC_FAILED screen
   * so users and bug-reporters can tell network/TLS/server failures apart.
   * Returns a stable c-string valid until the next request.
   */
  static const char* lastFailureDetail();

  /** HTTP status code from the last request (for diagnostics). */
  static int lastHttpCode;
  /** Last esp_err_t from esp_http_client_perform (ESP_OK if request reached the server). */
  static int lastEspError;
  /** Free heap (bytes) captured at the start of the last failing request. */
  static unsigned lastHeapAtFailure;
  /** Largest contiguous free block (bytes) at the start of the last failing request. */
  static unsigned lastContigHeapAtFailure;
  /** Operation tag set at the start of each request, surfaced in lastFailureDetail. */
  static const char* lastOperation;

  /**
   * Minimum largest-contiguous-free heap block (bytes) required before attempting
   * a request. Below this, the client refuses with NETWORK_ERROR and lastFailureDetail
   * reports a heap-pressure message instead of attempting (and crashing) the TLS handshake.
   */
  static constexpr unsigned MIN_CONTIG_HEAP_FOR_TLS = 36 * 1024;
  // Relaxed threshold only for upload when reusing an already-established session.
  static constexpr unsigned MIN_CONTIG_HEAP_FOR_TLS_UPLOAD = 34 * 1024;
};

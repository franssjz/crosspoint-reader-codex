#include "WebUploadTransaction.h"

#include <Logging.h>
#include <mbedtls/sha256.h>

#include <cstring>

#include "WebPathUtils.h"
#include "util/AtomicFileReplace.h"
#include "util/WebUploadRecovery.h"

namespace {
// HTTP, WebSocket and WebDAV callbacks share the web task. Only one may own
// a staging file at a time, including transfers targeting the same book.
WebUploadTransaction* currentTransaction = nullptr;

bool copyChecked(String& destination, const String& source) {
  if (!destination.reserve(source.length())) return false;
  destination = source;
  return destination.length() == source.length();
}

bool makeSiblingPath(const String& target, const char* hexDigest, const char* suffix, String& output) {
  const size_t parentLength = static_cast<size_t>(target.lastIndexOf('/')) + 1;
  const size_t required = parentLength + strlen(".cpr-upload-") + 64 + strlen(suffix);
  if (!output.reserve(required)) return false;
  output = "";
  return output.concat(target.c_str(), parentLength) && output.concat(".cpr-upload-") && output.concat(hexDigest) &&
         output.concat(suffix) && output.length() == required;
}
}  // namespace

WebUploadTransaction::~WebUploadTransaction() {
  // The owner's HalFile closes separately. Leave any unfinished hidden file
  // for the next transaction; destruction must never unlink a committed book.
  if (currentTransaction == this) currentTransaction = nullptr;
}

bool WebUploadTransaction::begin(const String& target, HalFile& file) {
  cancel(file);
  if (currentTransaction) {
    LOG_ERR("WEB", "Another upload is already in progress");
    return false;
  }
  if (target == "/" || WebPathUtils::isProtected(target)) return false;
  if (!WebUploadRecovery::recover(Storage)) {
    LOG_ERR("WEB", "Previous upload recovery needs attention; refusing a new upload");
    return false;
  }

  // Hash-based sibling names stay below FAT's name limit even when the book
  // itself has a long UTF-8 filename. The full target path identifies the slot.
  uint8_t digest[32];
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  const bool hashed =
      mbedtls_sha256_starts(&hash, 0) == 0 &&
      mbedtls_sha256_update(&hash, reinterpret_cast<const uint8_t*>(target.c_str()), target.length()) == 0 &&
      mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  if (!hashed) {
    LOG_ERR("WEB", "Failed to identify upload transaction");
    return false;
  }
  char hex[65];
  constexpr char HEX_DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(digest); ++i) {
    hex[i * 2] = HEX_DIGITS[digest[i] >> 4];
    hex[i * 2 + 1] = HEX_DIGITS[digest[i] & 15];
  }
  hex[64] = '\0';
  if (!copyChecked(target_, target) || !makeSiblingPath(target, hex, ".partial", temporary_) ||
      !makeSiblingPath(target, hex, ".previous", backup_)) {
    LOG_ERR("WEB", "Out of memory preparing upload transaction");
    return false;
  }

  if (Storage.exists(target_.c_str())) {
    auto existing = Storage.open(target_.c_str());
    if (!existing) return false;
    const bool directory = existing.isDirectory();
    existing.close();
    if (directory) return false;
  }
  if (Storage.exists(temporary_.c_str()) && !Storage.remove(temporary_.c_str())) return false;
  if (!Storage.openFileForWrite("WEB", temporary_.c_str(), file)) return false;
  active_ = true;
  currentTransaction = this;
  return true;
}

bool WebUploadTransaction::finish(HalFile& file, const size_t expectedBytes) {
  if (!active_ || !file) return false;
  file.flush();
  const bool complete = file.fileSize() == expectedBytes;
  const bool closed = file.close();
  active_ = false;
  currentTransaction = nullptr;
  if (!complete || !closed) {
    LOG_ERR("WEB", "Upload was not fully written/closed: %s", target_.c_str());
    // Preserve the old target and the temporary on an uncertain storage error.
    return false;
  }
  const bool hadTarget = Storage.exists(target_.c_str());
  if (!WebUploadRecovery::prepare(Storage, target_.c_str(), temporary_.c_str(), backup_.c_str())) {
    LOG_ERR("WEB", "Upload journal could not be committed; previous file preserved");
    return false;
  }
  // An older backup without a journal may belong to a book the user explicitly
  // deleted. Do not resurrect it when creating a new file of the same name.
  const bool promoted = hadTarget
                            ? AtomicFileReplace::promote(Storage, temporary_.c_str(), target_.c_str(), backup_.c_str())
                            : Storage.rename(temporary_.c_str(), target_.c_str());
  if (!promoted) {
    LOG_ERR("WEB", "Upload replacement failed; preserved recovery files: %s", target_.c_str());
    return false;
  }
  if (!WebUploadRecovery::complete(Storage)) {
    LOG_ERR("WEB", "Uploaded file committed; journal cleanup deferred to recovery");
  }
  return true;
}

void WebUploadTransaction::cancel(HalFile& file) {
  if (file) file.close();
  if (active_ && !temporary_.isEmpty() && !Storage.remove(temporary_.c_str())) {
    LOG_ERR("WEB", "Failed to remove incomplete upload: %s", temporary_.c_str());
  }
  active_ = false;
  if (currentTransaction == this) currentTransaction = nullptr;
}

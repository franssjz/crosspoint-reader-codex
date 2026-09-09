#pragma once

// Promotion of an already completely written/validated temporary file. Keep
// the previous committed file until the next successful transaction. In
// particular, never unlink the only committed copy to make rename succeed.
// Storage is a template parameter so failure paths can be exercised on host.
namespace AtomicFileReplace {
template <typename FileSystem>
bool promote(FileSystem& fs, const char* temporary, const char* target, const char* backup) {
  if (!fs.exists(temporary)) return false;
  if (!fs.exists(target) && fs.exists(backup) && !fs.rename(backup, target)) return false;
  const bool hadTarget = fs.exists(target);
  if (hadTarget) {
    if (fs.exists(backup) && !fs.remove(backup)) return false;
    if (!fs.rename(target, backup)) return false;
  }
  if (fs.rename(temporary, target)) return true;
  // Leave the completed temporary and backup available even when rollback
  // also fails (e.g. the card was removed). The loader can recover the backup.
  if (hadTarget) fs.rename(backup, target);
  return false;
}

template <typename FileSystem>
bool recover(FileSystem& fs, const char* target, const char* backup) {
  if (fs.exists(target)) return true;
  return fs.exists(backup) && fs.rename(backup, target);
}
}  // namespace AtomicFileReplace

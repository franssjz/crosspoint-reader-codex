#pragma once

#include <Arduino.h>
#include <HalStorage.h>

// Upload into a hidden sibling and promote only after the complete body has
// reached storage. A failed/cancelled transfer never removes the current book.
class WebUploadTransaction {
 public:
  WebUploadTransaction() = default;
  ~WebUploadTransaction();
  WebUploadTransaction(const WebUploadTransaction&) = delete;
  WebUploadTransaction& operator=(const WebUploadTransaction&) = delete;
  bool begin(const String& target, HalFile& file);
  bool finish(HalFile& file, size_t expectedBytes);
  void cancel(HalFile& file);

 private:
  String target_;
  String temporary_;
  String backup_;
  bool active_ = false;
};

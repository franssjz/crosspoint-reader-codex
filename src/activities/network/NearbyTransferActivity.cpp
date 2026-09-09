#include "NearbyTransferActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "KOReaderDocumentId.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/WebPathUtils.h"
#include "util/NetworkMemory.h"

namespace {
constexpr const char* TAG = "NEARBY";
constexpr uint8_t RADIO_CHANNEL = 1;
using State = NearbyProtocol::Session::State;
using Error = NearbyProtocol::Session::Error;
}  // namespace

NearbyTransferActivity::NearbyTransferActivity(GfxRenderer& renderer, MappedInputManager& input, Mode mode,
                                               std::string path, NearbyProtocol::Position position,
                                               PositionHandler handler)
    : Activity("NearbyTransfer", renderer, input),
      mode_(mode),
      path_(std::move(path)),
      configuredPosition_(position),
      positionHandler_(std::move(handler)),
      session_(*this, "CPR-vCodex") {}

NearbyTransferActivity::~NearbyTransferActivity() {
  abort();
  transport_.end();
}

bool NearbyTransferActivity::isSending() const { return mode_ == Mode::SendFile || mode_ == Mode::SendPosition; }
bool NearbyTransferActivity::isPosition() const {
  return mode_ == Mode::SendPosition || mode_ == Mode::ReceivePosition;
}

void NearbyTransferActivity::setInitialError(bool radio) {
  {
    RenderLock lock(*this);
    radioError_ = radio;
    uiState_ = State::Error;
    uiError_ = Error::Invalid;
  }
  requestUpdate();
}

bool NearbyTransferActivity::validateBookIdentity() const {
  // Calculate from the file, bypassing the path/size identity cache: a same-size
  // replacement must not apply another book's incoming position.
  return !path_.empty() && configuredPosition_.bookId.back() == 0 &&
         NearbyProtocol::sameBook(configuredPosition_, KOReaderDocumentId::calculate(path_));
}

void NearbyTransferActivity::onEnter() {
  Activity::onEnter();
  if (isPosition() && (!validateBookIdentity() || (mode_ == Mode::ReceivePosition && !positionHandler_))) {
    LOG_ERR(TAG, "Position transfer requires a matching content identity and receiver callback");
    setInitialError(false);
    return;
  }

  uint64_t fileBytes = 0;
  if (mode_ == Mode::SendFile) {
    const auto slash = path_.find_last_of('/');
    const std::string_view filename(path_.data() + (slash == std::string::npos ? 0 : slash + 1),
                                    path_.size() - (slash == std::string::npos ? 0 : slash + 1));
    if (!NearbyProtocol::validFilename(filename) || !openSource()) {
      setInitialError(false);
      return;
    }
    fileBytes = source_.fileSize64();
    const bool closed = source_.close();
    if (!closed || fileBytes > NearbyProtocol::MAX_FILE_BYTES) {
      setInitialError(false);
      return;
    }
  }

  // The parent reader retains its own data. Nearby transfers never release,
  // reload or modify reading-statistics state.
  NetworkMemory::prepareBeforeNetwork(renderer, TAG, "before-radio", false);
  memoryPrepared_ = true;
  if (!transport_.begin(RADIO_CHANNEL)) {
    LOG_ERR(TAG, "ESP-NOW startup failed");
    setInitialError(true);
    return;
  }
  const uint32_t now = millis(), id = esp_random();
  if (mode_ == Mode::SendFile) {
    const auto slash = path_.find_last_of('/');
    session_.beginSendFile(now, id, std::string_view(path_).substr(slash == std::string::npos ? 0 : slash + 1),
                           fileBytes);
  } else if (mode_ == Mode::SendPosition) {
    session_.beginSendPosition(now, id, configuredPosition_);
  } else {
    session_.beginReceive(now, mode_ == Mode::ReceivePosition ? std::string_view(configuredPosition_.bookId.data(), 32)
                                                              : std::string_view{});
  }
  updateUi(true);
}

void NearbyTransferActivity::onExit() {
  if (exited_) return;
  exited_ = true;
  session_.cancel();
  transport_.end();
  if (memoryPrepared_) {
    NetworkMemory::restoreAfterNetwork(renderer, TAG, "after-radio", false);
    memoryPrepared_ = false;
  }
  Activity::onExit();
}

bool NearbyTransferActivity::skipLoopDelay() {
  return session_.state() == State::Sending || session_.state() == State::Receiving;
}

bool NearbyTransferActivity::preventAutoSleep() { return uiState_ != State::Error && uiState_ != State::Success; }

void NearbyTransferActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (session_.state() == State::OfferPending) {
      session_.reject(millis());
      updateUi(true);
    } else {
      session_.cancel();
      finish();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (uiState_ == State::Error || uiState_ == State::Success) {
      finish();
      return;
    }
    if (session_.state() == State::OfferPending)
      session_.accept(millis());
    else if (session_.state() == State::Discovering)
      session_.selectPeer(static_cast<size_t>(selectedPeer_), millis());
    updateUi(true);
  }
  // Keep an initial validation/radio error visible; an unstarted protocol is
  // otherwise Idle and must not overwrite that error with its first snapshot.
  if (!transport_.started()) return;
  if (session_.state() == State::Discovering && session_.peerCount()) {
    navigator_.onNext([this] {
      RenderLock lock(*this);
      selectedPeer_ = ButtonNavigator::nextIndex(selectedPeer_, static_cast<int>(session_.peerCount()));
      requestUpdate();
    });
    navigator_.onPrevious([this] {
      RenderLock lock(*this);
      selectedPeer_ = ButtonNavigator::previousIndex(selectedPeer_, static_cast<int>(session_.peerCount()));
      requestUpdate();
    });
  }
  // Bound each iteration even if unrelated devices flood the discovery channel.
  for (unsigned i = 0; i < 8 && transport_.poll(event_); ++i) {
    session_.receive(event_.sourceMac.data(), event_.data.data(), event_.length, millis());
  }
  session_.tick(millis());
  updateUi();
}

void NearbyTransferActivity::updateUi(bool force) {
  const auto state = session_.state();
  const uint32_t now = millis();
  const bool changed = state != uiState_ || session_.peerCount() != uiPeerCount_;
  if (!force && !changed && (state != State::Sending && state != State::Receiving)) return;
  if (!force && !changed && static_cast<uint32_t>(now - lastPaint_) < 2000) return;
  const uint8_t percent =
      session_.offer().bytes ? static_cast<uint8_t>(session_.transferred() * 100 / session_.offer().bytes) : 0;
  if (!force && !changed && percent == uiPercent_) return;
  {
    RenderLock lock(*this);
    uiState_ = state;
    uiError_ = session_.error();
    uiPeerCount_ = session_.peerCount();
    for (size_t i = 0; i < uiPeerCount_; ++i) uiPeers_[i] = session_.peer(i);
    uiOffer_ = session_.offer();
    uiPercent_ = percent;
    uiSpine_ = session_.position().spineIndex;
    if (state == State::Success && !receivedPath_.isEmpty()) {
      snprintf(uiReceivedPath_.data(), uiReceivedPath_.size(), "%s", receivedPath_.c_str());
    }
  }
  lastPaint_ = now;
  requestUpdate();
}

bool NearbyTransferActivity::send(const uint8_t* mac, const uint8_t* data, size_t length) {
  return transport_.send(mac, data, length);
}

bool NearbyTransferActivity::openSource() {
  if (source_) return true;
  const String normalized = WebPathUtils::normalize(path_.c_str());
  return !normalized.isEmpty() && !WebPathUtils::isProtected(normalized) &&
         Storage.openFileForRead(TAG, normalized.c_str(), source_) && !source_.isDirectory();
}

int NearbyTransferActivity::read(uint8_t* data, size_t length) {
  if (!openSource() || source_.fileSize64() != session_.offer().bytes) return -1;
  return source_.read(data, length);
}

bool NearbyTransferActivity::finishSend(uint64_t expectedBytes) {
  if (!openSource()) return false;
  const bool complete = source_.fileSize64() == expectedBytes && source_.position() == expectedBytes;
  const bool closed = source_.close();
  return complete && closed;
}

bool NearbyTransferActivity::beginReceive(const NearbyProtocol::Offer& offer) {
  if (!NearbyProtocol::validFilename(offer.name.data()) || offer.bytes > NearbyProtocol::MAX_FILE_BYTES) return false;
  if (!Storage.exists("/Received") && !Storage.mkdir("/Received")) return false;
  const size_t length = strlen(offer.name.data());
  const char* dot = strrchr(offer.name.data(), '.');
  if (!dot || !receivedPath_.reserve(length + 32)) {
    LOG_ERR(TAG, "Out of memory preparing received filename");
    return false;
  }
  for (unsigned suffix = 1; suffix < 1000; ++suffix) {
    receivedPath_ = "/Received/";
    if (suffix == 1) {
      if (!receivedPath_.concat(offer.name.data())) return false;
    } else {
      char number[16];
      snprintf(number, sizeof(number), " (%u)", suffix);
      if (!receivedPath_.concat(offer.name.data(), static_cast<size_t>(dot - offer.name.data())) ||
          !receivedPath_.concat(number) || !receivedPath_.concat(dot))
        return false;
    }
    if (!Storage.exists(receivedPath_.c_str())) return upload_.begin(receivedPath_, destination_);
  }
  return false;
}

size_t NearbyTransferActivity::write(const uint8_t* data, size_t length) { return destination_.write(data, length); }

bool NearbyTransferActivity::finishReceive(uint64_t expectedBytes) {
  // Nearby always keeps both names. Refuse a late collision rather than replacing
  // a file created after the confirmation, even though the journal could recover it.
  if (expectedBytes > NearbyProtocol::MAX_FILE_BYTES || Storage.exists(receivedPath_.c_str())) return false;
  return upload_.finish(destination_, static_cast<size_t>(expectedBytes));
}

bool NearbyTransferActivity::applyPosition(const NearbyProtocol::Position& position) {
  return mode_ == Mode::ReceivePosition && positionHandler_ && validateBookIdentity() &&
         NearbyProtocol::sameBook(position, configuredPosition_.bookId.data()) && positionHandler_(position);
}

void NearbyTransferActivity::abort() {
  source_.close();
  upload_.cancel(destination_);
}

void NearbyTransferActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 isPosition() ? tr(STR_NEARBY_POSITION) : tr(STR_NEARBY_TRANSFER));
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const char* status = tr(STR_NEARBY_LISTENING);
  if (uiState_ == State::Discovering)
    status = tr(STR_NEARBY_SEARCHING);
  else if (uiState_ == State::AwaitingApproval)
    status = tr(STR_NEARBY_APPROVAL);
  else if (uiState_ == State::OfferPending)
    status = tr(STR_NEARBY_OFFER);
  else if (uiState_ == State::Receiving)
    status = tr(STR_NEARBY_RECEIVING);
  else if (uiState_ == State::Sending)
    status = tr(STR_NEARBY_SENDING);
  else if (uiState_ == State::Success)
    status = tr(STR_NEARBY_SUCCESS);
  else if (uiState_ == State::Error) {
    status = radioError_ ? tr(STR_NEARBY_RADIO_FAILED) : tr(STR_NEARBY_FAILED);
    if (uiError_ == Error::Timeout)
      status = tr(STR_NEARBY_TIMEOUT);
    else if (uiError_ == Error::Rejected)
      status = tr(STR_NEARBY_REJECTED);
    else if (uiError_ == Error::Verification)
      status = tr(STR_NEARBY_VERIFY_FAILED);
    else if (uiError_ == Error::StorageFailure)
      status = tr(STR_NEARBY_STORAGE_FAILED);
  }
  renderer.drawCenteredText(UI_10_FONT_ID, y, status);
  y += lineHeight * 2;
  if (uiState_ == State::Discovering) {
    for (size_t i = 0; i < uiPeerCount_ && y + lineHeight < height - metrics.buttonHintsHeight; ++i) {
      char label[48];
      const auto& peer = uiPeers_[i];
      snprintf(label, sizeof(label), "%s [%02X%02X%02X]", peer.name.data(), peer.mac[3], peer.mac[4], peer.mac[5]);
      if (static_cast<int>(i) == selectedPeer_)
        renderer.drawRect(metrics.contentSidePadding, y - 3, width - metrics.contentSidePadding * 2, lineHeight, true);
      renderer.drawCenteredText(UI_10_FONT_ID, y, label);
      y += lineHeight + metrics.verticalSpacing;
    }
  } else {
    if (uiState_ == State::OfferPending) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, uiOffer_.sender.data());
      y += lineHeight;
    }
    if (!isPosition() && uiOffer_.name[0]) {
      // Renderer clips long labels; the received filename itself stays intact.
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, uiOffer_.name.data());
      y += lineHeight;
      char size[40];
      snprintf(size, sizeof(size), "%llu KiB", static_cast<unsigned long long>((uiOffer_.bytes + 1023) / 1024));
      renderer.drawCenteredText(UI_10_FONT_ID, y, size);
      y += lineHeight;
    }
    if (isPosition() && !path_.empty()) {
      const char* filename = strrchr(path_.c_str(), '/');
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, filename ? filename + 1 : path_.c_str());
      y += lineHeight;
    }
    if (uiState_ == State::Receiving || uiState_ == State::Sending) {
      char progress[8];
      snprintf(progress, sizeof(progress), "%u%%", uiPercent_);
      renderer.drawCenteredText(UI_10_FONT_ID, y, progress);
      y += lineHeight;
    }
    if (uiState_ == State::Success && uiReceivedPath_[0]) {
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, y, uiReceivedPath_.data());
      y += lineHeight;
    }
    if (!isSending() && uiState_ != State::Error) {
      renderer.drawCenteredText(SMALL_FONT_ID, y + metrics.verticalSpacing,
                                isPosition() ? tr(STR_NEARBY_POSITION_DESC) : tr(STR_NEARBY_KEEP_BOTH));
    }
  }
  const bool selectable = uiState_ == State::OfferPending || uiState_ == State::Success || uiState_ == State::Error ||
                          (uiState_ == State::Discovering && uiPeerCount_);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), selectable ? tr(STR_SELECT) : "",
                                            uiState_ == State::Discovering ? tr(STR_DIR_UP) : "",
                                            uiState_ == State::Discovering ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#pragma once

#include <HalStorage.h>
#include <NearbyTransfer.h>

#include <array>
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "network/NearbyTransferProtocol.h"
#include "network/WebUploadTransaction.h"
#include "util/ButtonNavigator.h"

class NearbyTransferActivity final : public Activity, private NearbyProtocol::Backend {
 public:
  enum class Mode : uint8_t { SendFile, ReceiveFile, SendPosition, ReceivePosition };
  // The caller validates the anchor against the currently open EPUB and applies
  // it without recording reading time/pages. A false result never reports success.
  using PositionHandler = std::function<bool(const NearbyProtocol::Position&)>;

  NearbyTransferActivity(GfxRenderer& renderer, MappedInputManager& input, Mode mode, std::string path = {},
                         NearbyProtocol::Position position = {}, PositionHandler handler = {});
  ~NearbyTransferActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override;
  bool preventAutoSleep() override;

 private:
  Mode mode_;
  std::string path_;
  NearbyProtocol::Position configuredPosition_;
  PositionHandler positionHandler_;
  freeink::nearby::EspNowTransport transport_;
  freeink::nearby::EspNowTransport::Event event_;
  NearbyProtocol::Session session_;
  WebUploadTransaction upload_;
  HalFile source_, destination_;
  String receivedPath_;
  ButtonNavigator navigator_;
  bool memoryPrepared_ = false;
  bool exited_ = false;
  uint32_t lastPaint_ = 0;

  // Only these snapshots are read by the rendering task. Radio callbacks only
  // enqueue SDK events; protocol and SD operations always run in loop().
  NearbyProtocol::Session::State uiState_ = NearbyProtocol::Session::State::Idle;
  NearbyProtocol::Session::Error uiError_ = NearbyProtocol::Session::Error::None;
  bool radioError_ = false;
  std::array<NearbyProtocol::Session::Peer, NearbyProtocol::MAX_PEERS> uiPeers_{};
  size_t uiPeerCount_ = 0;
  int selectedPeer_ = 0;
  NearbyProtocol::Offer uiOffer_{};
  std::array<char, 200> uiReceivedPath_{};
  uint8_t uiPercent_ = 0;
  uint32_t uiSpine_ = 0;

  bool isSending() const;
  bool isPosition() const;
  void updateUi(bool force = false);
  void setInitialError(bool radio);
  bool validateBookIdentity() const;
  bool openSource();

  bool send(const uint8_t* mac, const uint8_t* data, size_t length) override;
  int read(uint8_t* data, size_t length) override;
  bool finishSend(uint64_t expectedBytes) override;
  bool beginReceive(const NearbyProtocol::Offer& offer) override;
  size_t write(const uint8_t* data, size_t length) override;
  bool finishReceive(uint64_t expectedBytes) override;
  bool applyPosition(const NearbyProtocol::Position& position) override;
  void abort() override;
};

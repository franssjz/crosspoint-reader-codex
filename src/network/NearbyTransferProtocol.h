#pragma once

#include <NearbyTransfer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Files use CrossInk's CIFT v1 wire format. Position offers deliberately use a
// separate capability and version: CPR visible-text offsets are not page numbers
// and must never be interpreted by a reader with a different position model.
namespace NearbyProtocol {

constexpr size_t MAX_NAME = 180;
constexpr size_t MAX_DEVICE_NAME = 20;
constexpr size_t MAX_PEERS = 4;
constexpr uint64_t MAX_FILE_BYTES = 0xffffffffULL;
constexpr uint16_t CHUNK_BYTES = freeink::nearby::COMPAT_CHUNK_BYTES;
constexpr uint32_t RETRY_MS = 1000;
constexpr uint32_t IDLE_TIMEOUT_MS = 30000;
constexpr uint32_t APPROVAL_TIMEOUT_MS = 90000;

struct Position {
  std::array<char, 33> bookId{};
  uint32_t spineIndex = 0;
  uint32_t visibleTextOffset = 0;
};

struct Offer {
  std::array<char, MAX_NAME + 1> name{};
  std::array<char, MAX_DEVICE_NAME + 1> sender{};
  uint64_t bytes = 0;
};

bool validText(std::string_view text);
bool validFilename(std::string_view name);
bool validBookId(std::string_view id);
bool sameBook(const Position& position, std::string_view expectedId);
bool encodeOffer(const Offer& offer, uint8_t* output, size_t capacity, uint16_t& length);
bool decodeOffer(const uint8_t* data, size_t length, Offer& offer);
bool encodePosition(const Position& position, std::string_view sender, uint8_t* output, size_t capacity,
                    uint16_t& length);
bool decodePosition(const uint8_t* data, size_t length, Position& position, char* sender);

class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool send(const uint8_t* mac, const uint8_t* data, size_t length) = 0;
  virtual int read(uint8_t* data, size_t length) = 0;
  virtual bool finishSend(uint64_t expectedBytes) = 0;
  virtual bool beginReceive(const Offer& offer) = 0;
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  virtual bool finishReceive(uint64_t expectedBytes) = 0;
  virtual bool applyPosition(const Position& position) = 0;
  virtual void abort() = 0;
};

class Session {
 public:
  enum class State : uint8_t {
    Idle,
    Listening,
    Discovering,
    AwaitingApproval,
    OfferPending,
    Sending,
    Receiving,
    Success,
    Error
  };
  enum class Error : uint8_t { None, Invalid, StorageFailure, Timeout, Rejected, Cancelled, Verification };
  struct Peer {
    std::array<uint8_t, 6> mac{};
    std::array<char, MAX_DEVICE_NAME + 1> name{};
  };

  explicit Session(Backend& backend, const char* deviceName);
  bool beginSendFile(uint32_t now, uint32_t id, std::string_view filename, uint64_t bytes);
  bool beginSendPosition(uint32_t now, uint32_t id, const Position& position);
  bool beginReceive(uint32_t now, std::string_view expectedBookId = {});
  bool selectPeer(size_t index, uint32_t now);
  bool accept(uint32_t now);
  void reject(uint32_t now);
  void cancel();
  void tick(uint32_t now);
  void receive(const uint8_t* mac, const uint8_t* data, size_t length, uint32_t now);

  State state() const { return state_; }
  Error error() const { return error_; }
  bool positionMode() const { return positionMode_; }
  size_t peerCount() const { return peerCount_; }
  const Peer& peer(size_t index) const { return peers_[index]; }
  const Offer& offer() const { return offer_; }
  const Position& position() const { return position_; }
  uint64_t transferred() const { return transferred_; }

 private:
  Backend& backend_;
  std::array<char, MAX_DEVICE_NAME + 1> deviceName_{};
  std::array<char, 33> expectedBookId_{};
  std::array<Peer, MAX_PEERS> peers_{};
  size_t peerCount_ = 0;
  std::array<uint8_t, 6> peerMac_{};
  std::array<uint8_t, 250> packet_{};
  std::array<uint8_t, CHUNK_BYTES> pending_{};
  std::array<uint8_t, 212> offerPayload_{};
  uint16_t offerLength_ = 0;
  uint16_t pendingLength_ = 0;
  Offer offer_{};
  Position position_{};
  State state_ = State::Idle;
  Error error_ = Error::None;
  bool sender_ = false;
  bool positionMode_ = false;
  uint32_t id_ = 0;
  uint32_t sequence_ = 0;
  uint32_t crc_ = 0xffffffffU;
  uint64_t transferred_ = 0;
  uint32_t lastSend_ = 0;
  uint32_t lastProgress_ = 0;
  uint32_t stateSince_ = 0;

  void reset(uint32_t now);
  void fail(Error error);
  bool send(freeink::nearby::PacketType type, const void* payload = nullptr, uint16_t length = 0, uint32_t sequence = 0,
            const uint8_t* mac = nullptr);
  void discover(uint32_t now);
  void sendOffer(uint32_t now);
  void sendAccept();
  void sendAck();
  void sendResult(bool success);
  bool nextChunk(uint32_t now);
  void resend(uint32_t now);
};

}  // namespace NearbyProtocol

#include "NearbyTransferProtocol.h"

#include <algorithm>
#include <cstring>

#include "util/WebPath.h"

namespace NearbyProtocol {
namespace nearby = freeink::nearby;
namespace {
constexpr uint8_t BROADCAST[6] = {255, 255, 255, 255, 255, 255};
constexpr uint8_t POSITION_MAGIC[4] = {'C', 'P', 'R', 'P'};

bool equalMac(const uint8_t* lhs, const uint8_t* rhs) { return lhs && rhs && memcmp(lhs, rhs, 6) == 0; }

bool positionCapability(const uint8_t* data, size_t length) {
  return data && length == sizeof(POSITION_MAGIC) && memcmp(data, POSITION_MAGIC, sizeof(POSITION_MAGIC)) == 0;
}

template <size_t N>
void copyText(std::array<char, N>& output, std::string_view input) {
  output.fill(0);
  if (input.size() < N) std::copy(input.begin(), input.end(), output.begin());
}
}  // namespace

bool validText(std::string_view text) {
  // Validate complete UTF-8, including overlong encodings, surrogate codepoints
  // and embedded controls. Names remain byte-identical; never truncate UTF-8.
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<uint8_t>(text[i++]);
    if (first < 0x80) {
      if (first < 0x20 || first == 0x7f) return false;
      continue;
    }
    uint32_t code = 0;
    uint32_t minimum = 0;
    size_t rest = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      code = first & 0x1f;
      minimum = 0x80;
      rest = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      code = first & 0x0f;
      minimum = 0x800;
      rest = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code = first & 7;
      minimum = 0x10000;
      rest = 3;
    } else {
      return false;
    }
    if (rest > text.size() - i) return false;
    while (rest--) {
      const auto next = static_cast<uint8_t>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      code = (code << 6) | (next & 0x3f);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) || (code >= 0x80 && code < 0xa0))
      return false;
  }
  return true;
}

bool validFilename(std::string_view name) {
  if (name.empty() || name.size() > MAX_NAME || !validText(name) || name.front() == '.' || name.back() == ' ' ||
      name.back() == '.' || name.find_first_of("/\\:*?\"<>|") != std::string_view::npos)
    return false;
  char path[MAX_NAME + 2];
  path[0] = '/';
  memcpy(path + 1, name.data(), name.size());
  path[name.size() + 1] = 0;
  if (WebPath::isProtected(std::string_view(path, name.size() + 1))) return false;
  const auto dot = name.find_last_of('.');
  if (dot == std::string_view::npos) return false;
  const auto extension = name.substr(dot);
  constexpr std::string_view supported[] = {".epub", ".txt", ".xtc", ".xtch", ".png", ".bmp"};
  for (const auto candidate : supported) {
    if (extension.size() != candidate.size()) continue;
    bool equal = true;
    for (size_t i = 0; i < candidate.size(); ++i) {
      char c = extension[i];
      if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      if (c != candidate[i]) equal = false;
    }
    if (equal) return true;
  }
  return false;
}

bool validBookId(std::string_view id) {
  return id.size() == 32 &&
         std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool sameBook(const Position& position, std::string_view expectedId) {
  return position.bookId[32] == 0 && validBookId(expectedId) &&
         std::string_view(position.bookId.data(), 32) == expectedId;
}

bool encodeOffer(const Offer& offer, uint8_t* output, size_t capacity, uint16_t& length) {
  length = 0;
  if (offer.name.back() || offer.sender.back()) return false;
  const std::string_view name(offer.name.data()), sender(offer.sender.data());
  const size_t required = 12 + name.size() + sender.size();
  if (!output || capacity < required || !validFilename(name) || sender.empty() || !validText(sender) ||
      offer.bytes > MAX_FILE_BYTES)
    return false;
  nearby::writeU64(output, offer.bytes);
  nearby::writeU16(output + 8, CHUNK_BYTES);
  output[10] = static_cast<uint8_t>(sender.size());
  output[11] = static_cast<uint8_t>(name.size());
  memcpy(output + 12, sender.data(), sender.size());
  memcpy(output + 12 + sender.size(), name.data(), name.size());
  length = static_cast<uint16_t>(required);
  return true;
}

bool decodeOffer(const uint8_t* data, size_t length, Offer& offer) {
  if (!data || length < 14) return false;
  const size_t senderLength = data[10], nameLength = data[11];
  if (!senderLength || senderLength > MAX_DEVICE_NAME || !nameLength || nameLength > MAX_NAME ||
      length != 12 + senderLength + nameLength || nearby::readU64(data) > MAX_FILE_BYTES ||
      nearby::readU16(data + 8) < CHUNK_BYTES)
    return false;
  const std::string_view sender(reinterpret_cast<const char*>(data + 12), senderLength);
  const std::string_view name(reinterpret_cast<const char*>(data + 12 + senderLength), nameLength);
  if (!validText(sender) || !validFilename(name)) return false;
  copyText(offer.name, name);
  copyText(offer.sender, sender);
  offer.bytes = nearby::readU64(data);
  return true;
}

bool encodePosition(const Position& position, std::string_view sender, uint8_t* output, size_t capacity,
                    uint16_t& length) {
  length = 0;
  if (!output || position.bookId.back() || !validBookId(std::string_view(position.bookId.data(), 32)) ||
      sender.empty() || sender.size() > MAX_DEVICE_NAME || !validText(sender) || capacity < 58 + sender.size())
    return false;
  // UINT64_MAX and the magic's byte at offset 10 also make this an invalid
  // file offer to older CrossInk receivers; it cannot become a bogus book.
  nearby::writeU64(output, UINT64_MAX);
  memcpy(output + 8, POSITION_MAGIC, 4);
  output[12] = 1;
  output[13] = static_cast<uint8_t>(sender.size());
  memcpy(output + 14, position.bookId.data(), 32);
  nearby::writeU32(output + 46, position.spineIndex);
  nearby::writeU32(output + 50, position.visibleTextOffset);
  memcpy(output + 54, sender.data(), sender.size());
  nearby::writeU32(output + 54 + sender.size(),
                   nearby::crc32Update(0xffffffffU, output, 54 + sender.size()) ^ 0xffffffffU);
  length = static_cast<uint16_t>(58 + sender.size());
  return true;
}

bool decodePosition(const uint8_t* data, size_t length, Position& position, char* sender) {
  if (!data || !sender || length < 59 || nearby::readU64(data) != UINT64_MAX || memcmp(data + 8, POSITION_MAGIC, 4) ||
      data[12] != 1 || data[13] == 0 || data[13] > MAX_DEVICE_NAME || length != static_cast<size_t>(58 + data[13]))
    return false;
  if ((nearby::crc32Update(0xffffffffU, data, length - 4) ^ 0xffffffffU) != nearby::readU32(data + length - 4))
    return false;
  const std::string_view id(reinterpret_cast<const char*>(data + 14), 32);
  const std::string_view name(reinterpret_cast<const char*>(data + 54), data[13]);
  if (!validBookId(id) || !validText(name)) return false;
  copyText(position.bookId, id);
  position.spineIndex = nearby::readU32(data + 46);
  position.visibleTextOffset = nearby::readU32(data + 50);
  memcpy(sender, name.data(), name.size());
  sender[name.size()] = 0;
  return true;
}

Session::Session(Backend& backend, const char* deviceName) : backend_(backend) {
  const std::string_view name = deviceName ? deviceName : "CPR";
  copyText(deviceName_, !name.empty() && name.size() <= MAX_DEVICE_NAME && validText(name) ? name : "CPR");
}

void Session::reset(uint32_t now) {
  backend_.abort();
  state_ = State::Idle;
  error_ = Error::None;
  peerMac_.fill(0);
  expectedBookId_.fill(0);
  peers_ = {};
  peerCount_ = 0;
  pendingLength_ = 0;
  offerLength_ = 0;
  transferred_ = 0;
  sequence_ = 0;
  crc_ = 0xffffffffU;
  lastSend_ = lastProgress_ = stateSince_ = now;
}

bool Session::beginSendFile(uint32_t now, uint32_t id, std::string_view filename, uint64_t bytes) {
  reset(now);
  sender_ = true;
  positionMode_ = false;
  id_ = id ? id : 1;
  if (!validFilename(filename) || bytes > MAX_FILE_BYTES) {
    fail(Error::Invalid);
    return false;
  }
  offer_ = {};
  copyText(offer_.name, filename);
  offer_.sender = deviceName_;
  offer_.bytes = bytes;
  if (!encodeOffer(offer_, offerPayload_.data(), offerPayload_.size(), offerLength_)) {
    fail(Error::Invalid);
    return false;
  }
  state_ = State::Discovering;
  discover(now);
  return true;
}

bool Session::beginSendPosition(uint32_t now, uint32_t id, const Position& position) {
  reset(now);
  sender_ = positionMode_ = true;
  id_ = id ? id : 1;
  position_ = position;
  if (!encodePosition(position, deviceName_.data(), offerPayload_.data(), offerPayload_.size(), offerLength_)) {
    fail(Error::Invalid);
    return false;
  }
  state_ = State::Discovering;
  discover(now);
  return true;
}

bool Session::beginReceive(uint32_t now, std::string_view expectedBookId) {
  reset(now);
  sender_ = false;
  positionMode_ = !expectedBookId.empty();
  if (positionMode_ && !validBookId(expectedBookId)) {
    fail(Error::Invalid);
    return false;
  }
  copyText(expectedBookId_, expectedBookId);
  state_ = State::Listening;
  return true;
}

bool Session::send(nearby::PacketType type, const void* payload, uint16_t length, uint32_t sequence,
                   const uint8_t* mac) {
  size_t encoded = 0;
  return nearby::encodePacket(packet_.data(), packet_.size(), type, id_, sequence, payload, length, encoded) &&
         backend_.send(mac ? mac : peerMac_.data(), packet_.data(), encoded);
}

void Session::discover(uint32_t now) {
  lastSend_ = now;
  send(nearby::PacketType::Discover, positionMode_ ? POSITION_MAGIC : nullptr, positionMode_ ? 4 : 0, 0, BROADCAST);
}

bool Session::selectPeer(size_t index, uint32_t now) {
  if (state_ != State::Discovering || index >= peerCount_) return false;
  peerMac_ = peers_[index].mac;
  state_ = State::AwaitingApproval;
  stateSince_ = lastProgress_ = now;
  sendOffer(now);
  return true;
}

void Session::sendOffer(uint32_t now) {
  lastSend_ = now;
  send(nearby::PacketType::Offer, offerPayload_.data(), offerLength_);
}

void Session::sendAccept() {
  uint8_t size[2];
  nearby::writeU16(size, CHUNK_BYTES);
  send(nearby::PacketType::Accept, size, sizeof(size));
}

void Session::sendAck() {
  uint8_t next[4];
  nearby::writeU32(next, sequence_);
  send(nearby::PacketType::Ack, next, sizeof(next));
}

void Session::sendResult(bool success) {
  const uint8_t result = success ? 0 : 1;
  send(nearby::PacketType::Result, &result, 1);
}

bool Session::accept(uint32_t now) {
  if (state_ != State::OfferPending) return false;
  if (positionMode_) {
    // A repeated offer after completion only resends the receipt; this callback
    // is invoked once, solely by the receiver's explicit confirmation.
    const bool applied = sameBook(position_, expectedBookId_.data()) && backend_.applyPosition(position_);
    sendResult(applied);
    if (!applied) {
      fail(Error::StorageFailure);
      return false;
    }
    state_ = State::Success;
  } else {
    if (!backend_.beginReceive(offer_)) {
      sendResult(false);
      fail(Error::StorageFailure);
      return false;
    }
    state_ = State::Receiving;
    sendAccept();
  }
  stateSince_ = lastProgress_ = now;
  return true;
}

void Session::reject(uint32_t now) {
  if (state_ != State::OfferPending) return;
  const uint8_t reason = 1;
  send(nearby::PacketType::Reject, &reason, 1);
  backend_.abort();
  state_ = State::Listening;
  peerMac_.fill(0);
  stateSince_ = now;
}

void Session::cancel() {
  if (state_ == State::AwaitingApproval || state_ == State::OfferPending || state_ == State::Sending ||
      state_ == State::Receiving)
    send(nearby::PacketType::Cancel);
  backend_.abort();
  state_ = State::Idle;
}

void Session::fail(Error error) {
  if (state_ == State::AwaitingApproval || state_ == State::OfferPending || state_ == State::Sending ||
      state_ == State::Receiving)
    send(nearby::PacketType::Cancel);
  backend_.abort();
  error_ = error;
  state_ = State::Error;
}

bool Session::nextChunk(uint32_t now) {
  const size_t wanted = static_cast<size_t>(std::min<uint64_t>(CHUNK_BYTES, offer_.bytes - transferred_));
  if (!wanted) {
    if (!backend_.finishSend(offer_.bytes)) {
      fail(Error::StorageFailure);
      return false;
    }
  } else {
    const int count = backend_.read(pending_.data(), wanted);
    if (count < 0 || static_cast<size_t>(count) != wanted) {
      fail(Error::StorageFailure);
      return false;
    }
    pendingLength_ = static_cast<uint16_t>(count);
    crc_ = nearby::crc32Update(crc_, pending_.data(), pendingLength_);
  }
  resend(now);
  return true;
}

void Session::resend(uint32_t now) {
  lastSend_ = now;
  if (pendingLength_) {
    send(nearby::PacketType::Data, pending_.data(), pendingLength_, sequence_);
  } else {
    uint8_t complete[12];
    nearby::writeU64(complete, offer_.bytes);
    nearby::writeU32(complete + 8, crc_ ^ 0xffffffffU);
    send(nearby::PacketType::Complete, complete, sizeof(complete));
  }
}

void Session::tick(uint32_t now) {
  if ((state_ == State::AwaitingApproval || state_ == State::OfferPending) &&
      static_cast<uint32_t>(now - stateSince_) >= APPROVAL_TIMEOUT_MS) {
    fail(Error::Timeout);
    return;
  }
  if ((state_ == State::Sending || state_ == State::Receiving) &&
      static_cast<uint32_t>(now - lastProgress_) >= IDLE_TIMEOUT_MS) {
    fail(Error::Timeout);
    return;
  }
  if (static_cast<uint32_t>(now - lastSend_) < RETRY_MS) return;
  if (state_ == State::Discovering)
    discover(now);
  else if (state_ == State::AwaitingApproval)
    sendOffer(now);
  else if (state_ == State::Sending)
    resend(now);
}

void Session::receive(const uint8_t* mac, const uint8_t* data, size_t length, uint32_t now) {
  nearby::PacketView packet;
  if (!mac || !nearby::decodePacket(data, length, packet) || packet.sessionId == 0) return;
  const auto type = packet.type;
  if (!sender_ && state_ == State::Listening && type == nearby::PacketType::Discover) {
    if ((positionMode_ && !positionCapability(packet.payload, packet.payloadLength)) ||
        (!positionMode_ && packet.payloadLength != 0))
      return;
    id_ = packet.sessionId;
    uint8_t advertisement[MAX_DEVICE_NAME + 4];
    const auto nameLength = strlen(deviceName_.data());
    const size_t prefix = positionMode_ ? 4 : 0;
    if (prefix) memcpy(advertisement, POSITION_MAGIC, prefix);
    memcpy(advertisement + prefix, deviceName_.data(), nameLength);
    send(nearby::PacketType::Advertise, advertisement, static_cast<uint16_t>(prefix + nameLength), 0, mac);
    return;
  }
  if (sender_ && state_ == State::Discovering && type == nearby::PacketType::Advertise && packet.sessionId == id_) {
    const size_t prefix = positionMode_ ? 4 : 0;
    if (packet.payloadLength <= prefix || packet.payloadLength > prefix + MAX_DEVICE_NAME ||
        (prefix && !positionCapability(packet.payload, 4)))
      return;
    const std::string_view name(reinterpret_cast<const char*>(packet.payload + prefix), packet.payloadLength - prefix);
    if (!validText(name)) return;
    for (size_t i = 0; i < peerCount_; ++i)
      if (equalMac(peers_[i].mac.data(), mac)) return;
    if (peerCount_ == MAX_PEERS) return;
    memcpy(peers_[peerCount_].mac.data(), mac, 6);
    copyText(peers_[peerCount_++].name, name);
    return;
  }
  if (!sender_ && state_ == State::Listening && type == nearby::PacketType::Offer) {
    if (packet.payloadLength > offerPayload_.size()) return;
    if (positionMode_) {
      if (!decodePosition(packet.payload, packet.payloadLength, position_, offer_.sender.data()) ||
          !sameBook(position_, expectedBookId_.data()))
        return;
    } else if (!decodeOffer(packet.payload, packet.payloadLength, offer_))
      return;
    id_ = packet.sessionId;
    memcpy(peerMac_.data(), mac, 6);
    memcpy(offerPayload_.data(), packet.payload, packet.payloadLength);
    offerLength_ = packet.payloadLength;
    transferred_ = sequence_ = 0;
    crc_ = 0xffffffffU;
    state_ = State::OfferPending;
    stateSince_ = lastProgress_ = now;
    return;
  }
  if (packet.sessionId != id_ || !equalMac(peerMac_.data(), mac)) return;
  if (type == nearby::PacketType::Cancel && state_ != State::Success && state_ != State::Error) {
    fail(Error::Cancelled);
    return;
  }
  if (sender_) {
    if (state_ == State::AwaitingApproval && type == nearby::PacketType::Reject) {
      fail(Error::Rejected);
      return;
    }
    if (!positionMode_ && state_ == State::AwaitingApproval && type == nearby::PacketType::Accept &&
        packet.payloadLength == 2 && nearby::readU16(packet.payload) == CHUNK_BYTES) {
      state_ = State::Sending;
      lastProgress_ = now;
      nextChunk(now);
    } else if (state_ == State::Sending && type == nearby::PacketType::Ack && packet.payloadLength == 4 &&
               pendingLength_ && nearby::readU32(packet.payload) == sequence_ + 1) {
      transferred_ += pendingLength_;
      pendingLength_ = 0;
      ++sequence_;
      lastProgress_ = now;
      nextChunk(now);
    } else if (type == nearby::PacketType::Result && packet.payloadLength == 1 &&
               ((positionMode_ && state_ == State::AwaitingApproval) ||
                (!positionMode_ && state_ == State::Sending && !pendingLength_ && transferred_ == offer_.bytes))) {
      if (packet.payload[0] != 0) {
        fail(Error::Verification);
        return;
      }
      state_ = State::Success;
    }
    return;
  }
  if (type == nearby::PacketType::Offer && packet.payloadLength == offerLength_ &&
      memcmp(packet.payload, offerPayload_.data(), offerLength_) == 0) {
    if (state_ == State::Receiving)
      sendAccept();
    else if (state_ == State::Success && positionMode_)
      sendResult(true);
    return;
  }
  if (!positionMode_ && type == nearby::PacketType::Data && state_ == State::Receiving) {
    if (packet.sequence != sequence_) {
      sendAck();
      return;
    }
    if (packet.payloadLength == 0 || packet.payloadLength > CHUNK_BYTES ||
        packet.payloadLength > offer_.bytes - transferred_) {
      fail(Error::Verification);
      return;
    }
    // Validate the entire range BEFORE touching the stage. An oversized final
    // packet cannot write outside the offer even if a sender is malformed.
    if (backend_.write(packet.payload, packet.payloadLength) != packet.payloadLength) {
      fail(Error::StorageFailure);
      return;
    }
    crc_ = nearby::crc32Update(crc_, packet.payload, packet.payloadLength);
    transferred_ += packet.payloadLength;
    ++sequence_;
    lastProgress_ = now;
    sendAck();
  } else if (!positionMode_ && type == nearby::PacketType::Complete && packet.payloadLength == 12 &&
             (state_ == State::Receiving || state_ == State::Success)) {
    const bool verified = nearby::readU64(packet.payload) == offer_.bytes && transferred_ == offer_.bytes &&
                          nearby::readU32(packet.payload + 8) == (crc_ ^ 0xffffffffU);
    if (!verified) {
      if (state_ != State::Success) fail(Error::Verification);
      return;
    }
    if (state_ == State::Success) {
      sendResult(true);
      return;
    }
    const bool committed = backend_.finishReceive(offer_.bytes);
    sendResult(committed);
    if (committed)
      state_ = State::Success;
    else
      fail(Error::StorageFailure);
  }
}

}  // namespace NearbyProtocol

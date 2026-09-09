#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>

#include "network/NearbyTransferProtocol.h"

namespace {
namespace wire = freeink::nearby;
using NearbyProtocol::Session;
using State = Session::State;
using Error = Session::Error;

struct Message {
  std::array<uint8_t, 6> from{}, to{};
  std::vector<uint8_t> bytes;
};

struct FakeBackend : NearbyProtocol::Backend {
  std::deque<Message>& messages;
  std::array<uint8_t, 6> mac{};
  std::vector<uint8_t> source, stage, target{'o', 'l', 'd'};
  size_t cursor = 0, written = 0;
  unsigned begins = 0, commits = 0, applications = 0;
  bool failBegin = false, failWrite = false, failCommit = false, failApply = false;
  bool corruptData = false;
  wire::PacketType droppedType = wire::PacketType::Cancel;
  unsigned drops = 0;
  NearbyProtocol::Position applied{};

  FakeBackend(std::deque<Message>& messages, uint8_t id) : messages(messages) { mac[5] = id; }
  bool send(const uint8_t* destination, const uint8_t* data, size_t length) override {
    wire::PacketView packet;
    if (!wire::decodePacket(data, length, packet)) return false;
    if (drops && packet.type == droppedType) {
      --drops;
      return true;
    }
    Message message;
    message.from = mac;
    std::copy_n(destination, 6, message.to.begin());
    message.bytes.assign(data, data + length);
    if (corruptData && packet.type == wire::PacketType::Data) message.bytes.back() ^= 1;
    messages.push_back(std::move(message));
    return true;
  }
  int read(uint8_t* output, size_t length) override {
    const size_t count = std::min(length, source.size() - cursor);
    std::copy_n(source.data() + cursor, count, output);
    cursor += count;
    return static_cast<int>(count);
  }
  bool finishSend(uint64_t bytes) override { return bytes == source.size() && cursor == bytes; }
  bool beginReceive(const NearbyProtocol::Offer&) override {
    ++begins;
    stage.clear();
    return !failBegin;
  }
  size_t write(const uint8_t* data, size_t length) override {
    if (failWrite) return 0;
    stage.insert(stage.end(), data, data + length);
    written += length;
    return length;
  }
  bool finishReceive(uint64_t bytes) override {
    if (failCommit || stage.size() != bytes) return false;
    target = stage;
    ++commits;
    return true;
  }
  bool applyPosition(const NearbyProtocol::Position& position) override {
    ++applications;
    applied = position;
    return !failApply;
  }
  void abort() override {
    cursor = 0;
    stage.clear();
  }
};

struct Pair {
  std::deque<Message> messages;
  FakeBackend a{messages, 1}, b{messages, 2};
  Session sender{a, "Sender"}, receiver{b, "Receiver"};
  uint32_t now = 100;
  void pump() {
    unsigned limit = 10000;
    while (!messages.empty() && --limit) {
      Message message = std::move(messages.front());
      messages.pop_front();
      if (message.from == a.mac)
        receiver.receive(message.from.data(), message.bytes.data(), message.bytes.size(), now);
      else
        sender.receive(message.from.data(), message.bytes.data(), message.bytes.size(), now);
    }
    ASSERT_NE(limit, 0u);
  }
  void advance(uint32_t delta) {
    now += delta;
    sender.tick(now);
    receiver.tick(now);
    pump();
  }
  void offerFile(size_t bytes = 600) {
    a.source.resize(bytes);
    for (size_t i = 0; i < bytes; ++i) a.source[i] = static_cast<uint8_t>(i * 13);
    ASSERT_TRUE(receiver.beginReceive(now));
    ASSERT_TRUE(sender.beginSendFile(now, 123, "Corazón 日本語.epub", bytes));
    pump();
    ASSERT_EQ(sender.peerCount(), 1u);
    ASSERT_TRUE(sender.selectPeer(0, now));
    pump();
    ASSERT_EQ(receiver.state(), State::OfferPending);
  }
  void inject(wire::PacketType type, uint32_t sequence, const void* data, uint16_t length, uint32_t sessionId = 123,
              uint8_t macId = 1) {
    std::array<uint8_t, 1100> bytes{};
    size_t encoded = 0;
    ASSERT_TRUE(wire::encodePacket(bytes.data(), bytes.size(), type, sessionId, sequence, data, length, encoded));
    std::array<uint8_t, 6> mac{};
    mac[5] = macId;
    receiver.receive(mac.data(), bytes.data(), encoded, now);
  }
};

NearbyProtocol::Position position(char first = 'a') {
  NearbyProtocol::Position result;
  std::fill_n(result.bookId.begin(), 32, 'a');
  result.bookId[0] = first;
  result.spineIndex = 7;
  result.visibleTextOffset = 123456;
  return result;
}

TEST(NearbyProtocol, PreservesUnicodeAndExtensionsWithoutTruncation) {
  EXPECT_TRUE(NearbyProtocol::validFilename("Corazón 日本語.EPUB"));
  EXPECT_TRUE(NearbyProtocol::validFilename(std::string(175, 'a') + ".epub"));
  EXPECT_FALSE(NearbyProtocol::validFilename(std::string(176, 'a') + ".epub"));
  for (const auto name :
       {"../book.epub", ".private.epub", "book.epub ", "book.epub.", "a/b.epub", "a\\b.epub", "x.bin"})
    EXPECT_FALSE(NearbyProtocol::validFilename(name)) << name;
  EXPECT_FALSE(NearbyProtocol::validFilename(std::string("x\0.epub", 7)));
  EXPECT_FALSE(NearbyProtocol::validFilename("\xc0\xaf.epub"));
  EXPECT_FALSE(NearbyProtocol::validFilename("\xed\xa0\x80.epub"));
  EXPECT_FALSE(NearbyProtocol::validFilename("\xf4\x90\x80\x80.epub"));
}

TEST(NearbyProtocol, ValidatesLengthsBeforeReadingWireData) {
  NearbyProtocol::Offer source, decoded;
  strcpy(source.name.data(), "book.epub");
  strcpy(source.sender.data(), "CrossInk");
  source.bytes = 1234;
  std::array<uint8_t, 212> bytes{};
  uint16_t length = 0;
  ASSERT_TRUE(NearbyProtocol::encodeOffer(source, bytes.data(), bytes.size(), length));
  // CrossInk advertises 1024; this receiver negotiates the C3-compatible 224.
  wire::writeU16(bytes.data() + 8, wire::V2_CHUNK_BYTES);
  ASSERT_TRUE(NearbyProtocol::decodeOffer(bytes.data(), length, decoded));
  EXPECT_STREQ(decoded.name.data(), source.name.data());
  EXPECT_EQ(decoded.bytes, source.bytes);
  for (size_t truncated = 0; truncated < length; ++truncated)
    EXPECT_FALSE(NearbyProtocol::decodeOffer(bytes.data(), truncated, decoded));
  bytes[11] = 255;
  EXPECT_FALSE(NearbyProtocol::decodeOffer(bytes.data(), length, decoded));
  bytes[11] = 9;
  wire::writeU64(bytes.data(), UINT64_MAX);
  EXPECT_FALSE(NearbyProtocol::decodeOffer(bytes.data(), length, decoded));
}

TEST(NearbySession, RequiresReceiverConsentBeforeAnyStorageWrite) {
  Pair pair;
  pair.offerFile();
  EXPECT_EQ(pair.b.begins, 0u);
  EXPECT_EQ(pair.b.written, 0u);
  const uint8_t unwanted[] = {1, 2};
  pair.inject(wire::PacketType::Data, 0, unwanted, sizeof(unwanted));
  EXPECT_EQ(pair.b.written, 0u);
  pair.receiver.reject(pair.now);
  pair.pump();
  EXPECT_EQ(pair.receiver.state(), State::Listening);
  EXPECT_EQ(pair.sender.error(), Error::Rejected);
  EXPECT_EQ(pair.b.target, (std::vector<uint8_t>{'o', 'l', 'd'}));
}

TEST(NearbySession, TransfersCompleteFileAndEmptyFile) {
  for (size_t size : {size_t{0}, size_t{1}, size_t{224}, size_t{225}, size_t{17000}}) {
    Pair pair;
    pair.offerFile(size);
    ASSERT_TRUE(pair.receiver.accept(pair.now));
    pair.pump();
    EXPECT_EQ(pair.sender.state(), State::Success) << size;
    EXPECT_EQ(pair.receiver.state(), State::Success) << size;
    EXPECT_EQ(pair.b.target, pair.a.source);
    EXPECT_EQ(pair.b.commits, 1u);
  }
}

TEST(NearbySession, LostAcceptAckAndResultRetryWithoutDuplicatingBytesOrCommit) {
  for (const auto drop : {wire::PacketType::Accept, wire::PacketType::Ack, wire::PacketType::Result}) {
    Pair pair;
    pair.offerFile();
    pair.b.droppedType = drop;
    pair.b.drops = 1;
    ASSERT_TRUE(pair.receiver.accept(pair.now));
    pair.pump();
    pair.advance(NearbyProtocol::RETRY_MS);
    EXPECT_EQ(pair.sender.state(), State::Success);
    EXPECT_EQ(pair.b.target, pair.a.source);
    EXPECT_EQ(pair.b.written, pair.a.source.size());
    EXPECT_EQ(pair.b.commits, 1u);
  }
}

TEST(NearbySession, CrcFailureDoesNotReplaceOriginal) {
  Pair pair;
  pair.offerFile();
  pair.a.corruptData = true;
  ASSERT_TRUE(pair.receiver.accept(pair.now));
  pair.pump();
  EXPECT_EQ(pair.receiver.error(), Error::Verification);
  EXPECT_EQ(pair.b.commits, 0u);
  EXPECT_EQ(pair.b.target, (std::vector<uint8_t>{'o', 'l', 'd'}));
}

TEST(NearbySession, ValidatesSequencePeerSessionAndRemainingBytesBeforeWrite) {
  Pair pair;
  pair.offerFile(2);
  ASSERT_TRUE(pair.receiver.accept(pair.now));
  pair.messages.clear();
  const uint8_t bytes[] = {1, 2, 3};
  pair.inject(wire::PacketType::Data, 0, bytes, 2, 456);
  pair.inject(wire::PacketType::Data, 0, bytes, 2, 123, 8);
  pair.inject(wire::PacketType::Data, 1, bytes, 2);
  EXPECT_EQ(pair.b.written, 0u);
  pair.inject(wire::PacketType::Data, 0, bytes, 3);
  EXPECT_EQ(pair.b.written, 0u);
  EXPECT_EQ(pair.receiver.error(), Error::Verification);
}

TEST(NearbySession, StorageFailuresAndCancellationKeepOriginal) {
  for (int failure = 0; failure < 4; ++failure) {
    Pair pair;
    pair.offerFile();
    pair.b.failBegin = failure == 0;
    pair.b.failWrite = failure == 1;
    pair.b.failCommit = failure == 2;
    pair.receiver.accept(pair.now);
    if (failure == 3) pair.receiver.cancel();
    pair.pump();
    EXPECT_EQ(pair.b.commits, 0u);
    EXPECT_EQ(pair.b.target, (std::vector<uint8_t>{'o', 'l', 'd'}));
  }
}

TEST(NearbySession, TimesOutWithoutReceivingDataAndAcrossClockWrap) {
  Pair pair;
  pair.now = UINT32_MAX - 100;
  pair.offerFile();
  ASSERT_TRUE(pair.receiver.accept(pair.now));
  pair.messages.clear();
  pair.now += NearbyProtocol::IDLE_TIMEOUT_MS;
  pair.receiver.tick(pair.now);
  EXPECT_EQ(pair.receiver.error(), Error::Timeout);
  EXPECT_EQ(pair.b.commits, 0u);
}

TEST(NearbyPosition, VersionCrcAndBookIdentityAreMandatory) {
  std::array<uint8_t, 100> data{};
  NearbyProtocol::Position decoded;
  char name[NearbyProtocol::MAX_DEVICE_NAME + 1];
  uint16_t length = 0;
  ASSERT_TRUE(NearbyProtocol::encodePosition(position(), "Sender", data.data(), data.size(), length));
  ASSERT_TRUE(NearbyProtocol::decodePosition(data.data(), length, decoded, name));
  EXPECT_TRUE(NearbyProtocol::sameBook(decoded, position().bookId.data()));
  EXPECT_FALSE(NearbyProtocol::sameBook(decoded, position('b').bookId.data()));
  EXPECT_FALSE(NearbyProtocol::sameBook(decoded, "legacy:/book.epub"));
  for (size_t truncated = 0; truncated < length; ++truncated)
    EXPECT_FALSE(NearbyProtocol::decodePosition(data.data(), truncated, decoded, name));
  NearbyProtocol::Offer file;
  EXPECT_FALSE(NearbyProtocol::decodeOffer(data.data(), length, file));
  data[50] ^= 1;
  EXPECT_FALSE(NearbyProtocol::decodePosition(data.data(), length, decoded, name));
  data[50] ^= 1;
  data[12] = 2;
  EXPECT_FALSE(NearbyProtocol::decodePosition(data.data(), length, decoded, name));
}

TEST(NearbyPosition, ConsentAppliesExactlyOnceEvenWhenReceiptIsLost) {
  Pair pair;
  const auto anchor = position();
  ASSERT_TRUE(pair.receiver.beginReceive(pair.now, anchor.bookId.data()));
  ASSERT_TRUE(pair.sender.beginSendPosition(pair.now, 123, anchor));
  pair.pump();
  ASSERT_EQ(pair.sender.peerCount(), 1u);
  ASSERT_TRUE(pair.sender.selectPeer(0, pair.now));
  pair.pump();
  EXPECT_EQ(pair.b.applications, 0u);
  EXPECT_EQ(pair.receiver.state(), State::OfferPending);
  pair.b.droppedType = wire::PacketType::Result;
  pair.b.drops = 1;
  ASSERT_TRUE(pair.receiver.accept(pair.now));
  pair.pump();
  pair.advance(NearbyProtocol::RETRY_MS);
  EXPECT_EQ(pair.sender.state(), State::Success);
  EXPECT_EQ(pair.b.applications, 1u);
  EXPECT_EQ(pair.b.applied.visibleTextOffset, anchor.visibleTextOffset);
  EXPECT_EQ(pair.b.begins, 0u);
  EXPECT_EQ(pair.b.written, 0u);
}

TEST(NearbyPosition, WrongBookNeverPromptsOrApplies) {
  Pair pair;
  ASSERT_TRUE(pair.receiver.beginReceive(pair.now, position('b').bookId.data()));
  ASSERT_TRUE(pair.sender.beginSendPosition(pair.now, 123, position()));
  pair.pump();
  ASSERT_TRUE(pair.sender.selectPeer(0, pair.now));
  pair.pump();
  EXPECT_EQ(pair.receiver.state(), State::Listening);
  EXPECT_EQ(pair.b.applications, 0u);
  pair.advance(NearbyProtocol::APPROVAL_TIMEOUT_MS);
  EXPECT_EQ(pair.sender.error(), Error::Timeout);
}

TEST(NearbyPosition, FileReceiverDoesNotAdvertisePositionCapability) {
  Pair pair;
  ASSERT_TRUE(pair.receiver.beginReceive(pair.now));
  ASSERT_TRUE(pair.sender.beginSendPosition(pair.now, 123, position()));
  pair.pump();
  EXPECT_EQ(pair.sender.peerCount(), 0u);
  EXPECT_EQ(pair.receiver.state(), State::Listening);
}
}  // namespace

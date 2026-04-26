#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <gtest/gtest.h>

#include "core/Context.h"
#include "protocol/Ticket.h"
#include "protocol/TicketJson.h"
#include "protocol/TicketFrame.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

class TicketEdgeCaseFixture : public ::testing::Test
{
protected:
  void SetUp() override { _io_ctx.start(); }
  void TearDown() override { _io_ctx.stop(); }

  asio::io_context& getIoContext() { return _io_ctx; }

private:
  IoContextWrapper _io_ctx;
};

TEST_F(TicketEdgeCaseFixture, SerializeEmptyTicket)
{
  TicketJson serializer;

  TicketInfo info;
  info.ticket_id = "";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "";
  info.timeout = std::chrono::milliseconds(0);

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id, "");
  EXPECT_EQ(deserialized->command, "");
}

TEST_F(TicketEdgeCaseFixture, FrameLengthPrefixZeroLength)
{
  auto prefix = ::Network::TicketFrame::makeLengthPrefix(0);
  EXPECT_EQ(prefix.size(), 4);

  for (auto byte : prefix)
  {
    EXPECT_EQ(static_cast<unsigned char>(byte), 0);
  }
}

TEST_F(TicketEdgeCaseFixture, FrameLengthPrefixMaxLength)
{
  auto prefix = ::Network::TicketFrame::makeLengthPrefix(4294967295U);
  EXPECT_EQ(prefix.size(), 4);

  for (auto byte : prefix)
  {
    EXPECT_EQ(static_cast<unsigned char>(byte), 255);
  }
}

TEST_F(TicketEdgeCaseFixture, MalformedJsonBody)
{
  TicketJson serializer;

  // Write a frame with valid length prefix but invalid JSON body
  std::string bad_json = "not json";
  std::vector<std::byte> body(bad_json.size());
  for (size_t i = 0; i < bad_json.size(); ++i)
    body[i] = std::byte(bad_json[i]);

  auto deserialized = serializer.deserialize(std::span(body));
  EXPECT_FALSE(deserialized.has_value());
}

TEST_F(TicketEdgeCaseFixture, MalformedJsonMissingRequiredFields)
{
  TicketJson serializer;

  // JSON without ticket_id or type - should use defaults
  std::string partial_json = R"({"status": "pending"})";
  std::vector<std::byte> data(partial_json.size());
  for (size_t i = 0; i < partial_json.size(); ++i)
    data[i] = std::byte(partial_json[i]);

  auto deserialized = serializer.deserialize(std::span(data));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_TRUE(deserialized->ticket_id.empty());
  EXPECT_EQ(deserialized->type, TicketType::SHORT);
  EXPECT_EQ(deserialized->status, TicketStatus::PENDING);
}

TEST_F(TicketEdgeCaseFixture, DeserializeNullSpan)
{
  TicketJson serializer;
  auto result = serializer.deserialize(std::span<std::byte>{});
  EXPECT_FALSE(result.has_value());
}

TEST_F(TicketEdgeCaseFixture, DeserializeGarbageData)
{
  TicketJson serializer;

  std::vector<std::byte> garbage(100);
  for (size_t i = 0; i < garbage.size(); ++i)
    garbage[i] = std::byte(i % 256);

  auto result = serializer.deserialize(std::span(garbage));
  EXPECT_FALSE(result.has_value());
}

TEST_F(TicketEdgeCaseFixture, TicketTypeToString)
{
  EXPECT_EQ(ticket_type_to_string(TicketType::SHORT), "short");
  EXPECT_EQ(ticket_type_to_string(TicketType::LONG), "long");
}

TEST_F(TicketEdgeCaseFixture, TicketStatusToString)
{
  EXPECT_EQ(ticket_status_to_string(TicketStatus::PENDING), "pending");
  EXPECT_EQ(ticket_status_to_string(TicketStatus::RUNNING), "running");
  EXPECT_EQ(ticket_status_to_string(TicketStatus::COMPLETED), "completed");
  EXPECT_EQ(ticket_status_to_string(TicketStatus::FAILED), "failed");
  EXPECT_EQ(ticket_status_to_string(TicketStatus::CANCELLED), "cancelled");
}

TEST_F(TicketEdgeCaseFixture, SocketNotConnectedInitially)
{
  TcpSocket socket(getIoContext());
  EXPECT_FALSE(socket.isConnected());
}

TEST_F(TicketEdgeCaseFixture, SocketCloseAfterConnect)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();
  EXPECT_FALSE(socket.isConnected());
}

TEST_F(TicketEdgeCaseFixture, LargeTicketPayload)
{
  TicketJson serializer;

  std::string large_payload(65536, 'a');
  for (size_t i = 0; i < large_payload.size(); ++i)
    large_payload[i] = static_cast<char>('a' + (i % 26));

  TicketInfo info;
  info.ticket_id = "large";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "upload";
  info.timeout = std::chrono::milliseconds(5000);
  info.payload = large_payload;

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->payload, large_payload);
}

}  // namespace Network::Test

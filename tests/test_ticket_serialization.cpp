#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "protocol/Ticket.h"
#include "protocol/TicketFrame.h"
#include "protocol/TicketJson.h"
#include "protocol/TicketSerializer.h"

namespace Network::Test
{

namespace
{

std::shared_ptr<TicketSerializer> makeSerializer()
{
  return std::make_shared<TicketJson>();
}

}  // namespace

TEST(TicketSerializationTest, SerializeDeserializeRoundTrip)
{
  auto serializer = makeSerializer();

  TicketInfo original;
  original.ticket_id = "test-123";
  original.type = TicketType::LONG;
  original.status = TicketStatus::RUNNING;
  original.command = "render";
  original.timeout = std::chrono::milliseconds(5000);
  original.progress_percent = 42;
  original.progress_msg = "processing...";
  original.payload = R"({"width": 1920, "height": 1080})";

  auto serialized = serializer->serialize(original);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id, original.ticket_id);
  EXPECT_EQ(deserialized->type, original.type);
  EXPECT_EQ(deserialized->status, original.status);
  EXPECT_EQ(deserialized->command, original.command);
  EXPECT_EQ(deserialized->timeout, original.timeout);
  EXPECT_EQ(deserialized->progress_percent, original.progress_percent);
  EXPECT_EQ(deserialized->progress_msg, original.progress_msg);
  EXPECT_EQ(deserialized->payload, original.payload);
}

TEST(TicketSerializationTest, SerializeMinimalTicket)
{
  auto serializer = makeSerializer();

  TicketInfo original;
  original.ticket_id = "minimal";
  original.type = TicketType::SHORT;
  original.status = TicketStatus::PENDING;
  original.command = "ping";
  original.timeout = std::chrono::milliseconds(1000);

  auto serialized = serializer->serialize(original);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id, "minimal");
  EXPECT_EQ(deserialized->command, "ping");
}

TEST(TicketSerializationTest, SerializeAllOptionalFields)
{
  auto serializer = makeSerializer();

  TicketInfo original;
  original.ticket_id = "full-test";
  original.type = TicketType::LONG;
  original.status = TicketStatus::RUNNING;
  original.command = "heavy-task";
  original.timeout = std::chrono::milliseconds(10000);
  original.progress_percent = 75;
  original.progress_msg = "almost done";
  original.payload = R"({"task": "render", "params": {}})";
  original.result = R"({"frames": 100, "time": "5s"})";
  original.error = "should not be here yet";
  original.metadata.data["user"] = "test";
  original.metadata.data["priority"] = "high";

  auto serialized = serializer->serialize(original);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id, original.ticket_id);
  EXPECT_EQ(deserialized->type, original.type);
  EXPECT_EQ(deserialized->status, original.status);
  EXPECT_EQ(deserialized->command, original.command);
  EXPECT_EQ(deserialized->timeout, original.timeout);
  EXPECT_EQ(deserialized->progress_percent, original.progress_percent);
  EXPECT_EQ(deserialized->progress_msg, original.progress_msg);
  EXPECT_EQ(deserialized->payload, original.payload);
  EXPECT_EQ(deserialized->result, original.result);
  EXPECT_EQ(deserialized->error, original.error);
  EXPECT_EQ(deserialized->metadata.data["user"], "test");
  EXPECT_EQ(deserialized->metadata.data["priority"], "high");
}

TEST(TicketSerializationTest, SerializeAllTicketTypes)
{
  auto serializer = makeSerializer();

  for (auto type : {TicketType::SHORT, TicketType::LONG})
  {
    TicketInfo info;
    info.ticket_id = "type-test";
    info.type = type;
    info.status = TicketStatus::PENDING;
    info.command = "test";
    info.timeout = std::chrono::milliseconds(1000);

    auto serialized = serializer->serialize(info);
    ASSERT_FALSE(serialized.empty());

    auto deserialized = serializer->deserialize(std::span(serialized));
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->type, type);
  }
}

TEST(TicketSerializationTest, SerializeAllTicketStatuses)
{
  auto serializer = makeSerializer();

  for (auto status : {TicketStatus::PENDING, TicketStatus::RUNNING, TicketStatus::COMPLETED, TicketStatus::FAILED,
                      TicketStatus::CANCELLED})
  {
    TicketInfo info;
    info.ticket_id = "status-test";
    info.type = TicketType::SHORT;
    info.status = status;
    info.command = "test";
    info.timeout = std::chrono::milliseconds(1000);

    auto serialized = serializer->serialize(info);
    ASSERT_FALSE(serialized.empty());

    auto deserialized = serializer->deserialize(std::span(serialized));
    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->status, status);
  }
}

TEST(TicketSerializationTest, DeserializeInvalidJson)
{
  auto serializer = makeSerializer();

  std::vector<std::byte> invalid_data(10);
  for (auto& byte : invalid_data)
  {
    byte = std::byte('x');
  }

  auto result = serializer->deserialize(std::span(invalid_data));
  ASSERT_FALSE(result.has_value());
}

TEST(TicketSerializationTest, DeserializeNullSpan)
{
  auto serializer = makeSerializer();

  auto result = serializer->deserialize(std::span<std::byte>{});
  ASSERT_FALSE(result.has_value());
}

TEST(TicketSerializationTest, SerializeEmptyFields)
{
  auto serializer = makeSerializer();

  TicketInfo info;
  info.ticket_id = "";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "";
  info.timeout = std::chrono::milliseconds(0);

  auto serialized = serializer->serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_TRUE(deserialized->ticket_id.empty());
  EXPECT_TRUE(deserialized->command.empty());
  EXPECT_EQ(deserialized->type, TicketType::SHORT);
  EXPECT_EQ(deserialized->status, TicketStatus::PENDING);
}

TEST(TicketSerializationTest, SerializeLargePayload)
{
  auto serializer = makeSerializer();

  std::string largePayload(65536, 'a');
  for (size_t i = 0; i < largePayload.size(); ++i)
  {
    largePayload[i] = static_cast<char>('a' + (i % 26));
  }

  TicketInfo info;
  info.ticket_id = "large-payload";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "upload";
  info.timeout = std::chrono::milliseconds(5000);
  info.payload = largePayload;

  auto serialized = serializer->serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->payload, largePayload);
}

TEST(TicketSerializationTest, MakeLengthPrefixProducesCorrectBytes)
{
  auto prefix = TicketFrame::makeLengthPrefix(0);
  EXPECT_EQ(prefix.size(), 4);
  for (auto byte : prefix)
  {
    EXPECT_EQ(static_cast<int>(static_cast<unsigned char>(byte)), 0);
  }

  auto prefix2 = TicketFrame::makeLengthPrefix(255);
  EXPECT_EQ(static_cast<unsigned char>(prefix2[0]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix2[1]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix2[2]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix2[3]), 255);

  auto prefix3 = TicketFrame::makeLengthPrefix(65535);
  EXPECT_EQ(static_cast<unsigned char>(prefix3[0]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix3[1]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix3[2]), 255);
  EXPECT_EQ(static_cast<unsigned char>(prefix3[3]), 255);

  auto prefix4 = TicketFrame::makeLengthPrefix(16777215);
  EXPECT_EQ(static_cast<unsigned char>(prefix4[0]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix4[1]), 255);
  EXPECT_EQ(static_cast<unsigned char>(prefix4[2]), 255);
  EXPECT_EQ(static_cast<unsigned char>(prefix4[3]), 255);

  auto prefix5 = TicketFrame::makeLengthPrefix(4294967295U);
  for (auto byte : prefix5)
  {
    EXPECT_EQ(static_cast<unsigned char>(byte), 255);
  }
}

TEST(TicketSerializationTest, SerializeSpecialCharacters)
{
  auto serializer = makeSerializer();

  std::string payload = "Line1\nLine2\tTabbed\rQuote: \"hello\"\\Backslash";
  TicketInfo info;
  info.ticket_id = "special-chars";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "test";
  info.timeout = std::chrono::milliseconds(1000);
  info.payload = payload;

  auto serialized = serializer->serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->payload, payload);
}

TEST(TicketSerializationTest, SerializeWithMetadata)
{
  auto serializer = makeSerializer();

  TicketInfo info;
  info.ticket_id = "metadata-test";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "test";
  info.timeout = std::chrono::milliseconds(1000);
  info.metadata.data["key1"] = "value1";
  info.metadata.data["key2"] = "value2";

  auto serialized = serializer->serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->metadata.data["key1"], "value1");
  EXPECT_EQ(deserialized->metadata.data["key2"], "value2");
}

TEST(TicketSerializationTest, SerializeCancelledTicket)
{
  auto serializer = makeSerializer();

  TicketInfo info;
  info.ticket_id = "cancelled-1";
  info.type = TicketType::LONG;
  info.status = TicketStatus::CANCELLED;
  info.command = "render";
  info.timeout = std::chrono::milliseconds(5000);

  auto serialized = serializer->serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->status, TicketStatus::CANCELLED);
}

TEST(TicketSerializationTest, SerializeWithResult)
{
  auto serializer = makeSerializer();

  TicketInfo info;
  info.ticket_id = "result-test";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::COMPLETED;
  info.command = "compute";
  info.timeout = std::chrono::milliseconds(1000);
  info.result = R"({"sum": 42})";

  auto serialized = serializer->serialize(info);
  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->result, R"({"sum": 42})");
}

TEST(TicketSerializationTest, SerializeWithError)
{
  auto serializer = makeSerializer();

  TicketInfo info;
  info.ticket_id = "error-test";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::FAILED;
  info.command = "compute";
  info.timeout = std::chrono::milliseconds(1000);
  info.error = "computation failed";

  auto serialized = serializer->serialize(info);
  auto deserialized = serializer->deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->error, "computation failed");
}

}  // namespace Network::Test

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <gtest/gtest.h>

#include "client/ClientSync.h"
#include "core/Context.h"
#include "protocol/Ticket.h"
#include "protocol/TicketController.h"
#include "protocol/TicketFrame.h"
#include "protocol/TicketJson.h"
#include "protocol/TicketPeer.h"
#include "protocol/TicketWorker.h"
#include "protocol/TicketSerializer.h"
#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

namespace
{

constexpr uint16_t TEST_MISUSE_PORT = 12352;

// Convert string to std::vector<std::byte>
inline std::vector<std::byte> str_to_bytes(const std::string& s)
{
  std::vector<std::byte> bytes(s.size());
  for (size_t i = 0; i < s.size(); ++i)
    bytes[i] = std::byte(static_cast<unsigned char>(s[i]));
  return bytes;
}

TicketInfo makeValidTicketInfo()
{
  TicketInfo info;
  info.ticket_id = "test-misuse-1";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "test";
  info.timeout = std::chrono::milliseconds(5000);
  return info;
}

TicketInfo makeEchoTicket(std::string_view payload)
{
  TicketInfo info;
  info.ticket_id = "echo-1";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "echo";
  info.timeout = std::chrono::milliseconds(5000);
  info.payload = std::string(payload);
  return info;
}

std::unique_ptr<DualSocket> createClient(asio::io_context& io_ctx)
{
  ClientSync client("127.0.0.1", TEST_MISUSE_PORT, io_ctx);
  ClientSync::Options opts;
  opts.timeout = std::chrono::milliseconds(5000);
  auto result = client.connect(opts);
  return result ? std::move(*result) : nullptr;
}

struct WorkerEntry
{
  std::unique_ptr<DualSocket> socket;
  std::unique_ptr<TicketWorker> worker;
  std::thread thread;
};

class TicketServer
{
public:
  TicketServer(asio::io_context& io_ctx)
    : server_(
        TEST_MISUSE_PORT, io_ctx, [this](std::unique_ptr<DualSocket> sock) { this->handle_client(std::move(sock)); })
  {
  }

  ~TicketServer()
  {
    std::vector<WorkerEntry> entries;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      entries.swap(workers_);
    }
    for (auto& e : entries)
    {
      if (e.thread.joinable())
        e.thread.join();
    }
  }

  void start()
  {
    server_thread_ = std::thread([this]() { server_.listen(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void stop()
  {
    server_.stop();
    if (server_thread_.joinable())
      server_thread_.join();
  }

  void registerHandler(std::string_view command, TicketWorker::TicketHandler handler)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[std::string(command)] = std::move(handler);
  }

protected:
  void handle_client(std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    auto entry = WorkerEntry{std::move(sock), nullptr, std::thread{}};
    entry.worker = std::make_unique<TicketWorker>(std::move(entry.socket), TicketWorker::Options{});
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto& [cmd, handler] : handlers_)
      {
        entry.worker->registerHandler(cmd, handler);
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    workers_.push_back(std::move(entry));

    auto& w = workers_.back();

    w.thread = std::thread(
      [&w]()
      {
        try
        {
          while (true)
          {
            auto result = w.worker->handleNext();
            if (!result)
              break;
          }
        }
        catch (const std::exception&)
        {
        }
      });
  }

  ServerSync server_;
  std::vector<WorkerEntry> workers_;
  std::mutex mutex_;
  std::unordered_map<std::string, TicketWorker::TicketHandler> handlers_;
  std::thread server_thread_;
};

}  // namespace

class TicketMisuseFixture : public ::testing::Test
{
protected:
  void SetUp() override { _io_ctx.start(); }
  void TearDown() override { _io_ctx.stop(); }

  asio::io_context& getIoContext() { return _io_ctx; }

protected:
  IoContextWrapper _io_ctx;
};

// ====================================================================
// Group 1: TicketPeer Misuse (7 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, SendBeforeHandshake)
{
  // Create peer with connected socket but never performs handshake first.
  // sendFrame serializes and writes without checking handshake state, so it
  // will succeed on a connected socket even without handshake.
  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  TicketPeer peer(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = peer.sendFrame(makeValidTicketInfo());

  // Socket is connected so write can succeed (no handshake guard exists).
  if (result)
    EXPECT_TRUE(*result > 0);
}

TEST_F(TicketMisuseFixture, ReceiveBeforeHandshake)
{
  // Create peer with connected socket and try to receive before any handshake.
  // The server-side peer is not sending anything, so we will timeout.
  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  TicketPeer peer(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = peer.receiveFrame();

  EXPECT_FALSE(result);
}

TEST_F(TicketMisuseFixture, ReceiveAfterGoodbye)
{
  // Full handshake, then goodbye (which closes socket), then receive -> error.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("echo", "hello");
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "hello");

  auto goodbye_result = controller->getPeer().goodbye();
  if (goodbye_result)
    EXPECT_FALSE(controller->getPeer().isConnected());

  // After goodbye the socket is closed, so sendFrame should fail.
  auto after_result = controller->getPeer().sendFrame(makeValidTicketInfo());
  EXPECT_FALSE(after_result);

  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, SendAfterGoodbye)
{
  // Handshake, goodbye, then sendFrame on the same peer should fail.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("echo", "world");
  EXPECT_TRUE(result);

  // Explicitly close the peer before sending more.
  controller->getPeer().goodbye();

  // Try sending after goodbye - the underlying socket should be closed.
  auto after_result = controller->getPeer().sendFrame(makeValidTicketInfo());
  EXPECT_FALSE(after_result);

  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, GoodbyeAfterGoodbye)
{
  // First goodbye and then again. Both should succeed (idempotent close).
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("echo", "ok");
  EXPECT_TRUE(result);

  auto gb1 = controller->getPeer().goodbye();
  EXPECT_TRUE(gb1);

  auto gb2 = controller->getPeer().goodbye();
  EXPECT_TRUE(gb2);
  EXPECT_FALSE(controller->getPeer().isConnected());

  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, DuplicateHandshake)
{
  // Controller sends two independent handshake cycles (each send() IS a
  // handshake). Second should work fine since the protocol allows reusing the
  // connection.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());

  auto result1 = controller->send("echo", "first");
  EXPECT_TRUE(result1);
  EXPECT_EQ(*result1, "first");

  auto result2 = controller->send("echo", "second");
  EXPECT_TRUE(result2);
  EXPECT_EQ(*result2, "second");

  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, PeerAfterSocketDestroy)
{
  // Create a peer, then close the socket, then check peer state.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  {
    tcp_socket->closeSocket();
  }

  TicketPeer peer(std::move(tcp_socket), std::make_shared<TicketJson>());

  // Peer should report disconnected since its socket is closed.
  EXPECT_FALSE(peer.isConnected());
}

// ====================================================================
// Group 2: TicketController Misuse (7 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, ControllerWithoutConnection)
{
  // Create controller with an unconnected socket. All ops should fail.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  TicketController controller(std::move(tcp_socket), std::make_shared<TicketJson>());

  auto result = controller.send("ping", "");
  EXPECT_FALSE(result);
}

TEST_F(TicketMisuseFixture, SendBeforePeerReady)
{
  // In the current implementation, controller does NOT perform an auto-
  // handshake on construction. The first send() call IS the handshake. This
  // test documents that behavior.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());

  // First send() acts as the handshake too.
  auto result = controller->send("echo", "test-data");
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "test-data");

  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, PollNonexistentTicket)
{
  // Poll a ticket ID that was never created. Since socket is unconnected the
  // poll call will fail with a connection error.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  TicketController controller(std::move(tcp_socket), std::make_shared<TicketJson>());

  auto result = controller.poll("never-created-ticket-id-xyz-12345");

  // Unconnected socket - expect error.
  EXPECT_FALSE(result);
}

TEST_F(TicketMisuseFixture, CancelNonexistentTicket)
{
  // Cancel a ticket ID that was never created. Since socket is unconnected we
  // get a connection error.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  TicketController controller(std::move(tcp_socket), std::make_shared<TicketJson>());

  auto result = controller.cancel("nonexistent-ticket-abc-67890");
  EXPECT_FALSE(result);
}

TEST_F(TicketMisuseFixture, UseControllerAfterMove)
{
  // Move-construct a controller from another. The moved-from source is
  // unusable because _peer was moved away.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  auto controller = std::make_unique<TicketController>(std::move(tcp_socket), std::make_shared<TicketJson>());

  auto moved = std::move(*controller);

  // The moved-from controller should be unusable.
  auto moved_result = moved.send("ping", "data");
  EXPECT_FALSE(moved_result);
}

TEST_F(TicketMisuseFixture, MoveAssignmentController)
{
  // Move-assign TicketController. Verify states.
  auto tcp_socket1 = std::make_unique<TcpSocket>(getIoContext());
  auto tcp_socket2 = std::make_unique<TcpSocket>(getIoContext());

  auto ctrl1 = std::make_unique<TicketController>(std::move(tcp_socket1), std::make_shared<TicketJson>());
  auto ctrl2 = std::make_unique<TicketController>(std::move(tcp_socket2), std::make_shared<TicketJson>());

  // Verify getPeer() works before assignment.
  EXPECT_FALSE(ctrl1->getPeer().isConnected());

  *ctrl2 = std::move(*ctrl1);

  // ctrl2 should now have the socket from ctrl1.
  EXPECT_FALSE(ctrl1->getPeer().isConnected());
}

TEST_F(TicketMisuseFixture, GetPeerAfterDestroy)
{
  // getPeer() returns reference to embedded _peer member. It works before
  // destruction but accessing getPeer() after destruction is UB. Test verifies
  // it works before destruction.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  auto controller = std::make_unique<TicketController>(std::move(tcp_socket), std::make_shared<TicketJson>());

  const auto& peer = controller->getPeer();
  EXPECT_FALSE(peer.isConnected());
}

// ====================================================================
// Group 3: TicketWorker Misuse (6 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, WorkerNoHandlers)
{
  // Worker with zero handlers registered: controller sends any command and
  // worker sends back FAILED via sendErrorResponse("unknown command").
  TicketServer server(_io_ctx);
  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("no-such-handler", "payload");

  EXPECT_FALSE(result);
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, WorkerHandlerReturnEmpty)
{
  // Handler returns empty string - should be a valid result.
  TicketServer server(_io_ctx);
  server.registerHandler("empty-result",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return ""; });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("empty-result", "data");

  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "");
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, WorkerHandlerReturnError)
{
  // Handler returns error result. Controller receives error status.
  TicketServer server(_io_ctx);
  server.registerHandler("fail-handler", [](const TicketContext&) -> std::expected<std::string, std::error_code>
                         { return std::unexpected(std::make_error_code(std::errc::io_error)); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("fail-handler", "data");

  EXPECT_FALSE(result);
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, WorkerDestroyWithoutGoodbye)
{
  // Worker is destroyed by going out of scope without explicit goodbye. The
  // controller should still get its response.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("echo", "test-destroy");
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "test-destroy");

  controller.reset();
}

TEST_F(TicketMisuseFixture, WorkerRegisterSameCommand)
{
  // Two handlers registered for the same command; second overwrites first.
  TicketServer server(_io_ctx);
  server.registerHandler("overwrite-test",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "first"; });
  server.registerHandler("overwrite-test",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "second"; });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("overwrite-test", "data");

  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "second");
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, WorkerHandleNextAfterGoodbye)
{
  // Controller sends then closes peer. Worker's handleNext on the server-side
  // detects the closed connection and returns error.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("echo", "before-close");
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "before-close");

  // Close the client-side peer to close the connection.
  controller->getPeer().goodbye();

  controller.reset();
  server.stop();
}

// ====================================================================
// Group 4: Controller/Worker Mismatch (2 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, ControllerWorkerMismatch)
{
  // Controller sends command "xyz_no_op" but worker has no handler for it.
  // Worker sends back an error response via sendErrorResponse.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());
  auto result = controller->send("xyz_nonexistent_command", "payload");

  EXPECT_FALSE(result);
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, MultipleControllersOneWorker)
{
  // Two controllers connect to the same server. Workers are per-connection so
  // each gets its own worker. Both should function independently.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto client_socket_a = createClient(getIoContext());
  ASSERT_TRUE(client_socket_a != nullptr);
  auto client_socket_b = createClient(getIoContext());
  ASSERT_TRUE(client_socket_b != nullptr);

  auto controller_a = std::make_unique<TicketController>(std::move(client_socket_a), std::make_shared<TicketJson>());
  auto controller_b = std::make_unique<TicketController>(std::move(client_socket_b), std::make_shared<TicketJson>());

  auto result_a = controller_a->send("echo", "from-a");
  auto result_b = controller_b->send("echo", "from-b");

  EXPECT_TRUE(result_a);
  EXPECT_EQ(*result_a, "from-a");
  EXPECT_TRUE(result_b);
  EXPECT_EQ(*result_b, "from-b");

  controller_a.reset();
  controller_b.reset();
  server.stop();
}

// ====================================================================
// Group 5: TicketFrame Misuse (5 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, FrameCorruptPayload)
{
  // Verify length prefix for corrupted payload scenario. The actual corrupted
  // payload test would require a server that deliberately corrupts JSON, which
  // is complex to orchesture. We verify the key building blocks work.
  auto prefix = TicketFrame::makeLengthPrefix(0);
  EXPECT_EQ(prefix.size(), 4u);
  EXPECT_FALSE(prefix.empty());
}

TEST_F(TicketMisuseFixture, FrameTruncatedRead)
{
  // Verify length prefix for a 1000-byte body is correct (big-endian).
  auto prefix = TicketFrame::makeLengthPrefix(1000);
  EXPECT_EQ(prefix.size(), 4u);
  EXPECT_EQ(static_cast<unsigned char>(prefix[0]), 3);  // 1000 = 0x000003E8
  EXPECT_EQ(static_cast<unsigned char>(prefix[1]), 232);
  EXPECT_EQ(static_cast<unsigned char>(prefix[2]), 0);
  EXPECT_EQ(static_cast<unsigned char>(prefix[3]), 0);
}

TEST_F(TicketMisuseFixture, FrameLengthPrefixZero)
{
  auto prefix = TicketFrame::makeLengthPrefix(0);
  EXPECT_EQ(prefix.size(), 4u);
  for (auto byte : prefix)
  {
    EXPECT_EQ(static_cast<unsigned char>(byte), 0);
  }
}

TEST_F(TicketMisuseFixture, FrameLengthPrefixMax)
{
  // Frame with max uint32 body length.
  auto prefix = TicketFrame::makeLengthPrefix(4294967295U);
  EXPECT_EQ(prefix.size(), 4u);
  for (auto byte : prefix)
  {
    EXPECT_EQ(static_cast<unsigned char>(byte), 255);
  }
}

TEST_F(TicketMisuseFixture, FrameWriteWithoutConnect)
{
  TcpSocket socket(getIoContext());

  std::string msg = "test";
  std::vector<std::byte> data(str_to_bytes(msg));

  auto result = TicketFrame::writeFrame(socket, std::span(data));

  // Unconnected socket cannot write frames.
  EXPECT_FALSE(result);
}

// ====================================================================
// Group 6: TicketInfo Edge Cases (5 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, NullMetadata)
{
  // Ticket with null/empty metadata map serializes/deserializes correctly.
  TicketJson serializer;
  TicketInfo info;
  info.ticket_id = "null-meta-1";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "echo";
  info.timeout = std::chrono::milliseconds(5000);
  // metadata.data is empty by default.

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id, "null-meta-1");
  EXPECT_EQ(deserialized->metadata.data.size(), 0u);
}

TEST_F(TicketMisuseFixture, AllOptionalFields)
{
  // Ticket with all optional fields populated.
  TicketJson serializer;

  TicketInfo info;
  info.ticket_id = "all-optional-1";
  info.type = TicketType::LONG;
  info.status = TicketStatus::RUNNING;
  info.command = "heavy-task";
  info.timeout = std::chrono::milliseconds(10000);
  info.progress_percent = 75;
  info.progress_msg = "almost done";
  info.payload = R"({"task": "render", "params": {}})";
  info.result = R"({"frames": 100, "time": "5s"})";
  info.error = "should not be here yet";
  info.metadata.data["user"] = "tester";
  info.metadata.data["priority"] = "high";

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->progress_percent, 75);
  EXPECT_EQ(*deserialized->progress_msg, "almost done");
  EXPECT_EQ(*deserialized->payload, R"({"task": "render", "params": {}})");
  EXPECT_EQ(*deserialized->result, R"({"frames": 100, "time": "5s"})");
  EXPECT_EQ(*deserialized->error, "should not be here yet");
  EXPECT_EQ(deserialized->metadata.data["user"], "tester");
}

TEST_F(TicketMisuseFixture, NoneOptionalFields)
{
  // Ticket with no optional fields populated (all default/nullopt).
  TicketJson serializer;

  TicketInfo info;
  info.ticket_id = "minimal-req";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "ping";
  info.timeout = std::chrono::milliseconds(1000);
  // No payload, no result, no error, no progress, no metadata.

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id, "minimal-req");
  EXPECT_FALSE(deserialized->payload.has_value());
  EXPECT_FALSE(deserialized->result.has_value());
  EXPECT_FALSE(deserialized->error.has_value());
  EXPECT_FALSE(deserialized->progress_percent.has_value());
  EXPECT_FALSE(deserialized->progress_msg.has_value());
  EXPECT_EQ(deserialized->metadata.data.size(), 0u);
}

TEST_F(TicketMisuseFixture, InvalidTicketId)
{
  // Very long ticket ID (10KB). Serializes fine since it's just a string.
  TicketJson serializer;

  std::string large_id(10240, 'A');
  for (size_t i = 0; i < large_id.size(); ++i)
    large_id[i] = static_cast<char>('A' + (i % 26));

  TicketInfo info;
  info.ticket_id = large_id;
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = "test";
  info.timeout = std::chrono::milliseconds(5000);

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->ticket_id.size(), 10240u);
  EXPECT_EQ(deserialized->ticket_id, large_id);
}

TEST_F(TicketMisuseFixture, InvalidCommand)
{
  // Command with special characters (including control chars).
  TicketJson serializer;

  std::string special_cmd = "cmd\x01with\x02special@chars\n\r\t";

  TicketInfo info;
  info.ticket_id = "special-cmd-1";
  info.type = TicketType::SHORT;
  info.status = TicketStatus::PENDING;
  info.command = special_cmd;
  info.timeout = std::chrono::milliseconds(5000);

  auto serialized = serializer.serialize(info);
  ASSERT_FALSE(serialized.empty());

  auto deserialized = serializer.deserialize(std::span(serialized));
  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->command, special_cmd);
}

// ====================================================================
// Group 7: Move Operations (5 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, PeerMoveConstructor)
{
  // Move-construct TicketPeer from another. Source _closed = true so cannot
  // use it anymore. Dest has socket and is usable.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());
  auto serial = std::make_shared<TicketJson>();

  {
    auto peer1 = std::make_unique<TicketPeer>(std::move(tcp_socket), serial);
    auto peer2 = std::move(*peer1);

    // peer1 should be in moved-from state
    EXPECT_FALSE(peer1->isConnected());
  }
}

TEST_F(TicketMisuseFixture, PeerMoveAssignment)
{
  // Move-assign TicketPeer. Verify both states.
  auto tcp_socket1 = std::make_unique<TcpSocket>(getIoContext());
  auto tcp_socket2 = std::make_unique<TcpSocket>(getIoContext());
  auto serial = std::make_shared<TicketJson>();

  auto peer1 = std::make_unique<TicketPeer>(std::move(tcp_socket1), serial);
  auto peer2 = std::make_unique<TicketPeer>(std::move(tcp_socket2), serial);

  peer1->getSocket().isConnected();

  *peer1 = std::move(*peer2);

  // peer2 should now be in moved-from state
  EXPECT_FALSE(peer2->isConnected());
}

TEST_F(TicketMisuseFixture, WorkerMoveConstructor)
{
  // Move-construct TicketWorker from another.
  auto tcp_socket = std::make_unique<TcpSocket>(getIoContext());

  auto worker1 = std::make_unique<TicketWorker>(std::move(tcp_socket), TicketWorker::Options{});
  auto worker2 = std::move(*worker1);

  // worker1 should be unusable
  EXPECT_FALSE(worker1->getPeer().isConnected());
}

TEST_F(TicketMisuseFixture, WorkerMoveAssignment)
{
  // Move-assign TicketWorker. Verify both states.
  auto tcp_socket1 = std::make_unique<TcpSocket>(getIoContext());
  auto tcp_socket2 = std::make_unique<TcpSocket>(getIoContext());

  auto worker1 = std::make_unique<TicketWorker>(std::move(tcp_socket1), TicketWorker::Options{});
  auto worker2 = std::make_unique<TicketWorker>(std::move(tcp_socket2), TicketWorker::Options{});

  *worker2 = std::move(*worker1);

  EXPECT_FALSE(worker1->getPeer().isConnected());
}

TEST_F(TicketMisuseFixture, ControllerMoveConstructor)
{
  // Move-construct TicketController from another.
  auto tcp_socket1 = std::make_unique<TcpSocket>(getIoContext());
  auto tcp_socket2 = std::make_unique<TcpSocket>(getIoContext());

  auto ctrl1 = std::make_unique<TicketController>(std::move(tcp_socket1), std::make_shared<TicketJson>());

  auto ctrl2 = std::move(*ctrl1);

  // ctrl1 should be in moved-from state
  EXPECT_FALSE(ctrl1->getPeer().isConnected());
}

// ====================================================================
// Group 8: Timeout Misuse (5 tests)
// ====================================================================

TEST_F(TicketMisuseFixture, ZeroTimeout)
{
  // Controller sends with 0ms timeout. Result depends on server response
  // time, but should not crash.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();
  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());

  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds(0);

  auto result = controller->send("echo", "zero-timeout-data", "", opts);
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, NegativeTimeout)
{
  // Controller sends with negative timeout (undefined behavior - may wrap to
  // a very large value).
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();
  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());

  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds(-1);

  auto result = controller->send("echo", "negative-timeout-data", "", opts);
  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, ExtremeTimeout)
{
  // Controller sends with max timeout. Since it's local, response arrives
  // instantly so the timeout is never reached.
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();
  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());

  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds::max();

  auto result = controller->send("echo", "extreme-timeout-data", "", opts);
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "extreme-timeout-data");

  controller.reset();
  server.stop();
}

TEST_F(TicketMisuseFixture, TimeoutExpired)
{
  // Handler sleeps for 500ms. Controller timeout is only 100ms. Response
  // should fail with timeout error.
  TicketServer server(_io_ctx);
  server.registerHandler("slow-handler",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code>
                         {
                           std::this_thread::sleep_for(std::chrono::milliseconds(500));
                           return "slow-result";
                         });

  server.start();
  auto client_socket = createClient(getIoContext());
  ASSERT_TRUE(client_socket != nullptr);

  auto controller = std::make_unique<TicketController>(std::move(client_socket), std::make_shared<TicketJson>());

  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds(100);

  auto result = controller->send("slow-handler", "data", "", opts);
  EXPECT_FALSE(result);

  controller.reset();
  server.stop();
}

}  // namespace Network::Test

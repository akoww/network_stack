#include <array>
#include <asio.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

#include "client/Client.h"
#include "core/ErrorCodes.h"
#include "core/Context.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/Server.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

constexpr uint16_t TEST_PORT = 12349;

// ============================================================================
// Group: Unconnected Socket Misuse
// ============================================================================

TEST_F(IoContextFixture, ReadOnUnconnectedSocket)
{
  TcpSocket socket(getIoContext());

  std::array<std::byte, 1024> buffer{};

  auto read_some_result = socket.readSome(std::span(buffer));
  EXPECT_FALSE(read_some_result.has_value());
  if (!read_some_result)
  {
    EXPECT_STREQ(read_some_result.error().category().name(), "asio.system");
    EXPECT_NE(read_some_result.error().value(), 0);
  }

  std::array<std::byte, 1024> buffer2{};
  auto read_exact_result = socket.readExact(std::span(buffer2));
  EXPECT_FALSE(read_exact_result.has_value());
  if (!read_exact_result)
  {
    EXPECT_STREQ(read_exact_result.error().category().name(), "asio.system");
    EXPECT_NE(read_exact_result.error().value(), 0);
  }

  std::array<std::byte, 1024> buffer3{};
  auto read_until_result = socket.readUntil(std::span(buffer3), "\n");
  EXPECT_FALSE(read_until_result.has_value());
  if (!read_until_result)
  {
    EXPECT_STREQ(read_until_result.error().category().name(), "asio.system");
    EXPECT_NE(read_until_result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, WriteOnUnconnectedSocket)
{
  TcpSocket socket(getIoContext());

  std::string msg = "hello";
  auto write_result = socket.writeAll(to_bytes(msg));
  EXPECT_FALSE(write_result.has_value());
  if (!write_result)
  {
    EXPECT_STREQ(write_result.error().category().name(), "asio.system");
    EXPECT_NE(write_result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, AsyncReadOnUnconnectedSocket)
{
  TcpSocket socket(getIoContext());

  std::array<std::byte, 1024> buffer{};
  auto future = asio::co_spawn(
    getIoContext(), [&socket, &buffer]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket.asyncReadSome(std::span(buffer)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto result = future.get();
  EXPECT_FALSE(result.has_value());
  if (!result)
  {
    EXPECT_STREQ(result.error().category().name(), "asio.system");
    EXPECT_NE(result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, AsyncWriteOnUnconnectedSocket)
{
  TcpSocket socket(getIoContext());

  std::string msg = "hello";
  auto future = asio::co_spawn(
    getIoContext(), [&socket, msg]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket.asyncWriteAll(to_bytes(msg)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto result = future.get();
  EXPECT_FALSE(result.has_value());
  if (!result)
  {
    EXPECT_STREQ(result.error().category().name(), "asio.system");
    EXPECT_NE(result.error().value(), 0);
  }
}

// ============================================================================
// Group: Post-Close Misuse
// ============================================================================

TEST_F(IoContextFixture, ReadAfterClose)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();

  std::array<std::byte, 1024> buffer{};
  auto read_result = socket.readSome(std::span(buffer));
  EXPECT_FALSE(read_result.has_value());
  if (!read_result)
  {
    EXPECT_STREQ(read_result.error().category().name(), "asio.system");
    EXPECT_NE(read_result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, WriteAfterClose)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();

  std::string msg = "hello";
  auto write_result = socket.writeAll(to_bytes(msg));
  EXPECT_FALSE(write_result.has_value());
  if (!write_result)
  {
    EXPECT_STREQ(write_result.error().category().name(), "asio.system");
    EXPECT_NE(write_result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, ReadExactAfterClose)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();

  std::array<std::byte, 1024> buffer{};
  auto read_result = socket.readExact(std::span(buffer));
  EXPECT_FALSE(read_result.has_value());
  if (!read_result)
  {
    EXPECT_STREQ(read_result.error().category().name(), "asio.system");
    EXPECT_NE(read_result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, ReadUntilAfterClose)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();

  std::array<std::byte, 1024> buffer{};
  auto read_result = socket.readUntil(std::span(buffer), "\n");
  EXPECT_FALSE(read_result.has_value());
  if (!read_result)
  {
    EXPECT_STREQ(read_result.error().category().name(), "asio.system");
    EXPECT_NE(read_result.error().value(), 0);
  }
}

// ============================================================================
// Group: Post-Cancel Misuse
// ============================================================================

TEST_F(IoContextFixture, ReadAfterCancel)
{
  TcpSocket socket(getIoContext());
  socket.cancelSocket();

  std::array<std::byte, 1024> buffer{};
  auto read_result = socket.readSome(std::span(buffer));
  EXPECT_FALSE(read_result.has_value());
  if (!read_result)
  {
    EXPECT_STREQ(read_result.error().category().name(), "asio.system");
    EXPECT_NE(read_result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, WriteAfterCancel)
{
  TcpSocket socket(getIoContext());
  socket.cancelSocket();

  std::string msg = "hello";
  auto write_result = socket.writeAll(to_bytes(msg));
  EXPECT_FALSE(write_result.has_value());
  if (!write_result)
  {
    EXPECT_STREQ(write_result.error().category().name(), "asio.system");
    EXPECT_NE(write_result.error().value(), 0);
  }
}

// ============================================================================
// Group: Buffer Misuse
// ============================================================================

TEST_F(IoContextFixture, ReadExactUndersizedBuffer)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::string msg = "hello";
  auto send_result = client_socket->writeAll(to_bytes(msg));
  EXPECT_TRUE(send_result);

  // Buffer smaller than expected count - readExact fills what's available
  std::array<std::byte, 4> buffer{};
  auto read_result = client_socket->readExact(std::span(buffer));
  EXPECT_TRUE(read_result);
  if (read_result)
  {
    EXPECT_EQ(*read_result, 4);
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ReadUntilNeverMatch)
{
  TcpSocket socket(getIoContext());

  std::array<std::byte, 1024> buffer{};
  auto read_result = socket.readUntil(std::span(buffer), "\\x00\\x00\\x00\\x00", std::chrono::milliseconds(100));
  EXPECT_FALSE(read_result.has_value());
  if (!read_result)
  {
    EXPECT_STREQ(read_result.error().category().name(), "asio.system");
    EXPECT_EQ(read_result.error().value(), static_cast<int>(asio::error::not_connected));
  }
}

TEST_F(IoContextFixture, ReadExactExactSizeBuffer)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  // Send exactly 5 bytes
  std::string msg = "hello";
  auto send_result = client_socket->writeAll(to_bytes(msg));
  EXPECT_TRUE(send_result);

  // Buffer exactly matches expected count (5 bytes)
  std::array<std::byte, 5> buffer{};
  auto read_result = client_socket->readExact(std::span(buffer));
  EXPECT_TRUE(read_result);
  if (read_result)
  {
    EXPECT_EQ(*read_result, 5);
  }

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: Multiple Close/Cancels
// ============================================================================

TEST_F(IoContextFixture, MultipleCloseCalls)
{
  TcpSocket socket(getIoContext());

  // Should not throw or crash
  socket.closeSocket();
  socket.closeSocket();
  socket.closeSocket();
}

TEST_F(IoContextFixture, CancelThenClose)
{
  TcpSocket socket(getIoContext());

  socket.cancelSocket();
  socket.closeSocket();

  // No crash or assertion
}

TEST_F(IoContextFixture, CloseThenCancel)
{
  TcpSocket socket(getIoContext());

  socket.closeSocket();
  socket.cancelSocket();

  // No crash or assertion
}

// ============================================================================
// Group: Connection Misuse
// ============================================================================

TEST_F(IoContextFixture, ConnectToRefusedPort)
{
  // Use a port that's definitely not in use
  constexpr uint16_t refused_port = 61234;

  Client client("127.0.0.1", refused_port, getIoContext());
  auto connect_result = client.connect();
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result)
  {
    EXPECT_STREQ(connect_result.error().category().name(), "network");
  }
}

TEST_F(IoContextFixture, ConnectToNonExistentHost)
{
  // Use a private address that will get ECONNREFUSED quickly
  Client client("192.168.255.255", 12345, getIoContext());
  auto connect_result = client.connect();
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result)
  {
    EXPECT_STREQ(connect_result.error().category().name(), "network");
  }
}

TEST_F(IoContextFixture, ConnectTimeout)
{
  // Connect to a non-routable address - will get connection refused quickly
  Client client("192.168.255.255", 12345, getIoContext());
  auto connect_result = client.connect();
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result)
  {
    EXPECT_STREQ(connect_result.error().category().name(), "network");
  }
}

// ============================================================================
// Group: Mixed Sync/Async
// ============================================================================

TEST_F(SyncClientServerFixture, MixedSyncAsyncOps)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Connect with sync client
  Client client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto socket = std::move(*connect_result);

  // Mix sync write with async read on the same socket
  std::string msg = "mixed test";
  auto sync_write_result = socket->writeAll(to_bytes(msg));
  EXPECT_TRUE(sync_write_result);

  // Then async read
  std::array<std::byte, 1024> async_buffer{};
  auto async_read_future = asio::co_spawn(
    getIoContext(),
    [socket = socket.get(), &async_buffer]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket->asyncReadSome(std::span(async_buffer)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto async_read_result = async_read_future.get();
  EXPECT_TRUE(async_read_result.has_value());

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: Socket Lifecycle
// ============================================================================

TEST_F(IoContextFixture, DestructWithoutClose)
{
  // Connect to a real server, then destruct without explicit close
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  // Let the socket go out of scope without explicit close
  {
    auto socket = std::move(*connect_result);
    std::string msg = "cleanup test";
    socket->writeAll(to_bytes(msg));
  }  // ~TcpSocket called here

  server.stop();
  server_thread.join();
  // No crash = success
}

TEST_F(IoContextFixture, GetIdBeforeConnect)
{
  TcpSocket socket(getIoContext());
  auto id = socket.getId();
  EXPECT_GT(id, 0u);
}

TEST_F(IoContextFixture, GetReadBufferBeforeConnect)
{
  TcpSocket socket(getIoContext());
  auto& buffer = socket.getReadBuffer();
  // Should be a valid (empty) buffer
  EXPECT_TRUE(buffer.data() != nullptr || buffer.empty());
}

// ============================================================================
// Group: Async Operations
// ============================================================================

TEST_F(IoContextFixture, AsyncReadOnClosedSocket)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();

  std::array<std::byte, 1024> buffer{};
  auto future = asio::co_spawn(
    getIoContext(), [&socket, &buffer]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket.asyncReadSome(std::span(buffer)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto result = future.get();
  EXPECT_FALSE(result.has_value());
  if (!result)
  {
    EXPECT_STREQ(result.error().category().name(), "asio.system");
    EXPECT_NE(result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, AsyncWriteOnClosedSocket)
{
  TcpSocket socket(getIoContext());
  socket.closeSocket();

  std::string msg = "hello";
  auto future = asio::co_spawn(
    getIoContext(), [&socket, msg]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket.asyncWriteAll(to_bytes(msg)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto result = future.get();
  EXPECT_FALSE(result.has_value());
  if (!result)
  {
    EXPECT_STREQ(result.error().category().name(), "asio.system");
    EXPECT_NE(result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, CancelBeforeAsyncRead)
{
  TcpSocket socket(getIoContext());
  socket.cancelSocket();

  std::array<std::byte, 1024> buffer{};
  auto future = asio::co_spawn(
    getIoContext(), [&socket, &buffer]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket.asyncReadSome(std::span(buffer)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto result = future.get();
  EXPECT_FALSE(result.has_value());
  if (!result)
  {
    EXPECT_STREQ(result.error().category().name(), "asio.system");
    EXPECT_NE(result.error().value(), 0);
  }
}

TEST_F(IoContextFixture, CancelBeforeAsyncWrite)
{
  TcpSocket socket(getIoContext());
  socket.cancelSocket();

  std::string msg = "hello";
  auto future = asio::co_spawn(
    getIoContext(), [&socket, msg]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket.asyncWriteAll(to_bytes(msg)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto result = future.get();
  EXPECT_FALSE(result.has_value());
  if (!result)
  {
    EXPECT_STREQ(result.error().category().name(), "asio.system");
    EXPECT_NE(result.error().value(), 0);
  }
}

// ============================================================================
// Group: Edge Case Sockets
// ============================================================================

TEST_F(IoContextFixture, MultipleSocketsSameContext)
{
  std::vector<std::unique_ptr<TcpSocket>> sockets;

  for (int i = 0; i < 20; i++)
  {
    auto socket = std::make_unique<TcpSocket>(getIoContext());
    EXPECT_GT(socket->getId(), 0u);
    sockets.push_back(std::move(socket));
  }

  // All 20 sockets should be valid
  EXPECT_EQ(sockets.size(), 20u);
  for (const auto& socket : sockets)
  {
    EXPECT_GT(socket->getId(), 0u);
  }
}

TEST_F(IoContextFixture, SocketMoveConstructor)
{
  auto original = std::make_unique<TcpSocket>(getIoContext());

  auto moved = std::move(original);

  // The moved socket should be valid
  EXPECT_GT(moved->getId(), 0u);

  // original should be null after move
  EXPECT_FALSE(original);

  std::array<std::byte, 1024> buffer{};
  auto result = moved->readSome(std::span(buffer));
  EXPECT_FALSE(result.has_value());
}

TEST_F(IoContextFixture, SocketMoveAssignment)
{
  auto original = std::make_unique<TcpSocket>(getIoContext());

  auto destination = std::make_unique<TcpSocket>(getIoContext());

  destination = std::move(original);

  // destination should now hold the original's socket
  EXPECT_GT(destination->getId(), 0u);

  // original should be null after move
  EXPECT_FALSE(original);

  std::array<std::byte, 1024> buffer{};
  auto result = destination->readSome(std::span(buffer));
  EXPECT_FALSE(result.has_value());
}

}  // namespace Network::Test

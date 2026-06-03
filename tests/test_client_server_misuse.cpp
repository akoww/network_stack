#include <array>
#include <atomic>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <string>
#include <thread>

#include "client/Client.h"
#include "client/Client.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/Server.h"
#include "server/Server.h"
#include "socket/TcpSocket.h"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

namespace Network::Test
{

// Group: DNS and Connection Errors

TEST_F(IoContextFixture, ClientToNonExistentHost)
{
  ClientSync client("fake.invalid.host.localhost", 12345, getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(2000)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::DNS_RESOLUTION_FAILED));
    EXPECT_STREQ(ec.category().name(), "network");
  }
}

TEST_F(IoContextFixture, ClientToNonExistentIpv4)
{
  ClientSync client("192.0.2.1", 1, getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(2000)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
    EXPECT_STREQ(ec.category().name(), "network");
  }
}

TEST_F(IoContextFixture, ClientToWrongPort)
{
  uint16_t unused_port = 60000;
  ClientSync client("127.0.0.1", unused_port, getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(2000)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
    EXPECT_STREQ(ec.category().name(), "network");
  }
}

TEST_F(IoContextFixture, ConnectTimeoutOnRefusedPort)
{
  uint16_t unused_port = 60001;
  ClientSync client("127.0.0.1", unused_port, getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(500)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    // Timeout may return TIMEOUT or CONNECTION_REFUSED depending on timing
    EXPECT_TRUE(ec.value() == static_cast<int>(Network::Error::TIMEOUT) ||
                ec.value() == static_cast<int>(Network::Error::CONNECTION_REFUSED));
    EXPECT_STREQ(ec.category().name(), "network");
  }
}

TEST_F(IoContextFixture, ConnectTimeoutOnUnreachable)
{
  ClientSync client("192.0.2.1", 12345, getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(3000)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_TRUE(ec.value() == static_cast<int>(Network::Error::CONNECTION_TIMEOUT) ||
                ec.value() == static_cast<int>(Network::Error::CONNECTION_REFUSED) ||
                ec.value() == static_cast<int>(Network::Error::DNS_RESOLUTION_FAILED));
    EXPECT_STREQ(ec.category().name(), "network");
  }
}

// Group: Double/Invalid Operations

TEST_F(IoContextFixture, DoubleConnect)
{
  EchoServer server(12347, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto first_connect = client.connect();
  ASSERT_TRUE(first_connect.has_value()) << "First connect failed";

  auto second_connect = client.connect();
  EXPECT_FALSE(second_connect.has_value());
  if (!second_connect.has_value())
  {
    auto ec = second_connect.error();
    EXPECT_NE(ec.value(), static_cast<int>(Network::Error::NO_ERROR));
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ServerPortInUse)
{
  EchoServer server1(12348, getIoContext());
  std::thread server_thread1(
    [&server1]()
    {
      auto listen_result = server1.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server1 listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EchoServer server2(12348, getIoContext());
  auto listen_result = server2.listen();
  EXPECT_FALSE(listen_result.has_value());
  if (!listen_result.has_value())
  {
    auto ec = listen_result.error();
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::SERVER_LISTEN_FAILED));
    EXPECT_STREQ(ec.category().name(), "network");
  }

  server1.stop();
  server_thread1.join();
}

TEST_F(IoContextFixture, ConnectAfterServerDestroyed)
{
  uint16_t port = 12349;
  std::unique_ptr<EchoServer> server;
  std::thread server_thread;
  {
    server = std::make_unique<EchoServer>(port, getIoContext());
    server_thread = std::thread(
      [server]()
      {
        auto listen_result = server->listen();
        EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
      });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  ClientSync client("127.0.0.1", port, getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(2000)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_TRUE(ec.value() == static_cast<int>(Network::Error::CONNECTION_REFUSED) ||
                ec.value() == static_cast<int>(Network::Error::CONNECTION_LOST));
  }
}

TEST_F(IoContextFixture, ServerListenOnZeroPort)
{
  EchoServer server(0, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  EXPECT_GT(server.port(), 0);
  EXPECT_LT(server.port(), 65536);

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Client connect failed";

  auto client_socket = std::move(*connect_result);
  auto send_result = client_socket->writeAll(to_bytes("test"));
  EXPECT_TRUE(send_result);

  server.stop();
  server_thread.join();
}

// Group: Mismatched Contexts

TEST_F(IoContextFixture, ClientServerMismatchedIoContext)
{
  Network::IoContextWrapper other_ctx;
  other_ctx.start();

  EchoServer server(12350, other_ctx);
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect({std::chrono::milliseconds(2000)});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_NE(ec.value(), static_cast<int>(Network::Error::NO_ERROR));
  }

  other_ctx.stop();
  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SyncClientToAsyncServer)
{
  EchoServer server(12351, getIoContext());
  asio::co_spawn(
    getIoContext(),
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Sync client to async server failed";

  auto client_socket = std::move(*connect_result);
  auto send_result = client_socket->writeAll(to_bytes("hello"));
  EXPECT_TRUE(send_result);

  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer));
  EXPECT_TRUE(recv_result);
  if (recv_result)
  {
    auto response = to_string_view(buffer, *recv_result);
    EXPECT_EQ("hello", response);
  }

  server.stop();
}

TEST_F(IoContextFixture, AsyncClientToSyncServer)
{
  EchoServer server(12352, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    getIoContext(),
    [port = server.port()]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      ClientAsync client("127.0.0.1", port, getIoContext());
      return client.asyncConnect();
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();
  ASSERT_TRUE(connect_result.has_value()) << "Async client to sync server failed";

  auto client_socket = std::move(*connect_result);
  auto send_future = asio::co_spawn(
    getIoContext(),
    [socket = client_socket.get()]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return socket->asyncWriteAll(to_bytes("hello")); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto send_result = send_future.get();
  EXPECT_TRUE(send_result);

  std::array<std::byte, 1024> buffer{};
  auto recv_future = asio::co_spawn(
    getIoContext(),
    [&buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    {
    return asio::use_future, [&buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    {
      // placeholder - will be replaced
      co_return std::expected<std::size_t, std::error_code>{std::error_code{}};
    }};

  server.stop();
}

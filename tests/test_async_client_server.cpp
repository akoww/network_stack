#include <array>
#include <asio.hpp>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <thread>

#include "client/ClientAsync.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_io_context.h"
#include "server/ServerAsync.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

constexpr uint16_t TEST_PORT = 12346;

TEST_F(AsyncClientServerFixture, MinimalConstructor)
{
  ClientAsync client("127.0.0.1", 12345, _io_ctx);
  EXPECT_EQ(client.host(), "127.0.0.1");
  EXPECT_EQ(client.port(), 12345);
}

TEST_F(AsyncClientServerFixture, MinimalConstructorServer)
{
  EchoServer server(12345, _io_ctx);
  EXPECT_EQ(server.host(), "0.0.0.0");
  EXPECT_EQ(server.port(), 12345);
}

TEST_F(AsyncClientServerFixture, EchoServerMultipleMessages)
{
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    _io_ctx,
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
      co_return co_await client.connect({});
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    const std::string msg = "hello";
    auto send_future = asio::co_spawn(
      _io_ctx, [&client_socket, msg]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncWriteAll(to_bytes(msg)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();

    EXPECT_TRUE(send_result);

    std::array<std::byte, 1024> buffer{};
    auto recv_future = asio::co_spawn(
      _io_ctx, [&client_socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncReadSome(std::span(buffer)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, EchoServerConcurrentClients)
{
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();

      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::unique_ptr<AsyncSocket>> sockets;
  for (int i = 0; i < 3; i++)
  {
    auto connect_future = asio::co_spawn(
      _io_ctx,
      [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        co_return co_await client.connect({});
      },
      asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto connect_result = connect_future.get();
    if (connect_result)
    {
      sockets.emplace_back(std::move(*connect_result));
    }
  }

  auto send_recv = [&](AsyncSocket& socket, const std::string& msg)
  {
    auto send_future = asio::co_spawn(
      _io_ctx, [&socket, msg]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return socket.asyncWriteAll(to_bytes(msg)); }, asio::use_future);

    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);

    std::array<std::byte, 1024> buffer{};
    auto recv_future = asio::co_spawn(
      _io_ctx, [&socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return socket.asyncReadSome(std::span(buffer)); }, asio::use_future);

    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  };

  send_recv(*sockets[0], "client1");
  send_recv(*sockets[1], "client2");

  sockets.clear();
  server.stop();
}

TEST_F(AsyncClientServerFixture, ServerRestart)
{
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();
      (void)listen_result;
    },
    asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    _io_ctx,
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
      co_return co_await client.connect({});
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();

  std::unique_ptr<AsyncSocket> client_socket;
  if (connect_result)
  {
    client_socket = std::move(*connect_result);
  }

  server.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  _io_ctx.stop();
  _io_ctx.start();

  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();
      (void)listen_result;
    },
    asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientAsync client2("127.0.0.1", TEST_PORT, _io_ctx);
  auto connect_future2 = asio::co_spawn(
    _io_ctx, [&client2]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    { co_return co_await client2.connect({}); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result2 = connect_future2.get();
  EXPECT_TRUE(connect_result2.has_value());

  server.stop();
}

TEST_F(IoContextFixture, ConnectionRefused)
{
  ClientAsync client("127.0.0.1", 59999, getIoContext());
  auto connect_future = asio::co_spawn(
    getIoContext(), [&client]() mutable -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    { co_return co_await client.connect({}); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();
  EXPECT_TRUE(!connect_result.has_value());
}

TEST_F(IoContextFixture, InvalidHost)
{
  ClientAsync client("invalid.host.invalid", 12345, getIoContext());
  auto connect_future = asio::co_spawn(
    getIoContext(), [&client]() mutable -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    { co_return co_await client.connect({}); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();
  EXPECT_TRUE(!connect_result.has_value());
}

TEST_F(AsyncClientServerFixture, SpecialCharacters)
{
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    _io_ctx,
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
      co_return co_await client.connect({});
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    std::string special = "Hello, World! @#$%^&*()_+-=\n\t";
    auto send_future = asio::co_spawn(
      _io_ctx, [&client_socket, &special]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncWriteAll(to_bytes(special)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);

    std::array<std::byte, 1024> buffer{};
    auto recv_future = asio::co_spawn(
      _io_ctx, [&client_socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncReadSome(std::span(buffer)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(special, response);
    }
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, BinaryData)
{
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    _io_ctx,
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
      co_return co_await client.connect({});
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    std::vector<std::byte> binary_data(256);
    for (size_t i = 0; i < 256; i++)
    {
      binary_data[i] = static_cast<std::byte>(i);
    }
    auto send_future = asio::co_spawn(
      _io_ctx, [&client_socket, &binary_data]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncWriteAll(std::span(binary_data)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);

    std::array<std::byte, 512> buffer{};
    auto recv_future = asio::co_spawn(
      _io_ctx, [&client_socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncReadSome(std::span(buffer)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result && size_t(*recv_result) == 256)
    {
      for (size_t i = 0; i < 256; i++)
      {
        EXPECT_EQ(binary_data[i], buffer[i]);
      }
    }
  }

  server.stop();
}

}  // namespace Network::Test

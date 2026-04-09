#include <array>
#include <asio.hpp>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <thread>

#include "client/ClientAsync.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_io_context.h"
#include "server/ServerAsync.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

constexpr uint16_t TEST_PORT = 12351;

TEST_F(AsyncClientServerFixture, AsyncWriteTimeout)
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

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < data.size(); i++)
  {
    data[i] = static_cast<std::byte>(i % 256);
  }

  auto send_future = asio::co_spawn(
    _io_ctx, [&client_socket, data]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return client_socket->asyncWriteAll(std::span(data), std::chrono::milliseconds(100)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto send_result = send_future.get();

  EXPECT_TRUE(send_result.has_value());
  if (send_result)
  {
    EXPECT_GT(*send_result, 0);
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncReadTimeout)
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

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::array<std::byte, 1024> buffer{};
  auto recv_future = asio::co_spawn(
    _io_ctx, [&client_socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return client_socket->asyncReadSome(std::span(buffer), std::chrono::milliseconds(100)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto recv_result = recv_future.get();

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(), make_error_code(Network::Error::CONNECTION_TIMEOUT));

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncReadExactTimeout)
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

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> buffer(1024);
  auto recv_future = asio::co_spawn(
    _io_ctx, [&client_socket, buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return client_socket->asyncReadExact(std::span(buffer), std::chrono::milliseconds(100)); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto recv_result = recv_future.get();

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(), make_error_code(Network::Error::CONNECTION_TIMEOUT));

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncReadUntilTimeout)
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

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> buffer(1024);
  auto recv_future = asio::co_spawn(
    _io_ctx, [&client_socket, buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return client_socket->asyncReadUntil(std::span(buffer), "\n", std::chrono::milliseconds(100)); },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto recv_result = recv_future.get();

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(), make_error_code(Network::Error::CONNECTION_TIMEOUT));

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncNoTimeout)
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

  ASSERT_TRUE(connect_result.has_value());
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

  server.stop();
}

}  // namespace Network::Test

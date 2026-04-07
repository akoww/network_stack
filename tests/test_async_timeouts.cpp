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

namespace Network::Test {

constexpr uint16_t TEST_PORT = 12351;

TEST_F(AsyncClientServerFixture, AsyncWriteTimeout) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < data.size(); i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, data,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(
            std::span(data), std::chrono::milliseconds(100));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto send_result = send_future.get();

  EXPECT_TRUE(send_result.has_value());
  if (send_result) {
    EXPECT_GT(*send_result, 0);
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncReadTimeout) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::array<std::byte, 1024> buffer{};
  std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
  auto recv_future = recv_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, &buffer,
       promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncReadSome(
            std::span(buffer), std::chrono::milliseconds(100));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto recv_result = recv_future.get();

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(),
            make_error_code(Network::Error::ConnectionTimeout));

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncReadExactTimeout) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> buffer(1024);
  std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
  auto recv_future = recv_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, buffer,
       promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncReadExact(
            std::span(buffer), std::chrono::milliseconds(100));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto recv_result = recv_future.get();

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(),
            make_error_code(Network::Error::ConnectionTimeout));

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncReadUntilTimeout) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> buffer(1024);
  std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
  auto recv_future = recv_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, buffer,
       promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncReadUntil(
            std::span(buffer), "\n", std::chrono::milliseconds(100));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto recv_result = recv_future.get();

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(),
            make_error_code(Network::Error::ConnectionTimeout));

  server.stop();
}

TEST_F(AsyncClientServerFixture, AsyncNoTimeout) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  ASSERT_TRUE(connect_result.has_value());
  auto client_socket = std::move(*connect_result);

  const std::string msg = "hello";

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, msg,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(to_bytes(msg));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto send_result = send_future.get();

  EXPECT_TRUE(send_result);

  std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
  auto recv_future = recv_promise.get_future();

  std::array<std::byte, 1024> buffer{};
  asio::co_spawn(
      _io_ctx,
      [&client_socket, &buffer,
       promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncReadSome(std::span(buffer));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto recv_result = recv_future.get();

  EXPECT_TRUE(recv_result);
  if (recv_result) {
    auto response = to_string_view(buffer, *recv_result);
    EXPECT_EQ(msg, response);
  }

  server.stop();
}

} // namespace Network::Test

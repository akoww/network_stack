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

constexpr uint16_t TEST_TLS_PORT = 12350;

TEST_F(AsyncClientServerFixture, TlsEchoServerSingleMessage)
{
  EchoServer server(TEST_TLS_PORT, _io_ctx);
  server.get_ssl_context()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
  server.get_ssl_context()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                 asio::ssl::context::pem);

  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen_tls();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>> promise;
  auto future = promise.get_future();

  asio::co_spawn(
    _io_ctx,
    [this, &promise]() -> asio::awaitable<void>
    {
      ClientAsync client("127.0.0.1", TEST_TLS_PORT, _io_ctx);
      client.get_ssl_context()->set_verify_mode(asio::ssl::verify_none);
      auto result = co_await client.connect_tls({});
      promise.set_value(std::move(result));
    },
    asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    const std::string msg = "hello tls";

    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
      _io_ctx,
      [&client_socket, msg, promise = std::move(send_promise)]() mutable -> asio::awaitable<void>
      {
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
      [&client_socket, &buffer, promise = std::move(recv_promise)]() mutable -> asio::awaitable<void>
      {
        auto result = co_await client_socket->asyncReadSome(std::span(buffer));
        promise.set_value(std::move(result));
      },
      asio::detached);

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

TEST_F(AsyncClientServerFixture, TlsEchoServerMultipleMessages)
{
  EchoServer server(TEST_TLS_PORT, _io_ctx);
  server.get_ssl_context()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
  server.get_ssl_context()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                 asio::ssl::context::pem);

  asio::co_spawn(
    _io_ctx,
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.listen_tls();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>> promise;
  auto future = promise.get_future();

  asio::co_spawn(
    _io_ctx,
    [this, &promise]() -> asio::awaitable<void>
    {
      ClientAsync client("127.0.0.1", TEST_TLS_PORT, _io_ctx);
      client.get_ssl_context()->set_verify_mode(asio::ssl::verify_none);
      auto result = co_await client.connect_tls({});
      promise.set_value(std::move(result));
    },
    asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    const std::vector<std::string> messages = {"hello", "world", "tls test"};

    for (const auto& msg : messages)
    {
      std::promise<std::expected<std::size_t, std::error_code>> send_promise;
      auto send_future = send_promise.get_future();

      asio::co_spawn(
        _io_ctx,
        [&client_socket, msg, promise = std::move(send_promise)]() mutable -> asio::awaitable<void>
        {
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
        [&client_socket, &buffer, promise = std::move(recv_promise)]() mutable -> asio::awaitable<void>
        {
          auto result = co_await client_socket->asyncReadSome(std::span(buffer));
          promise.set_value(std::move(result));
        },
        asio::detached);

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      auto recv_result = recv_future.get();
      EXPECT_TRUE(recv_result);
      if (recv_result)
      {
        auto response = to_string_view(buffer, *recv_result);
        EXPECT_EQ(msg, response);
      }
    }
  }

  server.stop();
}

TEST_F(IoContextFixture, TlsConnectionRefused)
{
  ClientAsync client("127.0.0.1", 59997, get_io_context());
  client.get_ssl_context()->set_verify_mode(asio::ssl::verify_none);

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>> promise;
  auto future = promise.get_future();

  asio::co_spawn(
    get_io_context(),
    [&client, promise = std::move(promise)]() mutable -> asio::awaitable<void>
    {
      auto result = co_await client.connect_tls({});
      promise.set_value(std::move(result));
    },
    asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();
  EXPECT_TRUE(!connect_result.has_value());
}

}  // namespace Network::Test

#include <array>
#include <asio.hpp>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <thread>

#include "client/Client.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_certificate_paths.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_io_context.h"
#include "server/Server.h"
#include "socket/TcpSocket.h"
#include "socket/TlsOptions.h"

namespace Network::Test
{

constexpr uint16_t TEST_TLS_PORT = 12346;

TEST_F(AsyncClientServerFixture, TlsEchoServerSingleMessage)
{
  EchoServer server(TEST_TLS_PORT, getIoContext().get_executor());

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};
  asio::co_spawn(
    getIoContext().get_executor(),
    [&server, tls_opts = std::move(tls_opts)]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.asyncListenTls(tls_opts);
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    getIoContext().get_executor(),
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      Client client("127.0.0.1", TEST_TLS_PORT, getIoContext().get_executor());
      co_return co_await client.asyncConnectTls({}, {}, TlsOptions{.verify_peer = false});
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    const std::string msg = "hello tls";

    auto send_future = asio::co_spawn(
      getIoContext().get_executor(),
      [&client_socket, msg]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return client_socket->asyncWriteAll(to_bytes(msg)); }, asio::use_future);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();

    EXPECT_TRUE(send_result);

    std::array<std::byte, 1024> buffer{};
    auto recv_future = asio::co_spawn(
      getIoContext().get_executor(),
      [&client_socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
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

TEST_F(AsyncClientServerFixture, TlsEchoServerMultipleMessages)
{
  EchoServer server(TEST_TLS_PORT, getIoContext().get_executor());

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};
  asio::co_spawn(
    getIoContext().get_executor(),
    [&server, tls_opts = std::move(tls_opts)]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.asyncListenTls(tls_opts);
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    getIoContext().get_executor(),
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      Client client("127.0.0.1", TEST_TLS_PORT, getIoContext().get_executor());
      co_return co_await client.asyncConnectTls({}, {}, TlsOptions{.verify_peer = false});
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    const std::vector<std::string> messages = {"hello", "world", "tls test"};

    for (const auto& msg : messages)
    {
      auto send_future = asio::co_spawn(
        getIoContext().get_executor(),
        [&client_socket, msg]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
        { return client_socket->asyncWriteAll(to_bytes(msg)); }, asio::use_future);

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      auto send_result = send_future.get();
      EXPECT_TRUE(send_result);

      std::array<std::byte, 1024> buffer{};
      auto recv_future = asio::co_spawn(
        getIoContext().get_executor(),
        [&client_socket, &buffer]() mutable -> asio::awaitable<std::expected<std::size_t, std::error_code>>
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
  }

  server.stop();
}

TEST_F(IoContextFixture, TlsConnectionRefused)
{
  Client client("127.0.0.1", 59997, getIoContext().get_executor());

  TlsOptions tls_opts{.verify_peer = false};
  auto connect_future = asio::co_spawn(
    getIoContext().get_executor(),
    [&client]() mutable -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    { co_return co_await client.asyncConnectTls({}, {}, TlsOptions{.verify_peer = false}); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = connect_future.get();
  EXPECT_TRUE(!connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
    EXPECT_STREQ(ec.category().name(), "network");
    EXPECT_EQ(ec.message(), "Connection was refused by the remote host");
  }
}

}  // namespace Network::Test

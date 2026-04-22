#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <thread>

#include "client/ClientSync.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/ServerSync.h"
#include "socket/SslSocket.h"

namespace Network::Test
{

constexpr uint16_t TEST_TLS_PORT = 12347;

TEST_F(SyncClientServerFixture, TlsEchoServerSingleMessage)
{
  EchoServer server(TEST_TLS_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      auto listen_result = server.listen_tls();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen_tls failed";
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", TEST_TLS_PORT, _io_ctx);
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connect_tls({});
  ASSERT_TRUE(connect_result.has_value()) << "Cant connect client with TLS";

  auto client_socket = std::move(*connect_result);
  const std::string msg = "hello tls";
  auto send_result = client_socket->writeAll(to_bytes(msg));
  EXPECT_TRUE(send_result);

  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer));
  EXPECT_TRUE(recv_result);
  if (recv_result)
  {
    auto response = to_string_view(buffer, *recv_result);
    EXPECT_EQ(msg, response);
  }

  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, TlsEchoServerMultipleMessages)
{
  EchoServer server(TEST_TLS_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      auto listen_result = server.listen_tls();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen_tls failed";
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", TEST_TLS_PORT, _io_ctx);
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connect_tls({});
  ASSERT_TRUE(connect_result.has_value()) << "Cant connect client with TLS";

  auto client_socket = std::move(*connect_result);
  const std::vector<std::string> messages = {"hello", "world", "tls test"};
  for (const auto& msg : messages)
  {
    auto send_result = client_socket->writeAll(to_bytes(msg));
    EXPECT_TRUE(send_result);

    std::array<std::byte, 1024> buffer{};
    auto recv_result = client_socket->readSome(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, TlsConnectionRefused)
{
  ClientSync client("127.0.0.1", 59998, _io_ctx);
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connect_tls({});
  EXPECT_FALSE(connect_result.has_value());
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
    EXPECT_STREQ(ec.category().name(), "network");
    EXPECT_EQ(ec.message(), "Connection was refused by the remote host");
  }
}

}  // namespace Network::Test

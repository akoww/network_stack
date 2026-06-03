#include <array>
#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "client/Client.h"
#include "client/Client.h"
#include "core/Context.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/Server.h"
#include "server/Server.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/ssl.hpp>
#include <asio/ssl/context.hpp>
#include <asio/use_future.hpp>

namespace Network::Test
{

namespace
{

constexpr std::string_view SELF_SIGNED_CERT_PATH = "/home/akoww/source/network_stack/tests/certs/self_signed.crt";
constexpr std::string_view SELF_SIGNED_KEY_PATH = "/home/akoww/source/network_stack/tests/certs/self_signed.key";

auto createSslContextWithCert()
{
  auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  ctx->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
  ctx->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key", asio::ssl::context::pem);
  return ctx;
}

}  // namespace

// ============================================================================
// Group: Protocol Mismatch
// ============================================================================

TEST_F(IoContextFixture, TlsClientToPlainServer)
{
  constexpr uint16_t port = 50000;

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server]() { server.listen(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});

  EXPECT_FALSE(connect_result.has_value()) << "TLS client connecting to plain server should fail";
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_STREQ(ec.category().name(), "network");
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, PlainClientToTlsServer)
{
  constexpr uint16_t port = 50001;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Plain client should connect at TCP level";

  auto client_socket = std::move(*connect_result);
  const std::string msg = "hello";
  auto send_result = client_socket->writeAll(to_bytes(msg));
  EXPECT_TRUE(send_result);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer));
  if (recv_result && *recv_result > 0)
  {
    auto response = to_string_view(buffer, *recv_result);
    EXPECT_NE(msg, response) << "Should not get clean echo from TLS server via plain socket";
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, PlainWriteToTlsServerGarbledRead)
{
  constexpr uint16_t port = 50002;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  const std::string ping = "PING";
  auto send_result = client_socket->writeAll(to_bytes(ping));
  EXPECT_TRUE(send_result);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer));
  if (recv_result && *recv_result > 0)
  {
    auto resp = to_string_view(buffer, *recv_result);
    EXPECT_NE(ping, resp) << "Should not get clean PING from TLS server via plain socket";
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, TlsWriteToPlainServer)
{
  constexpr uint16_t port = 50003;

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});

  EXPECT_FALSE(connect_result.has_value()) << "TLS client should not connect to plain server";
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_STREQ(ec.category().name(), "network");
  }

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: Certificate Misuse
// ============================================================================

TEST_F(IoContextFixture, MissingCertificate)
{
  constexpr uint16_t port = 50004;

  // Give server an empty SSL context (no cert or key loaded)
  EchoServer server(port, getIoContext());
  auto empty_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  server.getSslContext() = empty_ctx;  // Replace default with empty

  std::thread server_thread([&server]() { server.listenTls(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Client tries to connect -- handshake should fail without cert
  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});

  EXPECT_FALSE(connect_result.has_value()) << "TLS handshake should fail without cert/key";
  if (connect_result)
  {
    server.stop();
  }

  server_thread.join();
  if (!connect_result)
  {
    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

TEST_F(IoContextFixture, MissingPrivateKey)
{
  constexpr uint16_t port = 50005;

  EchoServer server(port, getIoContext());
  // Load cert but with wrong key (CA cert, not server key)
  server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
  server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/ca.crt",
                                               asio::ssl::context::pem);

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      EXPECT_FALSE(listen_result.has_value());
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SelfSignedVerifyPeer)
{
  constexpr uint16_t port = 50006;

  EchoServer server(port, getIoContext());
  auto sctx = server.getSslContext();
  sctx->use_certificate_chain_file(std::string(SELF_SIGNED_CERT_PATH));
  sctx->use_private_key_file(std::string(SELF_SIGNED_KEY_PATH), asio::ssl::context::pem);

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      EXPECT_TRUE(listen_result.has_value());
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_peer);
  auto connect_result = client.connectTls({});

  EXPECT_FALSE(connect_result.has_value()) << "verify_peer should reject self-signed server cert";
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_STREQ(ec.category().name(), "network");
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SelfSignedAcceptAny)
{
  constexpr uint16_t port = 50007;

  EchoServer server(port, getIoContext());
  auto sctx = server.getSslContext();
  sctx->use_certificate_chain_file(std::string(SELF_SIGNED_CERT_PATH));
  sctx->use_private_key_file(std::string(SELF_SIGNED_KEY_PATH), asio::ssl::context::pem);

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      EXPECT_TRUE(listen_result.has_value());
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});

  EXPECT_TRUE(connect_result.has_value());
  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    const std::string msg = "self signed OK";
    auto send_result = client_socket->writeAll(to_bytes(msg));
    EXPECT_TRUE(send_result);

    std::array<std::byte, 1024> recv_buffer{};
    auto recv_result = client_socket->readSome(std::span(recv_buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(recv_buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  }

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: Key Pair Misuse
// ============================================================================

TEST_F(IoContextFixture, WrongKeyPair)
{
  constexpr uint16_t port = 50008;

  // Use server cert with client key -- mismatched pair
  auto mismatch_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  mismatch_ctx->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
  mismatch_ctx->use_private_key_file("/home/akoww/source/network_stack/tests/certs/client.key",
                                     asio::ssl::context::pem);

  EchoServer server(port, getIoContext());
  auto sctx = server.getSslContext();
  sctx->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
  sctx->use_private_key_file("/home/akoww/source/network_stack/tests/certs/client.key", asio::ssl::context::pem);

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      (void)listen_result;  // May or may not fail depending on TLS implementation
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: Handshake Aborts
// ============================================================================

TEST_F(IoContextFixture, TlsConnectThenServerStop)
{
  constexpr uint16_t port = 50009;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server.stop();
  server_thread.join();

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  EXPECT_FALSE(connect_result.has_value()) << "Client should not connect to stopped server";
}

TEST_F(IoContextFixture, SslContextDestroyBeforeConnect)
{
  constexpr uint16_t port = 50010;

  EchoServer server(port, getIoContext());
  // Replace default context with a fresh empty one, then destroy it
  auto empty_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  server.getSslContext() = empty_ctx;
  empty_ctx.reset();  // Destroy context before anyone uses it

  // Server's internal _ssl_context was already set, so this still works
  // but the original shared data is gone
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      (void)listen_result;  // May succeed with tlsv12_server default
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Client tries to connect -- result is unpredictable but must not crash
  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  (void)connect_result;

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SslContextDestroyAfterConnect)
{
  constexpr uint16_t port = 50011;

  std::shared_ptr<asio::ssl::context> server_ctx_ptr;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&]()
    {
      server_ctx_ptr = createSslContextWithCert();
      server.getSslContext() = server_ctx_ptr;
      server.listenTls();
      // After listen returns, destroy context
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      server_ctx_ptr.reset();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Connect immediately -- context is still valid
  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});

  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    // Server context has been destroyed, write should fail
    auto write_result = client_socket->writeAll(to_bytes("after destroy"));
    EXPECT_FALSE(write_result) << "Write after server context destroy should fail";
  }

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: Repeated Operations
// ============================================================================

TEST_F(IoContextFixture, TlsHandshakeRepeated)
{
  constexpr uint16_t port = 50012;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  for (int i = 0; i < 10; ++i)
  {
    Client client("127.0.0.1", port, getIoContext());
    client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
    auto connect_result = client.connectTls({});

    EXPECT_TRUE(connect_result.has_value()) << "Iter " << i;
    if (!connect_result)
    {
      continue;
    }

    auto client_socket = std::move(*connect_result);
    std::string msg = "msg-" + std::to_string(i);
    auto send_result = client_socket->writeAll(to_bytes(msg));
    EXPECT_TRUE(send_result) << "Iter " << i;

    std::array<std::byte, 1024> buffer{};
    auto recv_result = client_socket->readSome(std::span(buffer));
    EXPECT_TRUE(recv_result) << "Iter " << i;
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response) << "Iter " << i;
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, TlsConnectDisconnectRepeated)
{
  constexpr uint16_t port = 50013;
  constexpr int iterations = 50;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  int success_count = 0;
  for (int i = 0; i < iterations; ++i)
  {
    Client client("127.0.0.1", port, getIoContext());
    client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
    auto connect_result = client.connectTls({});
    if (!connect_result)
    {
      continue;
    }

    auto client_socket = std::move(*connect_result);
    std::string msg = "x" + std::to_string(i);
    auto send_result = client_socket->writeAll(to_bytes(msg));
    if (!send_result)
    {
      continue;
    }

    std::array<std::byte, 1024> buffer{};
    auto recv_result = client_socket->readSome(std::span(buffer));
    if (recv_result)
    {
      ++success_count;
    }
  }

  EXPECT_GT(success_count, iterations / 2) << "Most cycles should succeed";

  server.stop();
  server_thread.join();
}

// ============================================================================
// Group: SslSocket Specific
// ============================================================================

TEST_F(IoContextFixture, SslSocketGetSocket)
{
  constexpr uint16_t port = 50014;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  auto* ssl_sock = dynamic_cast<SslSocket*>(client_socket.get());
  ASSERT_NE(ssl_sock, nullptr) << "Socket should be SslSocket";

  auto& ssl_stream = ssl_sock->getSocket();
  EXPECT_TRUE(ssl_stream.next_layer().is_open());

  const std::string msg = "ssl socket check";
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

TEST_F(IoContextFixture, TlsReadOnUnconnected)
{
  asio::io_context io_ctx;

  auto ssl_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  SslSocket socket(io_ctx, *ssl_ctx);

  std::array<std::byte, 1024> buffer{};
  auto result = socket.readSome(std::span(buffer));
  EXPECT_FALSE(result.has_value()) << "readSome on unconnected socket should fail";

  io_ctx.stop();
}

TEST_F(IoContextFixture, TlsWriteOnUnconnected)
{
  asio::io_context io_ctx;

  auto ssl_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  SslSocket socket(io_ctx, *ssl_ctx);

  std::array<std::byte, 1024> buffer{};
  for (std::size_t i = 0; i < buffer.size(); ++i)
  {
    buffer[i] = static_cast<std::byte>(i);
  }

  auto result = socket.writeAll(std::span(buffer));
  EXPECT_FALSE(result.has_value()) << "writeAll on unconnected socket should fail";

  io_ctx.stop();
}

// ============================================================================
// Group: Edge Cases
// ============================================================================

TEST_F(IoContextFixture, TlsZeroLengthWrite)
{
  constexpr uint16_t port = 50015;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  auto send_result = client_socket->writeAll(std::span<const std::byte>{});
  EXPECT_TRUE(send_result) << "Zero-length write should succeed";

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, TlsLargePayload)
{
  constexpr uint16_t port = 50016;
  constexpr std::size_t payload_size = 100 * 1024;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::byte> payload(payload_size);
  for (std::size_t i = 0; i < payload_size; ++i)
  {
    payload[i] = static_cast<std::byte>(static_cast<unsigned char>(i % 256));
  }

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  auto send_result = client_socket->writeAll(std::span(payload));
  EXPECT_TRUE(send_result);

  std::vector<std::byte> recv_buffer(payload_size * 2);
  std::size_t total_read = 0;
  while (total_read < payload_size)
  {
    auto chunk_result = client_socket->readSome(
      std::span(recv_buffer.data() + total_read, static_cast<std::size_t>(recv_buffer.size() - total_read)));
    if (!chunk_result)
    {
      break;
    }
    total_read += *chunk_result;
  }
  EXPECT_EQ(total_read, payload_size);

  for (std::size_t i = 0; i < payload_size; ++i)
  {
    EXPECT_EQ(recv_buffer[i], payload[i]) << "Mismatch at byte " << i;
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, TlsBinaryData)
{
  constexpr uint16_t port = 50017;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::array<std::byte, 256> binary_data{};
  for (std::size_t i = 0; i < binary_data.size(); ++i)
  {
    binary_data[i] = static_cast<std::byte>(static_cast<unsigned char>(i));
  }

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  auto send_result = client_socket->writeAll(std::span(binary_data));
  EXPECT_TRUE(send_result);

  std::vector<std::byte> recv_buffer(binary_data.size() * 2);
  std::size_t total_read = 0;
  while (total_read < 256)
  {
    auto chunk_result = client_socket->readSome(
      std::span(recv_buffer.data() + total_read, static_cast<std::size_t>(recv_buffer.size() - total_read)));
    if (!chunk_result)
    {
      break;
    }
    total_read += *chunk_result;
  }

  EXPECT_EQ(total_read, binary_data.size());
  for (std::size_t i = 0; i < binary_data.size(); ++i)
  {
    EXPECT_EQ(recv_buffer[i], binary_data[i]) << "Mismatch at byte " << i;
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, TlsConcurrentReadWrite)
{
  constexpr uint16_t port = 50018;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      server.getSslContext()->use_certificate_chain_file("/home/akoww/source/network_stack/tests/certs/server.crt");
      server.getSslContext()->use_private_key_file("/home/akoww/source/network_stack/tests/certs/server.key",
                                                   asio::ssl::context::pem);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::atomic<std::size_t> total_read{0};

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);

  // Send multiple messages to keep the connection busy
  std::thread writer_thread(
    [&, client_socket = std::move(client_socket)]() mutable
    {
      for (int i = 0; i < 10; ++i)
      {
        std::string msg = "A" + std::to_string(i);
        auto send_result = client_socket->writeAll(to_bytes(msg));
        EXPECT_TRUE(send_result);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });

  // Read in parallel
  std::array<std::byte, 1024> buffer{};
  while (true)
  {
    auto chunk_result = client_socket->readSome(std::span(buffer));
    if (chunk_result && *chunk_result > 0)
    {
      total_read += *chunk_result;
    }
    else
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  writer_thread.join();
  server.stop();
  server_thread.join();

  EXPECT_GT(total_read, 0) << "Some data should be read from concurrent TLS connection";
}

// ============================================================================
// Group: Async TLS
// ============================================================================

TEST_F(IoContextFixture, AsyncTlsConnectToPlain)
{
  constexpr uint16_t port = 50019;

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);

  auto connect_future = asio::co_spawn(
    getIoContext(), [&client]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    { co_return co_await client.asyncConnectTls({}); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto connect_result = connect_future.get();

  EXPECT_FALSE(connect_result.has_value()) << "Async TLS client should fail connecting to plain server";
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    EXPECT_STREQ(ec.category().name(), "network");
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, AsyncTlsServerListenFail)
{
  constexpr uint16_t port = 50020;

  Server server(port, getIoContext(), [](std::unique_ptr<DualSocket>) {});
  server.getSslContext()->use_certificate_chain_file("/nonexistent/path/server.crt");

  auto listen_future = asio::co_spawn(
    getIoContext(), [&server]() -> asio::awaitable<std::expected<void, std::error_code>>
    { return server.asyncListenTls(); }, asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto listen_result = listen_future.get();

  EXPECT_FALSE(listen_result.has_value()) << "listen_tls with nonexistent cert should fail";
  if (!listen_result.has_value())
  {
    auto ec = listen_result.error();
    EXPECT_STREQ(ec.category().name(), "network");
  }
}

}  // namespace Network::Test

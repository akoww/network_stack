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

#include "client/Client.h"
#include "core/Context.h"
#include "fixtures/test_certificate_paths.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/Server.h"
#include "socket/SocketBase.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/use_future.hpp>

namespace Network::Test
{

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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value()) << "Plain client should connect at TCP level";

  auto client_socket = std::move(*connect_result);
  const std::string msg = "hello";  // sending 5 bytes is all the server needs for the handshake
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result = client.connect();
  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  const std::string ping = "PING";  // sending 4 bytes will block the server handshake until more is coming
  auto send_result = client_socket->writeAll(to_bytes(ping));
  EXPECT_TRUE(send_result);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::array<std::byte, 1024> buffer{};
  auto recv_result =
    client_socket->readSome(std::span(buffer), std::chrono::milliseconds(50));  // need to wait at max x ms
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

  EchoServer server(port, getIoContext());
  auto empty_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  server.setSslContext(empty_ctx);

  std::thread server_thread([&server]() { server.listenTls(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls(std::chrono::milliseconds(50));

  EXPECT_FALSE(connect_result.has_value()) << "TLS handshake should fail without cert/key";

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, MissingPrivateKey)
{
  constexpr uint16_t port = 50005;

  EchoServer server(port, getIoContext());

  auto ec1 = server.setCertificateChain(Network::Test::ServerCertPath());
  EXPECT_FALSE(ec1);
  auto ec2 = server.setPrivateKey(Network::Test::CaCertPath());  // wrong key used
  EXPECT_TRUE(ec2);

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      EXPECT_TRUE(listen_result.has_value());
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SelfSignedVerifyPeer)
{
  GTEST_SKIP() << "not implemented yet";
  constexpr uint16_t port = 50006;

  EchoServer server(port, getIoContext());
  auto ec1 = server.setCertificateChain(Network::Test::SelfSignedCertPath());
  EXPECT_FALSE(ec1);

  auto ec2 = server.setPrivateKey(Network::Test::SelfSignedKeyPath());
  EXPECT_FALSE(ec2);

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      EXPECT_TRUE(listen_result.has_value());
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_peer);
  auto connect_result = client.connectTls(std::chrono::milliseconds(200));

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
  server.setCertificateChain(std::string(Network::Test::SelfSignedCertPath()));
  server.setPrivateKey(std::string(Network::Test::SelfSignedKeyPath()));

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

  EchoServer server(port, getIoContext());
  server.setCertificateChain(Network::Test::ServerCertPath());
  server.setPrivateKey(std::string(Network::Test::ClientKeyPath()));

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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
  auto empty_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  server.setSslContext(empty_ctx);
  empty_ctx.reset();

  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listenTls();
      (void)listen_result;
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  (void)connect_result;

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SslContextDestroyAfterConnect)
{
  GTEST_SKIP() << "this is UB - might be a bad test!";
  constexpr uint16_t port = 50011;

  std::shared_ptr<asio::ssl::context> server_ctx_ptr;

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&]()
    {
      server_ctx_ptr = createSslContextWithCert();
      server.setSslContext(server_ctx_ptr);
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});

  server_ctx_ptr.reset();
  if (connect_result)
  {
    auto client_socket = std::move(*connect_result);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
  auto runner = std::thread([&]() { io_ctx.run(); });

  auto ssl_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  asio::ip::tcp::socket sock(getIoContext());
  asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(sock), *ssl_ctx);
  SslSocket socket(std::move(stream));

  std::array<std::byte, 1024> buffer{};
  auto result = socket.readSome(std::span(buffer));
  EXPECT_FALSE(result.has_value()) << "readSome on unconnected socket should fail";

  io_ctx.stop();
  runner.join();
}

TEST_F(IoContextFixture, TlsWriteOnUnconnected)
{
  asio::io_context io_ctx;

  auto ssl_ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
  asio::ip::tcp::socket sock(getIoContext());
  asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(sock), *ssl_ctx);
  SslSocket socket(std::move(stream));

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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
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
      server.setCertificateChain(Network::Test::ServerCertPath());
      server.setPrivateKey(Network::Test::ServerKeyPath());
      server.listenTls();
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::atomic<std::size_t> total_read{0};

  Client client("127.0.0.1", port, getIoContext());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls({});
  ASSERT_TRUE(connect_result.has_value());

  std::shared_ptr<DualSocket> client_socket = std::move(*connect_result);

  std::thread writer_thread(
    [&, client_socket]() mutable
    {
      for (int i = 0; i < 10; ++i)
      {
        std::string msg = "A" + std::to_string(i);
        auto send_result = client_socket->writeAll(to_bytes(msg));
        EXPECT_TRUE(send_result);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });

  std::array<std::byte, 1024> buffer{};
  while (true)
  {
    auto chunk_result = client_socket->readSome(std::span(buffer), std::chrono::milliseconds(100));
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

  EchoServer server(port, getIoContext());

  auto listen_future = asio::co_spawn(
    getIoContext(),
    [&server]() -> asio::awaitable<std::expected<void, std::error_code>>
    {
      auto ec = server.setCertificateChain("/nonexistent/path/server.crt");
      if (ec)
      {
        co_return std::unexpected(ec);
      }
      co_return co_await server.asyncListenTls();
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto listen_result = listen_future.get();

  EXPECT_FALSE(listen_result.has_value()) << "listen_tls with nonexistent cert should fail";
  if (!listen_result.has_value())
  {
    auto ec = listen_result.error();
    EXPECT_STREQ(ec.category().name(), "asio.system");
  }
}

}  // namespace Network::Test

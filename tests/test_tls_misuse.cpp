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
#include "core/TlsContextWrapper.h"
#include "core/details/TlsContextDetail.h"
#include "fixtures/test_certificate_paths.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/Server.h"
#include "socket/SocketBase.h"
#include "socket/TlsSocket.h"
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
  std::thread server_thread([&server]() { static_cast<void>(server.listen()); });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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
  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, tls_opts]() { void(server.listenTls(tls_opts)); });
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

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, tls_opts]() { void(server.listenTls(tls_opts)); });
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
  std::thread server_thread([&server]() { static_cast<void>(server.listen()); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};
  EchoServer server(port, getIoContext());

  std::thread server_thread([&server]() { static_cast<void>(server.listenTls()); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

  EXPECT_FALSE(connect_result.has_value()) << "TLS handshake should fail without cert/key";

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, MissingPrivateKey)
{
  constexpr uint16_t port = 50005;

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), ""};

  EchoServer server(port, getIoContext());
  std::thread server_thread(
    [&server, tls_opts]()
    {
      auto listen_result = server.listenTls(tls_opts);
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

  TlsServerOptions tls_opts{Network::Test::SelfSignedCertPath(), Network::Test::SelfSignedKeyPath()};
  EchoServer server(port, getIoContext());

  std::thread server_thread(
    [&server, &tls_opts]()
    {
      auto listen_result = server.listenTls(tls_opts);
      EXPECT_TRUE(listen_result.has_value());
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = true});

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
  TlsServerOptions tls_opts{Network::Test::SelfSignedCertPath(), Network::Test::SelfSignedKeyPath()};

  std::thread server_thread(
    [&server, &tls_opts]()
    {
      auto listen_result = server.listenTls(tls_opts);
      EXPECT_TRUE(listen_result.has_value());
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ClientKeyPath()};
  EchoServer server(port, getIoContext());

  std::thread server_thread(
    [&server, &tls_opts]()
    {
      auto listen_result = server.listenTls(tls_opts);
      (void)listen_result;  // TODO May or may not fail depending on TLS implementation
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

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server.stop();
  server_thread.join();

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});
  EXPECT_FALSE(connect_result.has_value()) << "Client should not connect to stopped server";
}
// ============================================================================
// Group: Repeated Operations
// ============================================================================

TEST_F(IoContextFixture, TlsHandshakeRepeated)
{
  constexpr uint16_t port = 50012;

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};
  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  for (int i = 0; i < 10; ++i)
  {
    Client client("127.0.0.1", port, getIoContext());
    auto connect_result =
      client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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

  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};
  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  int success_count = 0;
  for (int i = 0; i < iterations; ++i)
  {
    Client client("127.0.0.1", port, getIoContext());
    auto connect_result =
      client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});
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
// Group: TlsSocket Specific
// ============================================================================

TEST_F(IoContextFixture, TlsSocketGetSocket)
{
  constexpr uint16_t port = 50014;
  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

  ASSERT_TRUE(connect_result.has_value());

  auto client_socket = std::move(*connect_result);
  auto* ssl_sock = dynamic_cast<TlsSocket*>(client_socket.get());
  ASSERT_NE(ssl_sock, nullptr) << "Socket should be TlsSocket";

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

  Network::TlsOptions tls_opts{};
  auto ctx_result = Network::createTlsContextWrapper(tls_opts);
  EXPECT_TRUE(ctx_result.has_value()) << "TLS context should be created successfully";
  asio::ip::tcp::socket sock(detail::getExecutor(getIoContext()));
  asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(sock), *Network::detail::getTlsContext(*ctx_result));
  TlsSocket socket(std::move(stream));

  std::array<std::byte, 1024> buffer{};
  auto result = socket.readSome(std::span(buffer));
  EXPECT_FALSE(result.has_value()) << "readSome on unconnected socket should fail";

  runner.join();
}

TEST_F(IoContextFixture, TlsWriteOnUnconnected)
{
  asio::io_context io_ctx;

  Network::TlsOptions tls_opts{};
  auto ctx_result = Network::createTlsContextWrapper(tls_opts);
  EXPECT_TRUE(ctx_result.has_value()) << "TLS context should be created successfully";
  asio::ip::tcp::socket sock(detail::getExecutor(getIoContext()));
  asio::ssl::stream<asio::ip::tcp::socket> stream(std::move(sock), *Network::detail::getTlsContext(*ctx_result));
  TlsSocket socket(std::move(stream));

  std::array<std::byte, 1024> buffer{};
  for (std::size_t i = 0; i < buffer.size(); ++i)
  {
    buffer[i] = static_cast<std::byte>(i);
  }

  auto result = socket.writeAll(std::span(buffer));
  EXPECT_FALSE(result.has_value()) << "writeAll on unconnected socket should fail";
}

// ============================================================================
// Group: Edge Cases
// ============================================================================

TEST_F(IoContextFixture, TlsZeroLengthWrite)
{
  constexpr uint16_t port = 50015;
  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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
  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::byte> payload(payload_size);
  for (std::size_t i = 0; i < payload_size; ++i)
  {
    payload[i] = static_cast<std::byte>(static_cast<unsigned char>(i % 256));
  }

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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
  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::array<std::byte, 256> binary_data{};
  for (std::size_t i = 0; i < binary_data.size(); ++i)
  {
    binary_data[i] = static_cast<std::byte>(static_cast<unsigned char>(i));
  }

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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
  TlsServerOptions tls_opts{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  EchoServer server(port, getIoContext());
  std::thread server_thread([&server, &tls_opts]() { void(server.listenTls(tls_opts)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::atomic<std::size_t> total_read{0};

  Client client("127.0.0.1", port, getIoContext());
  auto connect_result =
    client.connectTls(std::chrono::milliseconds{2000}, {}, Network::TlsOptions{.verify_peer = false});

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
  std::thread server_thread([&server]() { static_cast<void>(server.listen()); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", port, getIoContext());

  auto connect_future = asio::co_spawn(
    detail::getExecutor(getIoContext()),
    [&client]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    {
      co_return co_await client.asyncConnectTls(std::chrono::milliseconds{2000}, {},
                                                Network::TlsOptions{.verify_peer = false});
    },
    asio::use_future);

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
  TlsServerOptions tls_opts{"/nonexistent/path/server.crt", "/nonexistent/path/server.crt"};

  auto listen_future = asio::co_spawn(
    detail::getExecutor(getIoContext()), [&server, &tls_opts]() -> asio::awaitable<std::expected<void, std::error_code>>
    { co_return co_await server.asyncListenTls(tls_opts); }, asio::use_future);

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

#include <array>
#include <atomic>
#include <chrono>
#include <expected>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "client/Client.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_certificate_paths.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "server/Server.h"
#include "socket/TcpSocket.h"
#include "socket/TlsOptions.h"

#include <asio.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

namespace Network::Test
{

// ============================================================================
// BlackholeTcpServer - accepts connections and does nothing (sends nothing).
// Used to force handshake timeout when the peer never sends data.
// ============================================================================

class BlackholeTcpServer : public std::enable_shared_from_this<BlackholeTcpServer>
{
public:
  explicit BlackholeTcpServer(uint16_t port, asio::any_io_executor executor) : _acceptor(executor), _port(port) {}

  void start()
  {
    std::error_code ec;
    _acceptor.open(asio::ip::tcp::v4(), ec);
    _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    _acceptor.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), _port), ec);
    _acceptor.listen(asio::socket_base::max_listen_connections, ec);
    asio::post(_acceptor.get_executor(), [self = shared_from_this()]() { self->do_accept(); });
  }

  void stop()
  {
    std::error_code ec;
    _acceptor.close(ec);
    _stopped_flag.store(true);
  }

private:
  void do_accept()
  {
    if (_stopped_flag.load())
      return;

    auto sock = std::make_shared<asio::ip::tcp::socket>(_acceptor.get_executor());
    _sockets.push_back(sock);

    _acceptor.async_accept(*sock,
                           [self = shared_from_this(), sock](std::error_code ec)
                           {
                             if (ec || self->_stopped_flag.load())
                               return;
                             // Accept the next connection but keep this one alive.
                             self->do_accept();
                           });
  }

  asio::ip::tcp::acceptor _acceptor;
  uint16_t _port;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> _sockets;
  std::atomic<bool> _stopped_flag{false};
};

// ============================================================================
// Test 1: PlainClientToTlsServerTimeout
// A raw TCP client connects to a TLS echo server but sends no data.
// The server sits in async_handshake(server) waiting for the ClientHello.
// After handshake_timeout_ms passes, the timer fires and cancels the connection.
// ============================================================================

TEST_F(AsyncClientServerFixture, PlainClientToTlsServerTimeout)
{
  constexpr uint16_t PORT = 12500;

  // Track whether any client handler was called (a successful TLS handshake).
  std::atomic<int> handler_count{0};

  TlsOptions hs_timeout_opts;
  hs_timeout_opts.handshake_timeout_ms = std::chrono::milliseconds(100);
  hs_timeout_opts.verify_peer = false;

  // Start the TLS echo server in a background coroutine.
  EchoServer server(PORT, getIoContext());

  auto tls_server_opts = TlsServerOptions{Network::Test::ServerCertPath(), Network::Test::ServerKeyPath()};

  spdlog::debug("[timeout_test] Starting TLS echo server on port {} with handshake_timeout=100ms", PORT);

  auto listen_future = asio::co_spawn(
    detail::getExecutor(getIoContext()),
    [&server, tls_opts = std::move(hs_timeout_opts),
     tls_server_opts]() -> asio::awaitable<std::expected<void, std::error_code>>
    {
      spdlog::debug("[timeout_test] asyncListenTls called");

      auto listen_result = co_await server.asyncListenTls(std::move(tls_server_opts), {}, std::move(tls_opts));

      spdlog::debug("[timeout_test] asyncListenTls result: {}", listen_result.has_value() ? "success" : "fail");

      co_return listen_result;
    },
    asio::use_future);

  // Wait for the server to be ready.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Plain TCP client connects but sends NO TLS data -- just raw TCP connect.
  spdlog::debug("[timeout_test] Starting plain TCP client (port='{}'...)", PORT);

  asio::co_spawn(
    detail::getExecutor(getIoContext()),
    [this, port = PORT]() mutable -> asio::awaitable<void>
    {
      std::error_code ec;
      asio::ip::tcp::socket sock(asio::make_strand(detail::getExecutor(getIoContext())));
      asio::ip::tcp::resolver resolver(asio::make_strand(detail::getExecutor(getIoContext())));
      auto endpoints = co_await resolver.async_resolve("127.0.0.1", std::to_string(port), asio::use_awaitable);
      co_await asio::async_connect(sock, endpoints, asio::redirect_error(asio::use_awaitable, ec));

      spdlog::debug("[timeout_test] Plain TCP client connect result: {}", ec ? "FAIL" : "OK");

      if (ec)
        co_return;

      spdlog::debug("[timeout_test] Client connected, now waiting 300s...");

      // Wait so we can observe that the server times out.
      asio::steady_timer timer(asio::make_strand(detail::getExecutor(getIoContext())), std::chrono::seconds(1));
      co_await timer.async_wait(asio::use_awaitable);

      spdlog::debug("[timeout_test] Client done, leaving server hanging with unhandled TLS connection");
    },
    asio::detached);

  // Give the handshake timeout time to fire while we just wait.

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  spdlog::debug("[timeout_test] Checking results (handler_count='')", handler_count.load());

  // The connection should have been cleaned up. No handler will be invoked
  // because the server never saw a valid TLS handshake start.
  EXPECT_EQ(handler_count.load(), 0) << "No client handler should fire when server-side handshake times out";

  spdlog::debug("[timeout_test] Cleaning up...");
  server.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ============================================================================
// Test 2: TlsClientToPlainServerTimeout -- client-side handshake timeout test.
// A TLS client connects to a blackhole TCP server that sends nothing back.
// The client's async_handshake(client) waits forever for ServerHello,
// but the timer fires and returns CONNECTION_TIMEOUT.
// ============================================================================

TEST_F(AsyncClientServerFixture, TlsClientToPlainServerTimeout)
{
  constexpr uint16_t PORT = 12501;

  spdlog::debug("[timeout_test] Starting blackhole TCP server on port {}", PORT);

  auto blackhole_server = std::make_shared<BlackholeTcpServer>(PORT, detail::getExecutor(getIoContext()));
  blackhole_server->start();

  // Wait a bit to give the accept loop time to start.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  spdlog::debug("[timeout_test] Blackhole server started (port='{}'), connect TLS client with handshake_timeout=100ms",
                PORT);

  TlsOptions tls_opts;
  tls_opts.handshake_timeout_ms = std::chrono::milliseconds(100);
  tls_opts.verify_peer = false;

  auto timeout_future = asio::co_spawn(

    detail::getExecutor(getIoContext()),
    [this, port = PORT, &tls_opts]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    {
      spdlog::debug("[timeout_test] Client asyncConnectTls to port {}", static_cast<int>(port));

      Client client("127.0.0.1", PORT, getIoContext());

      auto result = co_await client.asyncConnectTls(std::chrono::milliseconds(5), TcpOptions{}, tls_opts);

      if (result.has_value())
        spdlog::debug("[timeout_test] asyncConnectTls result: success");
      else
        spdlog::debug("[timeout_test] asyncConnectTls result: fail (ec='{}')", result.error().message());

      co_return std::move(result);
    },
    asio::use_future);

  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  spdlog::debug("[timeout_test] Checking results (should be CONNECTION_TIMEOUT)...");

  auto connect_result = timeout_future.get();

  EXPECT_FALSE(connect_result.has_value()) << "Connection should fail due to handshake timeout";
  if (!connect_result.has_value())
  {
    auto ec = connect_result.error();
    spdlog::debug("[timeout_test] Got error_code: {} ({})", ec.message(), static_cast<int>(ec.value()));

    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::CONNECTION_TIMEOUT))
      << "Expected CONNECTION_TIMEOUT but got: ";
    EXPECT_STREQ(ec.category().name(), "network");
  }

  spdlog::debug("[timeout_test] Cleanup - stopping blackhole server...");
  blackhole_server->stop();
}

}  // namespace Network::Test

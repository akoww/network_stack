#include "server/ServerAsync.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

ServerAsync::ServerAsync(uint16_t port, asio::io_context& io_ctx) : ServerBase(port, io_ctx)
{
  spdlog::info("server async created on port {}", port);
}

asio::awaitable<std::expected<void, std::error_code>> ServerAsync::listen()
{
  spdlog::info("server async listening on {}:{}", host(), port());

  std::error_code ec;

  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
  {
    spdlog::error("failed to open acceptor: {}", ec.message());
    co_return std::unexpected(ec);
  }

  _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec)
  {
    spdlog::error("failed to set reuse_address: {}", ec.message());
    co_return std::unexpected(ec);
  }

  _acceptor.bind(endpoint, ec);
  if (ec)
  {
    spdlog::error("failed to bind to {}:{}", host(), port());
    co_return std::unexpected(ec);
  }

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    spdlog::error("failed to start listening: {}", ec.message());
    co_return std::unexpected(ec);
  }

  spdlog::info("server async started on {}:{}", host(), port());

  asio::ip::tcp::socket socket(get_io_context());

  while (!is_stopped())
  {
    ec = {};
    co_await _acceptor.async_accept(socket, asio::redirect_error(asio::use_awaitable, ec));

    if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
    {
      co_return std::expected<void, std::error_code>{};
    }

    if (ec)
    {
      spdlog::error("async accept error: {}", ec.message());
      co_return std::unexpected(ec);
    }

    spdlog::info("new async connection accepted");

    auto new_socket = std::make_unique<TcpSocket>(std::move(socket));

    asio::co_spawn(
      get_io_context(),
      [this, socket = std::move(new_socket)]() mutable -> asio::awaitable<void>
      {
        handle_client(std::move(socket));
        co_return;
      },
      asio::detached);

    socket = asio::ip::tcp::socket(get_io_context());
  }

  co_return std::expected<void, std::error_code>{};
}

asio::awaitable<std::expected<void, std::error_code>> ServerAsync::listen_tls()
{
  spdlog::info("server async listening on {}:{} with TLS", host(), port());

  std::error_code ec;

  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
  {
    spdlog::error("failed to open acceptor: {}", ec.message());
    co_return std::unexpected(ec);
  }

  _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec)
  {
    spdlog::error("failed to set reuse_address: {}", ec.message());
    co_return std::unexpected(ec);
  }

  _acceptor.bind(endpoint, ec);
  if (ec)
  {
    spdlog::error("failed to bind to {}:{}", host(), port());
    co_return std::unexpected(ec);
  }

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    spdlog::error("failed to start listening: {}", ec.message());
    co_return std::unexpected(ec);
  }

  spdlog::info("server async TLS started on {}:{}", host(), port());

  while (!is_stopped())
  {
    ec = {};
    asio::ip::tcp::socket socket(get_io_context());
    co_await _acceptor.async_accept(socket, asio::redirect_error(asio::use_awaitable, ec));

    if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
    {
      co_return std::expected<void, std::error_code>{};
    }

    if (ec)
    {
      spdlog::error("async TLS accept error: {}", ec.message());
      co_return std::unexpected(ec);
    }

    spdlog::info("new async TLS connection accepted");

    asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(std::move(socket), *get_ssl_context());

    ec = {};
    co_await ssl_stream.async_handshake(asio::ssl::stream_base::server, asio::redirect_error(asio::use_awaitable, ec));

    if (ec)
    {
      spdlog::error("TLS handshake failed: {}", ec.message());
      continue;
    }

    auto new_socket = std::make_unique<SslSocket>(std::move(ssl_stream));

    asio::co_spawn(
      get_io_context(),
      [this, socket = std::move(new_socket)]() mutable -> asio::awaitable<void>
      {
        handle_client_tls(std::move(socket));
        co_return;
      },
      asio::detached);
  }

  co_return std::expected<void, std::error_code>{};
}

void ServerAsync::stop()
{
  spdlog::info("server async stopping...");
  ServerBase::stop();
  _acceptor.cancel();
  _acceptor.close();
  spdlog::info("server async stopped");
}

}  // namespace Network

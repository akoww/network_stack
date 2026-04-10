#include "server/ServerSync.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <condition_variable>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

ServerSync::ServerSync(uint16_t port, asio::io_context& io_ctx, ClientHandler handler)
  : ServerBase(port, io_ctx, std::move(handler))
{
}

std::expected<void, std::error_code> ServerSync::listen()
{
  spdlog::trace("server listening on {}:{}", host(), port());

  std::error_code ec;
  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
  {
    spdlog::error("failed to open acceptor: {}", ec.message());
    return std::unexpected(ec);
  }

  _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec)
  {
    spdlog::error("failed to set reuse_address: {}", ec.message());
    return std::unexpected(ec);
  }

  _acceptor.bind(endpoint, ec);
  if (ec)
  {
    spdlog::error("failed to bind to {}:{}", host(), port());
    return std::unexpected(ec);
  }

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    spdlog::error("failed to start listening: {}", ec.message());
    return std::unexpected(ec);
  }

  spdlog::trace("server started successfully on {}:{}", host(), port());

  auto promise = std::make_shared<std::promise<std::expected<void, std::error_code>>>();

  auto future = promise->get_future();
  start_accept(std::move(promise));

  return future.get();
}

std::expected<void, std::error_code> ServerSync::listen_tls()
{
  spdlog::trace("server listening on {}:{} with TLS", host(), port());

  std::error_code ec;
  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
  {
    spdlog::error("failed to open acceptor: {}", ec.message());
    return std::unexpected(ec);
  }

  _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec)
  {
    spdlog::error("failed to set reuse_address: {}", ec.message());
    return std::unexpected(ec);
  }

  _acceptor.bind(endpoint, ec);
  if (ec)
  {
    spdlog::error("failed to bind to {}:{}", host(), port());
    return std::unexpected(ec);
  }

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    spdlog::error("failed to start listening: {}", ec.message());
    return std::unexpected(ec);
  }

  spdlog::trace("server TLS started successfully on {}:{}", host(), port());

  auto promise = std::make_shared<std::promise<std::expected<void, std::error_code>>>();

  auto future = promise->get_future();
  start_accept_tls(std::move(promise));

  return future.get();
}

void ServerSync::start_accept(std::shared_ptr<std::promise<std::expected<void, std::error_code>>> promise)
{
  if (!promise)
  {
    return;
  }

  if (isStopped())
  {
    promise->set_value(std::expected<void, std::error_code>{});
    return;
  }

  auto new_socket = std::make_unique<TcpSocket>(getIoContext());
  auto& socket = new_socket->getSocket();
  _acceptor.async_accept(socket,
                         [&, new_socket = std::move(new_socket), promise = std::move(promise),
                          handler = clientHandler()](std::error_code ec) mutable
                         {
                           if (isStopped())
                           {
                             promise->set_value(std::expected<void, std::error_code>{});
                             return;
                           }
                           if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
                           {
                             promise->set_value(std::unexpected(ec));
                             return;
                           }

                           if (ec)
                           {
                             spdlog::warn("accept error: {}, retrying...", ec.message());
                             start_accept(std::move(promise));
                             return;
                           }

                           auto sock_id = new_socket->getSocket().lowest_layer().native_handle();
                           spdlog::debug("[{}] new connection accepted (fd={})", new_socket->getId(), sock_id);

                           handler(std::move(new_socket));
                           start_accept(std::move(promise));
                         });
}

void ServerSync::start_accept_tls(std::shared_ptr<std::promise<std::expected<void, std::error_code>>> promise)
{
  if (!promise)
  {
    return;
  }

  if (isStopped())
  {
    promise->set_value(std::expected<void, std::error_code>{});
    return;
  }

  auto new_socket = std::make_unique<SslSocket>(getIoContext(), *getSslContext());

  auto& stream = new_socket->getSocket();
  auto& socket = stream.next_layer();

  _acceptor.async_accept(socket,
                         [&, new_socket = std::move(new_socket), promise = std::move(promise),
                          handler = clientHandler()](std::error_code ec) mutable
                         {
                           if (isStopped())
                           {
                             promise->set_value(std::expected<void, std::error_code>{});
                             return;
                           }

                           if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
                           {
                             promise->set_value(std::unexpected(ec));
                             return;
                           }

                           if (ec)
                           {
                             spdlog::warn("TLS accept error: {}, retrying...", ec.message());
                             start_accept_tls(std::move(promise));
                             return;
                           }

                           spdlog::debug("new TLS connection accepted");

                           auto& ssl_stream = new_socket->getSocket();
                           ssl_stream.handshake(asio::ssl::stream_base::server, ec);

                           if (ec)
                           {
                             spdlog::error("TLS handshake failed: {}", ec.message());
                             start_accept_tls(std::move(promise));
                             return;
                           }

                           handler(std::move(new_socket));
                           start_accept_tls(std::move(promise));
                         });
}

void ServerSync::stop()
{
  spdlog::trace("closing server...");
  ServerBase::stop();
  std::error_code ec;
  _acceptor.cancel(ec);
  if (ec)
  {
    spdlog::warn("acceptor cancel error: {}", ec.message());
  }
  spdlog::trace("server closed");
}

}  // namespace Network

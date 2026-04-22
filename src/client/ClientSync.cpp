#include "client/ClientSync.h"
#include "core/ErrorTranslation.h"
#include "socket/SslSocket.h"
#include "socket/SyncSocketInterface.h"
#include "socket/TcpSocket.h"

#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

ClientSync::ClientSync(std::string_view host, uint16_t port, asio::io_context& io_ctx) : ClientBase(host, port, io_ctx)
{
}

std::expected<std::unique_ptr<DualSocket>, std::error_code> ClientSync::connect(Options /*opts*/)
{
  spdlog::trace("client connecting to {}:{}...", host(), port());

  std::error_code ec;

  asio::ip::tcp::resolver resolver(getIoContext());

  auto endpoints = resolver.resolve(host(), std::to_string(port()), ec);

  if (ec)
  {
    spdlog::error("DNS resolution failed for {}: {}", host(), ec.message());
    return std::unexpected(makeDnsError(ec));
  }

  asio::ip::tcp::socket socket(getIoContext());

  asio::connect(socket, endpoints, ec);

  if (ec)
  {
    spdlog::error("connection to {}:{} failed: {}", host(), port(), ec.message());
    return std::unexpected(makeConnectionError(ec));
  }

  spdlog::trace("client connected to {}:{} successfully", host(), port());
  auto tcp_socket = std::make_unique<TcpSocket>(std::move(socket));

  return tcp_socket;
}

std::expected<std::unique_ptr<DualSocket>, std::error_code> ClientSync::connect_tls(Options /*opts*/)
{
  spdlog::trace("client connecting to {}:{} using TLS...", host(), port());

  std::error_code ec;

  asio::ip::tcp::resolver resolver(getIoContext());

  auto endpoints = resolver.resolve(host(), std::to_string(port()), ec);

  if (ec)
  {
    spdlog::error("DNS resolution failed for {}: {}", host(), ec.message());
    return std::unexpected(makeDnsError(ec));
  }

  asio::ip::tcp::socket socket(getIoContext());

  asio::connect(socket, endpoints, ec);

  if (ec)
  {
    spdlog::error("connection to {}:{} failed: {}", host(), port(), ec.message());
    return std::unexpected(makeConnectionError(ec));
  }

  asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(std::move(socket), *getSslContext());

  ssl_stream.set_verify_mode(asio::ssl::verify_none, ec);
  if (ec)
  {
    spdlog::error("failed to set SSL verify mode: {}", ec.message());
    return std::unexpected(makeTlsError(ec));
  }

  ssl_stream.handshake(asio::ssl::stream_base::client, ec);

  if (ec)
  {
    spdlog::error("TLS handshake failed for {}:{}: {}", host(), port(), ec.message());
    return std::unexpected(makeTlsError(ec));
  }

  spdlog::trace("client TLS connected to {}:{} successfully", host(), port());
  auto ssl_socket = std::make_unique<SslSocket>(std::move(ssl_stream));

  return ssl_socket;
}

}  // namespace Network
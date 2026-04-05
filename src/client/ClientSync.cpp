#include "client/ClientSync.h"
#include "socket/SyncSocketInterface.h"
#include "socket/TcpSocket.h"

#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include "socket/TcpSocket.h"
#include <system_error>

namespace Network {

ClientSync::ClientSync(std::string_view host, uint16_t port,
                       asio::io_context &io_ctx)
    : ClientBase(host, port, io_ctx) {}

std::expected<std::unique_ptr<SyncSocket>, std::error_code>
ClientSync::connect(Options /*opts*/) {
  spdlog::info("client connecting to {}:{}...", host(), port());

  std::error_code ec;

  asio::ip::tcp::resolver resolver(get_io_context());

  auto endpoints = resolver.resolve(host(), std::to_string(port()), ec);

  if (ec) {
    spdlog::error("DNS resolution failed for {}: {}", host(), ec.message());
    return std::unexpected(ec);
  }

  asio::ip::tcp::socket socket(get_io_context());

  asio::connect(socket, endpoints, ec);

  if (ec) {
    spdlog::error("connection to {}:{} failed: {}", host(), port(),
                  ec.message());
    return std::unexpected(ec);
  }

  spdlog::info("client connected to {}:{} successfully", host(), port());
  auto tcp_socket = std::make_unique<TcpSocket>(std::move(socket));

  return tcp_socket;
}

} // namespace Network
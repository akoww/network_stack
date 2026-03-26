#include "client/ClientAsync.h"
#include "socket/TcpSocket.h"

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network {

ClientAsync::ClientAsync(std::string_view host, uint16_t port,
                         asio::io_context &io_ctx)
    : ClientBase(host, port, io_ctx) {}

asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
ClientAsync::connect(Options /*opts*/) {
  spdlog::info("client async connecting to {}:{}...", host(), port());

  std::error_code ec;

  asio::ip::tcp::resolver resolver(get_io_context());

  auto endpoints = co_await resolver.async_resolve(
      host(), std::to_string(port()),
      asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    spdlog::error("DNS resolution failed for {}: {}", host(), ec.message());
    co_return std::unexpected(ec);
  }

  asio::ip::tcp::socket socket(get_io_context());

  co_await asio::async_connect(socket, endpoints,
                               asio::redirect_error(asio::use_awaitable, ec));

  if (ec) {
    spdlog::error("connection to {}:{} failed: {}", host(), port(),
                  ec.message());
    co_return std::unexpected(ec);
  }

  spdlog::info("client async connected to {}:{} successfully", host(), port());

  auto tcp_socket = std::make_unique<TcpSocket>(std::move(socket));
  co_return std::move(tcp_socket);
}

} // namespace Network

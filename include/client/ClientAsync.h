#pragma once

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "ClientBase.h"
#include <expected>

namespace Network
{
class DualSocket;

/// @brief Asynchronous client implementation.
/// Provides coroutine-based async connection establishment to a remote server.
/// @section usage Usage
/// ```cpp
/// asio::co_spawn(io_ctx, [&]() -> asio::awaitable<void> {
///   ClientAsync client("example.com", 8080, io_ctx);
///   auto result = co_await client.connect({std::chrono::seconds(10)});
///   if (result) {
///     auto socket = std::move(result.value());
///     // use socket...
///   } else {
///     // handle error...
///   }
/// }, asio::detached);
/// ```
class ClientAsync : public ClientBase
{
public:
  /// @brief Construct with host, port, and io_context.
  ClientAsync(std::string_view host, uint16_t port, asio::io_context& io_ctx);

  /// @brief Asynchronously connect to the remote endpoint.
  /// @param opts Connection options including timeout.
  /// @return Socket on success, or error code on failure.
  asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> connect(Options opts);

  /// @brief Asynchronously connect to the remote endpoint using TLS.
  /// @param opts Connection options including timeout.
  /// @return TLS socket on success, or error code on failure.
  asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> connect_tls(Options opts);
};

}  // namespace Network

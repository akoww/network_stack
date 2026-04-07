#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>
#include <asio/ssl/context.hpp>

#include "ServerBase.h"

#include <expected>

namespace Network
{

class SslSocket;

/// @brief Asynchronous server implementation.
/// Uses coroutines for async accept and client handling.
/// Each accepted connection is handled via a new coroutine.
/// @section usage Usage
/// ```cpp
/// asio::co_spawn(io_ctx, []() -> asio::awaitable<void> {
///   ServerAsync server(8080, io_ctx);
///   auto result = co_await server.listen();
///   if (!result) {
///     // handle error...
///   }
/// }(), asio::detached);
/// ```
class ServerAsync : public ServerBase
{
public:
  /// @brief Construct with port and io_context.
  ServerAsync(uint16_t port, asio::io_context& io_ctx);

  /// @brief Asynchronously start accepting connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  asio::awaitable<std::expected<void, std::error_code>> listen();

  /// @brief Asynchronously start accepting TLS connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  asio::awaitable<std::expected<void, std::error_code>> listen_tls();

  /// @brief Stop accepting connections.
  /// Closes the acceptor and signals any waiting async operations.
  void stop();

private:
};

}  // namespace Network

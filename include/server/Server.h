#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>
#include <asio/ssl/context.hpp>

#include "ServerBase.h"
#include "socket/SocketBase.h"

#include <expected>

namespace Network
{

class TlsSocket;

class Server : public ServerBase, public ServerSync, public ServerAsync
{
public:
  /// @brief Construct with port and io_context.
  Server(uint16_t port, asio::any_io_executor io_ctx, ClientHandler handler);

  /// @brief Asynchronously start accepting connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  asio::awaitable<std::expected<void, std::error_code>> asyncListen() override;

  /// @brief Asynchronously start accepting TLS connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  asio::awaitable<std::expected<void, std::error_code>> asyncListenTls() override;

  /// @brief Start accepting connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  std::expected<void, std::error_code> listen() override;

  /// @brief Start accepting TLS connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  std::expected<void, std::error_code> listenTls() override;

  /// @brief Stop accepting connections.
  /// Closes the acceptor and signals any waiting async operations.
  void stop();

protected:
  asio::awaitable<void> acceptPlainSocket(asio::ip::tcp::socket socket);
  asio::awaitable<void> acceptTlsSocket(asio::ip::tcp::socket socket);

private:
};

}  // namespace Network

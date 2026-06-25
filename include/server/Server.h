#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>
#include <asio/ssl/context.hpp>

#include "ServerBase.h"
#include "core/Context.h"
#include "socket/TcpOptions.h"
#include "socket/TlsOptions.h"

#include <expected>

namespace Network
{

class TlsSocket;

class Server : public ServerBase, public ServerSync, public ServerAsync
{
public:
  /// @brief Construct with port and io_context.
  Server(uint16_t port, IoContextWrapper io_ctx, ClientHandler handler);

  /// @brief Asynchronously start accepting connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  asio::awaitable<std::expected<void, std::error_code>> asyncListen(TcpOptions tcp_opts = {}) override;

  /// @brief Asynchronously start accepting TLS connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @param tls_opts Optional TLS configuration options.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  asio::awaitable<std::expected<void, std::error_code>> asyncListenTls(TlsServerOptions tls_server_opts = {},
                                                                       TcpOptions tcp_opts = {},
                                                                       TlsOptions tls_opts = {}) override;

  /// @brief Start accepting connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  std::expected<void, std::error_code> listen(TcpOptions tcp_opts = {}) override;

  /// @brief Start accepting TLS connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @param tls_opts Optional TLS configuration options.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  std::expected<void, std::error_code> listenTls(TlsServerOptions tls_server_opts = {},
                                                 TcpOptions tcp_opts = {},
                                                 TlsOptions tls_opts = {}) override;

  /// @brief Stop accepting connections.
  /// Closes the acceptor and signals any waiting async operations.
  void stop();

protected:
  asio::awaitable<void> acceptPlainSocket(asio::ip::tcp::socket socket);
  asio::awaitable<void> acceptTlsSocket(asio::ip::tcp::socket socket,
                                        std::shared_ptr<asio::ssl::context> ctx,
                                        const TlsOptions& tls_opts);

private:
};

}  // namespace Network

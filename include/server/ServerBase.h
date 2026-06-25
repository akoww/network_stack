#pragma once

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>

#include "core/Context.h"
#include "socket/SocketBase.h"
#include "socket/TlsOptions.h"
#include "socket/TcpOptions.h"

#include <expected>
#include <filesystem>

namespace Network
{

class DualSocket;

/// @brief Base class for server implementations.
/// Provides common server functionality including acceptor management,
/// shutdown handling, and virtual client handling interface.
/// @section usage Usage
/// ```cpp
/// class MyServer : public ServerAsync {
///   void handle_client(std::unique_ptr<TcpSocket> socket) override {
///     // handle client...
///   }
/// };
/// asio::co_spawn(io_ctx, MyServer(port, io_ctx).listen(), asio::detached);
/// ```
class ServerBase
{
public:
  using ClientHandler = std::function<void(std::unique_ptr<DualSocket>)>;

  virtual ~ServerBase() = default;

  /// @brief Construct with port and io_context.
  /// @param port Port to bind to (0 for dynamic assignment).
  /// @param io_ctx ASIO io_context for async operations.
  /// @param handler Callback function for new incoming clients.
  explicit ServerBase(uint16_t port, IoContextWrapper io_ctx, ClientHandler handler);

  /// @brief Get the bound host.
  [[nodiscard]] std::string_view host() const;

  /// @brief Get the bound port.
  [[nodiscard]] uint16_t port() const;

  /// @brief Stop the server.
  /// Stops accepting new connections and closes the acceptor.
  void stop();

  /// @brief Check if the server has been stopped.
  /// @return true if server has been stopped, false otherwise.
  bool isStopped() const noexcept;

  ClientHandler clientHandler();

protected:
  std::atomic<bool> _stop_requested{false};
  asio::ip::tcp::acceptor _acceptor;

  std::string _host;
  uint16_t _port;
  IoContextWrapper _io_ctx;
  ClientHandler _handler;
};

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
class ServerAsync
{
public:
  virtual ~ServerAsync() = default;

  /// @brief Asynchronously start accepting connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  virtual asio::awaitable<std::expected<void, std::error_code>> asyncListen(TcpOptions tcp_opts = {}) = 0;

  /// @brief Asynchronously start accepting TLS connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @param tls_opts Optional TLS configuration options.
  /// @return error_code on failure (e.g., port already in use).
  /// @note This coroutine will not complete until server is stopped.
  virtual asio::awaitable<std::expected<void, std::error_code>> asyncListenTls(TlsServerOptions tls_server_opts = {},
                                                                               TcpOptions tcp_opts = {},
                                                                               TlsOptions tls_opts = {}) = 0;

private:
};

/// @brief Synchronous server implementation.
/// Uses blocking accept and client handling.
/// @note Server must be run in its own thread or the listen() call will block.
/// @section usage Usage
/// ```cpp
/// asio::io_context io_ctx;
/// ServerSync server(8080, io_ctx);
/// std::thread t([&]() { server.listen(); });
/// // ... run server ...
/// server.stop();
/// t.join();
/// ```
class ServerSync
{
public:
  virtual ~ServerSync() = default;

  /// @brief Start accepting connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  virtual std::expected<void, std::error_code> listen(TcpOptions tcp_opts = {}) = 0;

  /// @brief Start accepting TLS connections.
  /// @param tcp_opts Optional TCP socket configuration options for accepted sockets.
  /// @param tls_opts Optional TLS configuration options.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  virtual std::expected<void, std::error_code> listenTls(TlsServerOptions tls_server_opts = {},
                                                         TcpOptions tcp_opts = {},
                                                         TlsOptions tls_opts = {}) = 0;
};

}  // namespace Network

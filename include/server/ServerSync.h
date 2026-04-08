#pragma once

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "ServerBase.h"

#include <expected>
#include <future>
#include <memory>

namespace Network
{

class TcpSocket;
class SslSocket;

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
class ServerSync : public ServerBase
{
public:
  /// @brief Construct with port and io_context.
  explicit ServerSync(uint16_t port, asio::io_context& io_ctx, ClientHandler handler);

  /// @brief Start accepting connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  std::expected<void, std::error_code> listen();

  /// @brief Start accepting TLS connections.
  /// @return error_code on failure (e.g., port already in use).
  /// @note Blocks until listen() is called or server is stopped.
  std::expected<void, std::error_code> listen_tls();

  /// @brief Stop accepting connections.
  /// Closes the acceptor and signals threads waiting on accept to wake up.
  void stop();

protected:
  void start_accept(std::shared_ptr<std::promise<std::expected<void, std::error_code>>> promise);

  void start_accept_tls(std::shared_ptr<std::promise<std::expected<void, std::error_code>>> promise);

private:
};

}  // namespace Network

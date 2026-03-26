#pragma once

#include <atomic>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

namespace Network {

class TcpSocket;

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
class ServerBase {
public:
  virtual ~ServerBase() = default;

  /// @brief Construct with port and io_context.
  /// @param port Port to bind to (0 for dynamic assignment).
  /// @param io_ctx ASIO io_context for async operations.
  explicit ServerBase(uint16_t port, asio::io_context &io_ctx);

  /// @brief Get the bound host.
  std::string_view host() const;

  /// @brief Get the bound port.
  uint16_t port() const;

  /// @brief Get the io_context reference.
  asio::io_context &get_io_context();

  /// @brief Handle a new client connection.
  /// Called when a client connects to the server.
  /// @param socket Connected TCP socket for the client.
  virtual void handle_client(std::unique_ptr<TcpSocket> socket) = 0;

  /// @brief Stop the server.
  /// Stops accepting new connections and closes the acceptor.
  void stop();

  /// @brief Check if the server has been stopped.
  /// @return true if server has been stopped, false otherwise.
  bool is_stopped() const noexcept;

protected:
  std::atomic<bool> _stop_requested{false};
  asio::ip::tcp::acceptor _acceptor;

private:
  std::string _host;
  uint16_t _port;
  asio::io_context &_io_ctx;
};

}

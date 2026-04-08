#pragma once

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/context.hpp>

namespace Network
{

class BasicSocket;

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
  using ClientHandler = std::function<void(std::unique_ptr<BasicSocket>)>;

  virtual ~ServerBase() = default;

  /// @brief Construct with port and io_context.
  /// @param port Port to bind to (0 for dynamic assignment).
  /// @param io_ctx ASIO io_context for async operations.
  /// @param handler callback function for new incoming clients
  explicit ServerBase(uint16_t port, asio::io_context& io_ctx, ClientHandler handler);

  /// @brief Get the bound host.
  std::string_view host() const;

  /// @brief Get the bound port.
  uint16_t port() const;

  /// @brief Get the io_context reference.
  asio::io_context& get_io_context();

  /// @brief Get the SSL context for TLS connections.
  /// @return Shared pointer to SSL context.
  std::shared_ptr<asio::ssl::context> get_ssl_context();

  /// @brief Stop the server.
  /// Stops accepting new connections and closes the acceptor.
  void stop();

  /// @brief Check if the server has been stopped.
  /// @return true if server has been stopped, false otherwise.
  bool is_stopped() const noexcept;

  ClientHandler clientHandler();

protected:
  std::atomic<bool> _stop_requested{false};
  asio::ip::tcp::acceptor _acceptor;

private:
  std::string _host;
  uint16_t _port;
  asio::io_context& _io_ctx;
  ClientHandler _handler;
  std::shared_ptr<asio::ssl::context> _ssl_context;
};

}  // namespace Network

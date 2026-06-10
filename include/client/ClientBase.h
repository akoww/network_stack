#pragma once

#include <asio/io_context.hpp>
#include <asio/ssl/context.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>

#include <chrono>
#include <string>
#include <expected>

namespace Network
{

/// @brief Base class for client implementations.
/// Provides common configuration and accessors shared by synchronous and
/// asynchronous client implementations.
/// @section usage Usage
/// ```cpp
/// asio::io_context io_ctx;
/// ClientSync client("example.com", 8080, io_ctx);
/// auto socket = client.connect({std::chrono::seconds(10)});
/// ```
class ClientBase
{
public:
  /// @brief Construct with host and port.
  /// @param host Remote host address (domain name or IP).
  /// @param port Remote port number.
  /// @param io_ctx ASIO io_context for async operations.
  explicit ClientBase(std::string_view host, uint16_t port, asio::any_io_executor io_ctx);

  /// @brief Get the target host.
  [[nodiscard]] std::string_view host() const;

  /// @brief Get the target port.
  [[nodiscard]] uint16_t port() const;

  /// @brief Get the io_context reference.
  asio::any_io_executor& getIoContext();

  /// @brief Get the SSL context for TLS connections.
  /// @return Shared pointer to SSL context.
  std::shared_ptr<asio::ssl::context> getSslContext();

private:
  std::string _host;
  uint16_t _port;
  asio::any_io_executor _io_ctx;
  std::shared_ptr<asio::ssl::context> _ssl_context;
};

class DualSocket;

/// @brief Asynchronous client implementation.
/// Provides coroutine-based async connection establishment to a remote server.
/// @section usage Usage
/// ```cpp
/// asio::co_spawn(io_ctx, [&]() -> asio::awaitable<void> {
///   ClientAsync client("example.com", 8080, io_ctx);
///   auto result = co_await client.asyncConnect({std::chrono::seconds(10)});
///   if (result) {
///     auto socket = std::move(result.value());
///     // use socket...
///   } else {
///     // handle error...
///   }
/// }, asio::detached);
/// ```
class ClientAsync
{
public:
  virtual ~ClientAsync() = default;

  /// @brief Asynchronously connect to the remote endpoint.
  /// @param opts Connection options including timeout.
  /// @return Socket on success, or error code on failure.
  virtual asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> asyncConnect(
    std::chrono::milliseconds timeout) = 0;

  /// @brief Asynchronously connect to the remote endpoint using TLS.
  /// @param opts Connection options including timeout.
  /// @return TLS socket on success, or error code on failure.
  virtual asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> asyncConnectTls(
    std::chrono::milliseconds timeout) = 0;
};

/// @brief Synchronous client implementation.
/// Provides blocking connection establishment to a remote server.
/// @section usage Usage
/// ```cpp
/// asio::io_context io_ctx;
/// ClientSync client("example.com", 8080, io_ctx);
/// auto result = client.connect({std::chrono::seconds(10)});
/// if (result) {
///   auto socket = std::move(result.value());
///   // use socket...
/// } else {
///   // handle error...
/// }
/// ```
class ClientSync
{
public:
  virtual ~ClientSync() = default;

  /// @brief Connect to the remote endpoint.
  /// @param opts Connection options including timeout.
  /// @return Socket on success, or error code on failure.
  /// @note Blocks until connection is established or times out.
  virtual std::expected<std::unique_ptr<DualSocket>, std::error_code> connect(std::chrono::milliseconds timeout) = 0;

  /// @brief Connect to the remote endpoint using TLS.
  /// @param opts Connection options including timeout.
  /// @return TLS socket on success, or error code on failure.
  /// @note Blocks until connection is established or times out.
  virtual std::expected<std::unique_ptr<DualSocket>, std::error_code> connectTls(std::chrono::milliseconds timeout) = 0;

private:
};

}  // namespace Network

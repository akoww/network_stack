#pragma once

#include <asio/io_context.hpp>
#include <chrono>
#include <string>

namespace Network {

/// @brief Base class for client implementations.
/// Provides common configuration and accessors shared by synchronous and
/// asynchronous client implementations.
/// @section usage Usage
/// ```cpp
/// asio::io_context io_ctx;
/// ClientSync client("example.com", 8080, io_ctx);
/// auto socket = client.connect({std::chrono::seconds(10)});
/// ```
class ClientBase {
public:
  /// @brief Client configuration options.
  struct Options {
    std::chrono::milliseconds timeout = std::chrono::seconds(10);
  };

  /// @brief Construct with host and port.
  /// @param host Remote host address (domain name or IP).
  /// @param port Remote port number.
  /// @param io_ctx ASIO io_context for async operations.
  explicit ClientBase(std::string_view host, uint16_t port,
                      asio::io_context &io_ctx);

  /// @brief Get the target host.
  std::string_view host() const;

  /// @brief Get the target port.
  uint16_t port() const;

  /// @brief Get the io_context reference.
  asio::io_context &get_io_context();

private:
  std::string _host;
  uint16_t _port;
  asio::io_context &_io_ctx;
};

} // namespace Network

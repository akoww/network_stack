#pragma once

#include "ClientBase.h"

#include <expected>
#include <memory>

namespace Network
{

class SyncSocket;

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
class ClientSync : public ClientBase
{
public:
  /// @brief Construct with host, port, and io_context.
  explicit ClientSync(std::string_view host, uint16_t port, asio::io_context& io_ctx);

  /// @brief Connect to the remote endpoint.
  /// @param opts Connection options including timeout.
  /// @return Socket on success, or error code on failure.
  /// @note Blocks until connection is established or times out.
  std::expected<std::unique_ptr<SyncSocket>, std::error_code> connect(Options opts);

  /// @brief Connect to the remote endpoint using TLS.
  /// @param opts Connection options including timeout.
  /// @return TLS socket on success, or error code on failure.
  /// @note Blocks until connection is established or times out.
  std::expected<std::unique_ptr<SyncSocket>, std::error_code> connect_tls(Options opts);

private:
};

}  // namespace Network

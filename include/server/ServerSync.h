#pragma once

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include "ServerBase.h"

#include <expected>
#include <future>
#include <memory>

namespace Network {

class TcpSocket;

/// @brief Synchronous server implementation.
/// Uses blocking accept and client handling.
class ServerSync : public ServerBase {
 public:
  /// @brief Construct with port and io_context.
  explicit ServerSync(uint16_t port, asio::io_context& io_ctx);

  /// @brief Start accepting connections.
  std::expected<void, std::error_code> listen();
  /// @brief Stop accepting connections.
  void stop();

 protected:
  void start_accept(
      std::shared_ptr<std::promise<std::expected<void, std::error_code>>>
          promise);

 private:
};

}  // namespace Network

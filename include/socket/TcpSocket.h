#pragma once

#include "AsyncSocketInterface.h"
#include "SyncSocketInterface.h"

#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <optional>

namespace Network {

/// @brief TCP socket implementation using ASIO.
/// Supports both synchronous and asynchronous operations.
/// This class implements both SyncSocket and AsyncSocket interfaces,
/// allowing seamless switching between blocking and non-blocking I/O.
/// @section sync_usage Synchronous Usage
/// ```cpp
/// asio::io_context io_ctx;
/// TcpSocket socket(io_ctx);
/// auto result = socket.write_all(data);
/// ```
/// @section async_usage Asynchronous Usage
/// ```cpp
/// asio::co_spawn(io_ctx, async_operation(), asio::detached);
/// ```
class TcpSocket : public SyncSocket, public AsyncSocket {
private:
  asio::ip::tcp::socket socket_;
  std::vector<std::byte> read_buffer_;
  unsigned int id_ = 0;

public:
  /// @brief Construct with an io_context.
  explicit TcpSocket(asio::io_context &io_ctx);

  /// @brief Construct by moving in an existing socket.
  explicit TcpSocket(asio::ip::tcp::socket &&sock);

  ~TcpSocket() override;

  bool is_connected() const noexcept override;

  unsigned int get_id() const;

  // sync

  std::expected<std::size_t, std::error_code>
  write_all(std::span<const std::byte> buffer,
            std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code>
  read_some(std::span<std::byte> buffer,
            std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code>
  read_exact(std::span<std::byte> buffer,
             std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code>
  read_until(std::span<std::byte> buffer, std::string_view delimiter,
             std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  // async

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_write_all(std::span<const std::byte> buffer,
                  std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_some(std::span<std::byte> buffer,
                  std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_exact(std::span<std::byte> buffer,
                   std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_until(std::span<std::byte> buffer, std::string_view delimiter,
                   std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;
};
} // namespace Network

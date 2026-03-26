#pragma once

#include "SocketBaseInterface.h"

#include "AsyncInterface.h"
#include "SyncInterface.h"

#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/streambuf.hpp>

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
class TcpSocket : public SocketBase, public SyncSocket, public AsyncSocket {
private:
  asio::ip::tcp::socket socket_;
  std::vector<std::byte> read_buffer_;

public:
  /// @brief Construct with an io_context.
  explicit TcpSocket(asio::io_context &io_ctx);

  /// @brief Construct by moving in an existing socket.
  explicit TcpSocket(asio::ip::tcp::socket &&sock);

  ~TcpSocket() override;

  bool is_connected() const noexcept override;

  // sync

  /// @brief Send all data in the buffer via the socket.
  /// @param buffer Span of data to send.
  /// @return Number of bytes sent, or error code on failure.
  std::expected<std::size_t, std::error_code>
  write_all(std::span<const std::byte> buffer) override;

  /// @brief Receive data from the socket without blocking indefinitely.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received, or error code on failure.
  std::expected<std::size_t, std::error_code>
  read_some(std::span<std::byte> buffer) override;

  /// @brief Receive exactly buffer.size() bytes into the buffer.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  std::expected<std::size_t, std::error_code>
  read_exact(std::span<std::byte> buffer) override;

  /// @brief Receive data until delimiter is found.
  /// @param buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @return Number of bytes received (including delimiter), or error code.
  std::expected<std::size_t, std::error_code>
  read_until(std::span<std::byte> buffer, std::string_view delimiter) override;

  // async

  /// @brief Send all data in the buffer via the socket asynchronously.
  /// @param buffer Span of data to send.
  /// @return Number of bytes sent, or error code on failure.
  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_write_all(std::span<const std::byte> buffer) override;

  /// @brief Receive data from the socket asynchronously.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received, or error code on failure.
  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_some(std::span<std::byte> buffer) override;

  /// @brief Receive exactly buffer.size() bytes into the buffer asynchronously.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_exact(std::span<std::byte> buffer) override;

  /// @brief Receive data until delimiter is found asynchronously.
  /// @param buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @return Number of bytes received (including delimiter), or error code.
  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_until(std::span<std::byte> buffer,
                   std::string_view delimiter) override;
};
} // namespace Network

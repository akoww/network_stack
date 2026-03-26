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
class TcpSocket : public SocketBase, public SyncSocket, public AsyncSocket {
private:
  asio::ip::tcp::socket socket_;
  std::vector<char> read_buffer_;

public:
  /// @brief Construct with an io_context.
  explicit TcpSocket(asio::io_context &io_ctx);

  /// @brief Construct by moving in an existing socket.
  explicit TcpSocket(asio::ip::tcp::socket &&sock);

  ~TcpSocket();

  bool is_connected() const noexcept override;

  // sync

  std::expected<std::size_t, std::error_code>
  write_all(std::span<const std::byte> buffer) override;

  std::expected<std::size_t, std::error_code>
  read_some(std::span<std::byte> buffer) override;

  std::expected<std::size_t, std::error_code>
  read_exact(std::span<std::byte> buffer) override;

  std::expected<std::size_t, std::error_code>
  read_until(std::span<std::byte> buffer, std::string_view delimiter) override;

  // async

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_write_all(std::span<const std::byte> buffer) override;

  /// @brief Receive data from the socket.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received, or error code on failure.
  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_some(std::span<std::byte> buffer) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_exact(std::span<std::byte> buffer) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_until(std::span<std::byte> buffer,
                   std::string_view delimiter) override;
};
} // namespace Network
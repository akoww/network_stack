#pragma once

#include <asio/awaitable.hpp>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

namespace Network {

/// @brief Asynchronous socket interface.
/// Provides coroutine-based async send and receive operations.
/// All asynchronous socket implementations must inherit from this interface.
/// @note Operations return asio::awaitable and must be co_awaited.
class AsyncSocket {
public:
  virtual ~AsyncSocket() = default;

  /// @brief Receive data from the socket asynchronously.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received, or error code on failure.
  /// @note May return fewer bytes than buffer size.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_some(std::span<std::byte> buffer) = 0;

  /// @brief Send all data in the buffer via the socket asynchronously.
  /// @param buffer Span of data to send.
  /// @return Number of bytes sent, or error code on failure.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_write_all(std::span<const std::byte> buffer) = 0;

  /// @brief Receive data until delimiter is found asynchronously.
  /// @param buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @return Number of bytes received (including delimiter), or error code.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_until(std::span<std::byte> buffer, std::string_view delimiter) = 0;

  /// @brief Receive exactly len bytes into the buffer asynchronously.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_exact(std::span<std::byte> buffer) = 0;
};

} // namespace Network

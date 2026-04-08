#pragma once

#include <asio/awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <system_error>

namespace Network
{

/// @brief Asynchronous socket interface.
/// Provides coroutine-based async send and receive operations.
/// All asynchronous socket implementations must inherit from this interface.
/// @note Operations return asio::awaitable and must be co_awaited.
class AsyncSocket
{
public:
  virtual ~AsyncSocket() = default;

  /// @brief Receive data from the socket asynchronously.
  /// @param out_buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received, or error code on failure.
  /// @note May return fewer bytes than buffer size.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Send all data in the buffer via the socket asynchronously.
  /// @param in_buffer Span of data to send.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes sent, or error code on failure.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive data until delimiter is found asynchronously.
  /// @param out_buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (including delimiter), or error code.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive exactly len bytes into the buffer asynchronously.
  /// @param out_buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExact(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;
};

}  // namespace Network

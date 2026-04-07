#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <system_error>

#include "SocketBase.h"

namespace Network
{

/// @brief Synchronous socket interface.
/// Provides blocking send and receive operations.
/// All synchronous socket implementations must inherit from this interface.
/// @note Operations block until completion or error; use async versions for
/// non-blocking behavior.
class SyncSocket : public virtual SocketBase
{
public:
  virtual ~SyncSocket() = default;

  /// @brief Receive data from the socket without blocking indefinitely.
  /// @param out_buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received, or error code on failure.
  /// @note May return fewer bytes than buffer size.
  virtual std::expected<std::size_t, std::error_code> readSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Send all data in the buffer via the socket.
  /// @param in_buffer Span of data to send.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes sent, or error code on failure.
  virtual std::expected<std::size_t, std::error_code> writeAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive exactly len bytes into the buffer.
  /// @param buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  virtual std::expected<std::size_t, std::error_code> readExact(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive data until delimiter is found.
  /// @param buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (including delimiter), or error code.
  virtual std::expected<std::size_t, std::error_code> readUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;
};

}  // namespace Network

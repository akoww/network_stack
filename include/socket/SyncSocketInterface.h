#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include "SocketBaseInterface.h"

namespace Network {

/// @brief Synchronous socket interface.
/// Provides blocking send and receive operations.
/// All synchronous socket implementations must inherit from this interface.
/// @note Operations block until completion or error; use async versions for
/// non-blocking behavior.
class SyncSocket : public SocketBase {
public:
  virtual ~SyncSocket() = default;

  /// @brief Receive data from the socket without blocking indefinitely.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received, or error code on failure.
  /// @note May return fewer bytes than buffer size.
  virtual std::expected<std::size_t, std::error_code>
  read_some(std::span<std::byte> buffer) = 0;

  /// @brief Send all data in the buffer via the socket.
  /// @param buffer Span of data to send.
  /// @return Number of bytes sent, or error code on failure.
  virtual std::expected<std::size_t, std::error_code>
  write_all(std::span<const std::byte> buffer) = 0;

  /// @brief Receive exactly len bytes into the buffer.
  /// @param buffer Span to receive data into.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  virtual std::expected<std::size_t, std::error_code>
  read_exact(std::span<std::byte> buffer) = 0;

  /// @brief Receive data until delimiter is found.
  /// @param buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @return Number of bytes received (including delimiter), or error code.
  virtual std::expected<std::size_t, std::error_code>
  read_until(std::span<std::byte> buffer, std::string_view delimiter) = 0;
};

} // namespace Network

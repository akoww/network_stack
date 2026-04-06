#pragma once

#include <expected>
#include <system_error>

namespace Network {

/// @brief Base interface for all socket types.
/// Defines common functionality regardless of sync/async implementation.
/// All socket classes must inherit from this interface to ensure consistent
/// behavior.
class SocketBase {
  unsigned int _id = 0;

public:
  SocketBase();
  virtual ~SocketBase() = default;

  /// @brief Check if the socket is currently connected to a remote endpoint.
  /// @return true if connected, false otherwise.
  [[nodiscard]]
  virtual bool is_connected() const noexcept = 0;

  virtual void close_socket() noexcept = 0;
  virtual void cancel_socket() noexcept = 0;

  [[nodiscard]]
  virtual bool
  is_connection_closed(const std::error_code &ec) const noexcept = 0;

  unsigned int get_id() const;
};

} // namespace Network
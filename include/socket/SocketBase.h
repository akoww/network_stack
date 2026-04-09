#pragma once

#include <expected>

#include <asio/cancellation_signal.hpp>

#include "SyncSocketInterface.h"
#include "AsyncSocketInterface.h"
#include <system_error>

namespace Network
{

/// @brief Abstract base class for all socket types.
/// Defines common functionality regardless of sync/async implementation.
/// All socket classes must inherit from this class to ensure consistent
/// behavior.
class SocketBase
{
  unsigned int _id = 0;
  std::vector<std::byte> _read_buffer;
  asio::cancellation_signal _cancel_signal;

public:
  SocketBase();
  virtual ~SocketBase() = default;

  /// @brief Check if the socket is currently connected to a remote endpoint.
  /// @return true if connected, false otherwise.
  [[nodiscard]]
  virtual bool isConnected() const noexcept = 0;

  virtual void closeSocket() noexcept = 0;
  virtual void cancelSocket();

  [[nodiscard]]
  virtual bool isConnectionClosed(const std::error_code& ec) const noexcept = 0;

  std::vector<std::byte>& getReadBuffer() { return _read_buffer; }
  unsigned int getId() const;

  asio::cancellation_signal& cancelSignal() { return _cancel_signal; }
};

/// @brief Concrete socket class implementing both sync and async interfaces.
/// Provides dual-mode socket that supports both blocking and non-blocking operations.
/// TcpSocket and SslSocket inherit from this class.
class DualSocket : public SocketBase, public AsyncSocket, public SyncSocket
{
public:
  virtual ~DualSocket() = default;
};

}  // namespace Network
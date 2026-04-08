#pragma once

#include <expected>
#include "SyncSocketInterface.h"
#include "AsyncSocketInterface.h"
#include <system_error>

namespace Network
{

/// @brief Base interface for all socket types.
/// Defines common functionality regardless of sync/async implementation.
/// All socket classes must inherit from this interface to ensure consistent
/// behavior.
class SocketBase
{
  unsigned int _id = 0;

public:
  SocketBase();
  virtual ~SocketBase() = default;

  /// @brief Check if the socket is currently connected to a remote endpoint.
  /// @return true if connected, false otherwise.
  [[nodiscard]]
  virtual bool isConnected() const noexcept = 0;

  virtual void closeSocket() noexcept = 0;
  virtual void cancelSocket() noexcept = 0;

  [[nodiscard]]
  virtual bool isConnectionClosed(const std::error_code& ec) const noexcept = 0;

  unsigned int getId() const;
};

class BasicSocket : public SocketBase, public AsyncSocket, public SyncSocket
{
public:
  virtual ~BasicSocket() = default;
};

}  // namespace Network
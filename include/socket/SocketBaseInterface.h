#pragma once

#include <expected>

namespace Network {

/// @brief Base interface for all socket types.
/// Defines common functionality regardless of sync/async implementation.
/// All socket classes must inherit from this interface to ensure consistent behavior.
class SocketBase {
public:
    virtual ~SocketBase() = default;

    /// @brief Check if the socket is currently connected to a remote endpoint.
    /// @return true if connected, false otherwise.
    [[nodiscard]]
    virtual bool is_connected() const noexcept = 0;
};

}
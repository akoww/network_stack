#pragma once

#include <expected>

namespace Network
{

    /// @brief Base interface for all socket types.
    /// Defines common functionality regardless of sync/async implementation.
    class SocketBase
    {
    public:
        virtual ~SocketBase() = default;

        /// @brief Check if the socket is currently connected to a remote endpoint.
        [[nodiscard]]
        virtual bool is_connected() const noexcept = 0;
    };

}
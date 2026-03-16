#pragma once

#include <expected>
#include <span>
#include <cstddef>
#include <system_error>
#include <cstddef>

#include "BaseInterface.h"

namespace Network
{

    /// @brief Synchronous socket interface.
    /// Provides blocking send and receive operations.
    class SyncSocket : public SocketBase
    {
    public:
        virtual ~SyncSocket() = default;

        /// @brief Send data over the socket.
        /// @param buffer Span of bytes to send.
        /// @return Number of bytes sent, or error code on failure.
        virtual std::expected<std::size_t, std::error_code>
        send(std::span<const std::byte> buffer) = 0;

        /// @brief Receive data from the socket.
        /// @param buffer Span to receive data into.
        /// @return Number of bytes received, or error code on failure.
        virtual std::expected<std::size_t, std::error_code>
        receive(std::span<std::byte> buffer) = 0;
    };

}
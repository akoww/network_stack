#pragma once

#include <asio/awaitable.hpp>
#include <expected>
#include <span>
#include <system_error>
#include <cstddef>

#include "BaseInterface.h"

namespace Network
{

    /// @brief Asynchronous socket interface.
    /// Provides coroutine-based async send and receive operations.
    class AsyncSocket : public SocketBase
    {
    public:
        virtual ~AsyncSocket() = default;

        /// @brief Asynchronously send data over the socket.
        /// @param buffer Span of bytes to send.
        /// @return Number of bytes sent, or error code on failure.
        virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
        async_send(std::span<const std::byte> buffer) = 0;

        /// @brief Asynchronously receive data from the socket.
        /// @param buffer Span to receive data into.
        /// @return Number of bytes received, or error code on failure.
        virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
        async_receive(std::span<std::byte> buffer) = 0;
    };

}
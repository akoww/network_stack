#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>

#include "ServerBase.h"

#include <expected>

namespace Network
{

    /// @brief Asynchronous server implementation.
    /// Uses coroutines for async accept and client handling.
    class ServerAsync : public ServerBase
    {
    public:
        /// @brief Construct with port and io_context.
        ServerAsync(uint16_t port, asio::io_context &io_ctx);

        /// @brief Asynchronously start accepting connections.
        asio::awaitable<std::expected<void, std::error_code>> listen();
        /// @brief Stop accepting connections.
        void stop();

    private:
    };

}

#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>

#include "ServerBase.h"

#include <expected>
#include <memory>

namespace Network
{

    class TcpSocket;

    /// @brief Synchronous server implementation.
    /// Uses blocking accept and client handling.
    class ServerSync : public ServerBase
    {
    public:
        /// @brief Construct with port and io_context.
        explicit ServerSync(uint16_t port, asio::io_context &io_ctx);

        /// @brief Start accepting connections.
        std::expected<void, std::error_code> listen();
        /// @brief Stop accepting connections.
        void stop();

    private:
    };

}

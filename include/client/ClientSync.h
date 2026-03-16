#pragma once

#include "ClientBase.h"

#include <expected>
#include <memory>

namespace Network
{

    class TcpSocket;

    /// @brief Synchronous client implementation.
    /// Provides blocking connection establishment.
    class ClientSync : public ClientBase
    {
    public:
        /// @brief Construct with host, port, and io_context.
        explicit ClientSync(std::string_view host, uint16_t port, asio::io_context &io_ctx);

        /// @brief Connect to the remote endpoint.
        /// @param opts Connection options including timeout.
        /// @return Socket on success, or error code on failure.
        std::expected<std::unique_ptr<TcpSocket>, std::error_code> connect(Options opts);

    private:
    };

}
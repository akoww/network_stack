#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>

#include "ClientBase.h"

#include <expected>

namespace Network
{

    class TcpSocket;

    /// @brief Asynchronous client implementation.
    /// Provides coroutine-based async connection establishment.
    class ClientAsync : public ClientBase
    {
    public:
        /// @brief Construct with host, port, and io_context.
        ClientAsync(std::string_view host, uint16_t port, asio::io_context &io_ctx);

        /// @brief Asynchronously connect to the remote endpoint.
        /// @param opts Connection options including timeout.
        /// @return Socket on success, or error code on failure.
        asio::awaitable<std::expected<std::unique_ptr<TcpSocket>, std::error_code>> connect(Options opts);
    };

}

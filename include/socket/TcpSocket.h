#pragma once

#include "AsyncInterface.h"
#include "SyncInterface.h"

#include <asio/ip/tcp.hpp>
#include <asio/ip/basic_resolver.hpp>

namespace Network
{

    /// @brief TCP socket implementation using ASIO.
    /// Supports both synchronous and asynchronous operations.
    class TcpSocket : public SyncSocket, public AsyncSocket
    {
    private:
        asio::ip::tcp::socket socket_;

    public:
        /// @brief Construct with an io_context.
        explicit TcpSocket(asio::io_context &io_ctx);

        /// @brief Construct by moving in an existing socket.
        explicit TcpSocket(asio::ip::tcp::socket &&sock);

        ~TcpSocket();

        bool is_connected() const noexcept override;

        /// @brief Asynchronously send data.
        asio::awaitable<std::expected<std::size_t, std::error_code>>
        async_send(std::span<const std::byte> buffer) override;

        /// @brief Asynchronously receive data.
        asio::awaitable<std::expected<std::size_t, std::error_code>>
        async_receive(std::span<std::byte> buffer) override;

        /// @brief Synchronously send data.
        std::expected<std::size_t, std::error_code>
        send(std::span<const std::byte> buffer) override;

        /// @brief Synchronously receive data.
        std::expected<std::size_t, std::error_code>
        receive(std::span<std::byte> buffer) override;
    };
}
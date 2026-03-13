#pragma once

#include "AsyncInterface.h"
#include "SyncInterface.h"

#include <asio/ip/tcp.hpp>

namespace Network
{

    class AsioTcpSocket : public SyncSocket, public AsyncSocket
    {
    private:
        asio::ip::tcp::socket socket_;

    public:

        // Constructor taking io_context
        explicit AsioTcpSocket(asio::io_context& io_ctx);

        // Constructor taking an rvalue socket (move)
        explicit AsioTcpSocket(asio::ip::tcp::socket&& sock);

        ~AsioTcpSocket();

        bool is_connected() const noexcept override;

        // async
        asio::awaitable<std::expected<std::size_t, std::error_code>>
        async_send(std::span<const std::byte> buffer) override;

        asio::awaitable<std::expected<std::size_t, std::error_code>>
        async_receive(std::span<std::byte> buffer) override;

        // sync
        std::expected<std::size_t, std::error_code>
        send(std::span<const std::byte> buffer) override;

        std::expected<std::size_t, std::error_code>
        receive(std::span<std::byte> buffer) override;
    };
}
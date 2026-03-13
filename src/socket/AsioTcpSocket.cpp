#include "socket/AsioTcpSocket.h"

#include <system_error>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

namespace Network
{

    // Constructor taking io_context
    AsioTcpSocket::AsioTcpSocket(asio::io_context &io_ctx)
        : socket_(io_ctx) // initialize the asio socket
    {
    }

    // Constructor taking an rvalue socket (move)
    AsioTcpSocket::AsioTcpSocket(asio::ip::tcp::socket &&sock)
        : socket_(std::move(sock)) // move into member
    {
    }

    AsioTcpSocket::~AsioTcpSocket()
    {
        // Destructor: close the socket if needed
        if (socket_.is_open())
        {
            std::error_code ec;
            socket_.close(ec);
        }
    }

    bool AsioTcpSocket::is_connected() const noexcept
    {
        return false; // Dummy: not implemented yet
    }

    // -------------------- Async --------------------

    asio::awaitable<std::expected<std::size_t, std::error_code>>
    AsioTcpSocket::async_send(std::span<const std::byte> /*buffer*/)
    {
        co_return std::unexpected(std::make_error_code(std::errc::function_not_supported));
    }

    asio::awaitable<std::expected<std::size_t, std::error_code>>
    AsioTcpSocket::async_receive(std::span<std::byte> /*buffer*/)
    {
        co_return std::unexpected(std::make_error_code(std::errc::function_not_supported));
    }

    // -------------------- Sync --------------------

    std::expected<std::size_t, std::error_code>
    AsioTcpSocket::send(std::span<const std::byte> /*buffer*/)
    {
        return std::unexpected(std::make_error_code(std::errc::function_not_supported));
    }

    std::expected<std::size_t, std::error_code>
    AsioTcpSocket::receive(std::span<std::byte> /*buffer*/)
    {
        return std::unexpected(std::make_error_code(std::errc::function_not_supported));
    }

} // namespace Network
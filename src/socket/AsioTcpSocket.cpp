#include "socket/AsioTcpSocket.h"

#include <system_error>
#include <asio/buffer.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/error.hpp>
#include <asio/redirect_error.hpp>

namespace Network
{

    // Constructor taking io_context
    AsioTcpSocket::AsioTcpSocket(asio::io_context& io_ctx)
        : socket_(io_ctx)
    {
    }

    // Constructor taking an rvalue socket (move)
    AsioTcpSocket::AsioTcpSocket(asio::ip::tcp::socket&& sock)
        : socket_(std::move(sock))
    {
    }

    AsioTcpSocket::~AsioTcpSocket()
    {
        if (socket_.is_open()) {
            std::error_code ec;
            socket_.close(ec);
        }
    }

    bool AsioTcpSocket::is_connected() const noexcept
    {
        return socket_.is_open();
    }

    asio::awaitable<std::expected<std::size_t, std::error_code>>
    AsioTcpSocket::async_send(std::span<const std::byte> buffer)
    {
        std::error_code ec;
        std::size_t bytes_sent = co_await socket_.async_send(
            asio::buffer(buffer),
            asio::redirect_error(asio::use_awaitable, ec)
        );
        if (ec) {
            co_return std::unexpected(ec);
        }
        co_return bytes_sent;
    }

    asio::awaitable<std::expected<std::size_t, std::error_code>>
    AsioTcpSocket::async_receive(std::span<std::byte> buffer)
    {
        std::error_code ec;
        std::size_t bytes_received = co_await socket_.async_receive(
            asio::buffer(buffer),
            asio::redirect_error(asio::use_awaitable, ec)
        );
        if (ec) {
            co_return std::unexpected(ec);
        }
        co_return bytes_received;
    }

    std::expected<std::size_t, std::error_code>
    AsioTcpSocket::send(std::span<const std::byte> buffer)
    {
        std::error_code ec;
        std::size_t bytes_sent = socket_.send(asio::buffer(buffer), 0, ec);
        if (ec) {
            return std::unexpected(ec);
        }
        return bytes_sent;
    }

    std::expected<std::size_t, std::error_code>
    AsioTcpSocket::receive(std::span<std::byte> buffer)
    {
        std::error_code ec;
        std::size_t bytes_received = socket_.receive(asio::buffer(buffer), 0, ec);
        if (ec) {
            return std::unexpected(ec);
        }
        return bytes_received;
    }

} // namespace Network

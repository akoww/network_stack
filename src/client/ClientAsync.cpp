#include "client/ClientAsync.h"
#include "socket/TcpSocket.h"

#include <asio/ip/tcp.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/error.hpp>
#include <asio/redirect_error.hpp>
#include <asio/connect.hpp>
#include <memory>
#include <system_error>

namespace Network
{

    ClientAsync::ClientAsync(std::string_view host, uint16_t port, asio::io_context &io_ctx)
        : ClientBase(host, port, io_ctx)
    {
    }

    asio::awaitable<std::expected<std::unique_ptr<TcpSocket>, std::error_code>> ClientAsync::connect(Options opts)
    {
        std::error_code ec;

        asio::ip::tcp::resolver resolver(get_io_context());

        auto endpoints = co_await resolver.async_resolve(
            host(),
            std::to_string(port()),
            asio::redirect_error(asio::use_awaitable, ec));

        if (ec)
        {
            co_return std::unexpected(ec);
        }

        asio::ip::tcp::socket socket(get_io_context());

        co_await asio::async_connect(socket, endpoints, asio::redirect_error(asio::use_awaitable, ec));

        if (ec)
        {
            co_return std::unexpected(ec);
        }

        co_return std::make_unique<TcpSocket>(std::move(socket));
    }

}

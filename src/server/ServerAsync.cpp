#include "server/ServerAsync.h"
#include "socket/AsioTcpSocket.h"

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

namespace Network {

ServerAsync::ServerAsync(uint16_t port, asio::io_context& io_ctx)
    : ServerBase(port, io_ctx)
{
}

void ServerAsync::handle_client(std::unique_ptr<AsioTcpSocket> socket)
{
}

asio::awaitable<std::expected<void, std::error_code>> ServerAsync::listen()
{
    std::error_code ec;

    asio::ip::tcp::acceptor acceptor(get_io_context());
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

    acceptor.open(endpoint.protocol(), ec);
    if (ec) {
        co_return std::unexpected(ec);
    }

    acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        co_return std::unexpected(ec);
    }

    acceptor.bind(endpoint, ec);
    if (ec) {
        co_return std::unexpected(ec);
    }

    acceptor.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) {
        co_return std::unexpected(ec);
    }

    asio::ip::tcp::socket socket(get_io_context());

    while (true) {
        ec = {};
        co_await acceptor.async_accept(socket, asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
            co_return std::unexpected(ec);
        }

        auto new_socket = std::make_unique<AsioTcpSocket>(std::move(socket));

        asio::co_spawn(
            get_io_context(),
            [this, socket = std::move(new_socket)]() mutable -> asio::awaitable<void> {
                handle_client(std::move(socket));
                co_return;
            },
            asio::detached
        );

        socket = asio::ip::tcp::socket(get_io_context());
    }
}

}

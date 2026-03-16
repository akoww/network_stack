#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

#include <asio/ip/tcp.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/error.hpp>
#include <asio/connect.hpp>
#include <memory>
#include <system_error>

namespace Network
{

    ServerSync::ServerSync(uint16_t port, asio::io_context &io_ctx)
        : ServerBase(port, io_ctx)
    {
    }

    std::expected<void, std::error_code> ServerSync::listen()
    {
        std::error_code ec;

        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

        _acceptor.open(endpoint.protocol(), ec);
        if (ec)
        {
            return std::unexpected(ec);
        }

        _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec)
        {
            return std::unexpected(ec);
        }

        _acceptor.bind(endpoint, ec);
        if (ec)
        {
            return std::unexpected(ec);
        }

        _acceptor.listen(asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            return std::unexpected(ec);
        }

        asio::ip::tcp::socket socket(get_io_context());

        while (!_stop_requested.load())
        {
            ec = {};
            _acceptor.accept(socket, ec);

            if (ec)
            {
                if (_stop_requested.load())
                {
                    return {};
                }
                return std::unexpected(ec);
            }

            auto new_socket = std::make_unique<TcpSocket>(std::move(socket));

            handle_client(std::move(new_socket));

            socket = asio::ip::tcp::socket(get_io_context());
        }

        return {};
    }

    void ServerSync::stop()
    {
        ServerBase::stop();
        std::error_code ec;
        _acceptor.cancel(ec);
    }

}

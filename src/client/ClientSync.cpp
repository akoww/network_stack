#include "client/ClientSync.h"
#include "socket/TcpSocket.h"

#include <asio/ip/tcp.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <memory>
#include <system_error>

#include "socket/TcpSocket.h"

namespace Network
{

    ClientSync::ClientSync(std::string_view host, uint16_t port, asio::io_context &io_ctx)
        : ClientBase(host, port, io_ctx)
    {
    }

    std::expected<std::unique_ptr<TcpSocket>, std::error_code> ClientSync::connect(Options /*opts*/)
    {
        std::error_code ec;

        asio::ip::tcp::resolver resolver(get_io_context());

        auto endpoints = resolver.resolve(host(), std::to_string(port()), ec);

        if (ec)
        {
            return std::unexpected(ec);
        }

        asio::ip::tcp::socket socket(get_io_context());

        asio::connect(socket, endpoints, ec);

        if (ec)
        {
            return std::unexpected(ec);
        }

        return std::make_unique<TcpSocket>(std::move(socket));
    }

}
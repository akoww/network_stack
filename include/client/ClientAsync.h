#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>

#include "ClientBase.h"

#include "socket/AsioTcpSocket.h"

#include <expected>

namespace Network
{

class ClientAsync : public ClientBase
{
    public:

    ClientAsync(std::string_view host, uint16_t port, asio::io_context& io_ctx);

    asio::awaitable<std::expected<std::unique_ptr<AsioTcpSocket>, std::error_code>> connect(Options opts);

};

}

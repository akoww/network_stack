#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>
#include <asio/awaitable.hpp>

#include "ServerBase.h"

#include "socket/AsioTcpSocket.h"

#include <expected>

namespace Network {

class AsioTcpSocket;

class ServerAsync : public ServerBase {
public:
    ServerAsync(uint16_t port, asio::io_context& io_ctx);

    asio::awaitable<std::expected<void, std::error_code>> listen();
    void stop();

private:
    void handle_client(std::unique_ptr<AsioTcpSocket> socket);
};

}

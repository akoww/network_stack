#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/io_context.hpp>

#include "ServerBase.h"

#include "socket/AsioTcpSocket.h"

#include <expected>
#include <memory>

namespace Network {

class AsioTcpSocket;

class ServerSync : public ServerBase {
public:
    explicit ServerSync(uint16_t port, asio::io_context& io_ctx);

    std::expected<void, std::error_code> listen();
    void stop();

private:
    void handle_client(std::unique_ptr<AsioTcpSocket> socket);
};

}

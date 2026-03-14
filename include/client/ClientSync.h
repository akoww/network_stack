#pragma once

#include "ClientBase.h"
#include "socket/AsioTcpSocket.h"

#include <expected>
#include <memory>

namespace Network {

class ClientSync : public ClientBase
{
public:
    explicit ClientSync(std::string_view host, uint16_t port, asio::io_context& io_ctx);

    std::expected<std::unique_ptr<AsioTcpSocket>, std::error_code> connect(Options opts);

private:
};

}
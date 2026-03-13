#pragma once

#include <asio/io_context.hpp>

namespace Network {

class AsioTcpSocket;

class ServerBase {
public:
    explicit ServerBase(uint16_t port, asio::io_context& io_ctx);

    std::string_view host() const;
    uint16_t port() const;
    asio::io_context& get_io_context();

    virtual void handle_client(std::unique_ptr<AsioTcpSocket> socket) = 0;

private:
    std::string _host;
    uint16_t _port;
    asio::io_context& _io_ctx;
};

}

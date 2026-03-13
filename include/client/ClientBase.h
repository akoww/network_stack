#pragma once

#include <string>
#include <stdint.h>
#include <chrono>
#include <asio/io_context.hpp>

namespace Network
{

    class ClientBase
    {
    public:
        struct Options
        {
            std::chrono::milliseconds timeout;
        };

        explicit ClientBase(std::string_view host, uint16_t port, asio::io_context& io_ctx);

        std::string_view host() const;
        uint16_t port() const;
        asio::io_context& get_io_context();

    private:
        std::string _host;
        uint16_t _port;
        asio::io_context& _io_ctx;
    };

}

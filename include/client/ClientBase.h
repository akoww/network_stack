#pragma once

#include <string>
#include <stdint.h>
#include <chrono>

namespace Network
{

    class ClientBase
    {
    public:
        struct Options
        {
            std::chrono::milliseconds timeout;
        };

        explicit ClientBase(std::string_view host, uint16_t port);

        std::string_view host() const;
        uint16_t port() const;

    private:
        std::string _host;
        uint16_t _port;
    };

}
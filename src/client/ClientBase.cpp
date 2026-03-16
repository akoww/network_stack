
#include "client/ClientBase.h"

#include <asio/io_context.hpp>

namespace Network
{

    ClientBase::ClientBase(std::string_view host, uint16_t port, asio::io_context &io_ctx)
        : _host(std::string(host)), _port(port), _io_ctx(io_ctx)
    {
    }

    std::string_view ClientBase::host() const { return _host; }
    uint16_t ClientBase::port() const { return _port; }
    asio::io_context &ClientBase::get_io_context() { return _io_ctx; }

}

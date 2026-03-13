#include "server/ServerBase.h"

#include <asio/io_context.hpp>

namespace Network {

ServerBase::ServerBase(uint16_t port, asio::io_context& io_ctx)
    : _host("0.0.0.0"), _port(port), _io_ctx(io_ctx)
{
}

std::string_view ServerBase::host() const { return _host; }
uint16_t ServerBase::port() const { return _port; }
asio::io_context& ServerBase::get_io_context() { return _io_ctx; }

}

#include "client/ClientBase.h"

namespace Network
{

    ClientBase::ClientBase(std::string_view host, uint16_t port) : _host(std::string(host)), _port(port) {}

    std::string_view ClientBase::host() const { return _host; }
    uint16_t ClientBase::port() const { return _port; }

}

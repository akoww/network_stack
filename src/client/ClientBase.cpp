
#include "client/ClientBase.h"
#include <spdlog/spdlog.h>

namespace Network
{

ClientBase::ClientBase(std::string_view host, uint16_t port, IoContextWrapper io_ctx)
  : _host(std::string(host)), _port(port), _io_ctx(std::move(io_ctx))
{
  spdlog::trace("ClientBase created for {}:{}", host, port);
}

std::string_view ClientBase::host() const
{
  return _host;
}
uint16_t ClientBase::port() const
{
  return _port;
}

}  // namespace Network

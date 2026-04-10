
#include "client/ClientBase.h"
#include <asio/ssl/context.hpp>
#include <spdlog/spdlog.h>

namespace Network
{

ClientBase::ClientBase(std::string_view host, uint16_t port, asio::io_context& io_ctx)
  : _host(std::string(host)), _port(port), _io_ctx(io_ctx),
    _ssl_context(std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_client))
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
asio::io_context& ClientBase::getIoContext()
{
  return _io_ctx;
}
std::shared_ptr<asio::ssl::context> ClientBase::getSslContext()
{
  return _ssl_context;
}

}  // namespace Network

#include "server/ServerBase.h"
#include <asio/ssl/context.hpp>
#include <spdlog/spdlog.h>

namespace Network
{

ServerBase::ServerBase(uint16_t port, asio::io_context& io_ctx, ClientHandler handler)
  : _acceptor(io_ctx), _host("0.0.0.0"), _port(port), _io_ctx(io_ctx), _handler(std::move(handler)),
    _ssl_context(std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_server))
{
}

ServerBase::ClientHandler ServerBase::clientHandler()
{
  return _handler;
}

std::string_view ServerBase::host() const
{
  return _host;
}
uint16_t ServerBase::port() const
{
  return _port;
}
asio::io_context& ServerBase::get_io_context()
{
  return _io_ctx;
}
std::shared_ptr<asio::ssl::context> ServerBase::get_ssl_context()
{
  return _ssl_context;
}

void ServerBase::stop()
{
  spdlog::info("server stopping...");
  _stop_requested.store(true);
}

bool ServerBase::is_stopped() const noexcept
{
  return _stop_requested.load();
}

}  // namespace Network

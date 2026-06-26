#include "server/ServerBase.h"
#include <spdlog/spdlog.h>
#include <asio/ssl/context.hpp>
#include "core/details/ContextDetail.h"
#include "socket/TlsOptions.h"
#include "server/details/ServerBaseDetail.h"
#include "socket/TlsSocket.h"

namespace Network
{

ServerBase::ServerBase(uint16_t port, IoContextWrapper io_ctx, ClientHandler handler)
  : _p(std::make_unique<Private>(detail::getExecutor(io_ctx))), _host("0.0.0.0"), _port(port), _io_ctx(io_ctx),
    _handler(std::move(handler))
{
  spdlog::trace("ServerBase created on port {}", port);
}

ServerBase::~ServerBase() = default;

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

void ServerBase::stop()
{
  spdlog::trace("closing server...");
  _stop_requested.store(true);
  std::error_code ec;
  ServerAccess::getAcceptor(*this).cancel(ec);
  if (ec)
  {
    spdlog::warn("acceptor cancel error: {}", ec.message());
  }
  spdlog::trace("server closed");
}

bool ServerBase::isStopped() const noexcept
{
  return _stop_requested.load();
}

}  // namespace Network

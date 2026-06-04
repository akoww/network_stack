#include "server/ServerBase.h"
#include <asio/ssl/context.hpp>
#include <spdlog/spdlog.h>

namespace Network
{

ServerBase::ServerBase(uint16_t port, asio::io_context& io_ctx, ClientHandler handler)
  : _acceptor(io_ctx), _host("0.0.0.0"), _port(port), _io_ctx(io_ctx), _handler(std::move(handler)),
    _ssl_context(std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_server))
{
  spdlog::trace("ServerBase created on port {}", port);
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
asio::io_context& ServerBase::getIoContext()
{
  return _io_ctx;
}

std::error_code ServerBase::setCertificateChain(std::filesystem::path const& path)
{
  std::error_code ec;
  _ssl_context->use_certificate_chain_file(path.string(), ec);
  return ec;
}

std::error_code ServerBase::setPrivateKey(std::filesystem::path const& path)
{
  std::error_code ec;
  _ssl_context->use_private_key_file(path.string(), asio::ssl::context::file_format::pem, ec);
  return ec;
}
void ServerBase::stop()
{
  spdlog::trace("closing server...");
  _stop_requested.store(true);
  std::error_code ec;
  _acceptor.cancel(ec);
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

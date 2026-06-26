#pragma once

#include <asio/ip/tcp.hpp>

namespace Network
{

struct ServerAccess;

struct ServerBase::Private
{
  explicit Private(asio::any_io_executor exec) : acceptor(exec) {}

  asio::ip::tcp::acceptor acceptor;
};

struct ServerAccess
{
  static auto& getAcceptor(ServerBase& server) { return server._p->acceptor; }
  static const auto& getAcceptor(const ServerBase& server) { return server._p->acceptor; }
};

inline auto& getAcceptor(ServerBase& server)
{
  return ServerAccess::getAcceptor(server);
}

inline const auto& getAcceptor(const ServerBase& server)
{
  return ServerAccess::getAcceptor(server);
}

}  // namespace Network

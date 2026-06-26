#pragma once

#include "../TcpSocket.h"
#include <asio/ip/tcp.hpp>

namespace Network
{

struct TcpSocket::Private
{
  explicit Private(asio::ip::tcp::socket socket) : socket(std::move(socket)) {}

  asio::ip::tcp::socket socket;
};

namespace detail
{

struct TcpSocketAccess
{
  static asio::ip::tcp::socket& getSocket(TcpSocket& s) { return s._p->socket; }
  static const asio::ip::tcp::socket& getSocket(const TcpSocket& s) { return s._p->socket; }
};

inline asio::ip::tcp::socket& getSocket(TcpSocket& s)
{
  return TcpSocketAccess::getSocket(s);
}

inline const asio::ip::tcp::socket& getSocket(const TcpSocket& s)
{
  return TcpSocketAccess::getSocket(s);
}

}  // namespace detail
}  // namespace Network

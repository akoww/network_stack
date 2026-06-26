#pragma once

#include <asio/ip/tcp.hpp>
#include <asio/ssl/stream.hpp>

#include "../TlsSocket.h"

namespace Network
{

struct TlsSocketAccess;

struct TlsSocket::Private
{
  explicit Private(asio::ssl::stream<asio::ip::tcp::socket> stream) : stream(std::move(stream)) {}

  asio::ssl::stream<asio::ip::tcp::socket> stream;
};

namespace detail
{

struct TlsSocketAccess
{
  static asio::ssl::stream<asio::ip::tcp::socket>& getSocket(TlsSocket& s) { return s._p->stream; }
  static const asio::ssl::stream<asio::ip::tcp::socket>& getSocket(const TlsSocket& s) { return s._p->stream; }
};

inline asio::ssl::stream<asio::ip::tcp::socket>& getSocket(TlsSocket& s)
{
  return TlsSocketAccess::getSocket(s);
}
inline const asio::ssl::stream<asio::ip::tcp::socket>& getSocket(const TlsSocket& s)
{
  return TlsSocketAccess::getSocket(s);
}

}  // namespace detail
}  // namespace Network

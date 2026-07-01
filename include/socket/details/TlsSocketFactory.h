#pragma once

#include "../SocketBase.h"
#include "TcpSocketFactory.h"  // Included to get asio::ip::tcp::socket from its includes
#include <asio/ssl/stream.hpp>

namespace Network::detail
{

struct TlsSocketFactory
{
  static std::unique_ptr<DualSocket> create(asio::ssl::stream<asio::ip::tcp::socket> stream);
};

}  // namespace Network::detail

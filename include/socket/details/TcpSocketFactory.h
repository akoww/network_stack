#pragma once

#include "../SocketBase.h"
#include <asio/ip/tcp.hpp>

namespace Network::detail
{

struct TcpSocketFactory
{
  static std::unique_ptr<DualSocket> create(asio::ip::tcp::socket sock);
};

}  // namespace Network::detail

#include "socket/details/TcpSocketFactory.h"
#include "socket/TcpSocket.h"
#include <asio/ip/tcp.hpp>

namespace Network::detail
{

std::unique_ptr<DualSocket> TcpSocketFactory::create(asio::ip::tcp::socket sock)
{
  return std::make_unique<TcpSocket>(std::move(sock));
}

}  // namespace Network::detail

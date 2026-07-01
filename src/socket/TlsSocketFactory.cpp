#include "socket/details/TlsSocketFactory.h"
#include "socket/TlsSocket.h"

namespace Network::detail
{

std::unique_ptr<DualSocket> TlsSocketFactory::create(asio::ssl::stream<asio::ip::tcp::socket> stream)
{
  return std::make_unique<TlsSocket>(std::move(stream));
}

}  // namespace Network::detail

#include "socket/SocketBase.h"

#include <atomic>
#include <spdlog/spdlog.h>

namespace Network
{

namespace
{
std::atomic<unsigned int> id_counter = 0;
}

SocketBase::SocketBase() : _id(id_counter++)
{
  spdlog::trace("[{}] socket created", _id);
}

unsigned int SocketBase::getId() const
{
  return _id;
}

void SocketBase::cancelSocket()
{
  spdlog::trace("[{}] cancelling socket", _id);
  _cancel_signal.emit(asio::cancellation_type_t::all);
}

}  // namespace Network
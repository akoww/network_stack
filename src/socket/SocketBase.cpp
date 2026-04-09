#include "socket/SocketBase.h"

#include <atomic>

namespace Network
{

namespace
{
std::atomic<unsigned int> id_counter = 0;
}

SocketBase::SocketBase() : _id(id_counter++)
{
}

unsigned int SocketBase::getId() const
{
  return _id;
}

void SocketBase::cancelSocket()
{
  _cancel_signal.emit(asio::cancellation_type_t::all);
}

}  // namespace Network
#include "socket/SocketBase.h"

#include <atomic>

namespace Network
{

namespace
{
std::atomic<unsigned int> _id_counter = 0;
}

SocketBase::SocketBase() : _id(_id_counter++)
{
}

unsigned int SocketBase::getId() const
{
  return _id;
}

}  // namespace Network
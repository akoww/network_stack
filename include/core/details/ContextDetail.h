#pragma once
#include "../Context.h"

#include <asio.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>

namespace Network
{

// Define the Private structure
struct IoContextWrapper::Private
{
  explicit Private(unsigned int n) : pool_(n) {}

  asio::thread_pool pool_;
  std::once_flag shutdown_once_;
};

namespace detail
{

// Define the friend accessor struct
struct IoContextAccess
{
  // Static method that can access private members because the struct is a friend
  static asio::any_io_executor getExecutor(IoContextWrapper ctx) { return ctx._p->pool_.get_executor(); }

  // Add more accessors as needed (e.g., getIoContext, getStrand, etc.)
};

// Convenience free function that uses the accessor
inline asio::any_io_executor getExecutor(IoContextWrapper ctx)
{
  return IoContextAccess::getExecutor(ctx);
}

}  // namespace detail
}  // namespace Network
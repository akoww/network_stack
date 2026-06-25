#pragma once

#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace Network
{

namespace detail
{
struct IoContextAccess;
}

class IoContextWrapper final
{
public:
  IoContextWrapper(unsigned int thread_count = 4);
  IoContextWrapper(const IoContextWrapper&) = default;
  IoContextWrapper(IoContextWrapper&&) = default;
  IoContextWrapper& operator=(const IoContextWrapper&) = default;
  IoContextWrapper& operator=(IoContextWrapper&&) = default;
  ~IoContextWrapper() = default;

  // Optional: explicit, idempotent shutdown
  void shutdown() noexcept;

private:
  struct Private;
  std::shared_ptr<Private> _p;

  friend detail::IoContextAccess;
};

}  // namespace Network
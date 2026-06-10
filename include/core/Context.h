#pragma once

#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace Network
{

/// @brief Wrapper around asio::io_context with automatic background thread management.
/// Manages an io_context with a dedicated worker thread and work guard to keep it running.
/// Provides a singleton pattern for shared io_context access across the application.
///
/// Usage:
/// ```cpp
/// auto& ctx = IoContextWrapper::instance();
/// ctx.start();
/// // ... schedule work ...
/// ctx.stop();
/// ```
class IoContextWrapper final
{
public:
  IoContextWrapper(unsigned int thread_count = 4);
  ~IoContextWrapper();

  // Expose executor for all Asio async operations
  auto get_executor() { return pool_.get_executor(); }

private:
  IoContextWrapper(const IoContextWrapper&) = delete;
  IoContextWrapper& operator=(const IoContextWrapper&) = delete;

  asio::thread_pool pool_;  // Lazy-initialized pool
};

}  // namespace Network
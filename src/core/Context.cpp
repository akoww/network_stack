#include "core/Context.h"
#include <spdlog/spdlog.h>

namespace Network
{

IoContextWrapper::IoContextWrapper() : running_(false)
{
  spdlog::trace("start context");
}

IoContextWrapper::~IoContextWrapper()
{
  stop();
  spdlog::trace("stopped io context");
}

IoContextWrapper& IoContextWrapper::instance()
{
  static IoContextWrapper instance;
  [[maybe_unused]] static bool registered = [&]() -> bool
  {
    std::atexit([]() { instance.stop(); });
    return true;
  }();
  return instance;
}

// Start the io_context run loop in a dedicated background thread
void IoContextWrapper::start()
{
  if (running_)
  {
    return;
  }
  std::scoped_lock lock(mutex_);

  restart();
  work_guard_.emplace(this->get_executor());
  running_ = true;
  thread_ = std::thread(
    [this]()
    {
      asio::io_context::run();
      running_ = false;
    });
  spdlog::info("context started");
}

// Stop the loop and wait for the thread
void IoContextWrapper::stop()
{
  if (!running_)
  {
    return;
  }
  std::scoped_lock lock(mutex_);

  work_guard_.reset();
  // Inherit stop() from io_context
  asio::io_context::stop();

  if (thread_.joinable())
  {
    thread_.join();
  }

  // Optionally restart the context if you plan to use it again
  // this->restart();
  running_ = false;
  spdlog::info("context stopped");
}

bool IoContextWrapper::is_running() const
{
  return running_;
}

}  // namespace Network
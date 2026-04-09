#include "core/Context.h"
#include <spdlog/spdlog.h>

namespace Network
{

IoContextWrapper::IoContextWrapper() : _running(false)
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
  if (_running)
  {
    return;
  }
  std::scoped_lock lock(_mutex);

  restart();
  _work_guard.emplace(this->get_executor());
  _running = true;
  _thread = std::thread(
    [this]()
    {
      asio::io_context::run();
      _running = false;
    });
  spdlog::info("context started");
}

// Stop the loop and wait for the thread
void IoContextWrapper::stop()
{
  if (!_running)
  {
    return;
  }
  std::scoped_lock lock(_mutex);

  _work_guard.reset();
  // Inherit stop() from io_context
  asio::io_context::stop();

  if (_thread.joinable())
  {
    _thread.join();
  }

  // Optionally restart the context if you plan to use it again
  // this->restart();
  _running = false;
  spdlog::info("context stopped");
}

bool IoContextWrapper::isRunning() const
{
  return _running;
}

}  // namespace Network
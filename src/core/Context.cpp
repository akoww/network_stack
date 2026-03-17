#include "core/Context.h"

#include <iostream>

namespace Network {

IoContextWrapper::IoContextWrapper() : running_(false) {}

IoContextWrapper::~IoContextWrapper() {
  stop();
}

IoContextWrapper& IoContextWrapper::instance() {
  static IoContextWrapper instance;
  [[maybe_unused]] static bool registered = [&]() -> bool {
    std::atexit([]() { instance.stop(); });
    return true;
  }();
  return instance;
}

// Start the io_context run loop in a dedicated background thread
void IoContextWrapper::start() {
  if (running_) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);

  this->restart();
  work_guard_.emplace(this->get_executor());
  running_ = true;
  thread_ = std::thread([this]() {
    asio::io_context::run();
    running_ = false;
  });
  std::cout << "context started" << std::endl;
}

// Stop the loop and wait for the thread
void IoContextWrapper::stop() {
  if (!running_) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);

  work_guard_.reset();
  // Inherit stop() from io_context
  asio::io_context::stop();

  if (thread_.joinable()) {
    thread_.join();
  }

  // Optionally restart the context if you plan to use it again
  // this->restart();
  running_ = false;
  std::cout << "context stopped" << std::endl;
}

bool IoContextWrapper::is_running() const {
  return running_;
}

}  // namespace Network
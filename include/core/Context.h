#pragma once

#include <asio/io_context.hpp>
#include <thread>

namespace Network
{

class IoContextWrapper : public asio::io_context {
public:
    IoContextWrapper();

    ~IoContextWrapper();

    static IoContextWrapper& instance();

    // Start the io_context run loop in a dedicated background thread
    void start();

    // Stop the loop and wait for the thread
    void stop();

    bool is_running() const;

    // Inheritance allows us to access all io_context methods directly:
    // this->post(...), this->run(), this->poll(), this->stop(), etc.
    
private:
    std::mutex mutex_;
    std::thread thread_;
    std::atomic<bool> running_;

    using Guard = asio::executor_work_guard<asio::io_context::executor_type>;
    std::optional<Guard> work_guard_;
};

    
}
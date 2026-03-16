 #pragma once

 #include <asio/io_context.hpp>
 #include <thread>

 namespace Network
 {

 /// @brief Wrapper around asio::io_context with automatic background thread management.
 /// Manages an io_context with a dedicated worker thread and work guard to keep it running.
 class IoContextWrapper : public asio::io_context {
 public:
     IoContextWrapper();

     ~IoContextWrapper();

     static IoContextWrapper& instance();

     /// @brief Start the io_context run loop in a dedicated background thread.
     void start();

     /// @brief Stop the loop and wait for the thread to exit.
     void stop();

     /// @brief Check if the io_context is currently running.
     bool is_running() const;

     /// @brief Access the underlying io_context executor.
     /// Inheritance allows us to access all io_context methods directly.
     
 private:
     std::mutex mutex_;
     std::thread thread_;
     std::atomic<bool> running_;

     using Guard = asio::executor_work_guard<asio::io_context::executor_type>;
     std::optional<Guard> work_guard_;
 };

    
}
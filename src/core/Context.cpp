#include <spdlog/spdlog.h>

#include "core/Context.h"


namespace Network
{

    IoContextWrapper::IoContextWrapper() : running_(false), work_guard_(this->get_executor()) {}

    IoContextWrapper::~IoContextWrapper() {
        stop();
    }

    IoContextWrapper& IoContextWrapper::instance() {
        static IoContextWrapper instance;
        return instance;
    }

    // Start the io_context run loop in a dedicated background thread
    void IoContextWrapper::start() {
        if (running_) {
            return; 
        }

        running_ = true;
        thread_ = std::thread([this]() {
            try {
                // Inherit run() from io_context
                this->run();
            } catch (const std::exception& e) {
                // Handle exception if necessary
                spdlog::error("failed to run io_context");
            }
            running_ = false;
        });
    }

    // Stop the loop and wait for the thread
    void IoContextWrapper::stop() {
        if (!running_) {
            return;
        }   

        work_guard_.reset();
        // Inherit stop() from io_context
        asio::io_context::stop(); 
        
        if (thread_.joinable()) {
            thread_.join();
        }
        
        // Optionally restart the context if you plan to use it again
        // this->restart(); 
        running_ = false;
    }

    bool IoContextWrapper::is_running() const {
        return running_;
    }


}
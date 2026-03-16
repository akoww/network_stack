#pragma once

#include <string>
#include <stdint.h>
#include <chrono>
#include <asio/io_context.hpp>

namespace Network
{

    /// @brief Base class for client implementations.
    /// Provides common configuration and accessors.
    class ClientBase
    {
    public:
        /// @brief Client configuration options.
        struct Options
        {
            std::chrono::milliseconds timeout;
        };

        /// @brief Construct with host and port.
        explicit ClientBase(std::string_view host, uint16_t port, asio::io_context &io_ctx);

        /// @brief Get the target host.
        std::string_view host() const;
        /// @brief Get the target port.
        uint16_t port() const;
        /// @brief Get the io_context reference.
        asio::io_context &get_io_context();

    private:
        std::string _host;
        uint16_t _port;
        asio::io_context &_io_ctx;
    };

}

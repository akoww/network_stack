#pragma once


#include <asio/awaitable.hpp>
#include <expected>
#include <span>
#include <system_error>
#include <cstddef>

#include "BaseInterface.h"

namespace Network
{

class AsyncSocket : public SocketBase {
public:
    virtual ~AsyncSocket() = default;

    virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
    async_send(std::span<const std::byte> buffer) = 0;

    virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
    async_receive(std::span<std::byte> buffer) = 0;
};

}
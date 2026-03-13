#pragma once

#include <expected>
#include <span>
#include <cstddef>
#include <system_error>
#include <cstddef>

#include "BaseInterface.h"

namespace Network
{

class SyncSocket : public SocketBase {
public:
    virtual ~SyncSocket() = default;

    virtual std::expected<std::size_t, std::error_code>
    send(std::span<const std::byte> buffer) = 0;

    virtual std::expected<std::size_t, std::error_code>
    receive(std::span<std::byte> buffer) = 0;
};

}
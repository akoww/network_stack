#pragma once

#include <expected>

namespace Network
{

class SocketBase {
public:
    virtual ~SocketBase() = default;

    [[nodiscard]]
    virtual bool is_connected() const noexcept = 0;
};

}
#include <gtest/gtest.h>
#include "socket/AsioTcpSocket.h"

#include <asio.hpp>

namespace Network::Test
{


    TEST(AsioTcpSocketTest, MinimalConstructor)
    {
        asio::io_context io_ctx;

        // Construct the socket
        AsioTcpSocket socket(io_ctx);

        // Basic check: initially not connected
        EXPECT_FALSE(socket.is_connected());
    }

} // namespace Network

#include "socket/TcpSocket.h"
#include <gtest/gtest.h>

namespace Network::Test
{

    TEST(AsioTcpSocketTest, MinimalConstructor)
    {
        asio::io_context io_ctx;

        // Construct the socket
        TcpSocket socket(io_ctx);

        // Basic check: initially not connected
        EXPECT_FALSE(socket.is_connected());
    }

} // namespace Network

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <asio.hpp>

#include "client/ClientSync.h"
#include "server/ServerSync.h"
#include "socket/AsioTcpSocket.h"
#include "test_fixture_io_context.h"

namespace Network::Test {

TEST_F(IoContextFixture, MinimalConstructor) {
    ClientSync client("127.0.0.1", 12345, get_io_context());
    
    EXPECT_EQ(client.host(), "127.0.0.1");
    EXPECT_EQ(client.port(), 12345);
    EXPECT_EQ(&client.get_io_context(), &get_io_context());
}

TEST_F(IoContextFixture, MinimalConstructorServer) {
    ServerSync server(12345, get_io_context());
    
    EXPECT_EQ(server.host(), "0.0.0.0");
    EXPECT_EQ(server.port(), 12345);
    EXPECT_EQ(&server.get_io_context(), &get_io_context());
}

TEST_F(IoContextFixture, ListenAndServe) {
     ServerSync server(12346, get_io_context());
     
     std::thread server_thread([&server]() {
         auto listen_result = server.listen();
         EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
     });
     
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     ClientSync client("127.0.0.1", server.port(), get_io_context());
     auto connect_result = client.connect({});
     ASSERT_TRUE(connect_result.has_value()) << "Client connect failed: " << connect_result.error().message();
     
     server.stop();
     server_thread.join();
 }

TEST_F(IoContextFixture, StopBeforeListen) {
    ServerSync server(12346, get_io_context());
    
    server.stop();
    
    EXPECT_TRUE(server.is_stopped());
}


TEST_F(IoContextFixture, MultipleConnectionsSequential) {
     ServerSync server(12346, get_io_context());
     
     std::thread server_thread([&server]() {
         auto listen_result = server.listen();
         EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
     });
     
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     {
         ClientSync client("127.0.0.1", server.port(), get_io_context());
         auto connect_result = client.connect({});
         ASSERT_TRUE(connect_result.has_value()) << "Client connect failed";
     }
     
     server.stop();
     server_thread.join();
 }

} // namespace Network::Test

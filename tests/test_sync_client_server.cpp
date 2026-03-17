#include <gtest/gtest.h>
#include <asio.hpp>
#include <chrono>
#include <thread>

#include "client/ClientSync.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

namespace Network::Test {

TEST_F(IoContextFixture, MinimalConstructor) {
  ClientSync client("127.0.0.1", 12345, get_io_context());

  EXPECT_EQ(client.host(), "127.0.0.1");
  EXPECT_EQ(client.port(), 12345);
  EXPECT_EQ(&client.get_io_context(), &get_io_context());
}

TEST_F(IoContextFixture, MinimalConstructorServer) {
  TestSyncServer server(12345, get_io_context());

  EXPECT_EQ(server.host(), "0.0.0.0");
  EXPECT_EQ(server.port(), 12345);
  EXPECT_EQ(&server.get_io_context(), &get_io_context());
}

TEST_F(IoContextFixture, ListenAndServe) {
  TestSyncServer server(12346, get_io_context());

  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    std::cout << "listening end" << std::endl;
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value())
      << "Client connect failed: " << connect_result.error().message();

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, MultipleConnectionsSequential) {
  TestSyncServer server(12346, get_io_context());

  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  {
    ClientSync client("127.0.0.1", server.port(), get_io_context());
    auto connect_result = client.connect({});
    ASSERT_TRUE(connect_result.has_value()) << "Client connect failed";
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  server.stop();
  server_thread.join();
}

//-----------------------------------------------------------------------------

std::span<const std::byte> to_bytes(std::string_view sv) {
  return std::as_bytes(std::span(sv.data(), sv.size()));
}

std::string_view to_string_view(std::span<const std::byte> bytes,
                                std::size_t length) {
  if (length >= bytes.size())
    return "";
  return {reinterpret_cast<const char*>(bytes.data()), length};
}

TEST_F(SyncClientServerFixture, SimpleSendReceive) {
  ASSERT_TRUE(start_server());
  ASSERT_TRUE(connect_client());

  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  ASSERT_EQ(get_server_sockets().size(), 1);

  auto& server_socket = get_server_sockets().front();
  const auto& client_socket = get_client_socket();

  constexpr std::string_view msg = "hallo world";

  auto send_result = server_socket->send(to_bytes(msg));
  EXPECT_TRUE(send_result);

  std::array<std::byte, 1024> answer{};
  auto recv_result = client_socket->receive(std::span(answer));
  EXPECT_TRUE(recv_result);

  auto answer_str = to_string_view(answer, *recv_result);
  EXPECT_EQ(msg, answer_str);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

}  // namespace Network::Test

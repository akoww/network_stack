#include <array>
#include <asio.hpp>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <thread>

#include "client/ClientSync.h"
#include "core/ErrorCodes.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

constexpr uint16_t TEST_PORT = 12346;

TEST_F(IoContextFixture, SyncWriteTimeout)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < data.size(); i++)
  {
    data[i] = static_cast<std::byte>(i % 256);
  }

  auto send_result = client_socket->writeAll(std::span(data), std::chrono::milliseconds(100));
  EXPECT_TRUE(send_result);
  if (send_result)
  {
    EXPECT_GT(*send_result, 0);
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SyncReadTimeout)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer), std::chrono::milliseconds(100));

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(), make_error_code(Network::Error::CONNECTION_TIMEOUT));

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SyncReadExactTimeout)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> buffer(1024);
  auto recv_result = client_socket->readExact(std::span(buffer), std::chrono::milliseconds(100));

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(), make_error_code(Network::Error::CONNECTION_TIMEOUT));

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SyncReadUntilTimeout)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> buffer(1024);
  auto recv_result = client_socket->readUntil(std::span(buffer), "\n", std::chrono::milliseconds(100));

  EXPECT_FALSE(recv_result.has_value());
  EXPECT_EQ(recv_result.error(), make_error_code(Network::Error::CONNECTION_TIMEOUT));

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, SyncNoTimeout)
{
  EchoServer server(TEST_PORT, getIoContext());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), getIoContext());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  const std::string msg = "hello";
  auto send_result = client_socket->writeAll(to_bytes(msg));
  EXPECT_TRUE(send_result);

  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer), std::chrono::milliseconds(1000));
  EXPECT_TRUE(recv_result);
  if (recv_result)
  {
    auto response = to_string_view(buffer, *recv_result);
    EXPECT_EQ(msg, response);
  }

  server.stop();
  server_thread.join();
}

}  // namespace Network::Test

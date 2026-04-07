#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <thread>

#include "client/ClientSync.h"
#include "fixtures/test_fixture_io_context.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

constexpr uint16_t TEST_PORT = 12346;

TEST_F(IoContextFixture, MinimalConstructor)
{
  ClientSync client("127.0.0.1", TEST_PORT, get_io_context());
  EXPECT_EQ(client.host(), "127.0.0.1");
  EXPECT_EQ(client.port(), TEST_PORT);
  EXPECT_EQ(&client.get_io_context(), &get_io_context());
}

TEST_F(IoContextFixture, MinimalConstructorServer)
{
  EchoServer server(TEST_PORT, get_io_context());
  EXPECT_EQ(server.host(), "0.0.0.0");
  EXPECT_EQ(server.port(), TEST_PORT);
  EXPECT_EQ(&server.get_io_context(), &get_io_context());
}

TEST_F(IoContextFixture, MultipleClientsConcurrent)
{
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto client_thread_func = [](uint16_t port, asio::io_context& io_ctx)
  {
    ClientSync client("127.0.0.1", port, io_ctx);
    EXPECT_TRUE(client.connect({}).has_value()) << "Cant connect to server";
  };
  std::thread client1(client_thread_func, server.port(), std::ref(get_io_context()));
  std::thread client2(client_thread_func, server.port(), std::ref(get_io_context()));
  std::thread client3(client_thread_func, server.port(), std::ref(get_io_context()));
  client1.join();
  client2.join();
  client3.join();

  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, EchoServerMultipleMessages)
{
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", TEST_PORT, _io_ctx);
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Cant connect client";

  auto client_socket = std::move(*connect_result);
  const std::vector<std::string> messages = {"hello", "world", "test"};
  for (const auto& msg : messages)
  {
    auto send_result = client_socket->writeAll(to_bytes(msg));
    EXPECT_TRUE(send_result);
    std::array<std::byte, 1024> buffer{};
    auto recv_result = client_socket->readSome(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, EchoServerConcurrentClients)
{
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::unique_ptr<SyncSocket>> sockets;
  for (int i = 0; i < 3; i++)
  {
    ClientSync client("127.0.0.1", server.port(), _io_ctx);
    auto connect_result = client.connect({});
    if (connect_result)
    {
      sockets.push_back(std::move(*connect_result));
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto send_recv = [&](SyncSocket& socket, const std::string& msg)
  {
    auto send_result = socket.writeAll(to_bytes(msg));
    EXPECT_TRUE(send_result);
    std::array<std::byte, 1024> buffer{};
    auto recv_result = socket.readSome(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result)
    {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  };

  std::thread t1([&]() { send_recv(*sockets[0], "client1"); });
  std::thread t2([&]() { send_recv(*sockets[1], "client2"); });
  t1.join();
  t2.join();

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ConnectionRefused)
{
  ClientSync client("127.0.0.1", 59999, get_io_context());
  auto connect_result = client.connect({});
  EXPECT_FALSE(connect_result.has_value());
}

TEST_F(IoContextFixture, InvalidHost)
{
  ClientSync client("invalid.host.invalid", TEST_PORT, get_io_context());
  auto connect_result = client.connect({});
  EXPECT_FALSE(connect_result.has_value());
}

TEST_F(SyncClientServerFixture, SpecialCharacters)
{
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ClientSync client("127.0.0.1", server.port(), get_io_context());

  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);
  std::string special = "Hello, World! @#$%^&*()_+-=\n\t";
  auto send_result = client_socket->writeAll(to_bytes(special));
  EXPECT_TRUE(send_result);
  std::array<std::byte, 1024> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer));
  EXPECT_TRUE(recv_result);
  if (recv_result)
  {
    auto response = to_string_view(buffer, *recv_result);
    EXPECT_EQ(special, response);
  }

  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, BinaryData)
{
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ClientSync client("127.0.0.1", server.port(), get_io_context());

  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);
  std::vector<std::byte> binary_data(256);
  for (size_t i = 0; i < 256; i++)
  {
    binary_data[i] = static_cast<std::byte>(i);
  }
  auto send_result = client_socket->writeAll(std::span(binary_data));
  EXPECT_TRUE(send_result);
  std::array<std::byte, 512> buffer{};
  auto recv_result = client_socket->readSome(std::span(buffer));
  EXPECT_TRUE(recv_result);
  if (recv_result && size_t(*recv_result) == 256)
  {
    for (size_t i = 0; i < 256; i++)
    {
      EXPECT_EQ(binary_data[i], buffer[i]);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, LongBinaryData)
{
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread(
    [&server]()
    {
      auto listen_result = server.listen();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ClientSync client("127.0.0.1", server.port(), get_io_context());

  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> binary_data(1024 * 1024);
  for (std::size_t i = 0; auto& b : binary_data)
  {
    b = static_cast<std::byte>(i);
  }

  // write data
  auto send_result = client_socket->writeAll(std::span(binary_data));
  EXPECT_TRUE(send_result);

  std::vector<std::byte> received_data;
  // read all data back
  while (received_data.size() < binary_data.size())
  {
    std::array<std::byte, 1024> buffer{};
    auto recv_result = client_socket->readSome(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (!recv_result)
    {
      break;  // break on error
    }

    received_data.insert(received_data.end(), buffer.begin(), buffer.begin() + *recv_result);
  }

  EXPECT_EQ(received_data.size(), binary_data.size());

  server.stop();
  server_thread.join();
}

}  // namespace Network::Test

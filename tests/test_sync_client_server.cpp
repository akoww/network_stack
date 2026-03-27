#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

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

TEST_F(IoContextFixture, MultipleClientsConcurrent) {
  TestSyncServer server(12347, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto client_thread_func = [](uint16_t port, asio::io_context &io_ctx) {
    ClientSync client("127.0.0.1", port, io_ctx);
    client.connect({});
  };
  std::thread client1(client_thread_func, server.port(),
                      std::ref(get_io_context()));
  std::thread client2(client_thread_func, server.port(),
                      std::ref(get_io_context()));
  std::thread client3(client_thread_func, server.port(),
                      std::ref(get_io_context()));
  client1.join();
  client2.join();
  client3.join();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_TRUE(server.getSockets().size() >= 1)
      << "Expected at least 1 connection";
  server.stop();
  server_thread.join();
}

TEST_F(SyncClientServerFixture, EchoServerMultipleMessages) {
  class EchoServer : public TestSyncServer {
    std::vector<std::jthread> _threads;

  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestSyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      _threads.emplace_back([sock = std::move(sock)]() {
        std::array<std::byte, 1024> buffer{};
        while (true) {
          auto recv_result = sock->read_some(std::span(buffer));
          if (!recv_result || *recv_result == 0)
            break;
          auto send_result =
              sock->write_all(std::span(buffer).first(*recv_result));
          if (!send_result)
            break;
        }
      });
    }
  };
  EchoServer server(test_port, _io_ctx);
  server_thread_ = std::thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto connect_result = _client.connect({});
  if (connect_result) {
    _client_socket = std::move(*connect_result);
    const std::vector<std::string> messages = {"hello", "world", "test"};
    for (const auto &msg : messages) {
      auto send_result = _client_socket->write_all(to_bytes(msg));
      EXPECT_TRUE(send_result);
      std::array<std::byte, 1024> buffer{};
      auto recv_result = _client_socket->read_some(std::span(buffer));
      EXPECT_TRUE(recv_result);
      if (recv_result) {
        auto response = to_string_view(buffer, *recv_result);
        EXPECT_EQ(msg, response);
      }
    }
  }
  _client_socket.reset(); // shutdown
  server.stop();
  server_thread_.join();
}

TEST_F(SyncClientServerFixture, EchoServerConcurrentClients) {
  class EchoServer : public TestSyncServer {
    std::vector<std::jthread> _threads;

  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestSyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      _threads.emplace_back([sock = std::move(sock)]() {
        std::array<std::byte, 1024> buffer{};
        while (true) {
          auto recv_result = sock->read_some(std::span(buffer));
          if (!recv_result || *recv_result == 0)
            break;
          auto send_result =
              sock->write_all(std::span(buffer).first(*recv_result));
          if (!send_result)
            break;
        }
      });
    }
  };
  EchoServer server(test_port, _io_ctx);
  server_thread_ = std::thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::vector<std::unique_ptr<SyncSocket>> sockets;
  for (int i = 0; i < 3; i++) {
    ClientSync client("127.0.0.1", test_port, _io_ctx);
    auto connect_result = client.connect({});
    if (connect_result)
      sockets.push_back(std::move(*connect_result));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto send_recv = [&](SyncSocket &socket, const std::string &msg) {
    auto send_result = socket.write_all(to_bytes(msg));
    EXPECT_TRUE(send_result);
    std::array<std::byte, 1024> buffer{};
    auto recv_result = socket.read_some(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result) {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  };
  std::thread t1([&]() { send_recv(*sockets[0], "client1"); });
  std::thread t2([&]() { send_recv(*sockets[1], "client2"); });
  t1.join();
  t2.join();
  server.stop();
  server_thread_.join();
}

TEST_F(SyncClientServerFixture, ServerRestart) {
  TestSyncServer server(test_port, _io_ctx);
  server_thread_ = std::thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto connect_result = _client.connect({});
  if (connect_result)
    _client_socket = std::move(*connect_result);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  server.stop();
  server_thread_.join();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  restart_server();
  ClientSync client2("127.0.0.1", test_port, _io_ctx);
  auto connect_result2 = client2.connect({});
  EXPECT_TRUE(connect_result2.has_value());
  server.stop();
  server_thread_.join();
}

TEST_F(IoContextFixture, ConnectionRefused) {
  ClientSync client("127.0.0.1", 59999, get_io_context());
  auto connect_result = client.connect({});
  EXPECT_TRUE(!connect_result.has_value());
}

TEST_F(IoContextFixture, InvalidHost) {
  ClientSync client("invalid.host.invalid", 12345, get_io_context());
  auto connect_result = client.connect({});
  EXPECT_TRUE(!connect_result.has_value());
}

TEST_F(SyncClientServerFixture, SpecialCharacters) {
  class EchoServer : public TestSyncServer {
    std::vector<std::jthread> _threads;

  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestSyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      _threads.emplace_back([sock = std::move(sock)]() {
        std::array<std::byte, 1024> buffer{};
        auto recv_result = sock->read_some(std::span(buffer));
        if (recv_result && *recv_result > 0) {
          auto send_result =
              sock->write_all(std::span(buffer).first(*recv_result));
          EXPECT_TRUE(send_result);
        }
      });
    }
  };
  EchoServer server(test_port, _io_ctx);
  server_thread_ = std::thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto connect_result = _client.connect({});
  if (connect_result) {
    _client_socket = std::move(*connect_result);
    std::string special = "Hello, World! @#$%^&*()_+-=\n\t";
    auto send_result = _client_socket->write_all(to_bytes(special));
    EXPECT_TRUE(send_result);
    std::array<std::byte, 1024> buffer{};
    auto recv_result = _client_socket->read_some(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result) {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(special, response);
    }
  }
  server.stop();
  server_thread_.join();
}

TEST_F(SyncClientServerFixture, BinaryData) {
  class EchoServer : public TestSyncServer {
    std::vector<std::jthread> _threads;

  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestSyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      _threads.emplace_back([sock = std::move(sock)]() {
        std::array<std::byte, 1024> buffer{};
        auto recv_result = sock->read_some(std::span(buffer));
        if (recv_result && *recv_result > 0) {
          auto send_result =
              sock->write_all(std::span(buffer).first(*recv_result));
          EXPECT_TRUE(send_result);
        }
      });
    }
  };
  EchoServer server(test_port, _io_ctx);
  server_thread_ = std::thread([&server]() { server.listen(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto connect_result = _client.connect({});
  if (connect_result) {
    _client_socket = std::move(*connect_result);
    std::vector<std::byte> binary_data(256);
    for (size_t i = 0; i < 256; i++)
      binary_data[i] = static_cast<std::byte>(i);
    auto send_result = _client_socket->write_all(std::span(binary_data));
    EXPECT_TRUE(send_result);
    std::array<std::byte, 512> buffer{};
    auto recv_result = _client_socket->read_some(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (recv_result && size_t(*recv_result) == 256) {
      for (size_t i = 0; i < 256; i++)
        EXPECT_EQ(binary_data[i], buffer[i]);
    }
  }
  server.stop();
  server_thread_.join();
}

} // namespace Network::Test

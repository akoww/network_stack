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

namespace Network::Test {

constexpr uint16_t TEST_PORT = 12347;

TEST_F(IoContextFixture, ZeroSizeWrite) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::span<const std::byte> empty_span{};
  auto send_result = client_socket->write_all(empty_span);
  EXPECT_TRUE(send_result);
  if (send_result) {
    EXPECT_EQ(*send_result, 0);
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ZeroSizeRead) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::span<std::byte> empty_span{};
  auto recv_result = client_socket->read_some(empty_span);
  EXPECT_TRUE(recv_result);
  if (recv_result) {
    EXPECT_EQ(*recv_result, 0);
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, RapidConnectDisconnect) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  for (int i = 0; i < 100; i++) {
    ClientSync client("127.0.0.1", server.port(), get_io_context());
    auto connect_result = client.connect({});
    if (connect_result.has_value()) {
      auto client_socket = std::move(*connect_result);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ServerAbruptShutdownDuringWrite) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < 1024; i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  auto send_result = client_socket->write_all(std::span(data));
  if (send_result.has_value() && *send_result > 0) {
    server.stop();
  }

  server_thread.join();
}

TEST_F(IoContextFixture, ServerAbruptShutdownDuringRead) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::thread writer([&server]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.stop();
  });

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < 1024; i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  auto send_result = client_socket->write_all(std::span(data));
  (void)send_result;

  writer.join();
  server_thread.join();
}

TEST_F(IoContextFixture, FragmentedWriteRead) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::string message = "Hello, World!";
  for (char c : message) {
    std::array<std::byte, 1> send_buffer{std::byte(c)};
    auto send_result = client_socket->write_all(std::span(send_buffer));
    EXPECT_TRUE(send_result);
    if (!send_result) {
      break;
    }
  }

  std::string received;
  while (received.size() < message.size()) {
    std::array<std::byte, 1> buffer{};
    auto recv_result = client_socket->read_some(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (!recv_result || *recv_result == 0) {
      break;
    }
    received.push_back(static_cast<char>(buffer[0]));
  }

  EXPECT_EQ(message, received);

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, LargeWriteThenRead) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024 * 64);
  for (size_t i = 0; i < data.size(); i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  auto send_result = client_socket->write_all(std::span(data));
  EXPECT_TRUE(send_result);
  if (!send_result) {
    server.stop();
    server_thread.join();
    return;
  }

  std::vector<std::byte> received;
  while (received.size() < data.size()) {
    std::array<std::byte, 4096> buffer{};
    auto recv_result = client_socket->read_some(std::span(buffer));
    EXPECT_TRUE(recv_result);
    if (!recv_result || *recv_result == 0) {
      break;
    }
    received.insert(received.end(), buffer.begin(),
                    buffer.begin() + *recv_result);
  }

  EXPECT_EQ(received.size(), data.size());
  for (size_t i = 0; i < data.size(); i++) {
    EXPECT_EQ(received[i], data[i]);
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, MultipleClientsSameServer) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::unique_ptr<SyncSocket>> sockets;

  for (int i = 0; i < 10; i++) {
    ClientSync client("127.0.0.1", server.port(), get_io_context());
    auto connect_result = client.connect({});
    if (connect_result.has_value()) {
      sockets.push_back(std::move(*connect_result));
    }
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  for (size_t i = 0; i < sockets.size(); i++) {
    auto send_result = sockets[i]->write_all(to_bytes("test" + std::to_string(i)));
    EXPECT_TRUE(send_result);
  }

  for (size_t i = 0; i < sockets.size(); i++) {
    std::array<std::byte, 1024> buffer{};
    auto recv_result = sockets[i]->read_some(std::span(buffer));
    EXPECT_TRUE(recv_result);
  }

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ConcurrentWriteReadSameSocket) {
  EchoServer server(TEST_PORT, get_io_context());
  std::thread server_thread([&server]() {
    auto listen_result = server.listen();
    EXPECT_TRUE(listen_result.has_value()) << "Server listen failed";
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientSync client("127.0.0.1", server.port(), get_io_context());
  auto connect_result = client.connect({});
  ASSERT_TRUE(connect_result.has_value()) << "Client not connected";

  auto client_socket = std::move(*connect_result);

  std::atomic<bool> done = false;
  std::atomic<int> send_count = 0;
  std::atomic<int> recv_count = 0;

  std::thread sender([&]() {
    int i = 0;
    while (!done && i < 100) {
      std::string msg = "msg" + std::to_string(i);
      auto send_result = client_socket->write_all(to_bytes(msg));
      if (send_result) {
        send_count++;
      }
      i++;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  std::thread receiver([&]() {
    int i = 0;
    while (!done && i < 100) {
      std::array<std::byte, 1024> buffer{};
      auto recv_result = client_socket->read_some(std::span(buffer));
      if (recv_result && *recv_result > 0) {
        recv_count++;
      }
      i++;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    done = true;
  });

  sender.join();
  receiver.join();

  EXPECT_GT(send_count, 0);
  EXPECT_GT(recv_count, 0);

  server.stop();
  server_thread.join();
}

} // namespace Network::Test

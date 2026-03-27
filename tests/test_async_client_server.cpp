#include <array>
#include <asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "client/ClientAsync.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_io_context.h"
#include "server/ServerAsync.h"
#include "socket/TcpSocket.h"

namespace Network::Test {

TEST_F(AsyncClientServerFixture, MinimalConstructor) {
  asio::io_context io_ctx;
  ClientAsync client("127.0.0.1", 12345, io_ctx);
  EXPECT_EQ(client.host(), "127.0.0.1");
  EXPECT_EQ(client.port(), 12345);
}

TEST_F(AsyncClientServerFixture, MinimalConstructorServer) {
  asio::io_context io_ctx;
  TestAsyncServer server(12345, io_ctx);
  EXPECT_EQ(server.host(), "0.0.0.0");
  EXPECT_EQ(server.port(), 12345);
}

TEST_F(AsyncClientServerFixture, EchoServerMultipleMessages) {
  class EchoServer : public TestAsyncServer {
  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestAsyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      std::cout << "EchoServer: Handling client" << std::endl;
      asio::co_spawn(
          get_io_context(),
          [sock = std::move(sock)]() mutable -> asio::awaitable<void> {
            std::array<std::byte, 1024> buffer{};
            while (true) {
              std::cout << "EchoServer: About to read" << std::endl;
              auto recv_result =
                  co_await sock->async_read_some(std::span(buffer));
              std::cout << "EchoServer: Read complete, result: "
                        << (recv_result ? std::to_string(*recv_result)
                                        : "error")
                        << std::endl;
              if (!recv_result || *recv_result == 0) {
                std::cout << "EchoServer: Connection closed" << std::endl;
                co_return;
              }
              std::cout << "EchoServer: About to write" << std::endl;
              auto send_result = co_await sock->async_write_all(
                  std::span(buffer).first(*recv_result));
              std::cout << "EchoServer: Write complete" << std::endl;
              if (!send_result) {
                std::cout << "EchoServer: Write failed" << std::endl;
                co_return;
              }
            }
          },
          asio::detached);
    }
  };

  asio::io_context io_ctx;
  EchoServer server(test_port, io_ctx);
  std::thread server_thread([&io_ctx, &server]() {
    asio::co_spawn(
        io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);
    io_ctx.run();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      io_ctx,
      [&io_ctx, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", test_port, io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (connect_result) {
    auto client_socket = std::move(*connect_result);
    std::cout << "Test: Connection ready, sending message" << std::endl;
    const std::string msg = "hello";
    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
        io_ctx,
        [&client_socket, msg,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
          auto result = co_await client_socket->async_write_all(to_bytes(msg));
          std::cout << "Test: Write complete, result: "
                    << (result ? std::to_string(*result) : "error")
                    << std::endl;
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();
    std::cout << "Test: Write result received: "
              << (send_result ? std::to_string(*send_result) : "error")
              << std::endl;
    EXPECT_TRUE(send_result);

    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    std::array<std::byte, 1024> buffer{};
    asio::co_spawn(
        io_ctx,
        [&client_socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->async_read_some(std::span(buffer));
          std::cout << "Test: Read complete, result: "
                    << (result ? std::to_string(*result) : "error")
                    << std::endl;
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();
    std::cout << "Test: Read result received" << std::endl;
    EXPECT_TRUE(recv_result);
    if (recv_result) {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(AsyncClientServerFixture, EchoServerConcurrentClients) {
  class EchoServer : public TestAsyncServer {
  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestAsyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      std::cout << "[EchoServer] Handling client connection" << std::endl;
      asio::co_spawn(
          get_io_context(),
          [sock = std::move(sock)]() mutable -> asio::awaitable<void> {
            std::array<std::byte, 1024> buffer{};
            while (true) {
              std::cout << "[EchoServer] About to read from client"
                        << std::endl;
              auto recv_result =
                  co_await sock->async_read_some(std::span(buffer));
              if (!recv_result || *recv_result == 0) {
                std::cout << "[EchoServer] Client disconnected or error"
                          << std::endl;
                co_return;
              }
              std::cout << "[EchoServer] Read " << *recv_result
                        << " bytes, echoing back" << std::endl;
              auto send_result = co_await sock->async_write_all(
                  std::span(buffer).first(*recv_result));
              if (!send_result) {
                std::cout << "[EchoServer] Write failed" << std::endl;
                co_return;
              }
              std::cout << "[EchoServer] Echo complete" << std::endl;
            }
          },
          asio::detached);
    }
  };

  std::cout << "[Test] Starting EchoServerConcurrentClients" << std::endl;
  asio::io_context io_ctx;
  EchoServer server(test_port, io_ctx);
  std::thread server_thread([&io_ctx, &server]() {
    std::cout << "[ServerThread] Starting ASIO io_context" << std::endl;
    asio::co_spawn(
        io_ctx,
        [&server]() -> asio::awaitable<void> {
          std::cout << "[ServerThread] About to listen()" << std::endl;
          auto listen_result = co_await server.listen();
          std::cout << "[ServerThread] listen() returned: "
                    << (listen_result ? "success" : "failed") << std::endl;
          (void)listen_result;
        },
        asio::detached);
    std::cout << "[ServerThread] Starting io_ctx.run()" << std::endl;
    io_ctx.run();
    std::cout << "[ServerThread] ASIO io_context stopped" << std::endl;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::unique_ptr<AsyncSocket, void (*)(AsyncSocket *)>> sockets;
  for (int i = 0; i < 3; i++) {
    std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
        promise;
    auto future = promise.get_future();

    asio::co_spawn(
        io_ctx,
        [&io_ctx, &promise]() mutable -> asio::awaitable<void> {
          std::cout << "[Test] Client connecting..." << std::endl;
          ClientAsync client("127.0.0.1", test_port, io_ctx);
          auto result = co_await client.connect({});
          std::cout << "[Test] Client connect result: "
                    << (result ? "success" : "failed") << std::endl;
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto connect_result = future.get();
    if (connect_result) {
      std::cout << "[Test] Client " << i << " connected, socket created"
                << std::endl;
      auto sock = std::move(*connect_result);
      sockets.emplace_back(sock.release(), [](AsyncSocket *s) {
        std::cout << "[Test] Socket destructor called" << std::endl;
        delete s;
      });
    }
  }

  auto send_recv = [&](AsyncSocket &socket, const std::string &msg) {
    std::cout << "[Test] [" << msg << "] Starting send_recv operation"
              << std::endl;
    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    std::cout << "[Test] [" << msg << "] About to async_write_all" << std::endl;
    asio::co_spawn(
        io_ctx,
        [&socket, msg,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
          std::cout << "[Test] [" << msg
                    << "] Inside write coro, about to async_write_all"
                    << std::endl;
          auto result = co_await socket.async_write_all(to_bytes(msg));
          std::cout << "[Test] [" << msg
                    << "] async_write_all complete, result: "
                    << (result ? std::to_string(*result) : "error")
                    << std::endl;
          promise.set_value(std::move(result));
        },
        asio::detached);

    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);
    std::cout << "[Test] [" << msg << "] Send result received" << std::endl;

    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    std::array<std::byte, 1024> buffer{};
    std::cout << "[Test] [" << msg << "] About to async_read_some" << std::endl;
    asio::co_spawn(
        io_ctx,
        [msg, &socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          std::cout << "[Test] [" << msg
                    << "] Inside read coro, about to async_read_some"
                    << std::endl;
          auto result = co_await socket.async_read_some(std::span(buffer));
          std::cout << "[Test] [" << msg
                    << "] async_read_some complete, result: "
                    << (result ? std::to_string(*result) : "error")
                    << std::endl;
          promise.set_value(std::move(result));
        },
        asio::detached);

    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result) {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);
      std::cout << "[Test] [" << msg << "] Received echo: " << response
                << std::endl;
    }
    std::cout << "[Test] [" << msg << "] send_recv operation completed"
              << std::endl;
  };

  std::cout << "[Test] Starting concurrent client threads t1 and t2"
            << std::endl;
  std::thread t1([&]() {
    send_recv(*sockets[0], "client1");
    std::cout << "[Test] t1 thread completed" << std::endl;
  });
  std::thread t2([&]() {
    send_recv(*sockets[1], "client2");
    std::cout << "[Test] t2 thread completed" << std::endl;
  });
  t1.join();
  std::cout << "[Test] t1 joined successfully" << std::endl;
  t2.join();
  std::cout << "[Test] t2 joined successfully" << std::endl;

  std::cout << "[Test] Closing client sockets before server stop" << std::endl;
  sockets.clear();
  std::cout << "[Test] Client sockets destroyed" << std::endl;

  std::cout << "[Test] About to stop server" << std::endl;
  server.stop();
  std::cout << "[Test] Server stopped" << std::endl;
  server_thread.join();
  std::cout << "[Test] Server thread joined successfully" << std::endl;
  std::cout << "[Test] EchoServerConcurrentClients test completed" << std::endl;
}

TEST_F(AsyncClientServerFixture, ServerRestart) {
  asio::io_context io_ctx;
  TestAsyncServer server(test_port, io_ctx);
  std::thread server_thread([&io_ctx, &server]() {
    asio::co_spawn(
        io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);
    io_ctx.run();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      io_ctx,
      [&io_ctx, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", test_port, io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  std::unique_ptr<AsyncSocket> client_socket;
  if (connect_result)
    client_socket = std::move(*connect_result);

  server.stop();
  server_thread.join();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  io_ctx.restart();

  server_thread = std::thread([&io_ctx, &server]() {
    asio::co_spawn(
        io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);
    io_ctx.run();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientAsync client2("127.0.0.1", test_port, io_ctx);
  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise2;
  auto future2 = promise2.get_future();

  asio::co_spawn(
      io_ctx,
      [&client2, &promise2]() mutable -> asio::awaitable<void> {
        auto result = co_await client2.connect({});
        promise2.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result2 = future2.get();
  EXPECT_TRUE(connect_result2.has_value());

  server.stop();
  server_thread.join();
}

TEST_F(IoContextFixture, ConnectionRefused) {
  ClientAsync client("127.0.0.1", 59999, get_io_context());
  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      get_io_context(),
      [&client,
       promise = std::move(promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();
  EXPECT_TRUE(!connect_result.has_value());
}

TEST_F(IoContextFixture, InvalidHost) {
  ClientAsync client("invalid.host.invalid", 12345, get_io_context());
  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      get_io_context(),
      [&client,
       promise = std::move(promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();
  EXPECT_TRUE(!connect_result.has_value());
}

TEST_F(AsyncClientServerFixture, SpecialCharacters) {
  class EchoServer : public TestAsyncServer {
  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestAsyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      asio::co_spawn(
          get_io_context(),
          [sock = std::move(sock)]() mutable -> asio::awaitable<void> {
            std::array<std::byte, 1024> buffer{};
            auto recv_result =
                co_await sock->async_read_some(std::span(buffer));
            if (recv_result && *recv_result > 0) {
              auto send_result = co_await sock->async_write_all(
                  std::span(buffer).first(*recv_result));
              EXPECT_TRUE(send_result);
            }
            co_return;
          },
          asio::detached);
    }
  };

  asio::io_context io_ctx;
  EchoServer server(test_port, io_ctx);
  std::thread server_thread([&io_ctx, &server]() {
    asio::co_spawn(
        io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);
    io_ctx.run();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      io_ctx,
      [&io_ctx, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", test_port, io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (connect_result) {
    auto client_socket = std::move(*connect_result);
    std::string special = "Hello, World! @#$%^&*()_+-=\n\t";
    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
        io_ctx,
        [&client_socket, &special,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->async_write_all(to_bytes(special));
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);

    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    std::array<std::byte, 1024> buffer{};
    asio::co_spawn(
        io_ctx,
        [&client_socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->async_read_some(std::span(buffer));
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result) {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(special, response);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(AsyncClientServerFixture, BinaryData) {
  class EchoServer : public TestAsyncServer {
  public:
    EchoServer(uint16_t port, asio::io_context &io_ctx)
        : TestAsyncServer(port, io_ctx) {}
    void handle_client(std::unique_ptr<TcpSocket> sock) override {
      asio::co_spawn(
          get_io_context(),
          [sock = std::move(sock)]() mutable -> asio::awaitable<void> {
            std::array<std::byte, 1024> buffer{};
            auto recv_result =
                co_await sock->async_read_some(std::span(buffer));
            if (recv_result && *recv_result > 0) {
              auto send_result = co_await sock->async_write_all(
                  std::span(buffer).first(*recv_result));
              EXPECT_TRUE(send_result);
            }
            co_return;
          },
          asio::detached);
    }
  };

  asio::io_context io_ctx;
  EchoServer server(test_port, io_ctx);
  std::thread server_thread([&io_ctx, &server]() {
    asio::co_spawn(
        io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);
    io_ctx.run();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      io_ctx,
      [&io_ctx, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", test_port, io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (connect_result) {
    auto client_socket = std::move(*connect_result);
    std::vector<std::byte> binary_data(256);
    for (size_t i = 0; i < 256; i++)
      binary_data[i] = static_cast<std::byte>(i);
    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
        io_ctx,
        [&client_socket, &binary_data,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->async_write_all(std::span(binary_data));
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);

    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    std::array<std::byte, 512> buffer{};
    asio::co_spawn(
        io_ctx,
        [&client_socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->async_read_some(std::span(buffer));
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result && size_t(*recv_result) == 256) {
      for (size_t i = 0; i < 256; i++)
        EXPECT_EQ(binary_data[i], buffer[i]);
    }
  }

  server.stop();
  server_thread.join();
}

} // namespace Network::Test

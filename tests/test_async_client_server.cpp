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

constexpr uint16_t TEST_PORT = 12346;

TEST_F(AsyncClientServerFixture, MinimalConstructor) {

  ClientAsync client("127.0.0.1", 12345, _io_ctx);
  EXPECT_EQ(client.host(), "127.0.0.1");
  EXPECT_EQ(client.port(), 12345);
}

TEST_F(AsyncClientServerFixture, MinimalConstructorServer) {

  EchoServer server(12345, _io_ctx);
  EXPECT_EQ(server.host(), "0.0.0.0");
  EXPECT_EQ(server.port(), 12345);
}

TEST_F(AsyncClientServerFixture, EchoServerMultipleMessages) {
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread([this, &server]() {
    asio::co_spawn(
        _io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);

  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (connect_result) {
    auto client_socket = std::move(*connect_result);
    const std::string msg = "hello";
    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [&client_socket, msg,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
          auto result = co_await client_socket->async_write_all(to_bytes(msg));
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
        _io_ctx,
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
      EXPECT_EQ(msg, response);
    }
  }

  server.stop();
  server_thread.join();
}

TEST_F(AsyncClientServerFixture, EchoServerConcurrentClients) {

  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread([this, &server]() {
    asio::co_spawn(
        _io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();

          (void)listen_result;
        },
        asio::detached);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<std::unique_ptr<AsyncSocket, void (*)(AsyncSocket *)>> sockets;
  for (int i = 0; i < 3; i++) {
    std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
        promise;
    auto future = promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [this, &promise]() mutable -> asio::awaitable<void> {
          ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
          auto result = co_await client.connect({});
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto connect_result = future.get();
    if (connect_result) {

      auto sock = std::move(*connect_result);
      sockets.emplace_back(sock.release(), [](AsyncSocket *s) {
        delete s;
      });
    }
  }

  auto send_recv = [&](AsyncSocket &socket, const std::string &msg) {

    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [&socket, msg,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {

          auto result = co_await socket.async_write_all(to_bytes(msg));
          promise.set_value(std::move(result));
        },
        asio::detached);

    auto send_result = send_future.get();
    EXPECT_TRUE(send_result);

    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    std::array<std::byte, 1024> buffer{};
    asio::co_spawn(
        _io_ctx,
        [msg, &socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          auto result = co_await socket.async_read_some(std::span(buffer));
          promise.set_value(std::move(result));
        },
        asio::detached);

    auto recv_result = recv_future.get();
    EXPECT_TRUE(recv_result);
    if (recv_result) {
      auto response = to_string_view(buffer, *recv_result);
      EXPECT_EQ(msg, response);

    }
  };

  std::thread t1([&]() {
    send_recv(*sockets[0], "client1");
  });
  std::thread t2([&]() {
    send_recv(*sockets[1], "client2");
  });
  t1.join();
  t2.join();

  sockets.clear();

  server.stop();
  server_thread.join();
}

TEST_F(AsyncClientServerFixture, ServerRestart) {
  
  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread([this, &server]() {
    asio::co_spawn(
        _io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);

  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
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
  _io_ctx.restart();

  server_thread = std::thread([this, &server]() {
    asio::co_spawn(
        _io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);

  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  ClientAsync client2("127.0.0.1", TEST_PORT, _io_ctx);
  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise2;
  auto future2 = promise2.get_future();

  asio::co_spawn(
      _io_ctx,
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


  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread([this, &server]() {
    asio::co_spawn(
        _io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);

  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
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
        _io_ctx,
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
        _io_ctx,
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

  EchoServer server(TEST_PORT, _io_ctx);
  std::thread server_thread([this, &server]() {
    asio::co_spawn(
        _io_ctx,
        [&server]() -> asio::awaitable<void> {
          auto listen_result = co_await server.listen();
          (void)listen_result;
        },
        asio::detached);

  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() mutable -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
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
        _io_ctx,
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
        _io_ctx,
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

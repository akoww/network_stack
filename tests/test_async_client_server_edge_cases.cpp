#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <string>
#include <thread>

#include "client/ClientAsync.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_fixture_io_context.h"
#include "server/ServerAsync.h"
#include "socket/TcpSocket.h"

namespace Network::Test {

constexpr uint16_t TEST_PORT = 12348;

TEST_F(AsyncClientServerFixture, ZeroSizeWrite) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::span<const std::byte> empty_span{};
  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, empty_span,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(empty_span);
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto send_result = send_future.get();

  EXPECT_TRUE(send_result);
  if (send_result) {
    EXPECT_EQ(*send_result, 0);
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, ZeroSizeRead) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::span<std::byte> empty_span{};
  std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
  auto recv_future = recv_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, &empty_span,
       promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncReadSome(empty_span);
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto recv_result = recv_future.get();

  EXPECT_TRUE(recv_result);
  if (recv_result) {
    EXPECT_EQ(*recv_result, 0);
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, RapidConnectDisconnect) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  for (int i = 0; i < 100; i++) {
    std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
        promise;
    auto future = promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [this, &promise]() -> asio::awaitable<void> {
          ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
          auto result = co_await client.connect({});
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto connect_result = future.get();
    if (connect_result) {
      auto client_socket = std::move(*connect_result);
    }
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, ServerAbruptShutdownDuringWrite) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < 1024; i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  std::thread writer([&server]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.stop();
  });

  asio::co_spawn(
      _io_ctx,
      [&client_socket, data,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(std::span(data));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  (void)send_future.get();

  writer.join();
}

TEST_F(AsyncClientServerFixture, ServerAbruptShutdownDuringRead) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024);
  for (size_t i = 0; i < 1024; i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  std::thread writer([&server, &send_future]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    send_future.wait();
    server.stop();
  });

  asio::co_spawn(
      _io_ctx,
      [&client_socket, data,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(std::span(data));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  (void)send_future.get();

  std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
  auto recv_future = recv_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket,
       promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
        std::array<std::byte, 1024> buffer{};
        auto result = co_await client_socket->asyncReadSome(std::span(buffer));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  (void)recv_future.get();

  writer.join();
}

TEST_F(AsyncClientServerFixture, FragmentedWriteRead) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::string message = "Hello, World!";
  for (char c : message) {
    std::array<std::byte, 1> send_buffer{std::byte(c)};
    std::promise<std::expected<std::size_t, std::error_code>> send_promise;
    auto send_future = send_promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [&client_socket, send_buffer,
         promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
          auto result = co_await client_socket->asyncWriteAll(send_buffer);
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto send_result = send_future.get();

    EXPECT_TRUE(send_result);
    if (!send_result) {
      break;
    }
  }

  std::string received;
  while (received.size() < message.size()) {
    std::array<std::byte, 1> buffer{};
    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [&client_socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->asyncReadSome(std::span(buffer));
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto recv_result = recv_future.get();

    EXPECT_TRUE(recv_result);
    if (!recv_result || *recv_result == 0) {
      break;
    }
    received.push_back(static_cast<char>(buffer[0]));
  }

  EXPECT_EQ(message, received);

  server.stop();
}

TEST_F(AsyncClientServerFixture, LargeWriteThenRead) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> data(1024 * 64);
  for (size_t i = 0; i < data.size(); i++) {
    data[i] = static_cast<std::byte>(i % 256);
  }

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, data,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(std::span(data));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto send_result = send_future.get();

  EXPECT_TRUE(send_result.has_value());

  std::vector<std::byte> received;
  while (received.size() < data.size()) {
    std::array<std::byte, 4096> buffer{};
    std::promise<std::expected<std::size_t, std::error_code>> recv_promise;
    auto recv_future = recv_promise.get_future();

    asio::co_spawn(
        _io_ctx,
        [&client_socket, &buffer,
         promise = std::move(recv_promise)]() mutable -> asio::awaitable<void> {
          auto result =
              co_await client_socket->asyncReadSome(std::span(buffer));
          promise.set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto recv_result = recv_future.get();

    EXPECT_TRUE(recv_result.has_value());
    if (!recv_result || !*recv_result) {
      break;
    }
    received.insert(received.end(), buffer.begin(),
                    buffer.begin() + *recv_result);
  }

  EXPECT_EQ(received.size(), data.size());

  server.stop();
}

TEST_F(AsyncClientServerFixture, SpecialCharactersWithNullBytes) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::string special = "Hello\n\tWorld!@#$%^&*()_+-=[]{}|;:',.<>?/~`";
  special += '\0';
  special += '\0';
  special += '\0';
  special += "Embedded\0Nulls";
  std::string expected = special;

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, special,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result = co_await client_socket->asyncWriteAll(to_bytes(special));
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
        auto result = co_await client_socket->asyncReadSome(std::span(buffer));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto recv_result = recv_future.get();
  EXPECT_TRUE(recv_result);

  if (recv_result) {
    auto response = std::string(reinterpret_cast<const char *>(buffer.data()),
                                *recv_result);
    EXPECT_EQ(expected, response);
  }

  server.stop();
}

TEST_F(AsyncClientServerFixture, BinaryDataAllBytes) {
  EchoServer server(TEST_PORT, _io_ctx);
  asio::co_spawn(
      _io_ctx,
      [&server]() -> asio::awaitable<void> {
        auto listen_result = co_await server.listen();
        (void)listen_result;
      },
      asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
      promise;
  auto future = promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [this, &promise]() -> asio::awaitable<void> {
        ClientAsync client("127.0.0.1", TEST_PORT, _io_ctx);
        auto result = co_await client.connect({});
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto connect_result = future.get();

  if (!connect_result) {
    server.stop();
    return;
  }

  auto client_socket = std::move(*connect_result);

  std::vector<std::byte> binary_data;
  for (int i = 0; i <= 255; i++)
    binary_data.push_back(static_cast<std::byte>(i));
  binary_data.push_back(std::byte(0));
  binary_data.push_back(std::byte(255));

  std::promise<std::expected<std::size_t, std::error_code>> send_promise;
  auto send_future = send_promise.get_future();

  asio::co_spawn(
      _io_ctx,
      [&client_socket, &binary_data,
       promise = std::move(send_promise)]() mutable -> asio::awaitable<void> {
        auto result =
            co_await client_socket->asyncWriteAll(std::span(binary_data));
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
        auto result = co_await client_socket->asyncReadSome(std::span(buffer));
        promise.set_value(std::move(result));
      },
      asio::detached);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto recv_result = recv_future.get();
  EXPECT_TRUE(recv_result);

  if (recv_result && size_t(*recv_result) == binary_data.size()) {
    for (size_t i = 0; i < binary_data.size(); i++)
      EXPECT_EQ(binary_data[i], buffer[i]);
  }

  server.stop();
}

} // namespace Network::Test

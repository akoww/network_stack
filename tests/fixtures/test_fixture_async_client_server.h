#pragma once

#include <gtest/gtest.h>

#include "client/ClientAsync.h"
#include "server/ServerAsync.h"
#include "socket/TcpSocket.h"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/awaitable.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <thread>

namespace Network::Test {

class TestAsyncServer : public ServerAsync {
public:
  TestAsyncServer(uint16_t port, asio::io_context &io_ctx)
      : ServerAsync(port, io_ctx) {}
  ~TestAsyncServer() override = default;

  void handle_client(std::unique_ptr<TcpSocket> sock) override {
    _sockets.push_back(std::move(sock));
  }

  const std::vector<std::unique_ptr<TcpSocket>> &getSockets() const {
    return _sockets;
  }

private:
  std::vector<std::unique_ptr<TcpSocket>> _sockets;
};

inline std::span<const std::byte> to_bytes(std::string_view sv) {
  return std::as_bytes(std::span(sv.data(), sv.size()));
}

inline std::string_view to_string_view(std::span<const std::byte> bytes,
                                       std::size_t length) {
  if (length >= bytes.size())
    return "";
  return {reinterpret_cast<const char *>(bytes.data()), length};
}

class AsyncClientServerFixture : public ::testing::Test {
public:
  static constexpr uint16_t test_port = 12345;

  AsyncClientServerFixture()
      : _server(test_port, _io_ctx), _client("127.0.0.1", test_port, _io_ctx) {}

  ~AsyncClientServerFixture() override { stop_server(); }

  asio::io_context &get_io_context() { return _io_ctx; }

  const std::vector<std::unique_ptr<TcpSocket>> &get_server_sockets() const {
    return _server.getSockets();
  }

  void start_server() {
    if (_server_thread.joinable()) {
      _server.stop();
      _server_thread.join();
    }

    _work_guard.emplace(_io_ctx.get_executor());

    asio::co_spawn(
        _io_ctx,
        [this]() -> asio::awaitable<void> {
          auto listen_result = co_await _server.listen();
          (void)listen_result;
        },
        asio::detached);

    _server_thread = std::thread([&]() { _io_ctx.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::expected<std::unique_ptr<AsyncSocket>, std::error_code> connect_client() {
    auto promise = std::make_shared<std::promise<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>>();
    auto future = promise->get_future();

    asio::co_spawn(
        _io_ctx,
        [this, promise = std::move(promise)]() mutable -> asio::awaitable<void> {
          auto result = co_await _client.connect({});
          promise->set_value(std::move(result));
        },
        asio::detached);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return future.get();
  }

  void disconnect_client() { _client_socket.reset(); }

  void stop_server() {
    if (_server_thread.joinable()) {
      _server.stop();
      _work_guard.reset();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      _server_thread.join();
    }
  }

  bool restart_server() {
    stop_server();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    start_server();
    return true;
  }

protected:
  asio::io_context _io_ctx;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>> _work_guard;
  TestAsyncServer _server;
  ClientAsync _client;
  std::unique_ptr<AsyncSocket> _client_socket{nullptr};
  std::thread _server_thread;
};

} // namespace Network::Test

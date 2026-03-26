#pragma once

#include <gtest/gtest.h>

#include "client/ClientSync.h"
#include "core/Context.h"
#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

#include <chrono>
#include <memory>
#include <thread>

namespace Network::Test {

class TestSyncServer : public ServerSync {
public:
  TestSyncServer(uint16_t port, asio::io_context &io_ctx)
      : ServerSync(port, io_ctx) {};
  virtual ~TestSyncServer() = default;
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

class SyncClientServerFixture : public ::testing::Test {
public:
  static constexpr uint16_t test_port = 12345;

  SyncClientServerFixture()
      : _server(test_port, _io_ctx), _client("127.0.0.1", test_port, _io_ctx) {}

  ~SyncClientServerFixture() override { stop_server(); };

  void SetUp() override { _io_ctx.start(); }

  void TearDown() override {}

  Network::IoContextWrapper &get_io_context() { return _io_ctx; }

  const std::unique_ptr<SyncSocket> &get_client_socket() {
    return _client_socket;
  }
  const std::vector<std::unique_ptr<TcpSocket>> &get_server_sockets() const {
    return _server.getSockets();
  }

  bool start_server() {
    server_thread_ = std::thread([this]() { _server.listen(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
  }

  bool connect_client() {
    auto connect_result = _client.connect({});
    if (!connect_result) {
      return false;
    }

    _client_socket = std::move(*connect_result);
    return true;
  }

  void disconnect_client() { _client_socket.reset(); }

  void stop_server() {
    _server.stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  bool restart_server() {
    stop_server();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server_thread_ = std::thread([this]() { _server.listen(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return true;
  }

protected:
  Network::IoContextWrapper _io_ctx;
  TestSyncServer _server;
  ClientSync _client;
  std::unique_ptr<SyncSocket> _client_socket{nullptr};
  std::thread server_thread_;
};

} // namespace Network::Test

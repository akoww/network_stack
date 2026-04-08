#pragma once

#include <gtest/gtest.h>

#include "client/ClientSync.h"
#include "core/Context.h"
#include "server/ServerSync.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

#include <memory>

namespace Network::Test
{

class EchoServer : public ServerSync
{
  std::vector<std::jthread> _threads;

public:
  EchoServer(uint16_t port, asio::io_context& io_ctx) : ServerSync(port, io_ctx) {}
  void handle_client(std::unique_ptr<BasicSocket> sock) override
  {
    _threads.emplace_back(
      [sock = std::move(sock)]()
      {
        std::array<std::byte, 1024> buffer{};
        while (true)
        {
          auto recv_result = sock->readSome(std::span(buffer));
          if (!recv_result || *recv_result == 0)
            break;
          auto send_result = sock->writeAll(std::span(buffer).first(*recv_result));
          if (!send_result)
            break;
        }
      });
  }
};

inline std::span<const std::byte> to_bytes(std::string_view sv)
{
  return std::as_bytes(std::span(sv.data(), sv.size()));
}

inline std::string_view to_string_view(std::span<const std::byte> bytes, std::size_t length)
{
  if (length >= bytes.size())
    return "";
  return {reinterpret_cast<const char*>(bytes.data()), length};
}

class SyncClientServerFixture : public ::testing::Test
{
public:
  void SetUp() override { _io_ctx.start(); }

  void TearDown() override {}

  Network::IoContextWrapper& get_io_context() { return _io_ctx; }

protected:
  Network::IoContextWrapper _io_ctx;
};

}  // namespace Network::Test

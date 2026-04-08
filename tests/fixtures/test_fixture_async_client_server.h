#pragma once

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "client/ClientAsync.h"
#include "core/Context.h"
#include "server/ServerAsync.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/use_awaitable.hpp>
#include <expected>
#include <future>
#include <memory>

namespace Network::Test
{

class EchoServer : public ServerAsync
{
public:
  EchoServer(uint16_t port, asio::io_context& io_ctx) : ServerAsync(port, io_ctx) { spdlog::info("created server"); }
  ~EchoServer() override { spdlog::info("stopped server"); }

  void handle_client(std::unique_ptr<BasicSocket> sock) override
  {
    asio::co_spawn(
      get_io_context(),
      [sock = std::move(sock)]() mutable -> asio::awaitable<void>
      {
        std::array<std::byte, 1024> buffer{};
        while (true)
        {
          auto recv_result = co_await sock->asyncReadSome(std::span(buffer));
          if (!recv_result || *recv_result == 0)
          {
            break;
          }
          auto send_result = co_await sock->asyncWriteAll(std::span(buffer).first(*recv_result));
          if (!send_result)
          {
            break;
          }
        }
        co_return;
      },
      asio::detached);
  }
};

inline std::span<const std::byte> to_bytes(std::string_view sv)
{
  return std::as_bytes(std::span(sv.data(), sv.size()));
}

inline std::string_view to_string_view(std::span<const std::byte> bytes, std::size_t length)
{
  if (length > bytes.size())
    return "";
  return {reinterpret_cast<const char*>(bytes.data()), length};
}

class AsyncClientServerFixture : public ::testing::Test
{
public:
  void SetUp() override { _io_ctx.start(); }

  void TearDown() override {}

  Network::IoContextWrapper& get_io_context() { return _io_ctx; }

protected:
  Network::IoContextWrapper _io_ctx;
};

}  // namespace Network::Test

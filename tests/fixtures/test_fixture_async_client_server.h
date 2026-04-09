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
#include <asio/use_future.hpp>
#include <expected>
#include <future>
#include <memory>

namespace Network::Test
{

class EchoServer : public ServerAsync
{
  struct Clients
  {
    unsigned int id;
    std::future<void> future;
    std::unique_ptr<DualSocket> sock;
  };

  mutable std::mutex mutex;
  std::vector<Clients> clients;

  void handle_client_impl(asio::io_context& context, std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    unsigned int client_id = sock->getId();

    auto entry = Clients{client_id, std::future<void>{}, std::move(sock)};
    DualSocket* sock_ptr = entry.sock.get();

    {
      std::lock_guard<std::mutex> lock(mutex);
      clients.push_back(std::move(entry));
      auto& client_ref = clients.back();

      client_ref.future = asio::co_spawn(
        context,
        [sock_ptr]() mutable -> asio::awaitable<void>
        {
          std::array<std::byte, 1024> buffer{};
          while (true)
          {
            auto recv_result = co_await sock_ptr->asyncReadSome(std::span(buffer));
            if (!recv_result || *recv_result == 0)
            {
              break;
            }
            auto send_result = co_await sock_ptr->asyncWriteAll(std::span(buffer).first(*recv_result));
            if (!send_result)
            {
              break;
            }
          }
          co_return;
        },
        asio::use_future);
    }
  }

public:
  ~EchoServer() override
  {
    std::vector<Clients> to_cleanup;
    {
      std::lock_guard<std::mutex> lock(mutex);
      for (auto& c : clients)
      {
        if (c.sock)
          c.sock->cancelSocket();
        to_cleanup.push_back(std::move(c));
      }
      clients.clear();
    }

    for (auto& c : to_cleanup)
    {
      if (c.future.valid())
        c.future.get();
    }

    spdlog::info("stopped server");
  }

  EchoServer(uint16_t port, asio::io_context& io_ctx)
    : ServerAsync(port,
                  io_ctx,
                  [this, &io_ctx](std::unique_ptr<DualSocket> sock) { handle_client_impl(io_ctx, std::move(sock)); })
  {
    spdlog::info("created server");
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

  Network::IoContextWrapper& getIoContext() { return _io_ctx; }

protected:
  Network::IoContextWrapper _io_ctx;
};

}  // namespace Network::Test

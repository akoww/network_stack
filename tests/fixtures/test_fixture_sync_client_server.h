#pragma once

#include <gtest/gtest.h>

#include "client/Client.h"
#include "core/Context.h"
#include "server/Server.h"
#include "socket/TlsSocket.h"
#include "socket/TcpSocket.h"
#include "test_certificate_paths.h"

#include <asio/io_context.hpp>
#include <memory>

namespace Network::Test
{

class EchoServer : public Server
{
  struct Clients
  {
    unsigned int id;
    std::thread tr;
    std::unique_ptr<DualSocket> sock;
  };

  mutable std::mutex mutex;
  std::vector<Clients> clients;

public:
  void handle_client(std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    Clients entry;
    entry.id = sock->getId();
    entry.sock = std::move(sock);
    DualSocket* sock_ptr = entry.sock.get();  // Pointer for the thread to use

    // Lock to add to vector
    {
      std::lock_guard<std::mutex> lock(mutex);
      clients.push_back(std::move(entry));
    }

    clients.back().tr = std::thread(
      [sock_ptr, client_id = clients.back().id]()
      {
        std::array<std::byte, 1024> buffer{};
        bool running = true;

        try
        {
          while (running)
          {
            // Read data
            auto recv_result = sock_ptr->readSome(std::span(buffer));

            // If disconnect or error, stop
            if (!recv_result || *recv_result == 0)
            {
              running = false;
              break;
            }

            // Echo data back
            auto send_result = sock_ptr->writeAll(std::span(buffer).first(*recv_result));
            if (!send_result)
            {
              running = false;
              break;
            }
          }
        }
        catch (const std::exception& e)
        {
          std::cerr << "Client " << client_id << " Error: " << e.what() << std::endl;
          running = false;
        }
      });
  }

  EchoServer(uint16_t port, asio::any_io_executor io_ctx)
    : Server(port, io_ctx, [this](std::unique_ptr<DualSocket> sock) { handle_client(std::move(sock)); })
  {
  }

  void setSslContext(std::shared_ptr<asio::ssl::context> ctx) { _ssl_context = std::move(ctx); }

  ~EchoServer()
  {
    std::vector<Clients> to_join;
    {
      std::lock_guard<std::mutex> lock(mutex);
      // Move all clients to a temporary vector to release the lock before joining
      for (auto& c : clients)
      {
        if (c.tr.joinable())
          to_join.push_back(std::move(c));
      }
      clients.clear();
    }

    // Cancel sockets and join threads outside of the main lock
    for (auto& c : to_join)
    {
      if (c.sock)
        c.sock->cancelSocket();

      if (c.tr.joinable())
        c.tr.join();
    }
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

inline std::shared_ptr<asio::ssl::context> createSslContextWithCert()
{
  auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_server);
  ctx->use_certificate_chain_file(std::string(Network::Test::ServerCertPath()));
  ctx->use_private_key_file(std::string(Network::Test::ServerKeyPath()), asio::ssl::context::pem);
  return ctx;
}

class SyncClientServerFixture : public ::testing::Test
{
public:
  SyncClientServerFixture() = default;
  virtual ~SyncClientServerFixture() = default;

  void SetUp() override {}

  void TearDown() override {}

  Network::IoContextWrapper& getIoContext() { return _io; }

protected:
  Network::IoContextWrapper _io;
};

}  // namespace Network::Test

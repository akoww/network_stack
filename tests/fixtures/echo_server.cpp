#include "core/Context.h"
#include "server/Server.h"
#include "socket/SocketBase.h"

#include <asio/ssl/context.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <spdlog/spdlog.h>
#include <vector>

namespace fs = std::filesystem;

volatile sig_atomic_t g_running = 1;
void signal_handler(int)
{
  g_running = 0;
}

struct Options
{
  uint16_t port = 9443;
  fs::path cert_chain;
  fs::path private_key;
};

Options parse_args(int argc, char** argv)
{
  Options opts;
  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
    {
      opts.port = static_cast<uint16_t>(std::stoul(argv[++i]));
    }
    else if (arg == "--cert-chain" && i + 1 < argc)
    {
      opts.cert_chain = fs::path(argv[++i]);
    }
    else if (arg == "--private-key" && i + 1 < argc)
    {
      opts.private_key = fs::path(argv[++i]);
    }
    else if (arg == "--help")
    {
      std::cerr << "Usage: echo_server --port <n> [--cert-chain <path>] [--private-key <path>]\n";
      exit(0);
    }
  }
  return opts;
}

class EchoServer : public Network::Server
{
public:
  using Base = Network::Server;

  struct ClientEntry
  {
    unsigned int id = 0;
    std::thread tr;
    std::unique_ptr<Network::DualSocket> sock;
  };

  explicit EchoServer(uint16_t port, asio::any_io_executor ctx)
    : Base(port, ctx, [this](std::unique_ptr<Network::DualSocket> sock) { handle_client(std::move(sock)); })
  {
  }

  ~EchoServer()
  {
    std::vector<ClientEntry> to_join;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (auto& c : m_clients)
      {
        if (c.tr.joinable())
          to_join.push_back(std::move(c));
      }
      m_clients.clear();
    }
    for (auto& c : to_join)
    {
      if (c.sock)
        c.sock->cancelSocket();
      if (c.tr.joinable())
        c.tr.join();
    }
  }

private:
  void handle_client(std::unique_ptr<Network::DualSocket> sock)
  {
    if (!sock)
      return;

    ClientEntry entry{};
    entry.id = sock->getId();
    entry.sock = std::move(sock);
    Network::DualSocket* sptr{entry.sock.get()};

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_clients.push_back(std::move(entry));
    }

    unsigned const client_id = static_cast<unsigned>(m_clients.back().id);
    m_clients.back().tr = std::thread(
      [sptr, client_id]()
      {
        std::array<std::byte, 4096> buffer{};
        bool running{true};
        try
        {
          while (running)
          {
            auto recv_result = sptr->readSome(std::span(buffer));
            if (!recv_result || *recv_result == 0)
              break;

            auto write_result = sptr->writeAll(std::span(buffer).first(*recv_result));
            if (!write_result)
              break;
          }
        }
        catch (const std::exception& e)
        {
          spdlog::error("Client {} error: {}", client_id, e.what());
          running = false;
        }
      });
  }

  mutable std::mutex m_mutex;
  std::vector<ClientEntry> m_clients;
};

int main(int argc, char** argv)
{
  auto g_sigaction = [](int sig)
  {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, nullptr);
  };
  g_sigaction(SIGINT);
  g_sigaction(SIGTERM);

  Options const opts = parse_args(argc, argv);

  Network::IoContextWrapper io_ctx;

  EchoServer server(opts.port, io_ctx.get_executor());

  if (!opts.cert_chain.empty() && !opts.private_key.empty())
  {
    auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tlsv12_server);
    try
    {
      ctx->use_certificate_chain_file(opts.cert_chain.string());
    }
    catch (const std::system_error& e)
    {
      std::cerr << "Failed to load cert chain: " << opts.cert_chain.string() << ": " << e.what() << "\n";
      return 1;
    }
    try
    {
      ctx->use_private_key_file(opts.private_key.string(), asio::ssl::context::pem);
    }
    catch (const std::system_error& e)
    {
      std::cerr << "Failed to load private key: " << opts.private_key.string() << ": " << e.what() << "\n";
      return 1;
    }
    auto const result = server.listenTls();
    if (result)
    {
      std::cout << "PID: " << getpid() << "\n";
      std::cout << "Echo server listening on TLS port " << opts.port << "\n";
    }
    else
    {
      std::cerr << "TLS listen error: " << result.error().message() << "\n";
      return 1;
    }
  }
  else
  {
    auto const result = server.listen();
    if (result)
    {
      std::cout << "PID: " << getpid() << "\n";
      std::cout << "Echo server listening on port " << opts.port << "\n";
    }
    else
    {
      std::cerr << "Listen error: " << result.error().message() << "\n";
      return 1;
    }
  }
  std::cout.flush();

  return 0;
}

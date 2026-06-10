#include <array>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>

#include "client/Client.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "fixtures/test_certificate_paths.h"
#include "server/Server.h"

namespace Network::Test
{

constexpr uint16_t TLS_BW_PORT = 12348;

struct StageConfig
{
  const char* name;
  std::size_t total_bytes;
  std::size_t chunk_size;
};

TEST_F(AsyncClientServerFixture, AsyncTlsHugeBandwidth)
{
  EchoServer server(TLS_BW_PORT, getIoContext().get_executor());
  EXPECT_FALSE(server.setCertificateChain(Network::Test::ServerCertPath()));
  EXPECT_FALSE(server.setPrivateKey(Network::Test::ServerKeyPath()));

  asio::co_spawn(
    getIoContext().get_executor(),
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.asyncListenTls();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto connect_future = asio::co_spawn(
    getIoContext().get_executor(),
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      Client client("127.0.0.1", TLS_BW_PORT, getIoContext().get_executor());
      client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
      co_return co_await client.asyncConnectTls({});
    },
    asio::use_future);

  auto connect_result = connect_future.get();
  ASSERT_TRUE(connect_result.has_value()) << "Async TLS connect failed";

  auto client_socket = std::move(*connect_result);

  const StageConfig stages[]{
    {"            100 KB", 100ULL * 1024, 1024},
    {"                  1 MB", 1024ULL * 1024, 8 * 1024},
    {"                    5 MB", 5ULL * 1024 * 1024, 32 * 1024},
    {"                  100 MB", 100ULL * 1024 * 1024, 64 * 1024},
    {"                 512 MB", 512ULL * 1024 * 1024, 64 * 1024},
    {"                     1 GB", 1024ULL * 1024 * 1024, 64 * 1024},
  };

  auto overall_start = std::chrono::high_resolution_clock::now();
  bool all_passed = true;
  double total_bytes_sent = 0.0;

  for (const auto& stage : stages)
  {
    std::vector<std::byte> write_buffer(stage.total_bytes);
    for (std::size_t i = 0; i < stage.total_bytes; ++i)
      write_buffer[i] = static_cast<std::byte>((i * 7 + 13) & 0xFF);

    auto stage_start = std::chrono::high_resolution_clock::now();
    std::size_t bytes_sent = 0;
    bool stage_ok = true;

    while (bytes_sent < stage.total_bytes)
    {
      std::size_t remain = stage.total_bytes - bytes_sent;
      std::size_t chunk = std::min(stage.chunk_size, remain);
      auto data_ref = std::span(write_buffer).first(bytes_sent + chunk).subspan(bytes_sent);

      auto sock_ptr_for_capture = client_socket.get();
      auto send_future = asio::co_spawn(
        getIoContext().get_executor(),
        [sock = sock_ptr_for_capture, buf = data_ref]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
        { return sock->asyncWriteAll(buf); }, asio::use_future);

      auto write_ec = send_future.get();
      if (!write_ec)
      {
        std::cerr << "[BANDWIDTH] [" << stage.name << "] ERROR: async write failed at byte " << bytes_sent << "\n";
        stage_ok = false;
        break;
      }
      bytes_sent += chunk;

      // Read echo back in async 4KB batches
      std::size_t bytes_read = 0;
      while (bytes_read < chunk)
      {
        auto to_read = static_cast<std::size_t>(std::min(std::size_t{4096}, chunk - bytes_read));
        auto buf_ptr = std::make_unique<std::array<std::byte, 4096>>();
        auto sock_for_read = client_socket.get();

        auto recv_future = asio::co_spawn(
          getIoContext().get_executor(),
          [sock = sock_for_read, bufs = std::move(buf_ptr),
           sz = to_read]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
          { return sock->asyncReadSome(std::span(*bufs).first(sz)); },
          asio::use_future);

        auto read_ec = recv_future.get();
        if (read_ec && *read_ec > 0)
          bytes_read += static_cast<std::size_t>(*read_ec);
      }

      if (bytes_read != chunk)
      {
        std::cerr << "[BANDWIDTH] [" << stage.name << "] ERROR: async read mismatch, expected " << chunk << " got "
                  << bytes_read << "\n";
        stage_ok = false;
        break;
      }
    }

    auto stage_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(stage_end - stage_start).count();
    total_bytes_sent += static_cast<double>(bytes_sent);

    if (stage_ok)
    {
      double gbps = static_cast<double>(bytes_sent) * 8.0 / elapsed / (1024.0 * 1024.0 * 1024.0);
      std::cout << "[BANDWIDTH] [" << stage.name << "] OK: " << bytes_sent << " bytes in " << std::fixed
                << std::setprecision(3) << elapsed << "s (" << gbps << " Gbps)\n";
    }
    else
    {
      all_passed = false;
    }
  }

  auto overall_end = std::chrono::high_resolution_clock::now();
  double total_elapsed = std::chrono::duration<double>(overall_end - overall_start).count();

  std::cout << "[BANDWIDTH] ================================================\n";
  std::cout << "[BANDWIDTH] Async TLS progressive bandwidth\n";
  std::cout << "[BANDWIDTH] Total time: " << std::fixed << std::setprecision(3) << total_elapsed << "s\n";
  if (total_elapsed > 0)
  {
    double overall_gbps = static_cast<double>(total_bytes_sent * 8.0) / total_elapsed / (1024.0 * 1024.0 * 1024.0);
    std::cout << "[BANDWIDTH] Overall: " << overall_gbps << " Gbps\n";
    std::cout << "[BANDWITDH] Total data: " << (total_bytes_sent / (1024.0 * 1024.0 * 1024.0)) << " GB\n";
    std::cout << "[BANDWIDTH] ================================================\n";
  }

  EXPECT_TRUE(all_passed);
  server.stop();
}

}  // namespace Network::Test

#include <array>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>

#include "client/Client.h"
#include "fixtures/test_fixture_sync_client_server.h"
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

TEST_F(SyncClientServerFixture, SyncTlsHugeBandwidth)
{
  EchoServer server(TLS_BW_PORT, getIoContext().get_executor());
  std::thread server_thread(
    [&server]()
    {
      EXPECT_FALSE(server.setCertificateChain(Network::Test::ServerCertPath()));
      EXPECT_FALSE(server.setPrivateKey(Network::Test::ServerKeyPath()));
      auto listen_result = server.listenTls();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen_tls failed";
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  Client client("127.0.0.1", TLS_BW_PORT, getIoContext().get_executor());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls();
  ASSERT_TRUE(connect_result.has_value()) << "Client TLS connect failed";

  auto client_socket = std::move(*connect_result);

  const StageConfig stages[]{
    {"100 KB", 100ULL * 1024, 1024},
    {"          1 MB", 1024ULL * 1024, 8ULL * 1024},
    {"     5 MB", 5ULL * 1024 * 1024, 32 * 1024},
    {"       100 MB", 100ULL * 1024 * 1024, 64 * 1024},
    {"        512 MB", 512ULL * 1024 * 1024, 64 * 1024},
    {"           1 GB", 1024ULL * 1024 * 1024, 64 * 1024},
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
      auto data_span = std::span(write_buffer).first(bytes_sent + chunk).subspan(bytes_sent);

      auto write_ec = client_socket->writeAll(data_span);
      if (!write_ec)
      {
        std::cerr << "[BANDWIDTH] [" << stage.name << "] ERROR: write failed at byte " << bytes_sent << "\n";
        stage_ok = false;
        break;
      }
      bytes_sent += chunk;

      // Read echo back in 4KB batches
      std::size_t bytes_read = 0;
      while (bytes_read < chunk)
      {
        std::array<std::byte, 4096> read_buf{};
        auto to_read = static_cast<std::size_t>(std::min(std::size_t{4096}, chunk - bytes_read));
        auto read_ec = client_socket->readSome(std::span(read_buf).first(to_read));
        if (!read_ec || *read_ec == 0)
          break;
        bytes_read += static_cast<std::size_t>(*read_ec);
      }

      if (bytes_read != chunk)
      {
        std::cerr << "[BANDWIDTH] [" << stage.name << "] ERROR: read mismatch, expected " << chunk << " got "
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
  std::cout << "[BANDWIDTH] Sync TLS progressive bandwidth\n";
  std::cout << "[BANDWIDTH] Total time: " << std::fixed << std::setprecision(3) << total_elapsed << "s\n";
  if (total_elapsed > 0)
  {
    double overall_gbps = static_cast<double>(total_bytes_sent * 8.0) / total_elapsed / (1024.0 * 1024.0 * 1024.0);
    std::cout << "[BANDWIDTH] Overall: " << overall_gbps << " Gbps\n";
    std::cout << "[BANDWIDTH] Total data: " << (total_bytes_sent / (1024.0 * 1024.0 * 1024.0)) << " GB\n";
  }

  EXPECT_TRUE(all_passed);
  server.stop();
  server_thread.join();
}

}  // namespace Network::Test

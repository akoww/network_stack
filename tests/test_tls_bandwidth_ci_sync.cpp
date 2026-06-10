#include <array>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

#include "client/Client.h"
#include "fixtures/test_fixture_sync_client_server.h"
#include "fixtures/test_certificate_paths.h"
#include "server/Server.h"

namespace Network::Test
{

constexpr uint16_t TLS_BW_PORT = 12348;

TEST_F(SyncClientServerFixture, SyncTlsCiBandwidth)
{
  constexpr std::size_t total_bytes = 10 * 1024 * 1024;  // 10MB
  constexpr std::size_t chunk_size = 64 * 1024;          // 64KB

  EchoServer server(TLS_BW_PORT, getIoContext().get_executor());
  std::thread server_thread(
    [&server]()
    {
      EXPECT_FALSE(server.setCertificateChain(Network::Test::ServerCertPath()));
      EXPECT_FALSE(server.setPrivateKey(Network::Test::ServerKeyPath()));
      auto listen_result = server.listenTls();
      EXPECT_TRUE(listen_result.has_value()) << "Server listen_tls failed";
    });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client client("127.0.0.1", TLS_BW_PORT, getIoContext().get_executor());
  client.getSslContext()->set_verify_mode(asio::ssl::verify_none);
  auto connect_result = client.connectTls();
  ASSERT_TRUE(connect_result.has_value()) << "Client TLS connect failed";

  auto client_socket = std::move(*connect_result);

  // Generate data with patterned content
  std::vector<std::byte> write_buffer(total_bytes);
  for (std::size_t i = 0; i < total_bytes; ++i)
    write_buffer[i] = static_cast<std::byte>((i * 7 + 13) & 0xFF);

  auto send_start = std::chrono::high_resolution_clock::now();
  std::size_t bytes_sent = 0;

  while (bytes_sent < total_bytes)
  {
    std::size_t remain = total_bytes - bytes_sent;
    std::size_t chunk = std::min(chunk_size, remain);

    auto data_span = std::span(write_buffer).first(bytes_sent + chunk).subspan(bytes_sent);
    auto write_ec = client_socket->writeAll(data_span);
    EXPECT_TRUE(write_ec) << "Write failed at byte offset " << bytes_sent;
    if (!write_ec)
      break;
    bytes_sent += chunk;

    // Read echo back in 4KB batches
    std::size_t bytes_read = 0;
    while (bytes_read < chunk)
    {
      std::array<std::byte, 4096> read_buf{};
      auto to_read = static_cast<std::size_t>(std::min(std::size_t{4096}, chunk - bytes_read));
      auto read_ec = client_socket->readSome(std::span(read_buf).first(to_read));
      EXPECT_TRUE(read_ec) << "Read failed at read offset " << bytes_read;
      if (!read_ec || *read_ec == 0)
        break;
      bytes_read += static_cast<std::size_t>(*read_ec);
    }
    EXPECT_EQ(bytes_read, chunk) << "Echo size mismatch for chunk at byte " << (bytes_sent - chunk);
  }

  auto send_end = std::chrono::high_resolution_clock::now();
  double elapsed_s = std::chrono::duration<double>(send_end - send_start).count();
  double mb_per_s = static_cast<double>(static_cast<uint64_t>(bytes_sent) * 8) / (1024.0 * 1024.0) / elapsed_s;

  std::cout << "[BANDWIDTH] Sync CI: " << (total_bytes / (1024.0 * 1024.0)) << " MB via TLS\n";
  std::cout << "[BANDWIDTH]   Sent/echoed " << bytes_sent << " bytes in " << elapsed_s << "s\n";
  std::cout << "[BANDWIDTH]   Throughput: " << (mb_per_s / 1024.0) << " Gbps (" << mb_per_s << " Mbps)\n";

  server.stop();
  server_thread.join();
}

}  // namespace Network::Test

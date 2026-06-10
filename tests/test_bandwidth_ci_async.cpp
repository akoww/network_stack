#include <array>
#include <chrono>
#include <expected>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

#include "client/Client.h"
#include "fixtures/test_fixture_async_client_server.h"
#include "server/Server.h"

namespace Network::Test
{

constexpr uint16_t BW_PORT = 12349;

TEST_F(AsyncClientServerFixture, AsyncCiBandwidth)
{
  constexpr std::size_t total_bytes = 1024 * 1024 * 128;
  constexpr std::size_t chunk_size = 1024 * 16 * 16;
  // constexpr std::size_t total_bytes = 96 * chunk_size;  // 10MB

  EchoServer server(BW_PORT, getIoContext().get_executor());

  asio::co_spawn(
    getIoContext().get_executor(),
    [&server]() -> asio::awaitable<void>
    {
      auto listen_result = co_await server.asyncListen();
      (void)listen_result;
    },
    asio::detached);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto connect_future = asio::co_spawn(
    getIoContext().get_executor(),
    [this]() -> asio::awaitable<std::expected<std::unique_ptr<AsyncSocket>, std::error_code>>
    {
      Client client("127.0.0.1", BW_PORT, getIoContext().get_executor());
      co_return co_await client.asyncConnect();
    },
    asio::use_future);

  auto connect_result = connect_future.get();
  ASSERT_TRUE(connect_result.has_value()) << "Async connect failed";

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
    auto data_ref = std::span(write_buffer).first(bytes_sent + chunk).subspan(bytes_sent);

    auto send_chunk_start = std::chrono::high_resolution_clock::now();

    auto sock_ptr_for_capture = client_socket.get();
    auto send_future = asio::co_spawn(
      getIoContext().get_executor(),
      [sock = sock_ptr_for_capture, buf = data_ref]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
      { return sock->asyncWriteAll(buf); }, asio::use_future);

    auto write_ec = send_future.get();
    auto send_chunk_end = std::chrono::high_resolution_clock::now();

    auto elapsed_send_chunk =
      std::chrono::duration_cast<std::chrono::milliseconds>(send_chunk_end - send_chunk_start).count();
    spdlog::debug("client: sent {} bytes in {}ms", data_ref.size(), elapsed_send_chunk);

    EXPECT_TRUE(write_ec) << "Async write failed at byte offset " << bytes_sent;
    if (!write_ec)
      break;
    bytes_sent += chunk;

    // std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto recv_chunk_start = std::chrono::high_resolution_clock::now();

    // Read echo back in async 4KB batches
    std::size_t bytes_read = 0;
    while (bytes_read < chunk)
    {
      auto to_read = static_cast<std::size_t>(std::min(std::size_t{4096 * 8}, chunk - bytes_read));
      std::array<std::byte, 4096 * 8> bufs{};
      auto sock_for_read = client_socket.get();

      auto recv_future = asio::co_spawn(
        getIoContext().get_executor(),
        [sock = sock_for_read, &bufs, sz = to_read]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
        { return sock->asyncReadSome(std::span(bufs).first(sz)); }, asio::use_future);

      auto read_ec = recv_future.get();
      EXPECT_TRUE(read_ec) << "Async read failed at offset " << bytes_read;
      if (read_ec && *read_ec > 0)
        bytes_read += static_cast<std::size_t>(*read_ec);
    }

    auto recv_chunk_end = std::chrono::high_resolution_clock::now();
    auto elapsed_rcv_chunk =
      std::chrono::duration_cast<std::chrono::milliseconds>(recv_chunk_end - recv_chunk_start).count();
    spdlog::debug("client: RECV {} bytes in {}ms", data_ref.size(), elapsed_rcv_chunk);

    EXPECT_EQ(bytes_read, chunk) << "Echo size mismatch for async chunk at byte " << (bytes_sent - chunk);
  }

  auto send_end = std::chrono::high_resolution_clock::now();
  double elapsed_s = std::chrono::duration<double>(send_end - send_start).count();
  double mb_per_s = static_cast<double>(static_cast<uint64_t>(bytes_sent) * 8) / (1024.0 * 1024.0) / elapsed_s;

  std::cout << "[BANDWIDTH] Async CI: " << (total_bytes / (1024.0 * 1024.0)) << " MB via plain socket\n";
  std::cout << "[BANDWIDTH]   Sent/echoed " << bytes_sent << " bytes in " << elapsed_s << "s\n";
  std::cout << "[BANDWIDTH]   Throughput: " << (mb_per_s / 1024.0) << " Gbps (" << mb_per_s << " Mbps)\n";

  server.stop();
}

}  // namespace Network::Test

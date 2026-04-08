#pragma once

#include "SocketBase.h"

#include <asio/ip/tcp.hpp>
#include <optional>

namespace Network
{

/// @brief TCP socket implementation using ASIO.
/// Supports both synchronous and asynchronous operations.
/// This class implements both SyncSocket and AsyncSocket interfaces,
/// allowing seamless switching between blocking and non-blocking I/O.
/// @section sync_usage Synchronous Usage
/// ```cpp
/// asio::io_context io_ctx;
/// TcpSocket socket(io_ctx);
/// auto result = socket.writeAll(data);
/// ```
/// @section async_usage Asynchronous Usage
/// ```cpp
/// asio::co_spawn(io_ctx, async_operation(), asio::detached);
/// ```
class TcpSocket : public BasicSocket
{
private:
  asio::ip::tcp::socket socket_;
  std::vector<std::byte> readBuffer_;

public:
  /// @brief Construct with an io_context.
  explicit TcpSocket(asio::io_context& io_ctx);

  /// @brief Construct by moving in an existing socket.
  explicit TcpSocket(asio::ip::tcp::socket&& sock);

  ~TcpSocket() override;

  bool isConnected() const noexcept override;

  void closeSocket() noexcept override;
  void cancelSocket() noexcept override;

  bool isConnectionClosed(const std::error_code& ec) const noexcept override;

  asio::ip::tcp::socket& getSocket() { return socket_; }
  std::vector<std::byte>& getReadBuffer() { return readBuffer_; }

  // sync

  std::expected<std::size_t, std::error_code> writeAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code> readSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code> readExact(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code> readUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  // async

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExact(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;
};
}  // namespace Network

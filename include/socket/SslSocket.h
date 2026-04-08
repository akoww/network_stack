#pragma once

#include "SocketBase.h"

#include <asio/ip/tcp.hpp>
#include <asio/ssl/stream.hpp>
#include <memory>

namespace Network
{

class SslSocket : public BasicSocket
{
private:
  asio::ssl::stream<asio::ip::tcp::socket> _stream;
  std::vector<std::byte> _read_buffer;

public:
  explicit SslSocket(asio::io_context& context, asio::ssl::context& ssl_context);
  explicit SslSocket(asio::ssl::stream<asio::ip::tcp::socket> stream);
  ~SslSocket() override;

  bool isConnected() const noexcept override;

  void closeSocket() noexcept override;
  void cancelSocket() noexcept override;

  bool isConnectionClosed(const std::error_code& ec) const noexcept override;

  asio::ssl::stream<asio::ip::tcp::socket>& getSocket() { return _stream; }
  std::vector<std::byte>& getReadBuffer() { return _read_buffer; }

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

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExact(
    std::span<std::byte> bufout_fer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;
};

}  // namespace Network

#pragma once

#include "AsyncSocketInterface.h"
#include "SyncSocketInterface.h"

#include <asio/ip/tcp.hpp>
#include <asio/ssl/stream.hpp>
#include <memory>

namespace Network {

class SslSocket : public SyncSocket, public AsyncSocket {
private:
  asio::ssl::stream<asio::ip::tcp::socket> _stream;
  std::vector<std::byte> _read_buffer;

public:
  explicit SslSocket(asio::ssl::stream<asio::ip::tcp::socket> stream);
  ~SslSocket() override;

  bool is_connected() const noexcept override;

  void close_socket() noexcept override;
  void cancel_socket() noexcept override;

  bool is_connection_closed(const std::error_code &ec) const noexcept override;

  asio::ssl::stream<asio::ip::tcp::socket> &get_socket() { return _stream; }
  std::vector<std::byte> &get_read_buffer() { return _read_buffer; }

  std::expected<std::size_t, std::error_code> write_all(
      std::span<const std::byte> buffer,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code> read_some(
      std::span<std::byte> buffer,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code> read_exact(
      std::span<std::byte> buffer,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  std::expected<std::size_t, std::error_code> read_until(
      std::span<std::byte> buffer, std::string_view delimiter,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> async_write_all(
      std::span<const std::byte> buffer,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> async_read_some(
      std::span<std::byte> buffer,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> async_read_exact(
      std::span<std::byte> buffer,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;

  asio::awaitable<std::expected<std::size_t, std::error_code>> async_read_until(
      std::span<std::byte> buffer, std::string_view delimiter,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) override;
};

} // namespace Network

#pragma once

#include <asio/awaitable.hpp>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

namespace Network {

/// @brief Asynchronous socket interface.
/// Provides coroutine-based async send and receive operations.
class AsyncSocket {
public:
  virtual ~AsyncSocket() = default;

  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_some(std::span<std::byte> buffer) = 0;

  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_write_all(std::span<const std::byte> buffer) = 0;

  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_until(std::span<std::byte> buffer, std::string_view delimiter) = 0;

  virtual asio::awaitable<std::expected<std::size_t, std::error_code>>
  async_read_exact(std::span<std::byte> buffer) = 0;
};

} // namespace Network
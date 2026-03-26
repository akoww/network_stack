#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

namespace Network {

/// @brief Synchronous socket interface.
/// Provides blocking send and receive operations.
class SyncSocket {
public:
  virtual ~SyncSocket() = default;

  virtual std::expected<std::size_t, std::error_code>
  read_some(std::span<std::byte> buffer) = 0;

  virtual std::expected<std::size_t, std::error_code>
  write_all(std::span<const std::byte> buffer) = 0;

  virtual std::expected<std::size_t, std::error_code>
  read_exact(std::span<std::byte> buffer) = 0;

  virtual std::expected<std::size_t, std::error_code>
  read_until(std::span<std::byte> buffer, std::string_view delimiter) = 0;
};

} // namespace Network
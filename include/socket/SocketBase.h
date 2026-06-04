#pragma once

#include <expected>

#include <asio/cancellation_signal.hpp>
#include <asio/awaitable.hpp>

#include <system_error>
#include <vector>
#include <span>
#include <optional>
#include <chrono>

namespace Network
{

/// @brief Abstract base class for all socket types.
/// Defines common functionality regardless of sync/async implementation.
/// All socket classes must inherit from this class to ensure consistent
/// behavior.
class SocketBase
{
  unsigned int _id = 0;
  std::vector<std::byte> _read_buffer;
  asio::cancellation_signal _cancel_signal;

public:
  SocketBase();
  virtual ~SocketBase() = default;

  /// @brief Check if the socket is currently connected to a remote endpoint.
  /// @return true if connected, false otherwise.
  [[nodiscard]]
  virtual bool isConnected() const noexcept = 0;

  virtual void closeSocket() noexcept = 0;
  virtual void cancelSocket();

  [[nodiscard]]
  virtual bool isConnectionClosed(const std::error_code& ec) const noexcept = 0;

  std::vector<std::byte>& getReadBuffer() { return _read_buffer; }
  unsigned int getId() const;

  asio::cancellation_signal& cancelSignal() { return _cancel_signal; }
};

/// @brief Synchronous socket interface.
/// Provides blocking send and receive operations.
/// All synchronous socket implementations must inherit from this interface.
/// @note Operations block until completion or error; use async versions for
/// non-blocking behavior.
class SyncSocket
{
public:
  virtual ~SyncSocket() = default;

  /// @brief Receive data from the socket without blocking indefinitely.
  /// @param out_buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received, or error code on failure.
  /// @note May return fewer bytes than buffer size.
  virtual std::expected<std::size_t, std::error_code> readSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Send all data in the buffer via the socket.
  /// @param in_buffer Span of data to send.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes sent, or error code on failure.
  virtual std::expected<std::size_t, std::error_code> writeAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive exactly len bytes into the buffer.
  /// @param buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  virtual std::expected<std::size_t, std::error_code> readExact(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive data until delimiter is found.
  /// @param buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (including delimiter), or error code.
  virtual std::expected<std::size_t, std::error_code> readUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;
};

/// @brief Asynchronous socket interface.
/// Provides coroutine-based async send and receive operations.
/// All asynchronous socket implementations must inherit from this interface.
/// @note Operations return asio::awaitable and must be co_awaited.
class AsyncSocket
{
public:
  virtual ~AsyncSocket() = default;

  /// @brief Receive data from the socket asynchronously.
  /// @param out_buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received, or error code on failure.
  /// @note May return fewer bytes than buffer size.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadSome(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Send all data in the buffer via the socket asynchronously.
  /// @param in_buffer Span of data to send.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes sent, or error code on failure.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAll(
    std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive data until delimiter is found asynchronously.
  /// @param out_buffer Span to receive data into.
  /// @param delimiter Delimiter string to search for (e.g., "\\n").
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (including delimiter), or error code.
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadUntil(
    std::span<std::byte> out_buffer,
    std::string_view delimiter,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;

  /// @brief Receive exactly len bytes into the buffer asynchronously.
  /// @param out_buffer Span to receive data into.
  /// @param timeout Optional timeout duration. If std::nullopt, no timeout is
  /// applied.
  /// @return Number of bytes received (always equals buffer size on success),
  /// or error code on failure (timeout, connection closed, etc.).
  virtual asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExact(
    std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout = std::nullopt) = 0;
};

/// @brief Concrete socket class implementing both sync and async interfaces.
/// Provides dual-mode socket that supports both blocking and non-blocking operations.
/// TcpSocket and SslSocket inherit from this class.
class DualSocket : public SocketBase, public AsyncSocket, public SyncSocket
{
public:
  virtual ~DualSocket() = default;
};

}  // namespace Network
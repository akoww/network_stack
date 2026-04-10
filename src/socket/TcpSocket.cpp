#include "socket/TcpSocket.h"
#include "core/ErrorCodes.h"
#include <socket/SocketBaseImpl.h>

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_future.hpp>
#include <asio/error.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/streambuf.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>
#include <variant>

using namespace asio::experimental::awaitable_operators;

namespace Network
{

namespace
{
}  // namespace

TcpSocket::TcpSocket(asio::io_context& io_ctx) : _socket(io_ctx)
{
  spdlog::trace("[{}] TCP socket created", getId());
}

TcpSocket::TcpSocket(asio::ip::tcp::socket&& sock) : _socket(std::move(sock))
{
  spdlog::trace("[{}] TCP socket created (moved)", getId());
}

TcpSocket::~TcpSocket()
{
  spdlog::trace("[{}] TCP socket closing", getId());
  cancelSocket();
  closeSocket();
  spdlog::trace("[{}] TCP socket closed", getId());
}

void TcpSocket::closeSocket() noexcept
{
  if (_socket.is_open())
  {
    std::error_code ec;
    _socket.shutdown(asio::socket_base::shutdown_send, ec);
    _socket.close(ec);
  }
}

void TcpSocket::cancelSocket() noexcept
{
  SocketBase::cancelSocket();
  if (_socket.is_open())
  {
    std::error_code ec;
    _socket.cancel(ec);
  }
}

bool TcpSocket::isConnected() const noexcept
{
  return _socket.is_open();
}

bool TcpSocket::isConnectionClosed(const std::error_code& ec) const noexcept
{
  return (ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::broken_pipe ||
          ec == asio::error::not_connected);
}

std::expected<std::size_t, std::error_code> TcpSocket::writeAll(std::span<const std::byte> in_buffer,
                                                                std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::debug("[{}] writeAll {} bytes", getId(), in_buffer.size());

  auto future = asio::co_spawn(
    _socket.get_executor(), [this, in_buffer, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncWriteAll(in_buffer, timeout); }, asio::use_future);

  try
  {
    return future.get();
  }
  catch (...)
  {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

std::expected<std::size_t, std::error_code> TcpSocket::readSome(std::span<std::byte> out_buffer,
                                                                std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] readSome", getId());

  auto future = asio::co_spawn(
    _socket.get_executor(),
    [this, out_buffer, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncReadSome(out_buffer, timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("[{}] readSome {} bytes", getId(), *result);
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

std::expected<std::size_t, std::error_code> TcpSocket::readExact(std::span<std::byte> out_buffer,
                                                                 std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] readExact", getId());

  auto future = asio::co_spawn(
    _socket.get_executor(),
    [this, out_buffer, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncReadExact(out_buffer, timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("[{}] readExact {} bytes", getId(), *result);
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

std::expected<std::size_t, std::error_code> TcpSocket::readUntil(std::span<std::byte> out_buffer,
                                                                 std::string_view delimiter,
                                                                 std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] readUntil '{}'", getId(), delimiter);

  auto future = asio::co_spawn(
    _socket.get_executor(),
    [this, out_buffer, delimiter, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncReadUntil(out_buffer, delimiter, timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("[{}] readUntil {} bytes", getId(), *result);
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TcpSocket::asyncWriteAll(
  std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncWriteAllCommon(*this, in_buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TcpSocket::asyncReadSome(
  std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncReadSomeCommon(*this, out_buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TcpSocket::asyncReadExact(
  std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncReadExactCommon(*this, out_buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TcpSocket::asyncReadUntil(
  std::span<std::byte> out_buffer, std::string_view delim, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncReadUntilCommon(*this, out_buffer, delim, timeout);
}

}  // namespace Network

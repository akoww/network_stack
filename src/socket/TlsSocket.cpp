#include "socket/TlsSocket.h"
#include "socket/details/TlsSocketDetail.h"

#include "core/ErrorCodes.h"
#include "core/ErrorTranslation.h"
#include <socket/details/SocketBaseDetail.h>
#include "socket/TlsOptions.h"

#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/error.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/read.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <expected>
#include <future>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

TlsSocket::TlsSocket(asio::ssl::stream<asio::ip::tcp::socket> stream) : _p(std::make_shared<Private>(std::move(stream)))
{
  spdlog::trace("[{}] SSL socket created", getId());
}

TlsSocket::~TlsSocket()
{
  spdlog::trace("[{}] SSL socket closing", getId());
  cancelSocket();
  closeSocket();
}

void TlsSocket::closeSocket() noexcept
{
  if (detail::getSocket(*this).next_layer().is_open())
  {
    std::error_code ec;
    detail::getSocket(*this).next_layer().close(ec);
  }
}

void TlsSocket::cancelSocket() noexcept
{
  SocketBase::cancelSocket();
  if (detail::getSocket(*this).next_layer().is_open())
  {
    std::error_code ec;
    detail::getSocket(*this).next_layer().cancel(ec);
  }
}

bool TlsSocket::isConnected() const noexcept
{
  return detail::getSocket(*this).next_layer().is_open();
}

bool TlsSocket::isConnectionClosed(const std::error_code& ec) const noexcept
{
  return (ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::broken_pipe ||
          ec == asio::error::not_connected || ec == asio::ssl::error::stream_truncated);
}

std::expected<std::size_t, std::error_code> TlsSocket::writeAll(std::span<const std::byte> in_buffer,
                                                                std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::debug("[{}] SSL writeAll {} bytes", getId(), in_buffer.size());

  auto future = asio::co_spawn(
    detail::getSocket(*this).next_layer().get_executor(),
    [this, in_buffer, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncWriteAll(in_buffer, timeout); }, asio::use_future);

  try
  {
    return future.get();
  }
  catch (...)
  {
    return std::unexpected(makeWriteError(std::make_error_code(std::errc::operation_canceled)));
  }
}

std::expected<std::size_t, std::error_code> TlsSocket::readSome(std::span<std::byte> out_buffer,
                                                                std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] SSL readSome", getId());

  auto future = asio::co_spawn(
    detail::getSocket(*this).next_layer().get_executor(),
    [this, out_buffer, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncReadSome(out_buffer, timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("[{}] SSL readSome {} bytes", getId(), *result);
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(makeReadError(std::make_error_code(std::errc::operation_canceled)));
  }
}

std::expected<std::size_t, std::error_code> TlsSocket::readExact(std::span<std::byte> out_buffer,
                                                                 std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] SSL readExact", getId());
  auto future = asio::co_spawn(
    detail::getSocket(*this).next_layer().get_executor(),
    [this, out_buffer, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncReadExact(out_buffer, timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("[{}] SSL readExact {} bytes", getId(), *result);
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(makeReadError(std::make_error_code(std::errc::operation_canceled)));
  }
}

std::expected<std::size_t, std::error_code> TlsSocket::readUntil(std::span<std::byte> out_buffer,
                                                                 std::string_view delimiter,
                                                                 std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] SSL readUntil '{}'", getId(), delimiter);

  auto future = asio::co_spawn(
    detail::getSocket(*this).next_layer().get_executor(),
    [this, out_buffer, delimiter, timeout]() -> asio::awaitable<std::expected<std::size_t, std::error_code>>
    { return asyncReadUntil(out_buffer, delimiter, timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("[{}] SSL readUntil {} bytes", getId(), *result);
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(makeReadError(std::make_error_code(std::errc::operation_canceled)));
  }
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TlsSocket::asyncWriteAll(
  std::span<const std::byte> in_buffer, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncWriteAllCommon(*this, in_buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TlsSocket::asyncReadSome(
  std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncReadSomeCommon(*this, out_buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TlsSocket::asyncReadExact(
  std::span<std::byte> out_buffer, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncReadExactCommon(*this, out_buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TlsSocket::asyncReadUntil(
  std::span<std::byte> out_buffer, std::string_view delim, std::optional<std::chrono::milliseconds> timeout)
{
  return socket_detail::asyncReadUntilCommon(*this, out_buffer, delim, timeout);
}

}  // namespace Network

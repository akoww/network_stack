#pragma once

#include "AsyncSocketInterface.h"
#include "SyncSocketInterface.h"
#include "core/ErrorCodes.h"
#include "socket/SocketBase.h"

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#ifdef ASIO_HAS_STD_SSL
  #include <asio/ssl/error.hpp>
#endif
#include <chrono>
#include <expected>
#include <future>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>
#include <type_traits>
#include <variant>

namespace Network
{

using namespace asio::experimental::awaitable_operators;

namespace socket_detail
{

inline std::error_code makeTimeoutError()
{
  return make_error_code(Network::Error::ConnectionTimeout);
}

inline std::size_t move_data(std::vector<std::byte>& buffer, std::span<std::byte> out, const std::size_t max_len)
{
  std::size_t bytes_to_copy = std::min({max_len, buffer.size(), out.size()});

  std::ranges::copy_n(buffer.begin(), static_cast<std::ptrdiff_t>(bytes_to_copy), out.begin());

  buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(bytes_to_copy));

  auto as_sv = std::string_view(reinterpret_cast<const char*>(out.data()), bytes_to_copy);
  spdlog::trace("got data: \"{}\"", as_sv);

  return bytes_to_copy;
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadSomeCommon(
  SocketType& socket, std::span<std::byte> out, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncReadSome", socket.getId());

  std::error_code ec;
  std::ranges::fill(out, std::byte{0});

  if (!timeout)
    timeout = std::chrono::hours(24);

  if (out.empty())
  {
    co_return std::size_t{0};
  }

  auto& readBuffer = socket.getReadBuffer();
  auto& underlayingSocket = socket.getSocket();

  if (readBuffer.empty())
  {
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    auto executor = co_await asio::this_coro::executor;
    auto timer = asio::steady_timer(executor);
    timer.expires_after(timeout.value());

    std::error_code ec1, ec2;
    std::size_t bytes_received = 0;

    auto result = co_await(underlayingSocket.async_read_some(buffer, asio::redirect_error(asio::use_awaitable, ec1)) ||
                           timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

    if (result.index() == 1)
    {
      socket.cancelSocket();
      co_return std::unexpected(makeTimeoutError());
    }

    if (socket.isConnectionClosed(ec1))
    {
      socket.closeSocket();
      co_return std::unexpected(ec1);
    }
    if (ec1)
    {
      co_return std::unexpected(ec1);
    }

    bytes_received = std::get<std::size_t>(result);
    if (bytes_received == 0)
    {
      spdlog::warn("[{}] buffer empty, cant read any bytes and connection is closed", socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    dyn_buf.commit(bytes_received);
  }

  co_return move_data(readBuffer, out, std::numeric_limits<std::size_t>::max());
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExactCommon(
  SocketType& socket, std::span<std::byte> out, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncReadExact", socket.getId());

  std::ranges::fill(out, std::byte{0});
  if (!timeout)
    timeout = std::chrono::hours(24);

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  auto& readBuffer = socket.getReadBuffer();
  auto& underlayingSocket = socket.getSocket();

  std::error_code ec1, ec2;

  while (readBuffer.size() < out.size())
  {
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    std::size_t readBytes = 0;

    auto result = co_await(underlayingSocket.async_read_some(buffer, asio::redirect_error(asio::use_awaitable, ec1)) ||
                           timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

    if (result.index() == 1)
    {
      socket.cancelSocket();
      co_return std::unexpected(makeTimeoutError());
    }

    if (socket.isConnectionClosed(ec1))
    {
      socket.closeSocket();
      co_return std::unexpected(ec1);
    }
    if (ec1)
    {
      co_return std::unexpected(ec1);
    }

    readBytes = std::get<std::size_t>(result);
    if (readBytes == 0)
    {
      spdlog::warn(
        "[{}] buffer not enough, cant read any bytes and connection "
        "is closed",
        socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    dyn_buf.commit(readBytes);
  }

  co_return move_data(readBuffer, out, std::numeric_limits<std::size_t>::max());
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadUntilCommon(
  SocketType& socket,
  std::span<std::byte> out,
  std::string_view delim,
  std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncReadUntil", socket.getId());

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto delimAsSv = std::as_bytes(std::span(delim));

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  auto& readBuffer = socket.getReadBuffer();
  auto& underlayingSocket = socket.getSocket();

  std::error_code ec1, ec2;

  while (true)
  {
    auto it = std::search(readBuffer.begin(), readBuffer.end(), std::begin(delimAsSv), std::end(delimAsSv));
    if (it != readBuffer.end())
    {
      std::size_t len = static_cast<std::size_t>(it - readBuffer.begin() + 1);
      if (len > out.size())
      {
        spdlog::warn("[{}] result buffer has not enough space for result", socket.getId());
        co_return std::unexpected(make_error_code(Network::Error::ProtocolError));
      }

      co_return move_data(readBuffer, out, len + 1);
    }
    std::size_t readBytes = 0;
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    auto result = co_await(underlayingSocket.async_read_some(buffer, asio::redirect_error(asio::use_awaitable, ec1)) ||
                           timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

    if (result.index() == 1)
    {
      socket.cancelSocket();
      co_return std::unexpected(makeTimeoutError());
    }

    if (socket.isConnectionClosed(ec1))
    {
      socket.closeSocket();
      co_return std::unexpected(ec1);
    }
    if (ec1)
    {
      co_return std::unexpected(ec1);
    }

    readBytes = std::get<std::size_t>(result);
    if (readBytes == 0)
    {
      spdlog::warn(
        "[{}] buffer not enough, cant read any bytes and connection "
        "is closed",
        socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    dyn_buf.commit(readBytes);
  }

  co_return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAllCommon(
  SocketType& socket, std::span<const std::byte> buffer, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] async_writeAll", socket.getId());

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  auto& underlayingSocket = socket.getSocket();

  std::error_code ec1, ec2;
  std::size_t bytesTransferred = 0;

  auto result = co_await(
    asio::async_write(underlayingSocket, asio::buffer(buffer), asio::redirect_error(asio::use_awaitable, ec1)) ||
    timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

  if (result.index() == 1)
  {
    socket.cancelSocket();
    co_return std::unexpected(makeTimeoutError());
  }

  bytesTransferred = std::get<std::size_t>(result);

  if (socket.isConnectionClosed(ec1))
  {
    socket.closeSocket();
    co_return std::unexpected(ec1);
  }
  if (ec1)
  {
    co_return std::unexpected(ec1);
  }

  co_return bytesTransferred;
}

}  // namespace socket_detail

}  // namespace Network

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
#include <asio/experimental/cancellation_condition.hpp>
#include <asio/experimental/parallel_group.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <asio/bind_cancellation_slot.hpp>

#ifdef ASIO_HAS_STD_SSL
  #include <asio/ssl/error.hpp>
#endif
#include <chrono>
#include <expected>
#include <future>
#include <tuple>
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
  return make_error_code(Network::Error::CONNECTION_TIMEOUT);
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
  auto& readBuffer = socket.getReadBuffer();
  auto& underlyingSocket = socket.getSocket();

  auto sock_id = underlyingSocket.lowest_layer().native_handle();
  spdlog::trace("[{}] asyncReadSome (fd={})", socket.getId(), sock_id);

  std::error_code ec;
  std::ranges::fill(out, std::byte{0});

  if (!timeout)
    timeout = std::chrono::hours(24);

  if (out.empty())
  {
    co_return std::size_t{0};
  }

  if (readBuffer.empty())
  {
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    auto executor = co_await asio::this_coro::executor;
    auto timer = asio::steady_timer(executor, timeout.value());

    std::error_code ec1, ec2;
    std::size_t bytes_received = 0;

    spdlog::debug("[{}] waiting for data (timeout={}ms)", socket.getId(), timeout->count());

    auto [order, results] =
      co_await asio::experimental::make_parallel_group(
        underlyingSocket.async_read_some(buffer, asio::redirect_error(asio::deferred, ec1)),
        timer.async_wait(asio::redirect_error(asio::deferred, ec2)))
        .async_wait(asio::experimental::wait_for_one(),
                    asio::bind_cancellation_slot(socket.cancelSignal().slot(), asio::use_awaitable));

    if (order[0] == 1)
    {
      socket.cancelSocket();
      spdlog::debug("[{}] read timed out", socket.getId());
      co_return std::unexpected(makeTimeoutError());
    }
    if (socket.isConnectionClosed(ec1))
    {
      socket.closeSocket();
      spdlog::debug("[{}] connection closed during read", socket.getId());
      co_return std::unexpected(ec1);
    }
    if (ec1)
    {
      spdlog::debug("[{}] read error: {}", socket.getId(), ec1.message());
      co_return std::unexpected(ec1);
    }

    bytes_received = results;
    if (bytes_received == 0)
    {
      spdlog::warn("[{}] buffer empty, cant read any bytes and connection is closed", socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    spdlog::debug("[{}] received {} bytes", socket.getId(), bytes_received);
    dyn_buf.commit(bytes_received);
  }

  co_return move_data(readBuffer, out, std::numeric_limits<std::size_t>::max());
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExactCommon(
  SocketType& socket, std::span<std::byte> out, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncReadExact {} bytes", socket.getId(), out.size());

  std::ranges::fill(out, std::byte{0});
  if (!timeout)
    timeout = std::chrono::hours(24);

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor, timeout.value());

  auto& readBuffer = socket.getReadBuffer();
  auto& underlyingSocket = socket.getSocket();

  std::error_code ec1, ec2;
  std::size_t total_read = 0;

  while (readBuffer.size() < out.size())
  {
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    std::size_t readBytes = 0;

    spdlog::debug("[{}] reading (need {} more, timeout={}ms)", socket.getId(), out.size() - total_read,
                  timeout->count());

    auto [order, result] =
      co_await asio::experimental::make_parallel_group(
        underlyingSocket.async_read_some(buffer, asio::redirect_error(asio::deferred, ec1)),
        timer.async_wait(asio::redirect_error(asio::deferred, ec2)))
        .async_wait(asio::experimental::wait_for_one(),
                    asio::bind_cancellation_slot(socket.cancelSignal().slot(), asio::use_awaitable));

    if (order[0] == 1)
    {
      socket.cancelSocket();
      spdlog::debug("[{}] readExact timed out", socket.getId());
      co_return std::unexpected(makeTimeoutError());
    }

    if (socket.isConnectionClosed(ec1))
    {
      socket.closeSocket();
      spdlog::debug("[{}] connection closed during readExact", socket.getId());
      co_return std::unexpected(ec1);
    }
    if (ec1)
    {
      spdlog::debug("[{}] readExact error: {}", socket.getId(), ec1.message());
      co_return std::unexpected(ec1);
    }

    readBytes = result;
    if (readBytes == 0)
    {
      spdlog::warn(
        "[{}] buffer not enough, cant read any bytes and connection "
        "is closed",
        socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    spdlog::debug("[{}] readExact got {} bytes", socket.getId(), readBytes);
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
  spdlog::trace("[{}] asyncReadUntil delim='{}'", socket.getId(), delim);

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto delimAsSv = std::as_bytes(std::span(delim));

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  auto& readBuffer = socket.getReadBuffer();
  auto& underlyingSocket = socket.getSocket();

  std::error_code ec1, ec2;

  spdlog::debug("[{}] waiting for delimiter '{}' (timeout={}ms)", socket.getId(), delim, timeout->count());

  while (true)
  {
    auto it = std::search(readBuffer.begin(), readBuffer.end(), std::begin(delimAsSv), std::end(delimAsSv));
    if (it != readBuffer.end())
    {
      std::size_t len = static_cast<std::size_t>(it - readBuffer.begin() + 1);
      if (len > out.size())
      {
        spdlog::warn("[{}] result buffer has not enough space for result", socket.getId());
        co_return std::unexpected(make_error_code(Network::Error::PROTOCOL_ERROR));
      }

      spdlog::debug("[{}] found delimiter, moving {} bytes", socket.getId(), len);
      co_return move_data(readBuffer, out, len + 1);
    }
    std::size_t readBytes = 0;
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    auto [order, result] =
      co_await asio::experimental::make_parallel_group(
        underlyingSocket.async_read_some(buffer, asio::redirect_error(asio::deferred, ec1)),
        timer.async_wait(asio::redirect_error(asio::deferred, ec2)))
        .async_wait(asio::experimental::wait_for_one(),
                    asio::bind_cancellation_slot(socket.cancelSignal().slot(), asio::use_awaitable));

    if (order[0] == 1)
    {
      socket.cancelSocket();
      spdlog::debug("[{}] readUntil timed out", socket.getId());
      co_return std::unexpected(makeTimeoutError());
    }

    if (socket.isConnectionClosed(ec1))
    {
      socket.closeSocket();
      spdlog::debug("[{}] connection closed during readUntil", socket.getId());
      co_return std::unexpected(ec1);
    }
    if (ec1)
    {
      spdlog::debug("[{}] readUntil error: {}", socket.getId(), ec1.message());
      co_return std::unexpected(ec1);
    }

    readBytes = result;
    if (readBytes == 0)
    {
      spdlog::warn(
        "[{}] buffer not enough, cant read any bytes and connection "
        "is closed",
        socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    spdlog::debug("[{}] readUntil got {} bytes", socket.getId(), readBytes);
    dyn_buf.commit(readBytes);
  }

  co_return std::unexpected(make_error_code(Network::Error::PROTOCOL_ERROR));
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAllCommon(
  SocketType& socket, std::span<const std::byte> buffer, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncWriteAll {} bytes", socket.getId(), buffer.size());

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  auto& underlyingSocket = socket.getSocket();

  std::error_code ec1, ec2;
  std::size_t bytesTransferred = 0;

  spdlog::debug("[{}] writing {} bytes (timeout={}ms)", socket.getId(), buffer.size(), timeout->count());

  auto [order, result] =
    co_await asio::experimental::make_parallel_group(
      asio::async_write(underlyingSocket, asio::buffer(buffer), asio::redirect_error(asio::deferred, ec1)),
      timer.async_wait(asio::redirect_error(asio::deferred, ec2)))
      .async_wait(asio::experimental::wait_for_one(),
                  asio::bind_cancellation_slot(socket.cancelSignal().slot(), asio::use_awaitable));

  if (order[0] == 1)
  {
    socket.cancelSocket();
    spdlog::debug("[{}] write timed out", socket.getId());
    co_return std::unexpected(makeTimeoutError());
  }

  bytesTransferred = result;

  if (socket.isConnectionClosed(ec1))
  {
    socket.closeSocket();
    spdlog::debug("[{}] connection closed during write", socket.getId());
    co_return std::unexpected(ec1);
  }
  if (ec1)
  {
    spdlog::debug("[{}] write error: {}", socket.getId(), ec1.message());
    co_return std::unexpected(ec1);
  }

  spdlog::debug("[{}] wrote {} bytes", socket.getId(), bytesTransferred);
  co_return bytesTransferred;
}

}  // namespace socket_detail

}  // namespace Network

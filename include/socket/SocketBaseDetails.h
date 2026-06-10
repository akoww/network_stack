#pragma once

#include "core/ErrorCodes.h"
#include "socket/SocketBase.h"

#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/read.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <atomic>
#include <chrono>
#include <expected>
#include <limits>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

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

template <typename SocketType, typename MutableBuffer>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadWithTimeout(SocketType& socket,
                                                                                  MutableBuffer buffer,
                                                                                  std::chrono::milliseconds timeout)
{
  auto& underlyingSocket = socket.getSocket();
  auto executor = co_await asio::this_coro::executor;

  std::atomic<bool> timed_out{false};
  asio::steady_timer timer(executor, timeout);

  timer.async_wait(
    [&timed_out, &socket](const std::error_code& ec)
    {
      if (!ec)
      {
        timed_out.store(true);
        socket.cancelSocket();
      }
    });

  std::error_code read_ec;
  auto bytes = co_await underlyingSocket.async_read_some(
    buffer,
    asio::bind_cancellation_slot(socket.cancelSignal().slot(), asio::redirect_error(asio::use_awaitable, read_ec)));

  timer.cancel();

  if (read_ec)
  {
    if (read_ec == asio::error::operation_aborted && timed_out.load())
    {
      spdlog::debug("[{}] read timed out", socket.getId());
      co_return std::unexpected(makeTimeoutError());
    }
    co_return std::unexpected(read_ec);
  }

  co_return bytes;
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadSomeCommon(
  SocketType& socket, std::span<std::byte> out, std::optional<std::chrono::milliseconds> timeout)
{
  auto& readBuffer = socket.getReadBuffer();
  auto& underlyingSocket = socket.getSocket();

  if (!socket.isConnected())
  {
    spdlog::debug("[{}] readSome on unconnected socket", socket.getId());
    co_return std::unexpected(asio::error::not_connected);
  }

  auto sock_id = underlyingSocket.lowest_layer().native_handle();
  spdlog::trace("[{}] asyncReadSome (fd={})", socket.getId(), sock_id);

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

    spdlog::debug("[{}] waiting for data (timeout={}ms)", socket.getId(), timeout->count());

    auto result = co_await asyncReadWithTimeout(socket, buffer, timeout.value());

    if (!result)
      co_return std::unexpected(result.error());

    if (*result == 0)
    {
      spdlog::warn("[{}] buffer empty, cant read any bytes and connection is closed", socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    spdlog::debug("[{}] received {} bytes", socket.getId(), *result);
    dyn_buf.commit(*result);
  }

  co_return move_data(readBuffer, out, std::numeric_limits<std::size_t>::max());
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncReadExactCommon(
  SocketType& socket, std::span<std::byte> out, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncReadExact {} bytes", socket.getId(), out.size());

  if (!socket.isConnected())
  {
    spdlog::debug("[{}] readExact on unconnected socket", socket.getId());
    co_return std::unexpected(asio::error::not_connected);
  }

  std::ranges::fill(out, std::byte{0});
  if (!timeout)
    timeout = std::chrono::hours(24);

  auto& readBuffer = socket.getReadBuffer();

  std::size_t total_read = 0;

  while (readBuffer.size() < out.size())
  {
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    spdlog::debug("[{}] reading (need {} more, timeout={}ms)", socket.getId(), out.size() - total_read,
                  timeout->count());

    auto result = co_await asyncReadWithTimeout(socket, buffer, timeout.value());

    if (!result)
      co_return std::unexpected(result.error());

    if (*result == 0)
    {
      spdlog::warn(
        "[{}] buffer not enough, cant read any bytes and connection "
        "is closed",
        socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    spdlog::debug("[{}] readExact got {} bytes", socket.getId(), *result);
    dyn_buf.commit(*result);
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

  if (!socket.isConnected())
  {
    spdlog::debug("[{}] readUntil on unconnected socket", socket.getId());
    co_return std::unexpected(asio::error::not_connected);
  }

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto delimAsSv = std::as_bytes(std::span(delim));

  auto& readBuffer = socket.getReadBuffer();

  spdlog::debug("[{}] waiting for delimiter '{}' (timeout={}ms)", socket.getId(), delim, timeout->count());

  while (true)
  {
    auto it = std::search(readBuffer.begin(), readBuffer.end(), std::begin(delimAsSv), std::end(delimAsSv));
    if (it != readBuffer.end())
    {
      auto len = static_cast<std::size_t>(it - readBuffer.begin() + 1);
      if (len > out.size())
      {
        spdlog::warn("[{}] result buffer has not enough space for result", socket.getId());
        co_return std::unexpected(make_error_code(Network::Error::PROTOCOL_ERROR));
      }

      spdlog::debug("[{}] found delimiter, moving {} bytes", socket.getId(), len);
      co_return move_data(readBuffer, out, len + 1);
    }
    auto dyn_buf = asio::dynamic_buffer(readBuffer);
    auto buffer = dyn_buf.prepare(out.size());

    auto result = co_await asyncReadWithTimeout(socket, buffer, timeout.value());

    if (!result)
      co_return std::unexpected(result.error());

    if (*result == 0)
    {
      spdlog::warn(
        "[{}] buffer not enough, cant read any bytes and connection "
        "is closed",
        socket.getId());
      co_return std::unexpected(asio::error::eof);
    }
    spdlog::debug("[{}] readUntil got {} bytes", socket.getId(), *result);
    dyn_buf.commit(*result);
  }

  co_return std::unexpected(make_error_code(Network::Error::PROTOCOL_ERROR));
}

template <typename SocketType>
asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteAllCommon(
  SocketType& socket, std::span<const std::byte> buffer, std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] asyncWriteAll {} bytes", socket.getId(), buffer.size());

  if (!socket.isConnected())
  {
    spdlog::debug("[{}] writeAll on unconnected socket", socket.getId());
    co_return std::unexpected(asio::error::not_connected);
  }

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto& underlyingSocket = socket.getSocket();
  auto executor = co_await asio::this_coro::executor;

  std::atomic<bool> timed_out{false};
  asio::steady_timer timer(executor, timeout.value());

  timer.async_wait(
    [&timed_out, &socket](const std::error_code& ec)
    {
      if (!ec)
      {
        timed_out.store(true);
        socket.cancelSocket();
      }
    });

  spdlog::debug("[{}] writing {} bytes (timeout={}ms)", socket.getId(), buffer.size(), timeout->count());

  std::error_code write_ec;
  auto bytes = co_await asio::async_write(
    underlyingSocket, asio::buffer(buffer),
    asio::bind_cancellation_slot(socket.cancelSignal().slot(), asio::redirect_error(asio::use_awaitable, write_ec)));

  timer.cancel();

  if (write_ec)
  {
    if (write_ec == asio::error::operation_aborted && timed_out.load())
    {
      spdlog::debug("[{}] write timed out", socket.getId());
      co_return std::unexpected(makeTimeoutError());
    }
    co_return std::unexpected(write_ec);
  }

  spdlog::debug("[{}] wrote {} bytes", socket.getId(), bytes);
  co_return bytes;
}

}  // namespace socket_detail

}  // namespace Network

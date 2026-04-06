
#include "socket/TcpSocket.h"
#include "core/ErrorCodes.h"

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
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

namespace Network {

namespace {

unsigned int id_count = 1;

std::error_code make_timeout_error() {
  return make_error_code(Network::Error::ConnectionTimeout);
}

bool is_connection_closed(const std::error_code &ec) {
  return (ec == asio::error::eof || ec == asio::error::connection_reset ||
          ec == asio::error::broken_pipe || ec == asio::error::not_connected);
}

std::size_t move_data(std::vector<std::byte> &buffer, std::span<std::byte> out,
                      const std::size_t max_len) {
  std::size_t bytes_to_copy = std::min({max_len, buffer.size(), out.size()});

  std::ranges::copy_n(buffer.begin(),
                      static_cast<std::ptrdiff_t>(bytes_to_copy), out.begin());

  buffer.erase(buffer.begin(),
               buffer.begin() + static_cast<std::ptrdiff_t>(bytes_to_copy));

  auto as_sv = std::string_view(reinterpret_cast<const char *>(out.data()),
                                bytes_to_copy);
  spdlog::trace("got data: \"{}\"", as_sv);

  return bytes_to_copy;
}

} // namespace

TcpSocket::TcpSocket(asio::io_context &io_ctx)
    : socket_(io_ctx), id_(id_count++) {}

TcpSocket::TcpSocket(asio::ip::tcp::socket &&sock)
    : socket_(std::move(sock)), id_(id_count++) {}

TcpSocket::~TcpSocket() {
  if (socket_.is_open()) {
    std::error_code ec;
    socket_.close(ec);
  }
}

bool TcpSocket::is_connected() const noexcept { return socket_.is_open(); }

unsigned int TcpSocket::get_id() const { return id_; }

std::expected<std::size_t, std::error_code>
TcpSocket::write_all(std::span<const std::byte> buffer,
                     std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] write_all", get_id());

  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      socket_.get_executor(),
      [this, buffer, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await async_write_all(buffer, timeout);
        promise->set_value(result);
      },
      asio::detached);

  try {
    return future.get();
  } catch (...) {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

std::expected<std::size_t, std::error_code>
TcpSocket::read_some(std::span<std::byte> buffer,
                     std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] read_some", get_id());

  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      socket_.get_executor(),
      [this, buffer, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await async_read_some(buffer, timeout);
        promise->set_value(result);
      },
      asio::detached);

  try {
    return future.get();
  } catch (...) {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

std::expected<std::size_t, std::error_code>
TcpSocket::read_exact(std::span<std::byte> buffer,
                      std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] read_exact", get_id());
  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      socket_.get_executor(),
      [this, buffer, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await async_read_exact(buffer, timeout);
        promise->set_value(result);
      },
      asio::detached);

  try {
    return future.get();
  } catch (...) {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

std::expected<std::size_t, std::error_code>
TcpSocket::read_until(std::span<std::byte> buffer, std::string_view delimiter,
                      std::optional<std::chrono::milliseconds> timeout) {

  spdlog::trace("[{}] read_until", get_id());

  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      socket_.get_executor(),
      [this, buffer, delimiter, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await async_read_until(buffer, delimiter, timeout);
        promise->set_value(result);
      },
      asio::detached);

  try {
    return future.get();
  } catch (...) {
    return std::unexpected(std::make_error_code(std::errc::operation_canceled));
  }
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
TcpSocket::async_write_all(std::span<const std::byte> buffer,
                           std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] async_write_all", get_id());
  if (!timeout)
    timeout = std::chrono::hours(24);

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  std::error_code ec1, ec2;
  std::size_t bytes_transferred = 0;

  auto result = co_await (
      asio::async_write(socket_, asio::buffer(buffer),
                        asio::redirect_error(asio::use_awaitable, ec1)) ||
      timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

  if (result.index() == 1) {
    socket_.cancel();
    co_return std::unexpected(make_timeout_error());
  }

  bytes_transferred = std::get<std::size_t>(result);

  if (is_connection_closed(ec1)) {
    socket_.close();
    co_return std::unexpected(ec1);
  }
  if (ec1) {
    co_return std::unexpected(ec1);
  }

  co_return bytes_transferred;
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
TcpSocket::async_read_some(std::span<std::byte> out,
                           std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] async_read_some", get_id());

  std::error_code ec;
  std::ranges::fill(out, std::byte{0});

  if (!timeout)
    timeout = std::chrono::hours(24);

  if (out.empty()) {
    co_return std::size_t{0};
  }

  if (read_buffer_.empty()) {
    auto dyn_buf = asio::dynamic_buffer(read_buffer_);
    auto buffer = dyn_buf.prepare(out.size());

    auto executor = co_await asio::this_coro::executor;
    auto timer = asio::steady_timer(executor);
    timer.expires_after(timeout.value());

    std::error_code ec1, ec2;
    std::size_t bytes_received = 0;

    auto result = co_await (
        socket_.async_read_some(
            buffer, asio::redirect_error(asio::use_awaitable, ec1)) ||
        timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

    if (result.index() == 1) {
      socket_.cancel();
      co_return std::unexpected(make_timeout_error());
    }

    if (is_connection_closed(ec1)) {
      socket_.close();
      co_return std::unexpected(ec1);
    }
    if (ec1) {
      co_return std::unexpected(ec1);
    }

    bytes_received = std::get<std::size_t>(result);
    if (bytes_received == 0) {
      spdlog::warn(
          "[{}] buffer empty, cant read any bytes and connection is closed",
          get_id());
      co_return std::unexpected(asio::error::eof);
    }
    dyn_buf.commit(bytes_received);
  }

  co_return move_data(read_buffer_, out,
                      std::numeric_limits<std::size_t>::max());
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
TcpSocket::async_read_exact(std::span<std::byte> out,
                            std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] async_read_exact", get_id());

  std::ranges::fill(out, std::byte{0});
  if (!timeout)
    timeout = std::chrono::hours(24);

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  std::error_code ec1, ec2;

  while (read_buffer_.size() < out.size()) {
    auto dyn_buf = asio::dynamic_buffer(read_buffer_);
    auto buffer = dyn_buf.prepare(out.size());

    std::size_t read_bytes = 0;

    auto result = co_await (
        socket_.async_read_some(
            buffer, asio::redirect_error(asio::use_awaitable, ec1)) ||
        timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

    if (result.index() == 1) {
      socket_.cancel();
      co_return std::unexpected(make_timeout_error());
    }

    if (is_connection_closed(ec1)) {
      socket_.close();
      co_return std::unexpected(ec1);
    }
    if (ec1) {
      co_return std::unexpected(ec1);
    }

    read_bytes = std::get<std::size_t>(result);
    if (read_bytes == 0) {
      spdlog::warn("[{}] buffer not enough, cant read any bytes and connection "
                   "is closed",
                   get_id());
      co_return std::unexpected(asio::error::eof);
    }
    dyn_buf.commit(read_bytes);
  }

  co_return move_data(read_buffer_, out,
                      std::numeric_limits<std::size_t>::max());
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
TcpSocket::async_read_until(std::span<std::byte> out, std::string_view delim,
                            std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] async_read_until", get_id());

  if (!timeout)
    timeout = std::chrono::hours(24);

  auto delim_as_sv = std::as_bytes(std::span(delim));

  auto executor = co_await asio::this_coro::executor;
  auto timer = asio::steady_timer(executor);
  timer.expires_after(timeout.value());

  std::error_code ec1, ec2;

  while (true) {
    auto it = std::search(read_buffer_.begin(), read_buffer_.end(),
                          std::begin(delim_as_sv), std::end(delim_as_sv));
    if (it != read_buffer_.end()) {
      std::size_t len = static_cast<std::size_t>(it - read_buffer_.begin() + 1);
      if (len > out.size()) {
        spdlog::warn("[{}] result buffer has not enough space for result",
                     get_id());
        co_return std::unexpected(
            make_error_code(Network::Error::ProtocolError));
      }

      co_return move_data(read_buffer_, out, len + 1);
    }
    std::size_t read_bytes = 0;
    auto dyn_buf = asio::dynamic_buffer(read_buffer_);
    auto buffer = dyn_buf.prepare(out.size());

    auto result = co_await (
        socket_.async_read_some(
            buffer, asio::redirect_error(asio::use_awaitable, ec1)) ||
        timer.async_wait(asio::redirect_error(asio::use_awaitable, ec2)));

    if (result.index() == 1) {
      socket_.cancel();
      co_return std::unexpected(make_timeout_error());
    }

    if (is_connection_closed(ec1)) {
      socket_.close();
      co_return std::unexpected(ec1);
    }
    if (ec1) {
      co_return std::unexpected(ec1);
    }

    read_bytes = std::get<std::size_t>(result);
    if (read_bytes == 0) {
      spdlog::warn("[{}] buffer not enough, cant read any bytes and connection "
                   "is closed",
                   get_id());
      co_return std::unexpected(asio::error::eof);
    }
    dyn_buf.commit(read_bytes);
  }

  co_return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

} // namespace Network

#include "socket/SslSocket.h"
#include "core/ErrorCodes.h"
#include <socket/SocketBaseImpl.h>

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/read.hpp>
#include <asio/read_until.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/error.hpp>
#include <asio/ssl/stream.hpp>
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

SslSocket::SslSocket(asio::ssl::stream<asio::ip::tcp::socket> stream)
    : SocketBase(), _stream(std::move(stream)) {}

SslSocket::~SslSocket() { closeSocket(); }

void SslSocket::closeSocket() noexcept {
  if (_stream.next_layer().is_open()) {
    std::error_code ec;
    _stream.shutdown(ec);
    _stream.next_layer().close(ec);
  }
}

void SslSocket::cancelSocket() noexcept {
  if (_stream.next_layer().is_open()) {
    std::error_code ec;
    _stream.next_layer().cancel(ec);
  }
}

bool SslSocket::isConnected() const noexcept {
  return _stream.next_layer().is_open();
}

bool SslSocket::isConnectionClosed(const std::error_code &ec) const noexcept {
  return (ec == asio::error::eof || ec == asio::error::connection_reset ||
          ec == asio::error::broken_pipe || ec == asio::error::not_connected ||
          ec == asio::ssl::error::stream_truncated);
}

std::expected<std::size_t, std::error_code>
SslSocket::writeAll(std::span<const std::byte> buffer,
                    std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] writeAll", getId());

  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      _stream.next_layer().get_executor(),
      [this, buffer, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await asyncWriteAll(buffer, timeout);
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
SslSocket::readSome(std::span<std::byte> buffer,
                    std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] readSome", getId());

  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      _stream.next_layer().get_executor(),
      [this, buffer, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await asyncReadSome(buffer, timeout);
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
SslSocket::readExact(std::span<std::byte> buffer,
                     std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] readExact", getId());
  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      _stream.next_layer().get_executor(),
      [this, buffer, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await asyncReadExact(buffer, timeout);
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
SslSocket::readUntil(std::span<std::byte> buffer, std::string_view delimiter,
                     std::optional<std::chrono::milliseconds> timeout) {
  spdlog::trace("[{}] readUntil", getId());

  auto promise = std::make_shared<
      std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
      _stream.next_layer().get_executor(),
      [this, buffer, delimiter, promise = std::move(promise),
       timeout]() -> asio::awaitable<void> {
        auto result = co_await asyncReadUntil(buffer, delimiter, timeout);
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
SslSocket::asyncWriteAll(std::span<const std::byte> buffer,
                         std::optional<std::chrono::milliseconds> timeout) {
  return socket_detail::asyncWriteAllCommon(*this, buffer, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
SslSocket::asyncReadSome(std::span<std::byte> out,
                         std::optional<std::chrono::milliseconds> timeout) {
  return socket_detail::asyncReadSomeCommon(*this, out, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
SslSocket::asyncReadExact(std::span<std::byte> out,
                          std::optional<std::chrono::milliseconds> timeout) {
  return socket_detail::asyncReadExactCommon(*this, out, timeout);
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
SslSocket::asyncReadUntil(std::span<std::byte> out, std::string_view delim,
                          std::optional<std::chrono::milliseconds> timeout) {
  return socket_detail::asyncReadUntilCommon(*this, out, delim, timeout);
}

} // namespace Network

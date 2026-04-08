#include "socket/TcpSocket.h"
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

TcpSocket::TcpSocket(asio::io_context& io_ctx) : socket_(io_ctx)
{
  spdlog::info("[{}] created", getId());
}

TcpSocket::TcpSocket(asio::ip::tcp::socket&& sock) : socket_(std::move(sock))
{
  spdlog::info("[{}] created", getId());
}

TcpSocket::~TcpSocket()
{
  spdlog::info("[{}] close", getId());
  cancelSocket();
  closeSocket();
  spdlog::info("[{}] closed", getId());
}

void TcpSocket::closeSocket() noexcept
{
  if (socket_.is_open())
  {
    std::error_code ec;
    socket_.close(ec);
  }
}

void TcpSocket::cancelSocket() noexcept
{
  if (socket_.is_open())
  {
    std::error_code ec;
    socket_.cancel(ec);
  }
}

bool TcpSocket::isConnected() const noexcept
{
  return socket_.is_open();
}

bool TcpSocket::isConnectionClosed(const std::error_code& ec) const noexcept
{
  return (ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::broken_pipe ||
          ec == asio::error::not_connected);
}

std::expected<std::size_t, std::error_code> TcpSocket::writeAll(std::span<const std::byte> in_buffer,
                                                                std::optional<std::chrono::milliseconds> timeout)
{
  spdlog::trace("[{}] writeAll", getId());

  auto promise = std::make_shared<std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
    socket_.get_executor(),
    [this, in_buffer, promise = std::move(promise), timeout]() -> asio::awaitable<void>
    {
      auto result = co_await asyncWriteAll(in_buffer, timeout);
      promise->set_value(result);
    },
    asio::detached);

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

  auto promise = std::make_shared<std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
    socket_.get_executor(),
    [this, out_buffer, promise = std::move(promise), timeout]() -> asio::awaitable<void>
    {
      auto result = co_await asyncReadSome(out_buffer, timeout);
      promise->set_value(result);
    },
    asio::detached);

  try
  {
    return future.get();
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
  auto promise = std::make_shared<std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
    socket_.get_executor(),
    [this, out_buffer, promise = std::move(promise), timeout]() -> asio::awaitable<void>
    {
      auto result = co_await asyncReadExact(out_buffer, timeout);
      promise->set_value(result);
    },
    asio::detached);

  try
  {
    return future.get();
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
  spdlog::trace("[{}] readUntil", getId());

  auto promise = std::make_shared<std::promise<std::expected<std::size_t, std::error_code>>>();
  auto future = promise->get_future();

  asio::co_spawn(
    socket_.get_executor(),
    [this, out_buffer, delimiter, promise = std::move(promise), timeout]() -> asio::awaitable<void>
    {
      auto result = co_await asyncReadUntil(out_buffer, delimiter, timeout);
      promise->set_value(result);
    },
    asio::detached);

  try
  {
    return future.get();
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

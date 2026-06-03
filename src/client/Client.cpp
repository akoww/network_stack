#include "client/Client.h"
#include "core/ErrorTranslation.h"
#include "socket/SslSocket.h"
#include "socket/SocketBaseImpl.h"
#include "socket/TcpSocket.h"

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/experimental/parallel_group.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <asio/experimental/cancellation_condition.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>

#include <asio/bind_cancellation_slot.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

namespace
{

inline std::error_code makeTimeoutError()
{
  return make_error_code(Network::Error::CONNECTION_TIMEOUT);
}
}  // namespace

// ------

Client::Client(std::string_view host, uint16_t port, asio::io_context& io_ctx) : ClientBase(host, port, io_ctx)
{
}

std::expected<std::unique_ptr<DualSocket>, std::error_code> Client::connect(std::chrono::milliseconds timeout)
{
  spdlog::trace("connect to server ...");  // TODO fix this

  auto future = asio::co_spawn(
    getIoContext(), [this, timeout]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    { return Client::asyncConnect(timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("connected!");
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(makeReadError(std::make_error_code(std::errc::operation_canceled)));
  }
}

std::expected<std::unique_ptr<DualSocket>, std::error_code> Client::connectTls(std::chrono::milliseconds timeout)
{
  spdlog::trace("connect to tls server ...");  // TODO fix this

  auto future = asio::co_spawn(
    getIoContext(), [this, timeout]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    { return Client::asyncConnectTls(timeout); }, asio::use_future);

  try
  {
    auto result = future.get();
    if (result)
    {
      spdlog::debug("connected tls!");
    }
    return result;
  }
  catch (...)
  {
    return std::unexpected(makeReadError(std::make_error_code(std::errc::operation_canceled)));
  }
}

asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> Client::asyncConnect(
  std::chrono::milliseconds timeout)
{
  spdlog::trace("client async connecting to {}:{}...", host(), port());

  std::error_code ec;
  auto executor = co_await asio::this_coro::executor;

  asio::ip::tcp::resolver resolver(executor);

  auto endpoints =
    co_await resolver.async_resolve(host(), std::to_string(port()), asio::redirect_error(asio::use_awaitable, ec));

  if (ec)
  {
    spdlog::error("DNS resolution failed for {}: {}", host(), ec.message());
    co_return std::unexpected(makeDnsError(ec));
  }

  asio::ip::tcp::socket socket(executor);

  auto timer = asio::steady_timer(executor, timeout);

  std::error_code ec1, ec2;

  auto [order, results] = co_await asio::experimental::make_parallel_group(
                            asio::async_connect(socket, endpoints, asio::redirect_error(asio::deferred, ec1)),
                            timer.async_wait(asio::redirect_error(asio::deferred, ec2)))
                            .async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);

  if (order[0] == 1)
  {
    co_return std::unexpected(makeTimeoutError());
  }

  ec = ec1;
  if (ec)
  {
    spdlog::error("connection to {}:{} failed: {}", host(), port(), ec.message());
    co_return std::unexpected(makeConnectionError(ec));
  }

  spdlog::trace("client async connected to {}:{} successfully", host(), port());

  auto tcp_socket = std::make_unique<TcpSocket>(std::move(socket));
  co_return std::move(tcp_socket);
}

asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> Client::asyncConnectTls(
  std::chrono::milliseconds timeout)
{
  spdlog::trace("client async connecting to {}:{} using TLS...", host(), port());

  std::error_code ec;

  auto executor = co_await asio::this_coro::executor;

  asio::ip::tcp::resolver resolver(executor);

  auto endpoints =
    co_await resolver.async_resolve(host(), std::to_string(port()), asio::redirect_error(asio::use_awaitable, ec));

  if (ec)
  {
    spdlog::error("DNS resolution failed for {}: {}", host(), ec.message());
    co_return std::unexpected(makeDnsError(ec));
  }

  asio::ip::tcp::socket socket(executor);

  auto timer = asio::steady_timer(executor, timeout);

  std::error_code ec1, ec2;

  auto [order, results] = co_await asio::experimental::make_parallel_group(
                            asio::async_connect(socket, endpoints, asio::redirect_error(asio::deferred, ec1)),
                            timer.async_wait(asio::redirect_error(asio::deferred, ec2)))
                            .async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);

  if (order[0] == 1)
  {
    co_return std::unexpected(makeTimeoutError());
  }

  ec = ec1;
  if (ec)
  {
    spdlog::error("connection to {}:{} failed: {}", host(), port(), ec.message());
    co_return std::unexpected(makeConnectionError(ec));
  }

  asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(std::move(socket), *getSslContext());

  ssl_stream.set_verify_mode(asio::ssl::verify_none, ec);
  if (ec)
  {
    spdlog::error("failed to set SSL verify mode: {}", ec.message());
    co_return std::unexpected(makeTlsError(ec));
  }

  co_await ssl_stream.async_handshake(asio::ssl::stream_base::client, asio::redirect_error(asio::use_awaitable, ec));

  if (ec)
  {
    spdlog::error("TLS handshake failed for {}:{}: {}", host(), port(), ec.message());
    co_return std::unexpected(makeTlsError(ec));
  }

  spdlog::trace("client async TLS connected to {}:{} successfully", host(), port());

  auto ssl_socket = std::make_unique<SslSocket>(std::move(ssl_stream));
  co_return std::move(ssl_socket);
}

}  // namespace Network

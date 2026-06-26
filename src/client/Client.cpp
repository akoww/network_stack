#include "client/Client.h"
#include "core/ErrorCodes.h"
#include "core/ErrorTranslation.h"
#include "core/TlsContextWrapper.h"
#include "socket/TlsSocket.h"
#include "socket/TcpOptions.h"
#include "socket/details/SocketBaseDetail.h"
#include "socket/TcpSocket.h"
#include "core/details/ContextDetail.h"
#include "core/details/TlsContextDetail.h"

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
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/use_awaitable.hpp>

#include <spdlog/spdlog.h>

#include <memory>
#include <atomic>
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

Client::Client(std::string_view host, uint16_t port, IoContextWrapper io_ctx) : ClientBase(host, port, io_ctx)
{
}

std::expected<std::unique_ptr<DualSocket>, std::error_code> Client::connect(std::chrono::milliseconds timeout,
                                                                            TcpOptions tcp_opts)
{
  spdlog::trace("connect to server ...");

  auto future = asio::co_spawn(
    detail::getExecutor(_io_ctx),
    [this, timeout, tcp_opts]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    { return Client::asyncConnect(timeout, tcp_opts); }, asio::use_future);

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

std::expected<std::unique_ptr<DualSocket>, std::error_code> Client::connectTls(std::chrono::milliseconds timeout,
                                                                               TcpOptions tcp_opts,
                                                                               TlsOptions tls_opts)
{
  spdlog::trace("connect to tls server ...");

  auto future = asio::co_spawn(
    detail::getExecutor(_io_ctx),
    [this, timeout, tcp_opts,
     tls_opts]() -> asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>>
    { return Client::asyncConnectTls(timeout, tcp_opts, tls_opts); },
    asio::use_future);

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

namespace
{

std::error_code applyPreConnectTcpOptions(asio::ip::tcp::socket& sock, TcpOptions& opts)
{
  std::error_code ec;

  // 2. Disable Nagle's Algorithm (TCP_NODELAY)
  if (opts.nodelay)
  {
    sock.set_option(asio::ip::tcp::no_delay(true), ec);
    if (ec)
      return makeOptionError(ec);
  }

  // 3. Enable Keepalive (SO_KEEPALIVE)
  // Exact idle/interval/count are OS-level details. Asio's cross-platform API
  // only exposes the on/off switch. Parameters use OS defaults when omitted.
  if (opts.keepalive_idle || opts.keepalive_interval || opts.keepalive_count)
  {
    sock.set_option(asio::socket_base::keep_alive(true), ec);
    if (ec)
      return makeOptionError(ec);
  }

  // 4. Send Buffer Size
  if (opts.send_buf_size)
  {
    sock.set_option(asio::ip::tcp::socket::send_buffer_size(static_cast<int>(opts.send_buf_size.value())), ec);
    if (ec)
      return makeOptionError(ec);
  }

  // 5. Receive Buffer Size
  if (opts.recv_buf_size)
  {
    sock.set_option(asio::ip::tcp::socket::receive_buffer_size(static_cast<int>(opts.recv_buf_size.value())), ec);
    if (ec)
      return makeOptionError(ec);
  }

  // 6. Linger Timeout (seconds precision)
  if (opts.linger_timeout)
  {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(opts.linger_timeout.value()).count();
    sock.set_option(asio::ip::tcp::socket::linger(true, static_cast<int>(secs)), ec);
    if (ec)
      return makeOptionError(ec);
  }

  return ec;
}

}  // namespace

asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> Client::asyncConnect(
  std::chrono::milliseconds timeout, TcpOptions tcp_opts)
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

  asio::ip::tcp::socket sock(executor);

  auto timer = asio::steady_timer(executor, timeout);
  std::error_code ec1, ec2;

  auto [order, results] = co_await asio::experimental::make_parallel_group(
                            asio::async_connect(sock, endpoints, asio::redirect_error(asio::deferred, ec1)),
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

  if (auto apply_opts_ec = applyPreConnectTcpOptions(sock, tcp_opts))
  {
    spdlog::error("client failed to apply TCP options: {}", apply_opts_ec.message());
    co_return std::unexpected(apply_opts_ec);
  }

  spdlog::trace("client async connected to {}:{} successfully", host(), port());
  auto tcp_socket = std::make_unique<TcpSocket>(std::move(sock));
  co_return std::move(tcp_socket);
}

namespace
{
std::error_code applyPreConnectTlsOptions(asio::ssl::stream<asio::ip::tcp::socket>& stream, const TlsOptions& opts)
{
  namespace sys = asio::error;

  SSL* ssl = stream.native_handle();
  if (!ssl)
  {
    spdlog::error("Missing ssl context when applying TLS options");
    return makeOptionError(make_error_code(Network::Error::OPTION_ERROR));  // No native handle available yet
  }

  std::error_code ec;

#ifndef OPENSSL_API_3

#else
  // OpenSSL 3.x (and 1.1.1+) supports direct SSL_set_* calls

  uint32_t min_ver = SSL_VERSION_TLS_1_0;
  switch (opts.min_version)
  {
    case TlsVersion::TLS1_2:
      min_ver = SSL_VERSION_TLS_1_2;
      break;
    case TlsVersion::TLS1_3:
      min_ver = SSL_VERSION_TLS_1_3;
      break;
    default:
      min_ver = SSL_VERSION_DEFAULT_MIN;
      break;  // Auto defaults handled by OpenSSL
  }

  uint32_t max_ver = SSL_VERSION_TLS_1_3;
  switch (opts.max_version)
  {
    case TlsVersion::TLS1_2:
      max_ver = SSL_VERSION_TLS_1_2;
      break;
    default:
      max_ver = SSL_VERSION_DEFAULT_MAX;
      break;  // Auto allows highest
  }

  // Check return values (0 means error in OpenSSL API usage)
  if (SSL_set_min_proto_version(ssl, min_ver) == 0)
  {
    ec.assign(errno, std::system_category());
  }
  if (!ec && SSL_set_max_proto_version(ssl, max_ver) == 0)
  {
    ec.assign(errno, std::system_category());
  }
#endif

  // ==========================================================================
  // 2. Cipher Suites (Stream Level Override)
  // ==========================================================================
  if (!opts.cipher_suites.empty())
  {
    std::string cipher_list;
    for (size_t i = 0; i < opts.cipher_suites.size(); ++i)
    {
      if (i > 0)
        cipher_list += ':';
      cipher_list += opts.cipher_suites[i];
    }

    // SSL_set_cipher_list applies to the SSL object specifically, overriding context defaults.
    int ret = SSL_set_cipher_list(ssl, cipher_list.c_str());
    if (ret == 0)
    {
      spdlog::warn("Issue setting the ssl cipher list");
      ec = makeOptionError(make_error_code(Network::Error::OPTION_ERROR));
    }
  }

  // ==========================================================================
  // 3. Elliptic Curves (Stream Level Override)
  // ==========================================================================
  // OpenSSL allows setting curves per connection to ensure specific key exchange
  // parameters are preferred for this handshake, overriding Context defaults if needed.
  if (!opts.ecdh_curves.empty())
  {
    std::string curve_list;
    for (size_t i = 0; i < opts.ecdh_curves.size(); ++i)
    {
      if (i > 0)
        curve_list += ':';
      curve_list += opts.ecdh_curves[i];
    }

#ifdef OPENSSL_NO_EC
    // Fallback check: If EC support is compiled out, this might fail silently or error.
#else
    if (SSL_set1_curves_list(ssl, curve_list.c_str()) == 0)
    {
      spdlog::error("Can't set curves list");
      ec = makeOptionError(make_error_code(Network::Error::OPTION_ERROR));
    }
#endif
  }

  // ==========================================================================
  // 4. Verification Settings (ASIO Stream API)
  // ==========================================================================
  // These are the primary settings that MUST be applied to the stream object
  // and cannot be fully configured via Context alone for per-connection logic.

  if (opts.verify_peer)
  {
    stream.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
  }
  else
  {
    stream.set_verify_mode(asio::ssl::verify_none);
  }

  stream.set_verify_depth(opts.verify_depth);

  // ==========================================================================
  // 5. Session Tickets & Flags (Stream Level)
  // ==========================================================================
  // If tickets are explicitly disabled, clear the OP_NO_TICKET flag on this connection.
  if (!opts.enable_session_tickets)
  {
    // SSL_clear_options allows removing flags that might have been set on Context
    SSL_clear_options(ssl, SSL_OP_NO_TICKET);
  }
  else
  {
    // Ensure ticket support is allowed (Context usually handles this by default if enabled globally)
    SSL_set_options(ssl, SSL_OP_NO_TICKET);  // Wait, to allow tickets we want OP_NO_TICKET OFF.
    // Standard Context creation logic enables tickets by setting timeout.
    // Explicitly clearing the 'No Ticket' flag ensures it's active for this stream.
    SSL_clear_options(ssl, SSL_OP_NO_TICKET);
  }

  return ec;
}
}  // namespace

asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> Client::asyncConnectTls(
  std::chrono::milliseconds timeout, TcpOptions tcp_opts, TlsOptions tls_opts)
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

  asio::ip::tcp::socket sock(executor);

  auto timer = asio::steady_timer(executor, timeout);
  std::error_code ec1, ec2;

  auto [order, results] = co_await asio::experimental::make_parallel_group(
                            asio::async_connect(sock, endpoints, asio::redirect_error(asio::deferred, ec1)),
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

  if (auto apply_opts_ec = applyPreConnectTcpOptions(sock, tcp_opts))
  {
    spdlog::error("clientfailed to apply TCP options: {}", apply_opts_ec.message());
    co_return std::unexpected(apply_opts_ec);
  }

  auto ctx_result = createTlsContextWrapper(tls_opts);
  if (!ctx_result)
  {
    co_return std::unexpected(ctx_result.error());
  }
  asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(std::move(sock), *detail::getTlsContext(*ctx_result));

  if (auto tls_ec = applyPreConnectTlsOptions(ssl_stream, tls_opts))
  {
    spdlog::error("Can't create a TLS stream with given options");
    co_return std::unexpected(tls_ec);
  }

  // Perform TLS handshake (with timeout)
  auto hs_timeout = tls_opts.handshake_timeout_ms.value_or(std::chrono::hours(24));
  std::error_code hs_ec;
  std::atomic<bool> hs_timed_out{false};
  asio::steady_timer hs_timer(executor, hs_timeout);

  hs_timer.async_wait(
    [&hs_timed_out, &ssl_stream](const std::error_code& timer_ec)
    {
      if (!timer_ec)
      {
        hs_timed_out.store(true);
        ssl_stream.lowest_layer().cancel();
      }
    });

  co_await ssl_stream.async_handshake(asio::ssl::stream_base::client, asio::redirect_error(asio::use_awaitable, hs_ec));

  hs_timer.cancel();

  if (hs_ec == asio::error::operation_aborted && hs_timed_out.load())
  {
    spdlog::debug("TLS handshake timed out for {}:{}", host(), port());
    co_return std::unexpected(makeTimeoutError());
  }

  if (hs_ec)
  {
    spdlog::error("TLS handshake failed for {}:{}: {}", host(), port(), hs_ec.message());
    co_return std::unexpected(makeTlsError(hs_ec));
  }

  spdlog::trace("client async TLS connected to {}:{} successfully", host(), port());
  auto ssl_socket = std::make_unique<TlsSocket>(std::move(ssl_stream));
  co_return std::move(ssl_socket);
}

}  // namespace Network

#include "server/Server.h"
#include "core/ErrorCodes.h"
#include "core/ErrorTranslation.h"
#include "core/details/ContextDetail.h"
#include "socket/TlsSocket.h"
#include "socket/TcpSocket.h"

#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/ssl/context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <system_error>

namespace Network
{

Server::Server(uint16_t port, IoContextWrapper io_ctx, ClientHandler handler)
  : ServerBase(port, io_ctx, std::move(handler))
{
  spdlog::trace("server async created on port {}", port);
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

asio::awaitable<std::expected<void, std::error_code>> Server::asyncListen([[maybe_unused]] TcpOptions tcp_opts)
{
  spdlog::trace("server async listening on {}:{}", host(), port());

  std::error_code ec;
  auto executor = co_await asio::this_coro::executor;

  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
  {
    spdlog::error("failed to open acceptor: {}", ec.message());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  _acceptor.set_option(asio::socket_base::reuse_address(tcp_opts.reuse_addr), ec);
  if (ec)
  {
    spdlog::error("failed to set reuse_address: {}", ec.message());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  _acceptor.bind(endpoint, ec);
  if (ec)
  {
    spdlog::error("failed to bind to {}:{}", host(), port());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    spdlog::error("failed to start listening: {}", ec.message());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  spdlog::trace("server async started on {}:{}", host(), port());

  while (!isStopped())
  {
    ec = {};
    asio::ip::tcp::socket socket(executor);

    // TODO add timeout
    co_await _acceptor.async_accept(socket, asio::redirect_error(asio::use_awaitable, ec));

    if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
    {
      co_return std::expected<void, std::error_code>{};
    }

    if (ec)
    {
      spdlog::error("async accept error: {}", ec.message());
      co_return std::unexpected(makeServerError(ec, "accept"));
    }

    if (auto apply_opts_ec = applyPreConnectTcpOptions(socket, tcp_opts))
    {
      spdlog::error("server failed to apply TCP options: {}", apply_opts_ec.message());
      co_return std::unexpected(apply_opts_ec);
    }

    asio::co_spawn(executor, acceptPlainSocket(std::move(socket)), asio::detached);
  }

  co_return std::expected<void, std::error_code>{};
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

std::shared_ptr<asio::ssl::context> createServerTslContext(const TlsServerOptions& tls_server_opts,
                                                           const TlsOptions& tls_opts)
{
  auto ctx = createTlsContext(tls_opts, true /*server*/);

  std::error_code ec;
  if (!tls_server_opts._cert_chain_path.empty())
  {
    ctx->use_certificate_chain_file(tls_server_opts._cert_chain_path.string(), ec);
    if (ec)
    {
      spdlog::error("Failed to load certificate chain from {}: {}", tls_server_opts._cert_chain_path.string(),
                    ec.message());
      return nullptr;
    }
  }

  if (!tls_server_opts._private_key_path.empty())
  {
    ctx->use_private_key_file(tls_server_opts._private_key_path.string(), asio::ssl::context::pem, ec);
    if (ec)
    {
      spdlog::error("Failed to load private key from {}: {}", tls_server_opts._private_key_path.string(), ec.message());
      return nullptr;
    }
  }

  return ctx;
}
}  // namespace

asio::awaitable<std::expected<void, std::error_code>> Server::asyncListenTls(TlsServerOptions tls_server_opts,
                                                                             TcpOptions tcp_opts,
                                                                             TlsOptions tls_opts)
{
  spdlog::trace("server async listening on {}:{} with TLS", host(), port());

  std::error_code ec;
  auto executor = co_await asio::this_coro::executor;

  asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port());

  _acceptor.open(endpoint.protocol(), ec);
  if (ec)
  {
    spdlog::error("failed to open acceptor: {}", ec.message());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  _acceptor.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec)
  {
    spdlog::error("failed to set reuse_address: {}", ec.message());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  _acceptor.bind(endpoint, ec);
  if (ec)
  {
    spdlog::error("failed to bind to {}:{}", host(), port());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  _acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec)
  {
    spdlog::error("failed to start listening: {}", ec.message());
    co_return std::unexpected(makeSocketCreateError(ec));
  }

  auto tls_context = createServerTslContext(tls_server_opts, tls_opts);
  if (!tls_context)
  {
    co_return std::unexpected(makeOptionError(Network::Error::OPTION_ERROR));
  }

  spdlog::trace("server async TLS started on {}:{}", host(), port());

  while (!isStopped())
  {
    ec = {};
    asio::ip::tcp::socket socket(executor);

    co_await _acceptor.async_accept(socket, asio::redirect_error(asio::use_awaitable, ec));

    if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
    {
      co_return std::expected<void, std::error_code>{};
    }

    if (ec)
    {
      spdlog::error("async TLS accept error: {}", ec.message());
      co_return std::unexpected(makeServerError(ec, "accept"));
    }

    if (auto apply_opts_ec = applyPreConnectTcpOptions(socket, tcp_opts))
    {
      spdlog::error("server failed to apply TCP options: {}", apply_opts_ec.message());
      co_return std::unexpected(apply_opts_ec);
    }

    asio::co_spawn(executor, acceptTlsSocket(std::move(socket), tls_context, tls_opts), asio::detached);
  }

  co_return std::expected<void, std::error_code>{};
}

asio::awaitable<void> Server::acceptPlainSocket(asio::ip::tcp::socket socket)
{
  spdlog::debug("new async connection accepted");
  asio::co_spawn(
    co_await asio::this_coro::executor,
    [socket = std::move(socket), handler = this->clientHandler()]() mutable -> asio::awaitable<void>
    {
      handler(std::make_unique<TcpSocket>(std::move(socket)));
      co_return;
    },
    asio::detached);

  co_return;
}

asio::awaitable<void> Server::acceptTlsSocket(asio::ip::tcp::socket socket,
                                              std::shared_ptr<asio::ssl::context> ctx,
                                              const TlsOptions& tls_opts)
{
  spdlog::debug("new async TLS connection accepted");

  asio::ssl::stream<asio::ip::tcp::socket> ssl_stream(std::move(socket), *ctx);
  auto executor = co_await asio::this_coro::executor;

  if (auto tls_ec = applyPreConnectTlsOptions(ssl_stream, tls_opts))
  {
    spdlog::error("Can't create a TLS stream with given options");
    co_return;
  }

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

  co_await ssl_stream.async_handshake(asio::ssl::stream_base::server, asio::redirect_error(asio::use_awaitable, hs_ec));

  hs_timer.cancel();

  if (hs_ec == asio::error::operation_aborted && hs_timed_out.load())
  {
    spdlog::debug("TLS handshake timed out");
    co_return;
  }

  if (hs_ec)
  {
    spdlog::error("TLS handshake failed: {}", hs_ec.message());
    co_return;
  }

  asio::co_spawn(
    executor,
    [stream = std::move(ssl_stream), handler = clientHandler()]() mutable -> asio::awaitable<void>
    {
      handler(std::make_unique<TlsSocket>(std::move(stream)));
      co_return;
    },
    asio::detached);

  co_return;
}

void Server::stop()
{
  spdlog::trace("server async stopping...");
  ServerBase::stop();
  _acceptor.cancel();
  _acceptor.close();
  spdlog::trace("server async stopped");
}

std::expected<void, std::error_code> Server::listen(TcpOptions tcp_opts)
{
  spdlog::trace("starting sync server ...");  // TODO fix this

  auto future = asio::co_spawn(
    detail::getExecutor(_io_ctx),
    [this, tcp_opts = std::move(tcp_opts)]() mutable -> asio::awaitable<std::expected<void, std::error_code>>
    { return Server::asyncListen(std::move(tcp_opts)); }, asio::use_future);

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

std::expected<void, std::error_code> Server::listenTls(TlsServerOptions tls_server_opts,
                                                       TcpOptions tcp_opts,
                                                       TlsOptions tls_opts)
{
  spdlog::trace("starting sync server ...");  // TODO fix this

  auto future = asio::co_spawn(
    detail::getExecutor(_io_ctx),
    [this, tls_server_opts = std::move(tls_server_opts), tcp_opts = std::move(tcp_opts),
     tls_opts = std::move(tls_opts)]() mutable -> asio::awaitable<std::expected<void, std::error_code>>
    { return Server::asyncListenTls(std::move(tls_server_opts), std::move(tcp_opts), std::move(tls_opts)); },
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

}  // namespace Network

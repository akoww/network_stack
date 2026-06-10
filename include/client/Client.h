#pragma once

#include "ClientBase.h"
#include <chrono>
#include <expected>
#include <string_view>

namespace Network
{
class DualSocket;

/// @brief Unified client class implementing both synchronous and asynchronous connection patterns.
/// Combines what was previously ClientSync and ClientAsync into a single class.
/// Use connect()/connectTls() for blocking calls in threads, asyncConnect()/asyncConnectTls() for coroutines.
class Client : public ClientBase, public ClientSync, public ClientAsync
{
public:
  /// @brief Construct a client connecting to the given host and port.
  /// @param host Remote host address (domain name or IP).
  /// @param port Remote port number.
  /// @param io_ctx ASIO io_context for DNS resolution and connection setup.
  explicit Client(std::string_view host, uint16_t port, asio::any_io_executor io_ctx);

  /// @brief Connect to the remote server synchronously (blocking).
  /// @param timeout Connection timeout, defaults to 500ms. Pass explicit value for production use.
  /// @return Socket on success, or std::error_code on failure (DNS/connect refused/timeout).
  /// @note Resolves DNS via asio::ip::tcp::resolver, then performs async TCP handshake that blocks until complete.
  /// @note Use in a thread - this call blocks until connection is established or times out.
  std::expected<std::unique_ptr<DualSocket>, std::error_code> connect(
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) override;

  /// @brief Connect to the remote server synchronously using TLS.
  /// @param timeout Connection + handshake timeout, defaults to 500ms. Pass explicit value for production use.
  /// @return TLS socket on success, or std::error_code on failure (DNS/connect refused/timeout/TLS error).
  /// @note SSL context must be configured via getSslContext() before calling.
  /// @note SSL verify mode must be set (e.g. set_verify_mode(asio::ssl::verify_none)) unless using a CA-signed cert.
  std::expected<std::unique_ptr<DualSocket>, std::error_code> connectTls(
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) override;

  /// @brief Connect to the remote server asynchronously (coroutine-based).
  /// @param timeout Connection timeout, defaults to 500ms. Pass explicit value for production use.
  /// @return Socket on success, or std::error_code on failure.
  /// @note Use with co_await inside asio::co_spawn'ed coroutine.
  asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> asyncConnect(
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) override;

  /// @brief Connect to the remote server asynchronously using TLS.
  /// @param timeout Connection + handshake timeout, defaults to 500ms. Pass explicit value for production use.
  /// @return TLS socket on success, or std::error_code on failure.
  /// @note Use with co_await inside asio::co_spawn'ed coroutine.
  /// @note SSL context must be configured via getSslContext() before calling.
  asio::awaitable<std::expected<std::unique_ptr<DualSocket>, std::error_code>> asyncConnectTls(
    std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) override;
};

}  // namespace Network

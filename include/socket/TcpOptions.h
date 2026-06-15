#pragma once

#include <chrono>
#include <optional>
#include <cstdint>

#include <asio/ip/tcp.hpp>
#include "socket/TcpSocket.h"

namespace Network
{

class TcpSocket;

/// Configuration structure for TCP socket options.
///
/// Applicability:
/// - Server listener: listening socket / acceptor before bind() / listen().
/// - Incoming client: socket returned by accept().
/// - Client: outbound socket used for connect().
///
/// Options that do not apply to a particular socket type are ignored.
struct TcpOptions
{
  // === Connection & Reuse ===

  /// [Server listener]
  ///
  /// Allows binding to a port in TIME_WAIT state. Enables rapid server
  /// restarts without "Address already in use" errors.
  ///
  /// Applied before bind().
  bool reuse_addr = true;

  /// [Server listener]
  ///
  /// Allows multiple listeners to bind the same IP/port. The kernel
  /// distributes incoming connections across them.
  ///
  /// Applied before bind().
  ///
  /// Linux/BSD/macOS only.
  bool reuse_port = false;

  /// [Client, Incoming client]
  ///
  /// Disables Nagle's algorithm. Prevents buffering small outgoing packets,
  /// reducing latency at the cost of slightly higher network overhead.
  ///
  /// Applied to established TCP connections.
  bool nodelay = true;

  // === Keepalive & Liveness Detection ===

  /// [Client, Incoming client]
  ///
  /// Idle time before the first TCP keepalive probe is sent.
  std::optional<std::chrono::milliseconds> keepalive_idle = std::nullopt;

  /// [Client, Incoming client]
  ///
  /// Interval between keepalive probes when no response is received.
  std::optional<std::chrono::milliseconds> keepalive_interval = std::nullopt;

  /// [Client, Incoming client]
  ///
  /// Number of failed keepalive probes before the connection is considered
  /// dead and closed.
  ///
  /// Note: Stored as a duration type for API consistency but interpreted as
  /// an integer probe count by the operating system.
  std::optional<std::chrono::milliseconds> keepalive_count = std::nullopt;

  // === Buffering & Throughput ===

  /// [Server listener, Client, Incoming client]
  ///
  /// TCP send buffer size in bytes.
  ///
  /// When applied to a listening socket, accepted connections may inherit
  /// the value depending on platform behavior.
  std::optional<std::size_t> send_buf_size = std::nullopt;

  /// [Server listener, Client, Incoming client]
  ///
  /// TCP receive buffer size in bytes.
  ///
  /// When applied to a listening socket, accepted connections may inherit
  /// the value depending on platform behavior.
  std::optional<std::size_t> recv_buf_size = std::nullopt;

  // === Close Behavior ===

  /// [Client, Incoming client]
  ///
  /// Graceful close timeout. Controls how long close() waits for pending
  /// data to be acknowledged before the connection is terminated.
  std::optional<std::chrono::milliseconds> linger_timeout = std::nullopt;
};
}  // namespace Network

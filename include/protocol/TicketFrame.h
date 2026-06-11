#pragma once

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <system_error>
#include <vector>

#include "protocol/Ticket.h"
#include "socket/TlsSocket.h"
#include "socket/TcpSocket.h"

namespace Network
{

class TicketFrame
{
public:
  static constexpr std::size_t length_prefix_size = 4;

  static std::expected<std::vector<std::byte>, std::error_code> readFrame(
    TcpSocket& socket, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
  static std::expected<std::vector<std::byte>, std::error_code> readFrame(
    TlsSocket& socket, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

  static std::expected<std::size_t, std::error_code> writeFrame(
    TcpSocket& socket,
    std::span<const std::byte> frame,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
  static std::expected<std::size_t, std::error_code> writeFrame(
    TlsSocket& socket,
    std::span<const std::byte> frame,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

  static asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteFrame(
    TcpSocket& socket,
    std::span<const std::byte> frame,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
  static asio::awaitable<std::expected<std::size_t, std::error_code>> asyncWriteFrame(
    TlsSocket& socket,
    std::span<const std::byte> frame,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

  static asio::awaitable<std::expected<std::vector<std::byte>, std::error_code>> asyncReadFrame(
    TcpSocket& socket, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
  static asio::awaitable<std::expected<std::vector<std::byte>, std::error_code>> asyncReadFrame(
    TlsSocket& socket, std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

  static std::vector<std::byte> makeLengthPrefix(std::uint32_t length);
};

}  // namespace Network

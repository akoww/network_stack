#include "protocol/TicketFrame.h"

#include <array>
#include <bit>

#include "protocol/Ticket.h"
#include "socket/SslSocket.h"
#include "socket/TcpSocket.h"

namespace Network
{

namespace
{

std::expected<std::vector<std::byte>, std::error_code> readLengthPrefixSync(DualSocket& socket,
                                                                            std::chrono::milliseconds timeout)
{
  auto tcpSocket = dynamic_cast<TcpSocket*>(&socket);
  if (tcpSocket)
  {
    std::array<std::byte, TicketFrame::length_prefix_size> buf;
    auto result = tcpSocket->readExact(buf, timeout);
    if (!result)
    {
      return std::unexpected(result.error());
    }

    std::uint32_t length = 0;
    for (std::size_t i = 0; i < TicketFrame::length_prefix_size; ++i)
    {
      length = (length << 8) | static_cast<std::uint32_t>(buf[i]);
    }
    return std::vector<std::byte>(length);
  }

  auto sslSocket = dynamic_cast<SslSocket*>(&socket);
  if (sslSocket)
  {
    std::array<std::byte, TicketFrame::length_prefix_size> buf;
    auto result = sslSocket->readExact(buf, timeout);
    if (!result)
    {
      return std::unexpected(result.error());
    }

    std::uint32_t length = 0;
    for (std::size_t i = 0; i < TicketFrame::length_prefix_size; ++i)
    {
      length = (length << 8) | static_cast<std::uint32_t>(buf[i]);
    }
    return std::vector<std::byte>(length);
  }

  return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

asio::awaitable<std::expected<std::vector<std::byte>, std::error_code>> readLengthPrefixAsync(
  DualSocket& socket, std::chrono::milliseconds timeout)
{
  auto tcpSocket = dynamic_cast<TcpSocket*>(&socket);
  if (tcpSocket)
  {
    std::array<std::byte, TicketFrame::length_prefix_size> buf;
    auto result = co_await tcpSocket->asyncReadExact(buf, timeout);
    if (!result)
    {
      co_return std::unexpected(result.error());
    }

    std::uint32_t length = 0;
    for (std::size_t i = 0; i < TicketFrame::length_prefix_size; ++i)
    {
      length = (length << 8) | static_cast<std::uint32_t>(buf[i]);
    }
    co_return std::vector<std::byte>(length);
  }

  auto sslSocket = dynamic_cast<SslSocket*>(&socket);
  if (sslSocket)
  {
    std::array<std::byte, TicketFrame::length_prefix_size> buf;
    auto result = co_await sslSocket->asyncReadExact(buf, timeout);
    if (!result)
    {
      co_return std::unexpected(result.error());
    }

    std::uint32_t length = 0;
    for (std::size_t i = 0; i < TicketFrame::length_prefix_size; ++i)
    {
      length = (length << 8) | static_cast<std::uint32_t>(buf[i]);
    }
    co_return std::vector<std::byte>(length);
  }

  co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

}  // namespace

std::vector<std::byte> TicketFrame::makeLengthPrefix(std::uint32_t length)
{
  std::vector<std::byte> prefix(length_prefix_size);
  std::uint32_t net_length = std::bit_cast<std::uint32_t>(length);
  for (std::size_t i = 0; i < length_prefix_size; ++i)
  {
    prefix[i] = std::byte((net_length >> (8 * (length_prefix_size - 1 - i))) & 0xFF);
  }
  return prefix;
}

std::expected<std::vector<std::byte>, std::error_code> TicketFrame::readFrame(TcpSocket& socket,
                                                                              std::chrono::milliseconds timeout)
{
  auto lenBuf = readLengthPrefixSync(socket, timeout);
  if (!lenBuf)
  {
    return std::unexpected(lenBuf.error());
  }

  auto result = socket.readExact(*lenBuf, timeout);
  if (!result)
  {
    return std::unexpected(result.error());
  }
  return *lenBuf;
}

std::expected<std::vector<std::byte>, std::error_code> TicketFrame::readFrame(SslSocket& socket,
                                                                              std::chrono::milliseconds timeout)
{
  auto lenBuf = readLengthPrefixSync(socket, timeout);
  if (!lenBuf)
  {
    return std::unexpected(lenBuf.error());
  }

  auto result = socket.readExact(*lenBuf, timeout);
  if (!result)
  {
    return std::unexpected(result.error());
  }
  return *lenBuf;
}

std::expected<std::size_t, std::error_code> TicketFrame::writeFrame(TcpSocket& socket,
                                                                    std::span<const std::byte> frame,
                                                                    std::chrono::milliseconds timeout)
{
  auto prefix = makeLengthPrefix(static_cast<std::uint32_t>(frame.size()));
  auto prefixResult = socket.writeAll(std::span(prefix), timeout);
  if (!prefixResult)
  {
    return std::unexpected(prefixResult.error());
  }

  auto result = socket.writeAll(frame, timeout);
  if (!result)
  {
    return std::unexpected(result.error());
  }
  return *prefixResult + *result;
}

std::expected<std::size_t, std::error_code> TicketFrame::writeFrame(SslSocket& socket,
                                                                    std::span<const std::byte> frame,
                                                                    std::chrono::milliseconds timeout)
{
  auto prefix = makeLengthPrefix(static_cast<std::uint32_t>(frame.size()));
  auto prefixResult = socket.writeAll(std::span(prefix), timeout);
  if (!prefixResult)
  {
    return std::unexpected(prefixResult.error());
  }

  auto result = socket.writeAll(frame, timeout);
  if (!result)
  {
    return std::unexpected(result.error());
  }
  return *prefixResult + *result;
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TicketFrame::asyncWriteFrame(
  TcpSocket& socket, std::span<const std::byte> frame, std::chrono::milliseconds timeout)
{
  auto prefix = makeLengthPrefix(static_cast<std::uint32_t>(frame.size()));
  auto prefixResult = co_await socket.asyncWriteAll(std::span(prefix), timeout);
  if (!prefixResult)
  {
    co_return std::unexpected(prefixResult.error());
  }

  auto result = co_await socket.asyncWriteAll(frame, timeout);
  if (!result)
  {
    co_return std::unexpected(result.error());
  }
  co_return* prefixResult + *result;
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TicketFrame::asyncWriteFrame(
  SslSocket& socket, std::span<const std::byte> frame, std::chrono::milliseconds timeout)
{
  auto prefix = makeLengthPrefix(static_cast<std::uint32_t>(frame.size()));
  auto prefixResult = co_await socket.asyncWriteAll(std::span(prefix), timeout);
  if (!prefixResult)
  {
    co_return std::unexpected(prefixResult.error());
  }

  auto result = co_await socket.asyncWriteAll(frame, timeout);
  if (!result)
  {
    co_return std::unexpected(result.error());
  }
  co_return* prefixResult + *result;
}

asio::awaitable<std::expected<std::vector<std::byte>, std::error_code>> TicketFrame::asyncReadFrame(
  TcpSocket& socket, std::chrono::milliseconds timeout)
{
  auto lenBuf = co_await readLengthPrefixAsync(socket, timeout);
  if (!lenBuf)
  {
    co_return std::unexpected(lenBuf.error());
  }

  auto result = co_await socket.asyncReadExact(*lenBuf, timeout);
  if (!result)
  {
    co_return std::unexpected(result.error());
  }
  co_return* lenBuf;
}

asio::awaitable<std::expected<std::vector<std::byte>, std::error_code>> TicketFrame::asyncReadFrame(
  SslSocket& socket, std::chrono::milliseconds timeout)
{
  auto lenBuf = co_await readLengthPrefixAsync(socket, timeout);
  if (!lenBuf)
  {
    co_return std::unexpected(lenBuf.error());
  }

  auto result = co_await socket.asyncReadExact(*lenBuf, timeout);
  if (!result)
  {
    co_return std::unexpected(result.error());
  }
  co_return* lenBuf;
}

}  // namespace Network

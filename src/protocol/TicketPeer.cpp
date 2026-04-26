#include "protocol/TicketPeer.h"

#include <utility>

#include "protocol/TicketFrame.h"
#include "protocol/TicketJson.h"

namespace Network
{

namespace
{

std::shared_ptr<TicketSerializer> default_serializer()
{
  return std::make_shared<TicketJson>();
}

}  // namespace

TicketPeer::TicketPeer(std::unique_ptr<DualSocket> socket, std::shared_ptr<TicketSerializer> serializer)
  : _socket(std::move(socket)), _serializer(serializer ? serializer : default_serializer()), _closed(false)
{
}

TicketPeer::~TicketPeer()
{
  if (!_closed)
  {
    (void)goodbye();
  }
}

TicketPeer::TicketPeer(TicketPeer&& other) noexcept
  : _socket(std::move(other._socket)), _serializer(std::move(other._serializer)), _closed(other._closed)
{
  other._closed = true;
}

TicketPeer& TicketPeer::operator=(TicketPeer&& other) noexcept
{
  if (this != &other)
  {
    _socket = std::move(other._socket);
    _serializer = std::move(other._serializer);
    _closed = other._closed;
    other._closed = true;
  }
  return *this;
}

std::expected<std::size_t, std::error_code> TicketPeer::sendFrame(const TicketInfo& ticket)
{
  auto serialized = _serializer->serialize(ticket);
  if (serialized.empty())
  {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  auto tcpSocket = dynamic_cast<TcpSocket*>(_socket.get());
  if (tcpSocket)
  {
    return TicketFrame::writeFrame(*tcpSocket, std::span(serialized));
  }

  auto sslSocket = dynamic_cast<SslSocket*>(_socket.get());
  if (sslSocket)
  {
    return TicketFrame::writeFrame(*sslSocket, std::span(serialized));
  }

  return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TicketPeer::sendFrameAsync(const TicketInfo& ticket)
{
  auto serialized = _serializer->serialize(ticket);
  if (serialized.empty())
  {
    co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  auto tcpSocket = dynamic_cast<TcpSocket*>(_socket.get());
  if (tcpSocket)
  {
    co_return co_await TicketFrame::asyncWriteFrame(*tcpSocket, std::span(serialized));
  }

  auto sslSocket = dynamic_cast<SslSocket*>(_socket.get());
  if (sslSocket)
  {
    co_return co_await TicketFrame::asyncWriteFrame(*sslSocket, std::span(serialized));
  }

  co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

std::expected<TicketInfo, std::error_code> TicketPeer::receiveFrame()
{
  auto tcpSocket = dynamic_cast<TcpSocket*>(_socket.get());
  if (tcpSocket)
  {
    auto frame = TicketFrame::readFrame(*tcpSocket);
    if (!frame)
    {
      return std::unexpected(frame.error());
    }
    auto ticket = _serializer->deserialize(std::span(*frame));
    if (!ticket)
    {
      return std::unexpected(ticket.error());
    }
    return *ticket;
  }

  auto sslSocket = dynamic_cast<SslSocket*>(_socket.get());
  if (sslSocket)
  {
    auto frame = TicketFrame::readFrame(*sslSocket);
    if (!frame)
    {
      return std::unexpected(frame.error());
    }
    auto ticket = _serializer->deserialize(std::span(*frame));
    if (!ticket)
    {
      return std::unexpected(ticket.error());
    }
    return *ticket;
  }

  return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

asio::awaitable<std::expected<TicketInfo, std::error_code>> TicketPeer::receiveFrameAsync()
{
  auto tcpSocket = dynamic_cast<TcpSocket*>(_socket.get());
  if (tcpSocket)
  {
    auto frame = co_await TicketFrame::asyncReadFrame(*tcpSocket);
    if (!frame)
    {
      co_return std::unexpected(frame.error());
    }
    auto ticket = _serializer->deserialize(std::span(*frame));
    if (!ticket)
    {
      co_return std::unexpected(ticket.error());
    }
    co_return* ticket;
  }

  auto sslSocket = dynamic_cast<SslSocket*>(_socket.get());
  if (sslSocket)
  {
    auto frame = co_await TicketFrame::asyncReadFrame(*sslSocket);
    if (!frame)
    {
      co_return std::unexpected(frame.error());
    }
    auto ticket = _serializer->deserialize(std::span(*frame));
    if (!ticket)
    {
      co_return std::unexpected(ticket.error());
    }
    co_return* ticket;
  }

  co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
}

std::expected<TicketInfo, std::error_code> TicketPeer::handshake(TicketInfo request)
{
  auto result = sendFrame(request);
  if (!result)
  {
    return std::unexpected(result.error());
  }

  auto response = receiveFrame();
  if (!response)
  {
    return std::unexpected(response.error());
  }

  return *response;
}

asio::awaitable<std::expected<TicketInfo, std::error_code>> TicketPeer::handshakeAsync(TicketInfo request)
{
  auto result = co_await sendFrameAsync(request);
  if (!result)
  {
    co_return std::unexpected(result.error());
  }

  auto response = co_await receiveFrameAsync();
  if (!response)
  {
    co_return std::unexpected(response.error());
  }

  co_return* response;
}

std::expected<void, std::error_code> TicketPeer::goodbye()
{
  _closed = true;
  if (isConnected())
  {
    _socket->closeSocket();
  }
  return std::expected<void, std::error_code>{};
}

asio::awaitable<std::expected<void, std::error_code>> TicketPeer::goodbyeAsync()
{
  _closed = true;
  if (isConnected())
  {
    _socket->closeSocket();
  }
  co_return std::expected<void, std::error_code>{};
}

DualSocket& TicketPeer::getSocket()
{
  return *_socket;
}

const DualSocket& TicketPeer::getSocket() const
{
  return *_socket;
}

bool TicketPeer::isConnected() const noexcept
{
  return _socket->isConnected();
}

}  // namespace Network

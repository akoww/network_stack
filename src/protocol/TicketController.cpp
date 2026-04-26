#include "protocol/TicketController.h"

#include <algorithm>
#include <charconv>
#include <random>
#include <sstream>
#include <utility>

#include "protocol/TicketJson.h"
#include "socket/SocketBase.h"

namespace Network
{

namespace
{

std::string generate_uuid()
{
  static thread_local std::mt19937 gen(std::random_device{}());
  static thread_local std::uniform_int_distribution<> dist(0, 15);
  static thread_local std::uniform_int_distribution<> dist2(8, 11);

  const char* hex = "0123456789abcdef";
  std::string uuid;
  uuid.reserve(36);

  for (int i = 0; i < 36; ++i)
  {
    if (i == 8 || i == 13 || i == 18 || i == 23)
    {
      uuid.push_back('-');
      continue;
    }
    if (i == 12)
    {
      uuid.push_back('4');
      continue;
    }
    if (i == 16)
    {
      uuid.push_back(hex[dist2(gen)]);
      continue;
    }
    uuid.push_back(hex[dist(gen)]);
  }
  return uuid;
}

std::shared_ptr<TicketSerializer> default_serializer()
{
  return std::make_shared<TicketJson>();
}

}  // namespace

TicketController::TicketController(std::unique_ptr<DualSocket> socket, std::shared_ptr<TicketSerializer> serializer)
  : _peer(std::move(socket), serializer ? serializer : default_serializer()),
    _socketId(std::to_string(_peer.getSocket().getId()))
{
}

TicketController::~TicketController() = default;

TicketController::TicketController(TicketController&& other) noexcept = default;
TicketController& TicketController::operator=(TicketController&& other) noexcept = default;

std::string TicketController::generateTicketId()
{
  return generate_uuid() + "/" + _socketId + "#" + std::to_string(_ticketCounter++);
}

std::expected<std::string, std::error_code> TicketController::send(std::string_view command,
                                                                   std::string payload,
                                                                   std::string_view metadata,
                                                                   Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = generateTicketId();
  ticket.type = TicketType::SHORT;
  ticket.status = TicketStatus::PENDING;
  ticket.command = std::string(command);
  ticket.payload = std::move(payload);
  ticket.timeout = opts.timeout;

  if (!metadata.empty())
  {
    ticket.metadata.data["metadata"] = std::string(metadata);
  }

  auto response = _peer.handshake(std::move(ticket));
  if (!response)
  {
    return std::unexpected(response.error());
  }

  if (response->status == TicketStatus::FAILED)
  {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  if (!response->result.has_value())
  {
    return std::unexpected(std::make_error_code(std::errc::protocol_error));
  }

  return *response->result;
}

asio::awaitable<std::expected<std::string, std::error_code>> TicketController::sendAsync(std::string_view command,
                                                                                         std::string payload,
                                                                                         std::string_view metadata,
                                                                                         Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = generateTicketId();
  ticket.type = TicketType::SHORT;
  ticket.status = TicketStatus::PENDING;
  ticket.command = std::string(command);
  ticket.payload = std::move(payload);
  ticket.timeout = opts.timeout;

  if (!metadata.empty())
  {
    ticket.metadata.data["metadata"] = std::string(metadata);
  }

  auto response = co_await _peer.handshakeAsync(std::move(ticket));
  if (!response)
  {
    co_return std::unexpected(response.error());
  }

  if (response->status == TicketStatus::FAILED)
  {
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  if (!response->result.has_value())
  {
    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
  }

  co_return * response->result;
}

std::expected<TicketInfo, std::error_code> TicketController::sendLong(std::string_view command,
                                                                      std::string payload,
                                                                      std::function<void(const TicketInfo&)> onProgress,
                                                                      std::string_view metadata,
                                                                      Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = generateTicketId();
  ticket.type = TicketType::LONG;
  ticket.status = TicketStatus::PENDING;
  ticket.command = std::string(command);
  ticket.payload = std::move(payload);
  ticket.timeout = opts.timeout;

  if (!metadata.empty())
  {
    ticket.metadata.data["metadata"] = std::string(metadata);
  }

  auto response = _peer.handshake(std::move(ticket));
  if (!response)
  {
    return std::unexpected(response.error());
  }

  if (response->status == TicketStatus::FAILED)
  {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  if (response->status == TicketStatus::COMPLETED)
  {
    return *response;
  }

  if (response->status == TicketStatus::CANCELLED)
  {
    return *response;
  }

  while (response->status == TicketStatus::RUNNING)
  {
    if (onProgress)
    {
      onProgress(*response);
    }

    auto pollResult = poll(response->ticket_id, opts);
    if (!pollResult)
    {
      return std::unexpected(pollResult.error());
    }
    response = *pollResult;
  }

  if (response->status == TicketStatus::COMPLETED)
  {
    return *response;
  }

  if (response->status == TicketStatus::CANCELLED)
  {
    return *response;
  }

  if (response->status == TicketStatus::FAILED)
  {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  return std::unexpected(std::make_error_code(std::errc::protocol_error));
}

asio::awaitable<std::expected<TicketInfo, std::error_code>> TicketController::sendLongAsync(
  std::string_view command,
  std::string payload,
  std::function<void(const TicketInfo&)> onProgress,
  std::string_view metadata,
  Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = generateTicketId();
  ticket.type = TicketType::LONG;
  ticket.status = TicketStatus::PENDING;
  ticket.command = std::string(command);
  ticket.payload = std::move(payload);
  ticket.timeout = opts.timeout;

  if (!metadata.empty())
  {
    ticket.metadata.data["metadata"] = std::string(metadata);
  }

  auto response = co_await _peer.handshakeAsync(std::move(ticket));
  if (!response)
  {
    co_return std::unexpected(response.error());
  }

  if (response->status == TicketStatus::FAILED)
  {
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  if (response->status == TicketStatus::COMPLETED)
  {
    co_return* response;
  }

  if (response->status == TicketStatus::CANCELLED)
  {
    co_return* response;
  }

  while (response->status == TicketStatus::RUNNING)
  {
    if (onProgress)
    {
      onProgress(*response);
    }

    auto pollResult = co_await pollAsync(response->ticket_id, opts);
    if (!pollResult)
    {
      co_return std::unexpected(pollResult.error());
    }
    response = *pollResult;
  }

  if (response->status == TicketStatus::COMPLETED)
  {
    co_return* response;
  }

  if (response->status == TicketStatus::CANCELLED)
  {
    co_return* response;
  }

  if (response->status == TicketStatus::FAILED)
  {
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
}

std::expected<TicketInfo, std::error_code> TicketController::poll(std::string_view ticketId, Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = std::string(ticketId);
  ticket.type = TicketType::SHORT;
  ticket.status = TicketStatus::PENDING;
  ticket.command = "poll";
  ticket.timeout = opts.timeout;

  auto response = _peer.handshake(std::move(ticket));
  if (!response)
  {
    return std::unexpected(response.error());
  }

  if (response->status == TicketStatus::FAILED)
  {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  return *response;
}

asio::awaitable<std::expected<TicketInfo, std::error_code>> TicketController::pollAsync(std::string_view ticketId,
                                                                                        Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = std::string(ticketId);
  ticket.type = TicketType::SHORT;
  ticket.status = TicketStatus::PENDING;
  ticket.command = "poll";
  ticket.timeout = opts.timeout;

  auto response = co_await _peer.handshakeAsync(std::move(ticket));
  if (!response)
  {
    co_return std::unexpected(response.error());
  }

  if (response->status == TicketStatus::FAILED)
  {
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  co_return* response;
}

std::expected<void, std::error_code> TicketController::cancel(std::string_view ticketId, Options opts)
{
  TicketInfo ticket;
  ticket.ticket_id = std::string(ticketId);
  ticket.type = TicketType::SHORT;
  ticket.status = TicketStatus::CANCELLED;
  ticket.command = "cancel";
  ticket.timeout = opts.timeout;

  auto response = _peer.handshake(std::move(ticket));
  if (!response)
  {
    return std::unexpected(response.error());
  }

  if (response->status != TicketStatus::CANCELLED)
  {
    return std::unexpected(std::make_error_code(std::errc::io_error));
  }

  return std::expected<void, std::error_code>{};
}

TicketPeer& TicketController::getPeer()
{
  return _peer;
}

const TicketPeer& TicketController::getPeer() const
{
  return _peer;
}

}  // namespace Network

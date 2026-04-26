#include "protocol/TicketWorker.h"

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

TicketWorker::TicketWorker(std::unique_ptr<DualSocket> socket, Options opts)
  : _peer(std::move(socket), opts.serializer ? opts.serializer : default_serializer())
{
}

TicketWorker::~TicketWorker() = default;

TicketWorker::TicketWorker(TicketWorker&& other) noexcept = default;
TicketWorker& TicketWorker::operator=(TicketWorker&& other) noexcept = default;

void TicketWorker::registerHandler(std::string_view command, TicketHandler handler)
{
  _handlers[std::string(command)] = std::move(handler);
}

void TicketWorker::registerLongHandler(std::string_view command, TicketLongHandler handler)
{
  _longHandlers[std::string(command)] = std::move(handler);
}

std::expected<void, std::error_code> TicketWorker::handleNext()
{
  auto ticket = _peer.receiveFrame();
  if (!ticket)
  {
    return std::unexpected(ticket.error());
  }

  processTicket(std::move(*ticket));
  return std::expected<void, std::error_code>{};
}

asio::awaitable<std::expected<void, std::error_code>> TicketWorker::handleNextAsync()
{
  auto ticket = co_await _peer.receiveFrameAsync();
  if (!ticket)
  {
    co_return std::unexpected(ticket.error());
  }

  co_await processTicketAsync(std::move(*ticket));
  co_return std::expected<void, std::error_code>{};
}

void TicketWorker::processTicket(TicketInfo ticket)
{
  auto it = _longHandlers.find(ticket.command);
  if (it != _longHandlers.end())
  {
    // Long handlers not yet implemented
    sendErrorResponse(ticket.ticket_id, "long handlers not implemented", ticket.type, ticket.command, ticket.timeout);
    return;
  }

  auto handlerIt = _handlers.find(ticket.command);
  if (handlerIt == _handlers.end())
  {
    sendErrorResponse(ticket.ticket_id, "unknown command", ticket.type, ticket.command, ticket.timeout);
    return;
  }

  std::string payloadStr = ticket.payload.value_or("");
  TicketContext ctx;
  ctx.ticket_id = ticket.ticket_id;
  ctx.type = ticket.type;
  ctx.command = ticket.command;
  ctx.payload = payloadStr;
  ctx.metadata = ticket.metadata.data;
  ctx.timeout = ticket.timeout;

  ctx.progress = [](int, std::string_view) {};

  auto result = handlerIt->second(ctx);
  if (!result)
  {
    sendErrorResponse(ticket.ticket_id, result.error().message(), ticket.type, ticket.command, ticket.timeout);
    return;
  }

  TicketInfo response;
  response.ticket_id = ctx.ticket_id;
  response.status = TicketStatus::COMPLETED;
  response.result = *result;
  response.type = ticket.type;
  response.command = ticket.command;
  response.timeout = ticket.timeout;

  (void)sendResponse(response);
}

asio::awaitable<void> TicketWorker::processTicketAsync(TicketInfo ticket)
{
  auto it = _longHandlers.find(ticket.command);
  if (it != _longHandlers.end())
  {
    sendErrorResponse(ticket.ticket_id, "long handlers not implemented", ticket.type, ticket.command, ticket.timeout);
    co_return;
  }

  auto handlerIt = _handlers.find(ticket.command);
  if (handlerIt == _handlers.end())
  {
    sendErrorResponse(ticket.ticket_id, "unknown command", ticket.type, ticket.command, ticket.timeout);
    co_return;
  }

  std::string payloadStr = ticket.payload.value_or("");
  TicketContext ctx;
  ctx.ticket_id = ticket.ticket_id;
  ctx.type = ticket.type;
  ctx.command = ticket.command;
  ctx.payload = payloadStr;
  ctx.metadata = ticket.metadata.data;
  ctx.timeout = ticket.timeout;

  ctx.progress = [](int, std::string_view) {};

  auto result = handlerIt->second(ctx);
  if (!result)
  {
    sendErrorResponse(ticket.ticket_id, result.error().message(), ticket.type, ticket.command, ticket.timeout);
    co_return;
  }

  TicketInfo response;
  response.ticket_id = ctx.ticket_id;
  response.status = TicketStatus::COMPLETED;
  response.result = *result;
  response.type = ticket.type;
  response.command = ticket.command;
  response.timeout = ticket.timeout;

  (void)co_await sendResponseAsync(response);
}

std::expected<std::size_t, std::error_code> TicketWorker::sendResponse(const TicketInfo& response)
{
  return _peer.sendFrame(response);
}

asio::awaitable<std::expected<std::size_t, std::error_code>> TicketWorker::sendResponseAsync(const TicketInfo& response)
{
  co_return co_await _peer.sendFrameAsync(response);
}

TicketPeer& TicketWorker::getPeer()
{
  return _peer;
}

const TicketPeer& TicketWorker::getPeer() const
{
  return _peer;
}

void TicketWorker::sendErrorResponse(std::string_view ticketId,
                                     std::string_view error,
                                     TicketType type,
                                     std::string_view command,
                                     std::chrono::milliseconds timeout)
{
  TicketInfo response;
  response.ticket_id = std::string(ticketId);
  response.status = TicketStatus::FAILED;
  response.error = std::string(error);
  response.type = type;
  response.command = std::string(command);
  response.timeout = timeout;

  (void)sendResponse(response);
}

}  // namespace Network

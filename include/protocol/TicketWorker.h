#pragma once

#include <asio/awaitable.hpp>
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <map>
#include <unordered_map>

#include "protocol/Ticket.h"
#include "protocol/TicketContext.h"
#include "protocol/TicketPeer.h"

namespace Network
{

class TicketPeer;

/// @brief Worker - receives and processes tickets from a connected DualSocket.
/// Replaces the old TicketServer. Takes ownership of the socket and performs
/// a handshake on construction. On destruction, sends a goodbye frame.
class TicketWorker
{
public:
  using TicketHandler = std::function<std::expected<std::string, std::error_code>(const TicketContext& ctx)>;
  using TicketLongHandler = std::function<std::expected<TicketInfo, std::error_code>(const TicketContext& ctx,
                                                                                     TicketContext::ProgressPush push)>;

  struct Options
  {
    std::chrono::milliseconds timeout;
    std::shared_ptr<TicketSerializer> serializer;
  };

  TicketWorker(std::unique_ptr<DualSocket> socket, Options opts);
  ~TicketWorker();

  // Non-copyable
  TicketWorker(const TicketWorker&) = delete;
  TicketWorker& operator=(const TicketWorker&) = delete;

  /// @brief Move constructor
  TicketWorker(TicketWorker&& other) noexcept;
  /// @brief Move assignment
  TicketWorker& operator=(TicketWorker&& other) noexcept;

  void registerHandler(std::string_view command, TicketHandler handler);
  void registerLongHandler(std::string_view command, TicketLongHandler handler);

  /// @brief Wait for and process one ticket (sync).
  std::expected<void, std::error_code> handleNext();

  /// @brief Wait for and process one ticket (async).
  asio::awaitable<std::expected<void, std::error_code>> handleNextAsync();

  /// @brief Process a single ticket (sync).
  void processTicket(TicketInfo ticket);

  /// @brief Process a single ticket (async).
  asio::awaitable<void> processTicketAsync(TicketInfo ticket);

  /// @brief Send a response frame (sync).
  std::expected<std::size_t, std::error_code> sendResponse(const TicketInfo& response);

  /// @brief Send a response frame (async).
  asio::awaitable<std::expected<std::size_t, std::error_code>> sendResponseAsync(const TicketInfo& response);

  /// @brief Send an error response (best-effort; failures are silently ignored).

  TicketPeer& getPeer();
  const TicketPeer& getPeer() const;

private:
  void sendErrorResponse(std::string_view ticketId,
                         std::string_view error,
                         TicketType type,
                         std::string_view command,
                         std::chrono::milliseconds timeout);

  TicketPeer _peer;
  std::map<std::string, TicketHandler> _handlers;
  std::map<std::string, TicketLongHandler> _longHandlers;
};

}  // namespace Network

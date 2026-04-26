#pragma once

#include <asio/awaitable.hpp>
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "protocol/Ticket.h"
#include "protocol/TicketPeer.h"

namespace Network
{

/// @brief Controller - initiates and manages ticket operations on a connected DualSocket.
/// Replaces the old TicketClient. Takes ownership of the socket and performs
/// a handshake on construction. On destruction, sends a goodbye frame.
class TicketController
{
public:
  struct Options
  {
    std::chrono::milliseconds timeout;
  };

  TicketController(std::unique_ptr<DualSocket> socket, std::shared_ptr<TicketSerializer> serializer);
  ~TicketController();

  // Non-copyable
  TicketController(const TicketController&) = delete;
  TicketController& operator=(const TicketController&) = delete;

  /// @brief Move constructor
  TicketController(TicketController&& other) noexcept;
  /// @brief Move assignment
  TicketController& operator=(TicketController&& other) noexcept;

  /// @brief Send a short-lived ticket and wait for the result (sync).
  std::expected<std::string, std::error_code> send(std::string_view command,
                                                   std::string payload,
                                                   std::string_view metadata = "",
                                                   Options opts = Options{});

  /// @brief Send a short-lived ticket and wait for the result (async).
  asio::awaitable<std::expected<std::string, std::error_code>> sendAsync(std::string_view command,
                                                                         std::string payload,
                                                                         std::string_view metadata = "",
                                                                         Options opts = Options{});

  /// @brief Send a long-running ticket and poll for completion (sync).
  std::expected<TicketInfo, std::error_code> sendLong(std::string_view command,
                                                      std::string payload,
                                                      std::function<void(const TicketInfo&)> onProgress = nullptr,
                                                      std::string_view metadata = "",
                                                      Options opts = Options{});

  /// @brief Send a long-running ticket and poll for completion (async).
  asio::awaitable<std::expected<TicketInfo, std::error_code>> sendLongAsync(
    std::string_view command,
    std::string payload,
    std::function<void(const TicketInfo&)> onProgress = nullptr,
    std::string_view metadata = "",
    Options opts = Options{});

  /// @brief Poll a ticket by ID (sync).
  std::expected<TicketInfo, std::error_code> poll(std::string_view ticketId, Options opts = Options{});

  /// @brief Poll a ticket by ID (async).
  asio::awaitable<std::expected<TicketInfo, std::error_code>> pollAsync(std::string_view ticketId,
                                                                        Options opts = Options{});

  /// @brief Cancel a ticket by ID (sync).
  std::expected<void, std::error_code> cancel(std::string_view ticketId, Options opts = Options{});

  TicketPeer& getPeer();
  const TicketPeer& getPeer() const;

private:
  std::string generateTicketId();

  TicketPeer _peer;
  uint64_t _ticketCounter = 0;
  std::string _socketId;
};

}  // namespace Network

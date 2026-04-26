#pragma once

#include <asio/awaitable.hpp>
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "protocol/Ticket.h"

namespace Network
{

class DualSocket;
class TicketSerializer;

/// @brief Core ticket protocol peer that manages a DualSocket connection.
/// Handles frame-level read/write operations, handshake, and goodbye.
/// Both TicketController (executor) and TicketWorker (handler) use this.
class TicketPeer
{
public:
  struct Options
  {
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000);
  };

  explicit TicketPeer(std::unique_ptr<DualSocket> socket, std::shared_ptr<TicketSerializer> serializer);
  ~TicketPeer();

  // Non-copyable
  TicketPeer(const TicketPeer&) = delete;
  TicketPeer& operator=(const TicketPeer&) = delete;

  /// @brief Move constructor
  TicketPeer(TicketPeer&& other) noexcept;
  /// @brief Move assignment
  TicketPeer& operator=(TicketPeer&& other) noexcept;

  /// @brief Perform handshake: send initial ticket, receive response.
  /// @return Deserialized TicketInfo from the response.
  std::expected<TicketInfo, std::error_code> handshake(TicketInfo request);

  /// @brief Asynchronous handshake.
  asio::awaitable<std::expected<TicketInfo, std::error_code>> handshakeAsync(TicketInfo request);

  /// @brief Send goodbye frame and close the connection.
  std::expected<void, std::error_code> goodbye();

  /// @brief Asynchronous goodbye.
  asio::awaitable<std::expected<void, std::error_code>> goodbyeAsync();

  /// @brief Send a frame (sync).
  std::expected<std::size_t, std::error_code> sendFrame(const TicketInfo& ticket);

  /// @brief Send a frame (async).
  asio::awaitable<std::expected<std::size_t, std::error_code>> sendFrameAsync(const TicketInfo& ticket);

  /// @brief Receive a frame (sync).
  std::expected<TicketInfo, std::error_code> receiveFrame();

  /// @brief Receive a frame (async).
  asio::awaitable<std::expected<TicketInfo, std::error_code>> receiveFrameAsync();

  DualSocket& getSocket();
  const DualSocket& getSocket() const;

  bool isConnected() const noexcept;

private:
  std::unique_ptr<DualSocket> _socket;
  std::shared_ptr<TicketSerializer> _serializer;
  bool _closed = false;
};

}  // namespace Network

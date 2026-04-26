#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

namespace Network
{

enum class TicketType
{
  SHORT,
  LONG
};

enum class TicketStatus
{
  PENDING,
  RUNNING,
  COMPLETED,
  FAILED,
  CANCELLED
};

[[nodiscard]] constexpr std::string_view ticket_type_to_string(TicketType type) noexcept
{
  switch (type)
  {
    case TicketType::SHORT:
      return "short";
    case TicketType::LONG:
      return "long";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view ticket_status_to_string(TicketStatus status) noexcept
{
  switch (status)
  {
    case TicketStatus::PENDING:
      return "pending";
    case TicketStatus::RUNNING:
      return "running";
    case TicketStatus::COMPLETED:
      return "completed";
    case TicketStatus::FAILED:
      return "failed";
    case TicketStatus::CANCELLED:
      return "cancelled";
  }
  return "unknown";
}

struct TicketMetadata
{
  std::unordered_map<std::string, std::string> data;
};

struct TicketInfo
{
  std::string ticket_id;
  TicketType type;
  TicketStatus status;
  std::string command;
  TicketMetadata metadata;
  std::chrono::milliseconds timeout;
  std::optional<int> progress_percent;
  std::optional<std::string> progress_msg;
  std::optional<std::string> payload;
  std::optional<std::string> result;
  std::optional<std::string> error;
};

}  // namespace Network

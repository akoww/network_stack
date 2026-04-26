#include "protocol/TicketJson.h"

#include <nlohmann/json.hpp>
#include <string_view>

#include "protocol/Ticket.h"

namespace Network
{

namespace
{

constexpr std::string_view k_ticket_id = "ticket_id";
constexpr std::string_view k_type = "type";
constexpr std::string_view k_status = "status";
constexpr std::string_view k_command = "command";
constexpr std::string_view k_metadata = "metadata";
constexpr std::string_view k_timeout_ms = "timeout_ms";
constexpr std::string_view k_progress_percent = "progress_percent";
constexpr std::string_view k_progress_msg = "progress_msg";
constexpr std::string_view k_payload = "payload";
constexpr std::string_view k_result = "result";
constexpr std::string_view k_error = "error";

}  // namespace

void to_json(nlohmann::json& j, const TicketInfo& ticket)
{
  j = nlohmann::json{
    {k_ticket_id, ticket.ticket_id},
    {k_type, ticket_type_to_string(ticket.type)},
    {k_status, ticket_status_to_string(ticket.status)},
    {k_command, ticket.command},
    {k_metadata, ticket.metadata.data},
    {k_timeout_ms, static_cast<int>(ticket.timeout.count())},
  };

  if (ticket.progress_percent.has_value())
  {
    j[k_progress_percent] = *ticket.progress_percent;
  }
  if (ticket.progress_msg.has_value())
  {
    j[k_progress_msg] = *ticket.progress_msg;
  }
  if (ticket.payload.has_value())
  {
    j[k_payload] = *ticket.payload;
  }
  if (ticket.result.has_value())
  {
    j[k_result] = *ticket.result;
  }
  if (ticket.error.has_value())
  {
    j[k_error] = *ticket.error;
  }
}

void from_json(const nlohmann::json& j, TicketInfo& ticket)
{
  ticket.ticket_id = j.value(k_ticket_id, "");
  ticket.command = j.value(k_command, "");
  ticket.metadata.data = j.value(k_metadata, std::unordered_map<std::string, std::string>{});
  ticket.timeout = std::chrono::milliseconds(j.value(k_timeout_ms, 0));

  auto typeStr = j.value(k_type, "short");
  if (typeStr == "long")
  {
    ticket.type = TicketType::LONG;
  }
  else
  {
    ticket.type = TicketType::SHORT;
  }

  auto statusStr = j.value(k_status, "pending");
  if (statusStr == "running")
  {
    ticket.status = TicketStatus::RUNNING;
  }
  else if (statusStr == "completed")
  {
    ticket.status = TicketStatus::COMPLETED;
  }
  else if (statusStr == "failed")
  {
    ticket.status = TicketStatus::FAILED;
  }
  else if (statusStr == "cancelled")
  {
    ticket.status = TicketStatus::CANCELLED;
  }
  else
  {
    ticket.status = TicketStatus::PENDING;
  }

  if (j.contains(k_progress_percent))
  {
    ticket.progress_percent = j[k_progress_percent].get<int>();
  }
  if (j.contains(k_progress_msg))
  {
    ticket.progress_msg = j[k_progress_msg].get<std::string>();
  }
  if (j.contains(k_payload))
  {
    ticket.payload = j[k_payload].get<std::string>();
  }
  if (j.contains(k_result))
  {
    ticket.result = j[k_result].get<std::string>();
  }
  if (j.contains(k_error))
  {
    ticket.error = j[k_error].get<std::string>();
  }
}

TicketJson::TicketJson() = default;
TicketJson::~TicketJson() = default;

std::vector<std::byte> TicketJson::serialize(const TicketInfo& ticket)
{
  nlohmann::json j = ticket;
  std::string str = j.dump();
  std::vector<std::byte> bytes(str.size());
  for (std::size_t i = 0; i < str.size(); ++i)
  {
    bytes[i] = std::byte(str[i]);
  }
  return bytes;
}

std::expected<TicketInfo, std::error_code> TicketJson::deserialize(std::span<const std::byte> data)
{
  std::string str(data.size(), '\0');
  for (std::size_t i = 0; i < data.size(); ++i)
  {
    str[i] = static_cast<char>(data[i]);
  }

  try
  {
    nlohmann::json j = nlohmann::json::parse(str);
    TicketInfo ticket;
    j.get_to(ticket);
    return ticket;
  }
  catch (const nlohmann::json::exception&)
  {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
}

}  // namespace Network

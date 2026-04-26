#pragma once

#include <expected>
#include <span>
#include <system_error>

#include "protocol/Ticket.h"

namespace Network
{

class TicketSerializer
{
public:
  virtual ~TicketSerializer() = default;

  virtual std::vector<std::byte> serialize(const TicketInfo& ticket) = 0;
  virtual std::expected<TicketInfo, std::error_code> deserialize(std::span<const std::byte> data) = 0;
};

}  // namespace Network

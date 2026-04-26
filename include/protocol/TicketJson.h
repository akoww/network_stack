#pragma once

#include "protocol/TicketSerializer.h"

namespace Network
{

class TicketJson : public TicketSerializer
{
public:
  TicketJson();
  ~TicketJson() override;

  std::vector<std::byte> serialize(const TicketInfo& ticket) override;
  std::expected<TicketInfo, std::error_code> deserialize(std::span<const std::byte> data) override;
};

}  // namespace Network

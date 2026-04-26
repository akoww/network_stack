#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

namespace Network
{

struct TicketContext
{
  std::string ticket_id;
  TicketType type;
  std::string command;
  std::string_view payload;
  std::unordered_map<std::string, std::string> metadata;
  std::chrono::milliseconds timeout;

  using ProgressPush = std::function<void(int percent, std::string_view status)>;
  ProgressPush progress;
};

}  // namespace Network

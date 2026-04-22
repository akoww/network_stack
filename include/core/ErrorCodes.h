#pragma once

#include <system_error>

namespace Network
{
/// @brief Network error codes used throughout the stack.
/// 0 is intentionally reserved for 'no error'.
enum class Error
{
  NO_ERROR = 0,
  // Connection errors
  CONNECTION_REFUSED,
  CONNECTION_TIMEOUT,
  CONNECTION_LOST,
  CONNECTION_ESTABLISHMENT_FAILED,
  SOCKET_CREATE_FAILED,
  // DNS errors
  DNS_RESOLUTION_FAILED,
  // Communication errors
  READ_FAILED,
  WRITE_FAILED,
  TIMEOUT,
  // Protocol errors
  PROTOCOL_ERROR,
  // TLS errors
  TLS_HANDSHAKE_FAILED,
  TLS_VALIDATION_FAILED,
  // Server errors
  SERVER_LISTEN_FAILED,
  SERVER_ACCEPT_FAILED,
  // Generic errors
  INTERNAL_ERROR
};

/// @brief Custom error category for Network error codes.
/// Integrates with std::error_code for proper error handling.
class ErrorCategory : public std::error_category
{
public:
  /// @brief Returns the name of the error category.
  const char* name() const noexcept override;

  /// @brief Returns a human-readable message for the given error code.
  std::string message(int ev) const override;
};

/// @brief Returns the singleton instance of the network error category.
const ErrorCategory& getNetworkCategory();

/// @brief Creates an std::error_code from a Network::Error value.
std::error_code make_error_code(Network::Error err);
}  // namespace Network

namespace std
{
/// @brief Specialization to enable implicit conversion from Network::Error to
/// std::error_code.
template <>
struct is_error_code_enum<Network::Error> : true_type
{
};
}  // namespace std
#include "core/ErrorCodes.h"

namespace Network
{

const char* ErrorCategory::name() const noexcept
{
  return "network";
}

std::string ErrorCategory::message(int ev) const
{
  switch (static_cast<Error>(ev))
  {
    case Error::NO_ERROR:
      return "No error";

    case Error::CONNECTION_REFUSED:
      return "Connection was refused by the remote host";

    case Error::CONNECTION_TIMEOUT:
      return "Connection attempt timed out";

    case Error::CONNECTION_LOST:
      return "Connection was lost during communication";

    case Error::CONNECTION_ESTABLISHMENT_FAILED:
      return "Failed to establish connection to remote host";

    case Error::SOCKET_CREATE_FAILED:
      return "Failed to create socket";

    case Error::DNS_RESOLUTION_FAILED:
      return "Failed to resolve hostname (DNS lookup failed)";

    case Error::READ_FAILED:
      return "Failed to read data from socket";

    case Error::WRITE_FAILED:
      return "Failed to write data to socket";

    case Error::TIMEOUT:
      return "Operation timed out";

    case Error::PROTOCOL_ERROR:
      return "Protocol error occurred while communicating with the server";

    case Error::TLS_HANDSHAKE_FAILED:
      return "TLS handshake failed";

    case Error::TLS_VALIDATION_FAILED:
      return "TLS certificate validation failed";

    case Error::SERVER_LISTEN_FAILED:
      return "Failed to start server listener";

    case Error::SERVER_ACCEPT_FAILED:
      return "Failed to accept incoming connection";

    case Error::INTERNAL_ERROR:
      return "Internal error occurred";
  }
  return "Unknown network error";
}

const ErrorCategory& getNetworkCategory()
{
  static ErrorCategory cat;
  return cat;
}

/// @brief Creates an std::error_code from a Network::Error value.
std::error_code make_error_code(Network::Error err)
{
  return std::error_code(static_cast<int>(err), getNetworkCategory());
}

}  // namespace Network
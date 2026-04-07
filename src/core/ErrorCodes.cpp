#include "core/ErrorCodes.h"

namespace Network {

const char *ErrorCategory::name() const noexcept { return "network"; }

std::string ErrorCategory::message(int ev) const {
  switch (static_cast<Error>(ev)) {
  case Error::NoError:
    return "No error";

  case Error::ConnectionRefused:
    return "Connection was refused by the remote host";

  case Error::ConnectionTimeout:
    return "Connection attempt timed out";

  case Error::ConnectionLost:
    return "Connection was lost during communication";

  case Error::DnsFailure:
    return "Failed to resolve host name (DNS failure)";

  case Error::ProtocolError:
    return "Protocol error occurred while communicating with the server";
  }
  return "Unknown network error";
}

const ErrorCategory &getNetworkCategory() {
  static ErrorCategory cat;
  return cat;
}

/// @brief Creates an std::error_code from a Network::Error value.
std::error_code make_error_code(Network::Error err) {
  return std::error_code(static_cast<int>(err), getNetworkCategory());
}

} // namespace Network
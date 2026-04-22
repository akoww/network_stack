#include "core/ErrorTranslation.h"
#include "core/ErrorCodes.h"
#include <asio/error.hpp>

namespace Network
{

namespace
{
/// @brief Check if an ASIO error indicates connection closed.
bool isConnectionClosed(std::error_code ec)
{
  return (ec == asio::error::eof || ec == asio::error::connection_reset || ec == asio::error::broken_pipe ||
          ec == asio::error::not_connected);
}

/// @brief Check if an ASIO error indicates timeout.
bool isTimeout(std::error_code ec)
{
  return (ec == asio::error::timed_out);
}

/// @brief Check if an ASIO error indicates connection refused.
bool isConnectionRefused(std::error_code ec)
{
  return (ec == asio::error::connection_refused || ec == asio::error::connection_aborted ||
          ec == asio::error::broken_pipe);
}

/// @brief Check if an ASIO error indicates DNS resolution failure.
bool isDnsError(std::error_code ec)
{
  return (ec == asio::error::host_not_found || ec == asio::error::host_not_found_try_again ||
          ec == asio::error::try_again);
}

/// @brief Check if an ASIO error indicates socket creation failure.
bool isSocketCreateError(std::error_code ec)
{
  return (ec == asio::error::address_family_not_supported || ec == asio::error::already_open ||
          ec == asio::error::operation_not_supported);
}
}  // namespace

std::error_code makeConnectionError(std::error_code ec)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  if (isTimeout(ec))
  {
    return make_error_code(Network::Error::CONNECTION_TIMEOUT);
  }

  if (isConnectionRefused(ec))
  {
    return make_error_code(Network::Error::CONNECTION_REFUSED);
  }

  if (isConnectionClosed(ec))
  {
    return make_error_code(Network::Error::CONNECTION_LOST);
  }

  // Default to connection establishment failure
  return make_error_code(Network::Error::CONNECTION_ESTABLISHMENT_FAILED);
}

std::error_code makeReadError(std::error_code ec)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  if (isTimeout(ec))
  {
    return make_error_code(Network::Error::TIMEOUT);
  }

  if (isConnectionClosed(ec))
  {
    return make_error_code(Network::Error::CONNECTION_LOST);
  }

  if (ec == asio::error::no_buffer_space)
  {
    return make_error_code(Network::Error::READ_FAILED);
  }

  // Default to read failure
  return make_error_code(Network::Error::READ_FAILED);
}

std::error_code makeWriteError(std::error_code ec)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  if (isTimeout(ec))
  {
    return make_error_code(Network::Error::TIMEOUT);
  }

  if (ec == asio::error::no_buffer_space || ec == asio::error::broken_pipe)
  {
    return make_error_code(Network::Error::WRITE_FAILED);
  }

  if (isConnectionClosed(ec))
  {
    return make_error_code(Network::Error::CONNECTION_LOST);
  }

  // Default to write failure
  return make_error_code(Network::Error::WRITE_FAILED);
}

std::error_code makeDnsError(std::error_code ec)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  if (isDnsError(ec))
  {
    return make_error_code(Network::Error::DNS_RESOLUTION_FAILED);
  }

  // Default to DNS resolution failure
  return make_error_code(Network::Error::DNS_RESOLUTION_FAILED);
}

std::error_code makeTlsError(std::error_code ec)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  // Default to TLS handshake failure
  return make_error_code(Network::Error::TLS_HANDSHAKE_FAILED);
}

std::error_code makeServerError(std::error_code ec, std::string_view operation)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  if (operation == "listen" || operation.starts_with("listen"))
  {
    return make_error_code(Network::Error::SERVER_LISTEN_FAILED);
  }

  if (operation == "accept" || operation.starts_with("accept"))
  {
    return make_error_code(Network::Error::SERVER_ACCEPT_FAILED);
  }

  return make_error_code(Network::Error::INTERNAL_ERROR);
}

std::error_code makeSocketCreateError(std::error_code ec)
{
  if (!ec)
  {
    return make_error_code(Network::Error::NO_ERROR);
  }

  if (isSocketCreateError(ec))
  {
    return make_error_code(Network::Error::SOCKET_CREATE_FAILED);
  }

  // Default to socket create failure
  return make_error_code(Network::Error::SOCKET_CREATE_FAILED);
}

}  // namespace Network

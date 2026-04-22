#pragma once

#include "core/ErrorCodes.h"
#include <system_error>

namespace Network
{
/// @brief Convert ASIO error codes to context-sensitive Network errors for connection operations.
/// @param ec The ASIO error code.
/// @return A context-sensitive Network error code.
std::error_code makeConnectionError(std::error_code ec);

/// @brief Convert ASIO error codes to context-sensitive Network errors for read operations.
/// @param ec The ASIO error code.
/// @return A context-sensitive Network error code.
std::error_code makeReadError(std::error_code ec);

/// @brief Convert ASIO error codes to context-sensitive Network errors for write operations.
/// @param ec The ASIO error code.
/// @return A context-sensitive Network error code.
std::error_code makeWriteError(std::error_code ec);

/// @brief Convert ASIO error codes to context-sensitive Network errors for DNS operations.
/// @param ec The ASIO error code.
/// @return A context-sensitive Network error code.
std::error_code makeDnsError(std::error_code ec);

/// @brief Convert ASIO/SSL error codes to context-sensitive Network errors for TLS operations.
/// @param ec The error code (ASIO or SSL).
/// @return A context-sensitive Network error code.
std::error_code makeTlsError(std::error_code ec);

/// @brief Convert ASIO error codes to context-sensitive Network errors for server operations.
/// @param ec The ASIO error code.
/// @param operation The server operation (listen/accept).
/// @return A context-sensitive Network error code.
std::error_code makeServerError(std::error_code ec, std::string_view operation);

/// @brief Convert ASIO error codes to context-sensitive Network errors for socket creation.
/// @param ec The ASIO error code.
/// @return A context-sensitive Network error code.
std::error_code makeSocketCreateError(std::error_code ec);

}  // namespace Network

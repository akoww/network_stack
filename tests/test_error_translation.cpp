#include "core/ErrorTranslation.h"
#include <asio/error.hpp>
#include <gtest/gtest.h>
#include <system_error>

namespace Network::Test
{

TEST(MakeConnectionErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeConnectionError(ec);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
  EXPECT_EQ(result.category(), Network::getNetworkCategory());
}

TEST(MakeConnectionErrorTest, TimeoutTranslatesToConnectionTimeout)
{
  auto asio_ec = asio::error::timed_out;
  auto result = makeConnectionError(asio_ec);

  EXPECT_TRUE(result);
  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_TIMEOUT));
  EXPECT_EQ(result.category(), Network::getNetworkCategory());
}

TEST(MakeConnectionErrorTest, ConnectionRefusedTranslatesCorrectly)
{
  auto asio_ec = asio::error::connection_refused;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
}

TEST(MakeConnectionErrorTest, ConnectionAbortedTranslatesToConnectionRefused)
{
  auto asio_ec = asio::error::connection_aborted;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
}

TEST(MakeConnectionErrorTest, BrokenPipeTranslatesToConnectionRefused)
{
  auto asio_ec = asio::error::broken_pipe;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
}

TEST(MakeConnectionErrorTest, EofTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::eof;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeConnectionErrorTest, ConnectionResetTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::connection_reset;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeConnectionErrorTest, NotConnectedTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::not_connected;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeConnectionErrorTest, GenericErrorTranslatesToEstablishmentFailed)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_ESTABLISHMENT_FAILED));
}

TEST(MakeConnectionErrorTest, CarryCauseForTimeout)
{
  auto asio_ec = asio::error::timed_out;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_TIMEOUT));
  EXPECT_EQ(result.category(), Network::getNetworkCategory());
}

TEST(MakeConnectionErrorTest, CarryCauseForRefused)
{
  auto asio_ec = asio::error::connection_refused;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_REFUSED));
  EXPECT_EQ(result.category(), Network::getNetworkCategory());
}

TEST(MakeConnectionErrorTest, CarryCauseForLost)
{
  auto asio_ec = asio::error::eof;
  auto result = makeConnectionError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
  EXPECT_EQ(result.category(), Network::getNetworkCategory());
}

TEST(MakeReadErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeReadError(ec);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
}

TEST(MakeReadErrorTest, TimeoutTranslatesToTimeout)
{
  auto asio_ec = asio::error::timed_out;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::TIMEOUT));
}

TEST(MakeReadErrorTest, EofTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::eof;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeReadErrorTest, ConnectionResetTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::connection_reset;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeReadErrorTest, NoBufferSizeTranslatesToReadFailed)
{
  auto asio_ec = asio::error::no_buffer_space;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::READ_FAILED));
}

TEST(MakeReadErrorTest, GenericErrorTranslatesToReadFailed)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::READ_FAILED));
}

TEST(MakeReadErrorTest, CarryCauseForTimeout)
{
  auto asio_ec = asio::error::timed_out;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::TIMEOUT));
}

TEST(MakeReadErrorTest, CarryCauseForLost)
{
  auto asio_ec = asio::error::eof;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeWriteErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeWriteError(ec);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
}

TEST(MakeWriteErrorTest, TimeoutTranslatesToTimeout)
{
  auto asio_ec = asio::error::timed_out;
  auto result = makeReadError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::TIMEOUT));
}

TEST(MakeWriteErrorTest, EofTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::eof;
  auto result = makeWriteError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeWriteErrorTest, ConnectionResetTranslatesToConnectionLost)
{
  auto asio_ec = asio::error::connection_reset;
  auto result = makeWriteError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::CONNECTION_LOST));
}

TEST(MakeWriteErrorTest, BrokenPipeTranslatesToWriteFailed)
{
  auto asio_ec = asio::error::broken_pipe;
  auto result = makeWriteError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::WRITE_FAILED));
}

TEST(MakeWriteErrorTest, NoBufferSizeTranslatesToWriteFailed)
{
  auto asio_ec = asio::error::no_buffer_space;
  auto result = makeWriteError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::WRITE_FAILED));
}

TEST(MakeWriteErrorTest, GenericErrorTranslatesToWriteFailed)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeWriteError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::WRITE_FAILED));
}

TEST(MakeDnsErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeDnsError(ec);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
}

TEST(MakeDnsErrorTest, HostNotFoundTranslatesToDnsFailure)
{
  auto asio_ec = asio::error::host_not_found;
  auto result = makeDnsError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::DNS_RESOLUTION_FAILED));
}

TEST(MakeDnsErrorTest, HostNotFoundTryAgainTranslatesToDnsFailure)
{
  auto asio_ec = asio::error::host_not_found_try_again;
  auto result = makeDnsError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::DNS_RESOLUTION_FAILED));
}

TEST(MakeDnsErrorTest, TryAgainTranslatesToDnsFailure)
{
  auto asio_ec = asio::error::try_again;
  auto result = makeDnsError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::DNS_RESOLUTION_FAILED));
}

TEST(MakeDnsErrorTest, GenericErrorTranslatesToDnsFailure)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeDnsError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::DNS_RESOLUTION_FAILED));
}

TEST(MakeTlsErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeTlsError(ec);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
}

TEST(MakeTlsErrorTest, GenericErrorTranslatesToHandshakeFailed)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeTlsError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::TLS_HANDSHAKE_FAILED));
}

TEST(MakeServerErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeServerError(ec, "listen");

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
}

TEST(MakeServerErrorTest, ListenOperation)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeServerError(asio_ec, "listen");

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SERVER_LISTEN_FAILED));
}

TEST(MakeServerErrorTest, ListenOperationPartialMatch)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeServerError(asio_ec, "listen_tcp");

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SERVER_LISTEN_FAILED));
}

TEST(MakeServerErrorTest, AcceptOperation)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeServerError(asio_ec, "accept");

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SERVER_ACCEPT_FAILED));
}

TEST(MakeServerErrorTest, AcceptOperationPartialMatch)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeServerError(asio_ec, "accept_connection");

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SERVER_ACCEPT_FAILED));
}

TEST(MakeServerErrorTest, UnknownOperationDefaultsToInternalError)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeServerError(asio_ec, "unknown_operation");

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::INTERNAL_ERROR));
}

TEST(MakeSocketCreateErrorTest, SuccessReturnsNoError)
{
  std::error_code ec;
  auto result = makeSocketCreateError(ec);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.value(), 0);
}

TEST(MakeSocketCreateErrorTest, AddressFamilyNotSupported)
{
  auto asio_ec = asio::error::address_family_not_supported;
  auto result = makeSocketCreateError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SOCKET_CREATE_FAILED));
}

TEST(MakeSocketCreateErrorTest, AlreadyOpen)
{
  auto asio_ec = asio::error::already_open;
  auto result = makeSocketCreateError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SOCKET_CREATE_FAILED));
}

TEST(MakeSocketCreateErrorTest, OperationNotSupported)
{
  auto asio_ec = asio::error::operation_not_supported;
  auto result = makeSocketCreateError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SOCKET_CREATE_FAILED));
}

TEST(MakeSocketCreateErrorTest, GenericError)
{
  auto asio_ec = std::error_code(1, std::generic_category());
  auto result = makeSocketCreateError(asio_ec);

  EXPECT_EQ(result.value(), static_cast<int>(Network::Error::SOCKET_CREATE_FAILED));
}

}  // namespace Network::Test

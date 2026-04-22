#include "core/ErrorCodes.h"
#include "core/ErrorTranslation.h"
#include <asio/error.hpp>
#include <gtest/gtest.h>
#include <system_error>

namespace Network::Test
{

TEST(ErrorCategoryTest, CategoryName)
{
  EXPECT_STREQ(Network::getNetworkCategory().name(), "network");
}

TEST(ErrorCategoryTest, NoErrorMessage)
{
  std::error_code ec = Network::Error::NO_ERROR;
  EXPECT_EQ(ec.message(), "No error");
}

TEST(ErrorCategoryTest, ConnectionTimeoutMessage)
{
  std::error_code ec = Network::Error::CONNECTION_TIMEOUT;
  EXPECT_EQ(ec.message(), "Connection attempt timed out");
}

TEST(ErrorCategoryTest, ConnectionRefusedMessage)
{
  std::error_code ec = Network::Error::CONNECTION_REFUSED;
  EXPECT_EQ(ec.message(), "Connection was refused by the remote host");
}

TEST(ErrorCategoryTest, ConnectionLostMessage)
{
  std::error_code ec = Network::Error::CONNECTION_LOST;
  EXPECT_EQ(ec.message(), "Connection was lost during communication");
}

TEST(ErrorCategoryTest, ConnectionEstablishmentFailedMessage)
{
  std::error_code ec = Network::Error::CONNECTION_ESTABLISHMENT_FAILED;
  EXPECT_EQ(ec.message(), "Failed to establish connection to remote host");
}

TEST(ErrorCategoryTest, SocketCreateFailedMessage)
{
  std::error_code ec = Network::Error::SOCKET_CREATE_FAILED;
  EXPECT_EQ(ec.message(), "Failed to create socket");
}

TEST(ErrorCategoryTest, DnsResolutionFailedMessage)
{
  std::error_code ec = Network::Error::DNS_RESOLUTION_FAILED;
  EXPECT_EQ(ec.message(), "Failed to resolve hostname (DNS lookup failed)");
}

TEST(ErrorCategoryTest, ReadFailedMessage)
{
  std::error_code ec = Network::Error::READ_FAILED;
  EXPECT_EQ(ec.message(), "Failed to read data from socket");
}

TEST(ErrorCategoryTest, WriteFailedMessage)
{
  std::error_code ec = Network::Error::WRITE_FAILED;
  EXPECT_EQ(ec.message(), "Failed to write data to socket");
}

TEST(ErrorCategoryTest, TimeoutMessage)
{
  std::error_code ec = Network::Error::TIMEOUT;
  EXPECT_EQ(ec.message(), "Operation timed out");
}

TEST(ErrorCategoryTest, ProtocolErrorMessage)
{
  std::error_code ec = Network::Error::PROTOCOL_ERROR;
  EXPECT_EQ(ec.message(), "Protocol error occurred while communicating with the server");
}

TEST(ErrorCategoryTest, TlsHandshakeFailedMessage)
{
  std::error_code ec = Network::Error::TLS_HANDSHAKE_FAILED;
  EXPECT_EQ(ec.message(), "TLS handshake failed");
}

TEST(ErrorCategoryTest, TlsValidationFailedMessage)
{
  std::error_code ec = Network::Error::TLS_VALIDATION_FAILED;
  EXPECT_EQ(ec.message(), "TLS certificate validation failed");
}

TEST(ErrorCategoryTest, ServerListenFailedMessage)
{
  std::error_code ec = Network::Error::SERVER_LISTEN_FAILED;
  EXPECT_EQ(ec.message(), "Failed to start server listener");
}

TEST(ErrorCategoryTest, ServerAcceptFailedMessage)
{
  std::error_code ec = Network::Error::SERVER_ACCEPT_FAILED;
  EXPECT_EQ(ec.message(), "Failed to accept incoming connection");
}

TEST(ErrorCategoryTest, InternalErrorMessage)
{
  std::error_code ec = Network::Error::INTERNAL_ERROR;
  EXPECT_EQ(ec.message(), "Internal error occurred");
}

TEST(ErrorCategoryTest, UnknownErrorMessage)
{
  std::error_code ec(-999, Network::getNetworkCategory());
  EXPECT_EQ(ec.message(), "Unknown network error");
}

TEST(ErrorCategoryTest, ErrorCodeEvaluationNoError)
{
  std::error_code ec = Network::Error::NO_ERROR;
  EXPECT_FALSE(ec);
}

TEST(ErrorCategoryTest, ErrorCodeEvaluationWithError)
{
  std::error_code ec = Network::Error::CONNECTION_TIMEOUT;
  EXPECT_TRUE(ec);
}

TEST(ErrorCategoryTest, ErrorCodeValue)
{
  std::error_code ec = Network::Error::CONNECTION_TIMEOUT;
  EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::CONNECTION_TIMEOUT));
}

}  // namespace Network::Test

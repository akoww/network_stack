#include <gtest/gtest.h>
#include "core/ErrorCodes.h"

TEST(NetworkErrorTest, NoErrorIsSuccess)
{
    std::error_code ec = Network::Error::NoError;

    EXPECT_FALSE(ec);         // 0 should evaluate to false
    EXPECT_EQ(ec.value(), 0); // Value should be 0
    EXPECT_EQ(ec.category().name(), std::string("network"));
}

TEST(NetworkErrorTest, ConnectionTimeoutFails)
{
    std::error_code ec = Network::Error::ConnectionTimeout;

    EXPECT_TRUE(ec); // Should evaluate to true
    EXPECT_EQ(ec.value(), static_cast<int>(Network::Error::ConnectionTimeout));
    EXPECT_EQ(ec.message(), "Connection attempt timed out");
}

TEST(NetworkErrorTest, UnknownValueMessage)
{
    // Test the default case in the message switch
    std::error_code ec(-99, Network::get_network_category());
    EXPECT_EQ(ec.message(), "Unknown network error");
}
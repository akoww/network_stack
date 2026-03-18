#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "core/Context.h"
#include "protocol/FtpFileTransfer.h"

namespace Network::Test {

TEST(FtpIntegrationTest, ConnectAndDisconnect) {
    IoContextWrapper io_ctx;
    io_ctx.start();
    
    FtpFileTransfer::ConnectOptions opts;
    opts.username = "demo";
    opts.password = "password";
    opts.timeout = std::chrono::seconds(10);
    
    auto ftp_result = openFtpConnection("test.rebex.net", 21, io_ctx, opts);
    EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();
    
    if (ftp_result) {
        auto ftp = std::move(*ftp_result);
        EXPECT_TRUE(ftp->isAlive());
    }
    
    io_ctx.stop();
}

} // namespace Network::Test

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

TEST(FtpIntegrationTest, ExistsFile) {
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
        
        auto exists_result = ftp->exists("/readme.txt");
        EXPECT_TRUE(exists_result.has_value()) << "exists() failed: " << exists_result.error().message();
        EXPECT_TRUE(exists_result.value()) << "readme.txt should exist";
        
        auto not_exists_result = ftp->exists("/nonexistent_file_12345.txt");
        EXPECT_TRUE(not_exists_result.has_value()) << "exists() failed: " << not_exists_result.error().message();
        EXPECT_FALSE(not_exists_result.value()) << "nonexistent file should return false";
    }
    
    io_ctx.stop();
}

TEST(FtpIntegrationTest, ListDirectory) {
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
        
        auto list_result = ftp->list("/");
        EXPECT_TRUE(list_result.has_value()) << "list() failed: " << list_result.error().message();
        
         const auto& files = list_result.value();
         EXPECT_FALSE(files.empty()) << "Directory listing should not be empty";
         
         bool found = false;
         for (const auto& file : files) {
             if (file.file_name == "readme.txt") {
                 found = true;
                 EXPECT_GT(file.size, 0) << "readme.txt should have size > 0";
                 EXPECT_NE(file.date, std::chrono::system_clock::time_point{}) << "date should be valid";
                 break;
             }
         }
         EXPECT_TRUE(found) << "readme.txt should be in directory listing";
    }
    
    io_ctx.stop();
}

TEST(FtpIntegrationTest, ExistsAndListCombined) {
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
        
        auto list_result = ftp->list("/");
        EXPECT_TRUE(list_result.has_value());
        
        for (const auto& file : list_result.value()) {
            auto exists_result = ftp->exists("/" + file.file_name);
            EXPECT_TRUE(exists_result.has_value()) 
                << "exists() for " << file.file_name << " failed: " << exists_result.error().message();
            EXPECT_TRUE(exists_result.value()) << file.file_name << " should exist";
        }
    }
    
    io_ctx.stop();
}

} // namespace Network::Test

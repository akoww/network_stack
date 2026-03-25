#include <chrono>
#include <gtest/gtest.h>

#include <spdlog/spdlog.h>

#include "core/Context.h"
#include "protocol/FtpFileTransfer.h"

namespace Network::Test {

TEST(FtpIntegrationTest, ConnectAndDisconnect) {
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result) {
    auto ftp = std::unique_ptr<Network::FtpFileTransfer>(
        static_cast<Network::FtpFileTransfer *>(ftp_result->release()));
    EXPECT_TRUE(ftp->isAlive());
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ExistsFile) {
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result) {
    auto ftp = std::move(*ftp_result);

    auto exists_result = ftp->exists("/readme.txt");
    ASSERT_TRUE(exists_result.has_value())
        << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << "readme.txt should exist";

    auto not_exists_result = ftp->exists("/nonexistent_file_12345.txt");
    ASSERT_TRUE(not_exists_result.has_value())
        << "exists() failed: " << not_exists_result.error().message();
    EXPECT_FALSE(not_exists_result.value())
        << "nonexistent file should return false";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ListDirectory) {
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result) {
    auto ftp = std::move(*ftp_result);

    auto list_result = ftp->list("/");
    EXPECT_TRUE(list_result.has_value())
        << "list() failed: " << list_result.error().message();

    const auto &files = list_result.value();
    EXPECT_FALSE(files.empty()) << "Directory listing should not be empty";

    bool found = false;
    for (const auto &file : files) {
      spdlog::info("- {}", file.file_name);
      if (file.file_name == "readme.txt") {
        found = true;
        EXPECT_GT(file.size, 0) << "readme.txt should have size > 0";
        EXPECT_NE(file.date, std::chrono::system_clock::time_point{})
            << "date should be valid";
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
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result) {
    auto ftp = std::move(*ftp_result);

    auto list_result = ftp->list("/");
    EXPECT_TRUE(list_result.has_value());

    for (const auto &file : list_result.value()) {
      auto exists_result = ftp->exists("/" + file.file_name);
      EXPECT_TRUE(exists_result.has_value())
          << "exists() for " << file.file_name
          << " failed: " << exists_result.error().message();
      EXPECT_TRUE(exists_result.value()) << file.file_name << " should exist";
    }
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, NestedDirectoriesNavigation) {

  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  // GTEST_SKIP() << "Skipping since we dont have correct rights";

  if (ftp_result) {
    auto ftp = std::move(*ftp_result);

    const std::filesystem::path base_dir = "test_nested";
    const std::filesystem::path level1 = base_dir / "dir_a";
    const std::filesystem::path level2 = level1 / "dir_b";
    const std::filesystem::path level3 = level2 / "dir_c";

    auto create_result = ftp->createDir(base_dir);
    EXPECT_TRUE(create_result.has_value())
        << "Failed to create base directory: "
        << create_result.error().message();

    create_result = ftp->createDir(level1);
    EXPECT_TRUE(create_result.has_value())
        << "Failed to create level1 directory: "
        << create_result.error().message();

    create_result = ftp->createDir(level2);
    EXPECT_TRUE(create_result.has_value())
        << "Failed to create level2 directory: "
        << create_result.error().message();

    create_result = ftp->createDir(level3);
    EXPECT_TRUE(create_result.has_value())
        << "Failed to create level3 directory: "
        << create_result.error().message();

    auto exists_result = ftp->exists(base_dir);
    EXPECT_TRUE(exists_result.has_value())
        << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << base_dir << " should exist";

    exists_result = ftp->exists(level3);
    EXPECT_TRUE(exists_result.has_value())
        << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << level3 << " should exist";

    auto list_result = ftp->list(base_dir);
    EXPECT_TRUE(list_result.has_value())
        << "list() failed: " << list_result.error().message();
    bool found_dir_a = false;
    for (const auto &file : list_result.value()) {
      if (file.file_name == "dir_a") {
        found_dir_a = true;
        break;
      }
    }
    EXPECT_TRUE(found_dir_a) << "dir_a should be in " << base_dir;

    EXPECT_TRUE(ftp->remove(level3).has_value()) << "Can't remove directory";
    EXPECT_TRUE(ftp->remove(level2).has_value()) << "Can't remove directory";
    EXPECT_TRUE(ftp->remove(level1).has_value()) << "Can't remove directory";
    EXPECT_TRUE(ftp->remove(base_dir).has_value()) << "Can't remove directory";

    exists_result = ftp->exists(base_dir);
    EXPECT_TRUE(exists_result.has_value())
        << "exists() failed after removal: " << exists_result.error().message();
    EXPECT_FALSE(exists_result.value())
        << base_dir << " should not exist after removal";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, WriteAndReadFile) {
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result) {
    auto ftp = std::move(*ftp_result);

    std::vector<std::byte> test_data = {
        std::byte(0x48), std::byte(0x65), std::byte(0x6c), std::byte(0x6c),
        std::byte(0x6f), std::byte(0x20), std::byte(0x57), std::byte(0x6f),
        std::byte(0x72), std::byte(0x6c), std::byte(0x64)};
    std::filesystem::path test_path = "/upload/test_write_read.txt";

    auto write_result = ftp->write(test_path, test_data);
    if (!write_result) {
      GTEST_SKIP() << "Write failed (likely server doesn't allow uploads): "
                   << write_result.error().message();
    }
    EXPECT_TRUE(write_result.has_value());
    if (write_result) {
      EXPECT_EQ(write_result->file_name, "test_write_read.txt");
      EXPECT_EQ(write_result->size, test_data.size());
    }

    auto exists_result = ftp->exists(test_path);
    EXPECT_TRUE(exists_result.has_value())
        << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << "test file should exist";

    auto read_result = ftp->read(test_path);
    EXPECT_TRUE(read_result.has_value())
        << "read() failed: " << read_result.error().message();
    if (read_result) {
      EXPECT_EQ(*read_result, test_data)
          << "Read data should match written data";
    }

    EXPECT_TRUE(ftp->remove(test_path).has_value())
        << "Failed to remove test file";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ReadExistingFile) {
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value())
      << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result) {
    auto ftp = std::move(*ftp_result);

    std::filesystem::path test_path = "/readme.txt";

    auto exists_result = ftp->exists(test_path);
    if (!exists_result.has_value() || !exists_result.value()) {
      GTEST_SKIP() << "readme.txt does not exist on server";
    }

    auto read_result = ftp->read(test_path);
    EXPECT_TRUE(read_result.has_value())
        << "read() failed: " << read_result.error().message();
    if (read_result) {
      EXPECT_FALSE(read_result->empty()) << "readme.txt should have content";
      spdlog::info("Read {} bytes from {}", read_result->size(),
                   test_path.string());
    }
  }

  io_ctx.stop();
}

} // namespace Network::Test

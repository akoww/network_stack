#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "fixtures/FtpServerFixture.h"

#include "core/Context.h"
#include "core/ErrorCodes.h"
#include "protocol/FtpFileTransfer.h"

namespace Network::Test
{

TEST(FtpIntegrationTest, ConnectAndDisconnect)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp =
      std::unique_ptr<Network::FtpFileTransfer>(dynamic_cast<Network::FtpFileTransfer*>(ftp_result->release()));
    EXPECT_TRUE(ftp->isAlive());
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ExistsFile)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    auto exists_result = ftp->exists("/readme.txt");
    ASSERT_TRUE(exists_result.has_value()) << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << "readme.txt should exist";

    auto not_exists_result = ftp->exists("/nonexistent_file_12345.txt");
    ASSERT_TRUE(not_exists_result.has_value()) << "exists() failed: " << not_exists_result.error().message();
    EXPECT_FALSE(not_exists_result.value()) << "nonexistent file should return false";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ListDirectory)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    auto list_result = ftp->list("/");
    EXPECT_TRUE(list_result.has_value()) << "list() failed: " << list_result.error().message();

    const auto& files = list_result.value();
    EXPECT_FALSE(files.empty()) << "Directory listing should not be empty";

    bool found = false;
    for (const auto& file : files)
    {
      spdlog::info("- {}", file.file_name);
      if (file.file_name == "readme.txt")
      {
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

TEST(FtpIntegrationTest, ExistsAndListCombined)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    auto list_result = ftp->list("/");
    EXPECT_TRUE(list_result.has_value());

    for (const auto& file : list_result.value())
    {
      auto exists_result = ftp->exists("/" + file.file_name);
      EXPECT_TRUE(exists_result.has_value())
        << "exists() for " << file.file_name << " failed: " << exists_result.error().message();
      EXPECT_TRUE(exists_result.value()) << file.file_name << " should exist";
    }
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, NestedDirectoriesNavigation)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  // GTEST_SKIP() << "Skipping since we dont have correct rights";

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    const std::filesystem::path base_dir = "test_nested";
    const std::filesystem::path level1 = base_dir / "dir_a";
    const std::filesystem::path level2 = level1 / "dir_b";
    const std::filesystem::path level3 = level2 / "dir_c";

    auto create_result = ftp->createDir(base_dir);
    EXPECT_TRUE(create_result.has_value()) << "Failed to create base directory: " << create_result.error().message();

    create_result = ftp->createDir(level1);
    EXPECT_TRUE(create_result.has_value()) << "Failed to create level1 directory: " << create_result.error().message();

    create_result = ftp->createDir(level2);
    EXPECT_TRUE(create_result.has_value()) << "Failed to create level2 directory: " << create_result.error().message();

    create_result = ftp->createDir(level3);
    EXPECT_TRUE(create_result.has_value()) << "Failed to create level3 directory: " << create_result.error().message();

    auto exists_result = ftp->exists(base_dir);
    EXPECT_TRUE(exists_result.has_value()) << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << base_dir << " should exist";

    exists_result = ftp->exists(level3);
    EXPECT_TRUE(exists_result.has_value()) << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << level3 << " should exist";

    auto list_result = ftp->list(base_dir);
    EXPECT_TRUE(list_result.has_value()) << "list() failed: " << list_result.error().message();
    bool found_dir_a = false;
    for (const auto& file : list_result.value())
    {
      if (file.file_name == "dir_a")
      {
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
    EXPECT_TRUE(exists_result.has_value()) << "exists() failed after removal: " << exists_result.error().message();
    EXPECT_FALSE(exists_result.value()) << base_dir << " should not exist after removal";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, WriteAndReadFile)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::vector<std::byte> test_data = {std::byte(0x48), std::byte(0x65), std::byte(0x6c), std::byte(0x6c),
                                        std::byte(0x6f), std::byte(0x20), std::byte(0x57), std::byte(0x6f),
                                        std::byte(0x72), std::byte(0x6c), std::byte(0x64)};
    std::filesystem::path test_path = "/upload/test_write_read.txt";

    auto write_result = ftp->write(test_path, test_data);
    if (!write_result)
    {
      GTEST_SKIP() << "Write failed (likely server doesn't allow uploads): " << write_result.error().message();
    }
    EXPECT_TRUE(write_result.has_value());
    if (write_result)
    {
      EXPECT_EQ(write_result->file_name, "test_write_read.txt");
      EXPECT_EQ(write_result->size, test_data.size());
    }

    auto exists_result = ftp->exists(test_path);
    EXPECT_TRUE(exists_result.has_value()) << "exists() failed: " << exists_result.error().message();
    EXPECT_TRUE(exists_result.value()) << "test file should exist";

    auto read_result = ftp->read(test_path);
    EXPECT_TRUE(read_result.has_value()) << "read() failed: " << read_result.error().message();
    if (read_result)
    {
      EXPECT_EQ(*read_result, test_data) << "Read data should match written data";
    }

    EXPECT_TRUE(ftp->remove(test_path).has_value()) << "Failed to remove test file";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ReadExistingFile)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::filesystem::path test_path = "/readme.txt";

    auto exists_result = ftp->exists(test_path);
    if (!exists_result.has_value() || !exists_result.value())
    {
      GTEST_SKIP() << "readme.txt does not exist on server";
    }

    auto read_result = ftp->read(test_path);
    EXPECT_TRUE(read_result.has_value()) << "read() failed: " << read_result.error().message();
    if (read_result)
    {
      EXPECT_FALSE(read_result->empty()) << "readme.txt should have content";
      spdlog::info("Read {} bytes from {}", read_result->size(), test_path.string());
    }
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, IsDirectory)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    auto root_dir_result = ftp->isDirectory("/");
    EXPECT_TRUE(root_dir_result.has_value()) << "isDirectory('/') failed: " << root_dir_result.error().message();
    EXPECT_TRUE(root_dir_result.value()) << "root should be a directory";

    auto file_result = ftp->isDirectory("/readme.txt");
    if (file_result.has_value())
    {
      EXPECT_FALSE(file_result.value()) << "readme.txt should be a file";
    }
    else
    {
      spdlog::warn("isDirectory('/readme.txt') failed: {}", file_result.error().message());
    }

    auto nonexistent_result = ftp->isDirectory("/nonexistent_dir_12345");
    EXPECT_FALSE(nonexistent_result.has_value()) << "isDirectory() for nonexistent path should return error";

    const std::filesystem::path test_dir = "/test_isdir_dir";
    const std::filesystem::path test_file = "/test_isdir_file.txt";

    auto create_dir_result = ftp->createDir(test_dir);
    if (create_dir_result.has_value())
    {
      auto is_dir_result = ftp->isDirectory(test_dir);
      EXPECT_TRUE(is_dir_result.has_value());
      EXPECT_TRUE(is_dir_result.value()) << test_dir << " should be a directory";

      auto create_file_result = ftp->write(test_file, std::vector<std::byte>{std::byte(0x41)});
      if (create_file_result.has_value())
      {
        auto is_file_result = ftp->isDirectory(test_file);
        EXPECT_TRUE(is_file_result.has_value());
        EXPECT_FALSE(is_file_result.value()) << test_file << " should be a file";

        EXPECT_TRUE(ftp->remove(test_file).has_value()) << "Failed to remove test file";
      }

      EXPECT_TRUE(ftp->remove(test_dir).has_value()) << "Failed to remove test directory";
    }
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ReadWithCallback)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value()) << "FTP connection failed: " << ftp_result.error().message();

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::filesystem::path test_path = "/readme.txt";

    auto exists_result = ftp->exists(test_path);
    if (!exists_result.has_value() || !exists_result.value())
    {
      GTEST_SKIP() << "readme.txt does not exist on server";
    }

    std::vector<std::byte> collected_data;
    std::size_t callback_count = 0;

    auto read_result = ftp->read(test_path,
                                 [&](std::span<const std::byte> chunk)
                                 {
                                   callback_count++;
                                   for (std::byte b : chunk)
                                   {
                                     collected_data.push_back(b);
                                   }
                                   return static_cast<std::size_t>(chunk.size());
                                 });

    EXPECT_TRUE(read_result.has_value()) << "read(callback) failed: " << read_result.error().message();
    if (read_result)
    {
      EXPECT_GT(callback_count, 0) << "Callback should be called at least once";
      EXPECT_EQ(*read_result, collected_data.size()) << "Total bytes should match collected data size";

      auto direct_read_result = ftp->read(test_path);
      EXPECT_TRUE(direct_read_result.has_value()) << "direct read failed: " << direct_read_result.error().message();
      if (direct_read_result && !direct_read_result->empty())
      {
        EXPECT_EQ(collected_data, *direct_read_result) << "Callback data should match direct read";
      }
    }
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, ReadWithCallbackError)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value());

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::filesystem::path test_path = "/readme.txt";

    bool callback_called = false;

    auto read_result = ftp->read(test_path,
                                 [&](std::span<const std::byte>)
                                 {
                                   callback_called = true;
                                   return std::unexpected(make_error_code(Network::Error::CONNECTION_LOST));
                                 });

    EXPECT_FALSE(read_result.has_value()) << "Callback error should propagate";
    EXPECT_TRUE(callback_called) << "Callback should be called";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, WriteWithCallback)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value());

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::vector<std::byte> original_data = {std::byte(0x48), std::byte(0x65), std::byte(0x6c), std::byte(0x6c),
                                            std::byte(0x6f), std::byte(0x20), std::byte(0x57), std::byte(0x6f),
                                            std::byte(0x72), std::byte(0x6c), std::byte(0x64)};  // "Hello World"

    std::filesystem::path test_path = "/upload/test_write_callback.txt";

    std::vector<std::byte> chunks;
    std::size_t chunk_idx = 0;
    std::size_t callback_count = 0;

    auto write_result =
      ftp->write(test_path,
                 [&]()
                 {
                   callback_count++;
                   if (chunk_idx >= original_data.size())
                   {
                     return std::span<const std::byte>{};  // Empty span signals EOF
                   }

                   auto remaining = static_cast<std::ptrdiff_t>(original_data.size() - chunk_idx);
                   auto chunk_size = std::min<std::size_t>(3, static_cast<std::size_t>(remaining));

                   auto idx = static_cast<std::ptrdiff_t>(chunk_idx);
                   chunks.insert(chunks.end(), original_data.begin() + idx,
                                 original_data.begin() + idx + static_cast<std::ptrdiff_t>(chunk_size));

                   chunk_idx += chunk_size;
                   auto idx2 = static_cast<std::ptrdiff_t>(chunk_idx - chunk_size);
                   return std::span<const std::byte>(original_data.data() + idx2, chunk_size);
                 });

    if (!write_result)
    {
      GTEST_SKIP() << "Write failed (server may not allow uploads): " << write_result.error().message();
    }

    EXPECT_TRUE(write_result.has_value());
    if (write_result)
    {
      EXPECT_GT(callback_count, 0) << "Callback should be called";
      EXPECT_EQ(write_result->size, original_data.size()) << "Written size should match original";
      EXPECT_EQ(chunks.size(), original_data.size()) << "All data should be collected";

      auto read_result = ftp->read(test_path);
      EXPECT_TRUE(read_result.has_value()) << "read after write failed: " << read_result.error().message();
      if (read_result)
      {
        EXPECT_EQ(*read_result, original_data) << "Read data should match written data";
      }

      EXPECT_TRUE(ftp->remove(test_path).has_value()) << "Failed to remove test file";
    }
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, WriteWithCallbackMultipleChunks)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value());

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::filesystem::path test_path = "/upload/test_write_multiple_chunks.txt";

    std::vector<std::byte> original_data(1024);
    for (std::size_t i = 0; i < original_data.size(); i++)
    {
      original_data[i] = std::byte(i % 256);
    }

    std::size_t callback_count = 0;
    std::vector<std::size_t> chunk_sizes;

    auto write_result = ftp->write(test_path,
                                   [&]()
                                   {
                                     callback_count++;
                                     if (callback_count > 10)
                                     {
                                       return std::span<const std::byte>{};  // EOF after 10+ calls
                                     }

                                     std::size_t chunk_size = std::min<std::size_t>(100, original_data.size());
                                     if (chunk_size == 0)
                                     {
                                       return std::span<const std::byte>{};
                                     }

                                     auto span = std::span<const std::byte>(original_data.data(), chunk_size);
                                     return span;
                                   });

    if (!write_result)
    {
      GTEST_SKIP() << "Write multiple chunks failed: " << write_result.error().message();
    }

    EXPECT_TRUE(write_result.has_value());
    EXPECT_GT(callback_count, 1) << "Should have multiple callback calls";
    EXPECT_EQ(write_result->size, 1000) << "Should have written 1000 bytes";
  }

  io_ctx.stop();
}

TEST(FtpIntegrationTest, WriteWithCallbackError)
{
  IoContextWrapper io_ctx;
  io_ctx.start();

  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::seconds(10);
  opts.data_timeout = std::chrono::seconds(30);

  auto ftp_result = openFtpConnection("127.0.0.1", 2121, io_ctx, opts);
  EXPECT_TRUE(ftp_result.has_value());

  if (ftp_result)
  {
    auto ftp = std::move(*ftp_result);

    std::filesystem::path test_path = "/upload/test_write_error.txt";

    bool callback_called = false;

    auto write_result = ftp->write(test_path,
                                   [&]()
                                   {
                                     callback_called = true;
                                     std::error_code ec = make_error_code(Network::Error::CONNECTION_LOST);
                                     return std::unexpected(ec);
                                   });

    EXPECT_FALSE(write_result.has_value()) << "Write should fail on callback error";
    EXPECT_TRUE(callback_called) << "Callback should be called";
  }

  io_ctx.stop();
}

namespace
{

class FtpEnvironment : public ::testing::Environment
{
public:
  void SetUp() override { fixture_.start(); }

  void TearDown() override { fixture_.stop(); }

private:
  Network::Test::FtpServerFixture fixture_;
};

}  // namespace

}  // namespace Network::Test

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  ::testing::AddGlobalTestEnvironment(new Network::Test::FtpEnvironment());

  return RUN_ALL_TESTS();
}

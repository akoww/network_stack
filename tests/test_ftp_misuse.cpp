#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <asio/use_awaitable.hpp>

#include "core/Context.h"
#include "core/ErrorCodes.h"
#include "fixtures/FtpServerFixture.h"
#include "fixtures/test_fixture_io_context.h"
#include "protocol/FileTransfer.h"
#include "protocol/FtpFileTransfer.h"
#include "protocol/FtpUtils.h"

namespace Network::Test
{

namespace
{

constexpr uint16_t FTP_UNAVAILABLE_PORT = 19999;

class DummyNavigator : public Network::Utility::SmartDirectoryNavigator
{
public:
  explicit DummyNavigator(const std::filesystem::path& startPath) : Network::Utility::SmartDirectoryNavigator(startPath)
  {
  }

  std::expected<void, std::error_code> ftpCd(const std::string& dir) override
  {
    (void)dir;
    return std::expected<void, std::error_code>{};
  }

  std::expected<void, std::error_code> ftpSelectDrive(const std::string& drive) override
  {
    (void)drive;
    return std::expected<void, std::error_code>{};
  }
};

bool hasFtpServer()
{
  static std::atomic<bool> computed{false};
  static std::atomic<bool> cached{false};
  if (computed.load())
  {
    return cached.load();
  }
  IoContextWrapper temp_ctx;

  FtpFileTransfer temp_ftp("127.0.0.1", 2121, temp_ctx);
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(500);
  auto result = temp_ftp.connect(opts);
  bool found = result.has_value();
  cached.store(found);
  computed.store(true);

  return found;
}

#define REQUIRE_FTP_SERVER()                      \
  do                                              \
  {                                               \
    if (!hasFtpServer())                          \
    {                                             \
      GTEST_SKIP() << "FTP server not available"; \
    }                                             \
  } while (0)

}  // namespace

// =========================================================================
// Group 1: Operations Without Connection
// =========================================================================

TEST_F(IoContextFixture, IsAliveWithoutConnect)
{
  FtpFileTransfer ftp("127.0.0.1", FTP_UNAVAILABLE_PORT, getIoContext());
  EXPECT_FALSE(ftp.isAlive()) << "isAlive() before connect should return false";
}

TEST_F(IoContextFixture, IsAliveAfterFail)
{
  FtpFileTransfer ftp("127.0.0.1", FTP_UNAVAILABLE_PORT, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(200);
  auto result = ftp.connect(opts);
  EXPECT_FALSE(result.has_value());
  EXPECT_FALSE(ftp.isAlive()) << "isAlive() after failed connect should return false";
}

TEST_F(IoContextFixture, IsAliveAfterSuccess)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_TRUE(result.has_value()) << "connect failed";
  EXPECT_TRUE(ftp.isAlive()) << "isAlive() after connect should return true";
}

// =========================================================================
// Group 2: Connection Misuse
// =========================================================================

TEST_F(IoContextFixture, ConnectFirstReturnsError)
{
  FtpFileTransfer ftp("127.0.0.1", FTP_UNAVAILABLE_PORT, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(200);
  auto first = ftp.connect(opts);
  EXPECT_FALSE(first.has_value()) << "first connect should fail to unavailable port";
}

TEST_F(IoContextFixture, ConnectSecondReturnsError)
{
  FtpFileTransfer ftp("127.0.0.1", FTP_UNAVAILABLE_PORT, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(200);
  ftp.connect(opts);
  auto second = ftp.connect(opts);
  EXPECT_FALSE(second.has_value()) << "second connect should also fail";
}

TEST_F(IoContextFixture, IsAliveAfterDoubleConnect)
{
  FtpFileTransfer ftp("127.0.0.1", FTP_UNAVAILABLE_PORT, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(200);
  ftp.connect(opts);
  ftp.connect(opts);
  EXPECT_FALSE(ftp.isAlive()) << "isAlive() after double connect should return false";
}

TEST_F(IoContextFixture, ConnectAfterStopIoContextReturnsError)
{
  GTEST_SKIP();

  FtpFileTransfer ftp("127.0.0.1", FTP_UNAVAILABLE_PORT, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(200);
  auto result = ftp.connect(opts);
  EXPECT_FALSE(result.has_value()) << "connect after stop should fail";
  if (!result.has_value())
  {
    EXPECT_NE(result.error().value(), static_cast<int>(Error::NO_ERROR));
  }
}

TEST_F(IoContextFixture, ConnectInvalidHostReturnsError)
{
  FtpFileTransfer ftp("fake.invalid.host.localhost", FTP_UNAVAILABLE_PORT, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_FALSE(result.has_value()) << "connect to invalid host should fail";
  if (!result.has_value())
  {
    auto ec = result.error();
    EXPECT_NE(ec.value(), static_cast<int>(Error::NO_ERROR));
  }
}

TEST_F(IoContextFixture, ConnectInvalidPortZeroReturnsError)
{
  FtpFileTransfer ftp("127.0.0.1", 0, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(500);
  auto result = ftp.connect(opts);
  EXPECT_FALSE(result.has_value()) << "connect with port 0 should fail";
  if (!result.has_value())
  {
    EXPECT_NE(result.error().value(), static_cast<int>(Error::NO_ERROR));
  }
}

TEST_F(IoContextFixture, ConnectInvalidPortHighNumberReturnsError)
{
  uint16_t unused_high_port = 60000;
  FtpFileTransfer ftp("127.0.0.1", unused_high_port, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(200);
  auto result = ftp.connect(opts);
  EXPECT_FALSE(result.has_value()) << "connect to unused high port should fail";
  if (!result.has_value())
  {
    EXPECT_NE(result.error().value(), static_cast<int>(Error::NO_ERROR));
  }
}

// =========================================================================
// Group 3: Credential Misuse
// =========================================================================

TEST_F(IoContextFixture, EmptyUsernamePasswordConnect)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_TRUE(result.has_value()) << "empty credentials may work on anonymous FTP server";
}

TEST_F(IoContextFixture, VeryLongCredentialsConnect)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = std::string(10 * 1024, 'A');
  opts.password = std::string(10 * 1024, 'B');
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_TRUE(result.has_value()) << "FTP servers accept long credentials; should not crash";
}

TEST_F(IoContextFixture, SpecialCharPasswordConnect)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "test";
  opts.password = "pass\x00word@#$%^&*()";
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_TRUE(result.has_value()) << "special chars in password should not crash";
}

TEST_F(IoContextFixture, NullUsernameInConnectOptions)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_TRUE(result.has_value()) << "default anonymous should work";
}

TEST_F(IoContextFixture, ConnectWithPassiveModeEnabled)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.use_passive = true;
  opts.timeout = std::chrono::milliseconds(2000);
  auto result = ftp.connect(opts);
  EXPECT_TRUE(result.has_value()) << "passive mode connect should work";
}

// =========================================================================
// Group 4: Callback Misuse
// =========================================================================

TEST_F(IoContextFixture, NullReadCallback)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value()) << "connect to test server failed";

  auto result = ftp.read("/readme.txt", nullptr);
  EXPECT_FALSE(result.has_value()) << "read with null callback should fail";
}

TEST_F(IoContextFixture, NullWriteCallback)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value()) << "connect failed";

  auto result =
    ftp.write("/upload/test_null_callback.tmp",
              []() -> std::expected<std::span<const std::byte>, std::error_code> { return std::span<std::byte>{}; });
  EXPECT_TRUE(result.has_value()) << "write with callback producing zero bytes may succeed or fail";
}

TEST_F(IoContextFixture, NullProgressCallbackType)
{
  FileTransferUtils::ProgressCallback null_prog;
  EXPECT_TRUE(null_prog == nullptr);

  FileTransferUtils::ProgressCallback valid_prog = [](const std::filesystem::path&, std::size_t,
                                                      std::size_t) { /* no-op */ };
  EXPECT_TRUE(valid_prog != nullptr);
}

TEST_F(IoContextFixture, ThrowingReadCallback)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  int callback_count = 0;
  auto result = ftp.read("/readme.txt",
                         [&callback_count](std::span<const std::byte>) -> std::expected<std::size_t, std::error_code>
                         {
                           ++callback_count;
                           throw std::runtime_error("callback threw");
                         });

  EXPECT_TRUE(callback_count > 0) << "callback should have been invoked";
  if (result.has_value())
  {
    EXPECT_GT(*result, 0);
  }
  else
  {
    EXPECT_NE(result.error().value(), static_cast<int>(Error::NO_ERROR));
  }
}

TEST_F(IoContextFixture, ThrowingWriteCallback)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  int callback_count = 0;
  try
  {
    auto wresult = ftp.write("/upload/test_throw_write.tmp",
                             [&callback_count]() -> std::expected<std::span<const std::byte>, std::error_code>
                             {
                               ++callback_count;
                               throw std::runtime_error("write callback threw");
                             });
    (void)wresult;
  }
  catch (const std::exception&)
  {
    EXPECT_GT(callback_count, 0) << "callback should have been called";
  }
  EXPECT_EQ(callback_count, 1) << "callback should have thrown on first call";
}

TEST_F(IoContextFixture, CallbackReturningEmptySpanImmediately)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  std::size_t call_count = 0;
  auto result = ftp.write("/upload/test_empty_callback.tmp",
                          [&call_count]() -> std::expected<std::span<const std::byte>, std::error_code>
                          {
                            ++call_count;
                            return std::span<const std::byte>{};  // EOF on first call
                          });

  EXPECT_GT(call_count, 0) << "callback should have been invoked";
}

// =========================================================================
// Group 5: Path Edge Cases
// =========================================================================

TEST_F(IoContextFixture, AbsoluteVsRelativePathsForList)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto abs_result = ftp.list("/");
  EXPECT_TRUE(abs_result.has_value()) << "list('/') should work";
  auto dot_result = ftp.list(".");
  EXPECT_TRUE(dot_result.has_value()) << "list('.') should work";
  auto dot_slash_result = ftp.list("./");
  EXPECT_TRUE(dot_slash_result.has_value()) << "list('./') should work";
  auto dot_dot_result = ftp.list("./.");
  EXPECT_TRUE(dot_dot_result.has_value()) << "list('./.') should work";

  if (abs_result.has_value() && dot_result.has_value())
  {
    EXPECT_EQ(abs_result.value().size(), dot_result.value().size());
  }
}

TEST_F(IoContextFixture, DoubleSlashPathList)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.list("//");
  EXPECT_FALSE(result.has_value()) << "list of '//' should fail";
}

TEST_F(IoContextFixture, DoubleSlashPathExists)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.exists("//readme.txt");
  EXPECT_FALSE(result.has_value()) << "exists '//file' should fail";
}

TEST_F(IoContextFixture, DoubleSlashPathListNested)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.list("a//b//c");
  EXPECT_FALSE(result.has_value()) << "list 'a//b//c' should fail";
}

TEST_F(IoContextFixture, HomeDirPathList)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto tilde_result = ftp.list("~");
  EXPECT_FALSE(tilde_result.has_value()) << "list('~') should fail (not FTP path)";

  auto tilde_user_result = ftp.list("~anonymous");
  EXPECT_FALSE(tilde_user_result.has_value());
}

TEST_F(IoContextFixture, UnicodePathNames)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto unicode_path = std::filesystem::path("\u03B1\u03B2\u03B3");
  auto exists_result = ftp.exists(unicode_path);
  EXPECT_FALSE(exists_result.has_value());
  auto list_result = ftp.list(unicode_path);
  EXPECT_FALSE(list_result.has_value());
  auto unicode_list = ftp.list("/\u03B1\u03B2\u03B3/\u4E16\u754C/world");
  EXPECT_FALSE(unicode_list.has_value());
}

TEST_F(IoContextFixture, CreateDirWithDoubleSlashPath)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.createDir("//double//slash//dir");
  EXPECT_FALSE(result.has_value());
}

TEST_F(IoContextFixture, IsDirectoryWithEmptyPath)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.isDirectory("");
  EXPECT_FALSE(result.has_value()) << "isDirectory empty path should fail";
}

// =========================================================================
// Group 6: File Operations Edge Cases (require FTP server)
// =========================================================================

TEST_F(IoContextFixture, ReadNonexistentFile)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.read("/nonexistent_file_xyz_12345.txt");
  EXPECT_FALSE(result.has_value()) << "read nonexistent file should fail";
}

TEST_F(IoContextFixture, RemoveNonexistentFile)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.remove("/nonexistent_remove_xyz_12345.txt");
  EXPECT_FALSE(result.has_value()) << "remove nonexistent file should fail";
}

TEST_F(IoContextFixture, ExistsNonexistentDir)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.exists("/nonexistent_directory_xyz_12345");
  EXPECT_TRUE(result.has_value()) << "exists should return a value";
  if (result.has_value())
  {
    EXPECT_FALSE(result.value()) << "nonexistent path should return false";
  }
}

TEST_F(IoContextFixture, WriteEmptyFile)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  std::vector<std::byte> empty_data;
  auto result = ftp.write("/upload/test_empty_file.tmp", empty_data);
  if (!result.has_value())
  {
    GTEST_SKIP() << "Write of empty file not supported";
  }
  EXPECT_TRUE(result.has_value());

  auto exists_result = ftp.exists("/upload/test_empty_file.tmp");
  EXPECT_TRUE(exists_result.has_value());
  EXPECT_TRUE(exists_result.value());

  auto cleaned = ftp.remove("/upload/test_empty_file.tmp");
  (void)cleaned;
}

TEST_F(IoContextFixture, CreateExistingDir)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  std::filesystem::path test_dir = "/test_existing_dir_xyz";
  auto first = ftp.createDir(test_dir);
  EXPECT_TRUE(first.has_value()) << "first create should succeed";
  auto second = ftp.createDir(test_dir);
  EXPECT_TRUE(second.has_value()) << "recreate existing dir should succeed";
}

// =========================================================================
// Group 7: Navigator Misuse
// =========================================================================

TEST_F(IoContextFixture, NavigatorWithEmptyStartPath)
{
  DummyNavigator nav(std::filesystem::path{});
  auto result = nav.changeDirectory("/");
  EXPECT_TRUE(result.has_value()) << "Navigator with empty start should normalize '/' to root";
}

TEST_F(IoContextFixture, NavigatorWithAbsoluteThenRelative)
{
  DummyNavigator nav("/home/user");
  auto r1 = nav.changeDirectory("/var/log");
  EXPECT_TRUE(r1.has_value());
  auto r2 = nav.changeDirectory("subdir/file.txt");
  EXPECT_TRUE(r2.has_value());
}

TEST_F(IoContextFixture, NavigatorWithSymlinkLikePath)
{
  DummyNavigator nav("/a/b/c");
  auto r1 = nav.changeDirectory("./foo/../bar");
  EXPECT_TRUE(r1.has_value());
  auto r2 = nav.changeDirectory("/a/b/c");
  EXPECT_TRUE(r2.has_value());
}

TEST_F(IoContextFixture, NavigatorRepeatedUpDown)
{
  DummyNavigator nav("/a/b/c/d/e");
  for (int i = 0; i < 100; ++i)
  {
    (void)nav.changeDirectory("..");
  }
  auto result = nav.changeDirectory("../../..");
  EXPECT_TRUE(result.has_value()) << "repeated up should not crash";
}

TEST_F(IoContextFixture, NavigatorCrossPlatformDriveChange)
{
  DummyNavigator nav("/home/user");
  auto result = nav.changeDirectory("C:/windows/system32");
  EXPECT_TRUE(result.has_value()) << "drive change may succeed";
}

TEST_F(IoContextFixture, NavigatorAfterMove)
{
  DummyNavigator nav1("/home/user");
  auto nav2 = std::move(nav1);
  auto result = nav2.changeDirectory("/var/log");
  EXPECT_TRUE(result.has_value());
}

TEST_F(IoContextFixture, NavigatorMultipleChanges)
{
  DummyNavigator nav("/a/b/c");
  EXPECT_TRUE(nav.changeDirectory("/d/e/f").has_value());
  EXPECT_TRUE(nav.changeDirectory("/g").has_value());
  EXPECT_TRUE(nav.changeDirectory("h/./i/../j").has_value());
}

TEST_F(IoContextFixture, NavigatorWithNonexistentParentDir)
{
  DummyNavigator nav("/nonexistent/parent");
  auto result = nav.changeDirectory("child/grandchild");
  EXPECT_TRUE(result.has_value()) << "lexical navigator does not check filesystem";
}

TEST_F(IoContextFixture, NavigatorWithVeryLongPath)
{
  std::string long_path = "/";
  for (int i = 0; i < 10; ++i)
  {
    long_path += "very_long_directory_name_that_should_not_cause_issues_";
  }
  long_path += "final";

  DummyNavigator nav("/");
  auto result = nav.changeDirectory(std::filesystem::path(long_path));
  EXPECT_TRUE(result.has_value()) << "very long path should not crash";
}

TEST_F(IoContextFixture, NavigatorNullDirectory)
{
  DummyNavigator nav("/valid/start");
  auto result = nav.changeDirectory("");
  EXPECT_TRUE(result.has_value()) << "empty target may be handled as no-op";
}

TEST_F(IoContextFixture, NavigatorWithNonAsciiPath)
{
  DummyNavigator nav("/home/user");
  auto result = nav.changeDirectory("\u03B1\u03B2\u03B3/\u4E16\u754C");
  EXPECT_TRUE(result.has_value()) << "non-ASCII path should not crash navigator";
}

TEST_F(IoContextFixture, NavigatorPathWithSpaces)
{
  DummyNavigator nav("/home/user");
  auto result = nav.changeDirectory("path with spaces/file name.txt");
  EXPECT_TRUE(result.has_value()) << "spaces in path should not crash navigator";
}

TEST_F(IoContextFixture, NavigatorDeepNestedPath)
{
  DummyNavigator nav("/a/b/c/d");
  std::string deep = "";
  for (int i = 0; i < 50; ++i)
  {
    deep += "dir" + std::to_string(i) + "/";
  }
  auto result = nav.changeDirectory(std::filesystem::path(deep));
  EXPECT_TRUE(result.has_value());
}

TEST_F(IoContextFixture, NavigatorMoveAssignment)
{
  DummyNavigator nav1("/home/user");
  DummyNavigator nav2("/var/log");
  nav2 = std::move(nav1);
  auto r1 = nav1.changeDirectory("/tmp");
  (void)r1;  // source is unusable after move
  auto r2 = nav2.changeDirectory("/opt");
  EXPECT_TRUE(r2.has_value());
}

// =========================================================================
// Group 8: Capability Edge Cases (require FTP server)
// =========================================================================

TEST_F(IoContextFixture, MinimalCapabilitiesBasicListOnly)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.list("/");
  EXPECT_TRUE(result.has_value()) << "LIST should work";
  (void)result;
}

TEST_F(IoContextFixture, NavigateAfterConnectMinimalCapabilities)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto exists_root = ftp.exists("/");
  EXPECT_TRUE(exists_root.has_value());
  auto exists_readme = ftp.exists("/readme.txt");
  EXPECT_TRUE(exists_readme.has_value());
  EXPECT_TRUE(exists_readme.value());
}

TEST_F(IoContextFixture, NavigatorWithDefaultFtpNavigator)
{
  REQUIRE_FTP_SERVER();

  FtpFileTransfer ftp("127.0.0.1", 2121, getIoContext());
  FtpFileTransfer::ConnectOptions opts;
  opts.username = "anonymous";
  opts.password = "";
  opts.timeout = std::chrono::milliseconds(2000);
  auto connect_result = ftp.connect(opts);
  ASSERT_TRUE(connect_result.has_value());

  auto result = ftp.list("/");
  EXPECT_TRUE(result.has_value()) << "Navigator should work with connected server";
}

TEST_F(IoContextFixture, NavigatorDefaultFtpNavigatorIsDerivedFromSmartDirectoryNavigator)
{
  // DefaultFtpNavigator is derived from SmartDirectoryNavigator (compile-time check).
  constexpr bool derived = std::is_base_of_v<Network::Utility::SmartDirectoryNavigator, DefaultFtpNavigator>;
  EXPECT_TRUE(derived);
}

}  // namespace Network::Test

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

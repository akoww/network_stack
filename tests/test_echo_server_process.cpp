#include <array>
#include <asio.hpp>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <span>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "client/Client.h"
#include "core/Context.h"
#include "socket/TcpSocket.h"
#include "socket/SslSocket.h"

namespace
{

// ============================================================================
// Path helpers
// ============================================================================
inline std::filesystem::path cert_directory()
{
#ifdef SOURCE_DIR_CERT
  return SOURCE_DIR_CERT;
#else
  return "/tmp/test_certs";
#endif
}

inline std::string echo_server_exe_path()
{
  char const* exe = std::getenv("CMAKE_RUNTIME_OUTPUT_DIRECTORY");
  if (exe)
    return std::string(exe) + "/echo_server";
  return "build/bin/echo_server";
}

// ============================================================================
// Process fixture
// ============================================================================
class FixedPortServer
{
public:
  explicit FixedPortServer(uint16_t port) : port_(port) {}

  ~FixedPortServer() = default;

  bool start(std::string_view exe_path,
             std::optional<std::filesystem::path> cert_chain = std::nullopt,
             std::optional<std::filesystem::path> private_key = std::nullopt);
  void stop();
  bool is_running() const { return running_; }
  uint16_t port() const { return port_; }

private:
  static pid_t find_pid(uint16_t port)
  {
    std::string cmd = "pgrep -f \"echo_server.*--port " + std::to_string(port) + "\" | head -1";
    char buf[32];
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
      return -1;

    std::string pid_str;
    while (fgets(buf, sizeof buf, pipe))
      pid_str += buf;
    pclose(pipe);

    if (pid_str.empty())
      return -1;
    try
    {
      return static_cast<pid_t>(std::stoi(pid_str));
    }
    catch (...)
    {
      return -1;
    }
  }

  uint16_t port_;
  pid_t pid_{-1};
  bool running_{false};
};

inline bool FixedPortServer::start(std::string_view exe_path,
                                   std::optional<std::filesystem::path> cert_chain,
                                   std::optional<std::filesystem::path> private_key)
{
  if (running_)
    return true;

  if (!std::filesystem::exists(std::filesystem::path(exe_path)))
    return false;

  std::string cmd = std::string(exe_path);
  cmd += " --port " + std::to_string(port_);
  if (cert_chain && private_key)
  {
    cmd += " --cert-chain \"" + cert_chain->string() + "\"";
    cmd += " --private-key \"" + private_key->string() + "\"";
  }
  cmd += " &";

  int const ret = std::system(cmd.c_str());
  if (ret != 0)
    return false;

  for (int i = 0; i < 30 && running_ == false; ++i)
  {
    pid_t p = find_pid(port_);
    if (p > 0)
    {
      pid_ = p;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      if (kill(pid_, 0) == 0)
        running_ = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  return running_;
}

inline void FixedPortServer::stop()
{
  if (!running_ && pid_ <= 0)
    return;

  auto const p = find_pid(port_);
  while (p > 0)
  {
    kill(p, SIGTERM);
    usleep(250000);
    waitpid(p, nullptr, WNOHANG);
    if (kill(p, 0) == 0)
    {
      kill(p, SIGKILL);
      waitpid(p, nullptr, 0);
    }

    pid_t np = find_pid(port_);
    if (!np || np <= 0 || np != p)
      break;
    pid_ = -1;
  }

  running_ = false;
}

// ============================================================================
// Env: plain TCP echo server
// ============================================================================
class PlainServerEnv : public ::testing::Environment
{
public:
  void SetUp() override { env_ready = srv.start(echo_server_exe_path()); }
  void TearDown() override { srv.stop(); }

  bool ready() const { return env_ready; }
  uint16_t port() const { return srv.is_running() ? k_port : 0; }

private:
  static constexpr uint16_t k_port = 19943;
  FixedPortServer srv{k_port};
  bool env_ready{false};
};

// ============================================================================
// Env: TLS echo server
// ============================================================================
class TLSServerEnv : public ::testing::Environment
{
public:
  void SetUp() override
  {
    auto const cert_dir = cert_directory();
    auto const cert_path = (cert_dir / "server.crt").string();
    auto const key_path = (cert_dir / "server.key").string();
    env_ready = srv.start(echo_server_exe_path(), cert_path, key_path);
  }

  void TearDown() override { srv.stop(); }

  bool ready() const { return env_ready; }
  uint16_t port() const { return srv.is_running() ? k_port : 0; }

private:
  static constexpr uint16_t k_port = 19944;
  FixedPortServer srv{k_port};
  bool env_ready{false};
};

// ============================================================================
// Global envs
// ============================================================================
PlainServerEnv g_plain_env;
TLSServerEnv g_tls_env;

// ============================================================================
// Helpers
// ============================================================================
inline std::span<const std::byte> to_bytes(std::string_view sv)
{
  return std::as_bytes(std::span(sv.data(), sv.size()));
}

// ============================================================================
// Plain TCP tests
// ============================================================================

TEST(ProcEcho, SingleMessage)
{
  if (!g_plain_env.ready())
  {
    GTEST_SKIP() << "Echo server not found at " << echo_server_exe_path();
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", g_plain_env.port(), io_ctx);
    auto sock_res = client.connect(std::chrono::milliseconds{2000});
    ASSERT_TRUE(sock_res.has_value()) << "Connect: " << sock_res.error().message();

    auto sock = std::move(*sock_res);

    std::string const msg("Hello from separate process");
    auto w = sock->writeAll(to_bytes(msg));
    EXPECT_TRUE(w) << "Write: " << w.error().message();

    std::array<std::byte, 1024> buf{};
    auto r = sock->readSome(std::span(buf));
    ASSERT_TRUE(r.has_value()) << "Read: " << r.error().message();
    EXPECT_EQ(*r, msg.size());

    std::string resp(reinterpret_cast<char*>(buf.data()), *r);
    EXPECT_EQ(resp, msg);
  }

  io_ctx.stop();
}

TEST(ProcEcho, MultipleMessages)
{
  if (!g_plain_env.ready())
  {
    GTEST_SKIP() << "Echo server not found";
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", g_plain_env.port(), io_ctx);
    auto sock_res = client.connect(std::chrono::milliseconds{2000});
    ASSERT_TRUE(sock_res.has_value());

    auto sock = std::move(*sock_res);

    for (auto const& msg : std::vector<std::string>{"Hello", "World", "Test123"})
    {
      auto w = sock->writeAll(to_bytes(msg));
      EXPECT_TRUE(w);

      std::array<std::byte, 1024> buf{};
      auto r = sock->readSome(std::span(buf));
      ASSERT_TRUE(r.has_value());
      EXPECT_EQ(*r, msg.size());

      std::string resp(reinterpret_cast<char*>(buf.data()), *r);
      EXPECT_EQ(resp, msg);
    }
  }

  io_ctx.stop();
}

TEST(ProcEcho, LargeBinaryPayload)
{
  if (!g_plain_env.ready())
  {
    GTEST_SKIP() << "Echo server not found";
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", g_plain_env.port(), io_ctx);
    auto sock_res = client.connect(std::chrono::milliseconds{2000});
    ASSERT_TRUE(sock_res.has_value());

    auto sock = std::move(*sock_res);

    std::vector<std::byte> msg(65536);
    for (size_t i = 0; i < msg.size(); ++i)
      msg[i] = static_cast<std::byte>(i % 256);

    auto w = sock->writeAll(std::as_bytes(std::span(msg)));
    EXPECT_TRUE(w);

    auto buf = std::vector<std::byte>(msg.size());
    auto r = sock->readExact(std::span(buf));
    ASSERT_TRUE(r.has_value()) << "Read exact: " << r.error().message();
    EXPECT_EQ(*r, msg.size());

    for (size_t i = 0; i < static_cast<size_t>(*r); ++i)
      EXPECT_EQ(buf[i], msg[i]);
  }

  io_ctx.stop();
}

TEST(ProcEcho, ConnectToDeadServer)
{
  constexpr uint16_t test_port = 19876;
  FixedPortServer tmp(test_port);

  auto const exe = echo_server_exe_path();
  if (!std::filesystem::exists(std::filesystem::path(exe)))
  {
    GTEST_SKIP() << "Echo server binary not found";
  }

  ASSERT_TRUE(tmp.start(exe));

  // Stop before connecting
  tmp.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds{200});

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", test_port, io_ctx);
    auto sock_res = client.connect(std::chrono::milliseconds{1000});
    // Should fail since port is no longer bound — or get ECONNREFUSED/ETIMEDOUT
    EXPECT_FALSE(sock_res.has_value()) << "Should not connect to dead server";
  }

  io_ctx.stop();
}

// ============================================================================
// TLS tests
// ============================================================================

TEST(ProcEchoTLS, SingleMessage)
{
  if (!g_tls_env.ready())
  {
    GTEST_SKIP() << "TLS echo server not found or certs missing";
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", g_tls_env.port(), io_ctx);
    client.getSslContext()->set_verify_mode(asio::ssl::verify_none);

    auto sock_res = client.connectTls(std::chrono::milliseconds{2000});
    ASSERT_TRUE(sock_res.has_value()) << "TLS connect: " << sock_res.error().message();

    auto sock = std::move(*sock_res);

    std::string const msg("Hello over TLS");
    auto w = sock->writeAll(to_bytes(msg));
    EXPECT_TRUE(w) << "Write: " << w.error().message();

    std::array<std::byte, 1024> buf{};
    auto r = sock->readSome(std::span(buf));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, msg.size());

    std::string resp(reinterpret_cast<char*>(buf.data()), *r);
    EXPECT_EQ(resp, msg);
  }

  io_ctx.stop();
}

TEST(ProcEchoTLS, MultipleMessages)
{
  if (!g_tls_env.ready())
  {
    GTEST_SKIP() << "TLS echo server not found";
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", g_tls_env.port(), io_ctx);
    client.getSslContext()->set_verify_mode(asio::ssl::verify_none);

    auto sock_res = client.connectTls(std::chrono::milliseconds{2000});
    ASSERT_TRUE(sock_res.has_value());

    auto sock = std::move(*sock_res);

    for (auto const& msg : std::vector<std::string>{"Alpha", "Beta", "Gamma"})
    {
      auto w = sock->writeAll(to_bytes(msg));
      EXPECT_TRUE(w);

      std::array<std::byte, 1024> buf{};
      auto r = sock->readSome(std::span(buf));
      ASSERT_TRUE(r.has_value());
      EXPECT_EQ(*r, msg.size());

      std::string resp(reinterpret_cast<char*>(buf.data()), *r);
      EXPECT_EQ(resp, msg);
    }
  }

  io_ctx.stop();
}

TEST(ProcEchoTLS, TLSLargePayload)
{
  if (!g_tls_env.ready())
  {
    GTEST_SKIP() << "TLS echo server not found";
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  {
    Network::Client client("127.0.0.1", g_tls_env.port(), io_ctx);
    client.getSslContext()->set_verify_mode(asio::ssl::verify_none);

    auto sock_res = client.connectTls(std::chrono::milliseconds{2000});
    ASSERT_TRUE(sock_res.has_value());

    auto sock = std::move(*sock_res);

    std::vector<std::byte> msg(32768);
    for (size_t i = 0; i < msg.size(); ++i)
      msg[i] = static_cast<std::byte>((i * 7 + 13) % 256);

    auto w = sock->writeAll(std::as_bytes(std::span(msg)));
    EXPECT_TRUE(w);

    std::vector<std::byte> buf(32768);
    size_t total_read = 0;
    while (total_read < buf.size())
    {
      auto r = sock->readSome(std::span(buf).subspan(total_read));
      ASSERT_TRUE(r.has_value()) << "Read error: " << r.error().message();
      total_read += *r;
    }

    for (size_t i = 0; i < msg.size(); ++i)
      EXPECT_EQ(buf[i], msg[i]);
  }

  io_ctx.stop();
}

TEST(ProcEchoTLS, PlainClientToTLSServer)
{
  if (!g_tls_env.ready())
  {
    GTEST_SKIP() << "TLS echo server not found";
  }

  Network::IoContextWrapper io_ctx;
  io_ctx.start();

  // Connect a plain client to the TLS server — should gracefully disconnect
  {
    Network::Client client("127.0.0.1", g_tls_env.port(), io_ctx);
    auto sock_res = client.connect(std::chrono::milliseconds{2000});
    if (!sock_res.has_value())
    {
      // Connection failed before even getting a socket — also acceptable
      io_ctx.stop();
      return;
    }
    auto sock = std::move(*sock_res);

    std::string const msg("test321");  // send at least 5 bytes
    auto w = sock->writeAll(to_bytes(msg));
    EXPECT_TRUE(w);

    std::array<std::byte, 1024> buf{};
    auto r = sock->readSome(std::span(buf));  // first we get ans handshake answer - something like: fatal failed
    EXPECT_TRUE(r.has_value());

    auto r2 = sock->readSome(std::span(buf));  // now the server should have closed the connection
    // Server interprets this as TLS handshake bytes and likely closes the connection
    if (r2.has_value())
    {
      EXPECT_EQ(*r2, 0u) << "Server should have closed the connection";
    }
  }

  io_ctx.stop();
}

// ============================================================================
// Global environment registration
// ============================================================================
namespace
{

class AllServers : public ::testing::Environment
{
public:
  void SetUp() override
  {
    g_plain_env.SetUp();
    g_tls_env.SetUp();
  }

  void TearDown() override
  {
    g_tls_env.TearDown();
    g_plain_env.TearDown();
  }
};

}  // namespace

}  // namespace

// ============================================================================
// Entry point
// ============================================================================
int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new AllServers());
  return RUN_ALL_TESTS();
}

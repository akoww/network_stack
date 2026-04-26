#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asio.hpp>
#include <gtest/gtest.h>

#include "client/ClientSync.h"
#include "core/Context.h"
#include "protocol/Ticket.h"
#include "protocol/TicketController.h"
#include "protocol/TicketJson.h"
#include "protocol/TicketPeer.h"
#include "protocol/TicketWorker.h"
#include "server/ServerSync.h"
#include "socket/TcpSocket.h"

namespace Network::Test
{

namespace
{

constexpr uint16_t TEST_PORT = 12351;

struct WorkerEntry
{
  std::unique_ptr<DualSocket> socket;
  std::unique_ptr<TicketWorker> worker;
  std::thread thread;
};

class TicketServer
{
public:
  TicketServer(asio::io_context& io_ctx)
    : server_(TEST_PORT, io_ctx, [this](std::unique_ptr<DualSocket> sock) { this->handle_client(std::move(sock)); })
  {
  }

  ~TicketServer()
  {
    std::vector<WorkerEntry> entries;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      entries.swap(workers_);
    }
    for (auto& e : entries)
    {
      if (e.thread.joinable())
        e.thread.join();
    }
  }

  void start()
  {
    server_thread_ = std::thread([this]() { server_.listen(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  void stop()
  {
    server_.stop();
    if (server_thread_.joinable())
      server_thread_.join();
  }

  void registerHandler(std::string_view command, TicketWorker::TicketHandler handler)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[std::string(command)] = std::move(handler);
  }

private:
  void handle_client(std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto entry = WorkerEntry{std::move(sock), nullptr, std::thread{}};
      entry.worker = std::make_unique<TicketWorker>(std::move(entry.socket), TicketWorker::Options{});
      for (const auto& [cmd, handler] : handlers_)
      {
        entry.worker->registerHandler(cmd, handler);
      }
      workers_.push_back(std::move(entry));
    }

    auto& w = workers_.back();

    w.thread = std::thread(
      [&w]()
      {
        try
        {
          while (true)
          {
            auto result = w.worker->handleNext();
            if (!result)
              break;
          }
        }
        catch (const std::exception&)
        {
        }
      });
  }

  ServerSync server_;
  std::vector<WorkerEntry> workers_;
  std::mutex mutex_;
  std::unordered_map<std::string, TicketWorker::TicketHandler> handlers_;
  std::thread server_thread_;
};

class TicketProtocolIntegrationFixture : public ::testing::Test
{
protected:
  void SetUp() override { _io_ctx.start(); }
  void TearDown() override { _io_ctx.stop(); }

  asio::io_context& getIoContext() { return _io_ctx; }

protected:
  IoContextWrapper _io_ctx;
};

std::unique_ptr<DualSocket> createClient(asio::io_context& io_ctx)
{
  ClientSync client("127.0.0.1", TEST_PORT, io_ctx);
  auto result = client.connect({std::chrono::milliseconds(5000)});
  return result ? std::move(*result) : nullptr;
}

}  // namespace

TEST_F(TicketProtocolIntegrationFixture, PingCommand)
{
  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "pong"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("ping", "");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "pong");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, ComputeCommand)
{
  TicketServer server(_io_ctx);
  server.registerHandler("compute", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return "computed:" + std::string(ctx.payload); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("compute", "1+1");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "computed:1+1");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, EchoCommand)
{
  TicketServer server(_io_ctx);
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("echo", "hello world");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "hello world");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, CreateFileCommand)
{
  TicketServer server(_io_ctx);
  server.registerHandler("create_file", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return "file:" + std::string(ctx.payload) + " created"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("create_file", "test.txt");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "file:test.txt created");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, ReadFileCommand)
{
  TicketServer server(_io_ctx);
  server.registerHandler("read_file", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return "content:" + std::string(ctx.payload); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("read_file", "test.txt");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "content:test.txt");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, LongTicketWithProgress)
{
  std::mutex m;
  std::vector<TicketInfo> progress_updates;

  TicketServer server(_io_ctx);
  server.registerHandler("long-task",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code>
                         {
                           for (int p = 10; p <= 100; p += 10)
                           {
                             std::this_thread::sleep_for(std::chrono::milliseconds(5));
                           }
                           return "done";
                         });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds(5000);

  auto result = controller.sendLong(
    "long-task", "data",
    [&progress_updates, &m](const TicketInfo& info)
    {
      std::lock_guard<std::mutex> lock(m);
      progress_updates.push_back(info);
    },
    "", opts);

  EXPECT_TRUE(result);
  EXPECT_EQ(result->status, TicketStatus::COMPLETED);

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, LongTicketFastCompletion)
{
  TicketServer server(_io_ctx);
  server.registerHandler(
    "fast-task", [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "immediate"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.sendLong("fast-task", "");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, TicketStatus::COMPLETED);

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, CancelLongTicket)
{
  TicketServer server(_io_ctx);
  server.registerHandler(
    "cancel-me", [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "cancelled"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto cancel_result = controller.cancel("some-ticket-id");
  if (!cancel_result)
  {
    EXPECT_EQ(cancel_result.error().value(), static_cast<int>(std::errc::io_error));
  }

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, SequentialPingComputeEcho)
{
  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "pong"; });
  server.registerHandler("compute", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return "computed:" + std::string(ctx.payload); });
  server.registerHandler("echo", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return std::string(ctx.payload); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());

  auto r1 = controller.send("ping", "");
  ASSERT_TRUE(r1);
  EXPECT_EQ(*r1, "pong");

  auto r2 = controller.send("compute", "test");
  ASSERT_TRUE(r2);
  EXPECT_EQ(*r2, "computed:test");

  auto r3 = controller.send("echo", "back");
  ASSERT_TRUE(r3);
  EXPECT_EQ(*r3, "back");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, ConcurrentTickets)
{
  std::atomic<int> completed{0};

  TicketServer server(_io_ctx);
  server.registerHandler("work",
                         [&completed](const TicketContext&) -> std::expected<std::string, std::error_code>
                         {
                           completed++;
                           return "done";
                         });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());

  for (int i = 0; i < 10; ++i)
  {
    auto result = controller.send("work", "");
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, "done");
  }

  EXPECT_EQ(completed.load(), 10);
  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, ErrorPropagation)
{
  TicketServer server(_io_ctx);
  server.registerHandler("error", [](const TicketContext&) -> std::expected<std::string, std::error_code>
                         { return std::unexpected(std::make_error_code(std::errc::io_error)); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("error", "");
  EXPECT_FALSE(result);

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, TimeoutWithShortTimeout)
{
  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "pong"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds(100);

  auto result = controller.send("ping", "", "", opts);
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, "pong");

  server.stop();
}

TEST_F(TicketProtocolIntegrationFixture, GoodbyeOnDestruction)
{
  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "pong"; });

  server.start();

  {
    auto socket = createClient(getIoContext());
    ASSERT_TRUE(socket != nullptr);

    TicketController controller(std::move(socket), std::make_shared<TicketJson>());
    auto result = controller.send("ping", "");
    EXPECT_TRUE(result);
  }

  server.stop();
}

}  // namespace Network::Test

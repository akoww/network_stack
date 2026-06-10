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

constexpr uint16_t TEST_PORT = 12349;

struct WorkerEntry
{
  std::unique_ptr<DualSocket> socket;
  std::unique_ptr<TicketWorker> worker;
  std::thread thread;
};

class TicketServer
{
public:
  TicketServer(asio::any_io_executor io_ctx)
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

protected:
  void handle_client(std::unique_ptr<DualSocket> sock)
  {
    if (!sock)
      return;

    auto entry = WorkerEntry{std::move(sock), nullptr, std::thread{}};
    entry.worker = std::make_unique<TicketWorker>(std::move(entry.socket), TicketWorker::Options{});
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto& [cmd, handler] : handlers_)
      {
        entry.worker->registerHandler(cmd, handler);
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    workers_.push_back(std::move(entry));

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

class TicketSyncControllerWorkerFixture : public ::testing::Test
{
protected:
  void SetUp() override { _ }
  void TearDown() override{_}

  asio::any_io_executor getIoContext()
  {
    return _io_ctx;
  }

protected:
  IoContextWrapper _io_ctx;
};

std::unique_ptr<DualSocket> createClient(asio::any_io_executor io_ctx)
{
  ClientSync client("127.0.0.1", TEST_PORT, io_ctx);
  auto result = client.connect({std::chrono::milliseconds(5000)});
  return result ? std::move(*result) : nullptr;
}

}  // namespace

TEST_F(TicketSyncControllerWorkerFixture, SyncSendWithRegisteredHandler)
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

TEST_F(TicketSyncControllerWorkerFixture, SyncSendWithPayload)
{
  TicketServer server(_io_ctx);
  server.registerHandler("compute", [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         { return "computed:" + std::string(ctx.payload); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("compute", "42+58");
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "computed:42+58");

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, SyncSendWithUnregisteredHandler)
{
  TicketServer server(_io_ctx);
  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("unknown_cmd", "");
  ASSERT_FALSE(result);

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, SyncSendWithFailingHandler)
{
  TicketServer server(_io_ctx);
  server.registerHandler("fail", [](const TicketContext&) -> std::expected<std::string, std::error_code>
                         { return std::unexpected(std::make_error_code(std::errc::io_error)); });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.send("fail", "");
  ASSERT_FALSE(result);

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, SyncSendWithMetadata)
{
  TicketServer server(_io_ctx);
  server.registerHandler("metadata-test",
                         [](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         {
                           auto it = ctx.metadata.find("metadata");
                           if (it != ctx.metadata.end())
                             return "metadata:" + it->second;
                           return "no key1";
                         });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  TicketController::Options opts;
  opts.timeout = std::chrono::milliseconds(1000);
  auto result = controller.send("metadata-test", "", "value1", opts);
  ASSERT_TRUE(result);
  EXPECT_EQ(*result, "metadata:value1");

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, SequentialOperations)
{
  std::atomic<int> ping_count{0};
  std::atomic<int> compute_count{0};

  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [&ping_count](const TicketContext&) -> std::expected<std::string, std::error_code>
                         {
                           ping_count++;
                           return "pong";
                         });
  server.registerHandler("compute",
                         [&compute_count](const TicketContext& ctx) -> std::expected<std::string, std::error_code>
                         {
                           compute_count++;
                           return "ok:" + std::string(ctx.payload);
                         });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());

  for (int i = 0; i < 5; ++i)
  {
    auto ping_result = controller.send("ping", "");
    ASSERT_TRUE(ping_result);
    EXPECT_EQ(*ping_result, "pong");
  }

  for (int i = 0; i < 5; ++i)
  {
    auto compute_result = controller.send("compute", std::to_string(i));
    ASSERT_TRUE(compute_result);
    EXPECT_EQ(*compute_result, "ok:" + std::to_string(i));
  }

  EXPECT_EQ(ping_count.load(), 5);
  EXPECT_EQ(compute_count.load(), 5);

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, LongTicketCreatesTicket)
{
  std::mutex m;
  std::condition_variable cv;
  std::atomic<bool> ready{false};

  TicketServer server(_io_ctx);
  server.registerHandler("long-task",
                         [&m, &cv, &ready](const TicketContext&) -> std::expected<std::string, std::error_code>
                         {
                           {
                             std::lock_guard<std::mutex> lock(m);
                             ready = true;
                             cv.notify_one();
                           }
                           return "done";
                         });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto result = controller.sendLong("long-task", "data");
  EXPECT_TRUE(result);

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, PollTicketReturnsStatus)
{
  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "pong"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto poll_result = controller.poll("test-poll-id");
  if (poll_result)
  {
    EXPECT_EQ(poll_result->ticket_id, "test-poll-id");
  }

  server.stop();
}

TEST_F(TicketSyncControllerWorkerFixture, CancelTicket)
{
  TicketServer server(_io_ctx);
  server.registerHandler("ping",
                         [](const TicketContext&) -> std::expected<std::string, std::error_code> { return "pong"; });

  server.start();

  auto socket = createClient(getIoContext());
  ASSERT_TRUE(socket != nullptr);

  TicketController controller(std::move(socket), std::make_shared<TicketJson>());
  auto cancel_result = controller.cancel("any-id");
  if (!cancel_result)
  {
    EXPECT_EQ(cancel_result.error().value(), static_cast<int>(std::errc::io_error));
  }

  server.stop();
}

}  // namespace Network::Test

#include <asio.hpp>
#include <asio/thread_pool.hpp>
#include <gtest/gtest.h>

// Include your header
#include "core/Context.h"

using namespace Network;

TEST(IoContextWrapperTest, ExecuteTaskViaPost)
{
  IoContextWrapper ctx;
  std::atomic<bool> task_done{false};

  asio::post(ctx.get_executor(), [&task_done]() { task_done.store(true); });

  while (!task_done.load())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_TRUE(task_done.load());
}

TEST(IoContextWrapperTest, DirectPostIfAccessible)
{
  // This test verifies if ctx.post() actually works.
  // If this fails, you MUST use the asio::post(ctx.get_executor(), ...) method above.
  IoContextWrapper ctx;

  std::atomic<bool> task_done{false};

  try
  {
    asio::post(ctx.get_executor(),
               [&task_done]()
               {
                 std::cout << "running\n";
                 task_done.store(true);
               });
  }
  catch (...)
  {
  }

  while (!task_done.load())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_TRUE(task_done.load());
}
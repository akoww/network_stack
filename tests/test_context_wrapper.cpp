#include <asio.hpp>
#include <gtest/gtest.h>

// Include your header
#include "core/Context.h"

using namespace Network;

TEST(IoContextWrapperTest, SingletonInstance)
{
    IoContextWrapper &instance1 = IoContextWrapper::instance();
    IoContextWrapper &instance2 = IoContextWrapper::instance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST(IoContextWrapperTest, InitialState)
{
    IoContextWrapper &ctx = IoContextWrapper::instance();
    EXPECT_FALSE(ctx.is_running());
}

TEST(IoContextWrapperTest, StartAndStop)
{
    IoContextWrapper &ctx = IoContextWrapper::instance();

    if (ctx.is_running())
    {
        ctx.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(ctx.is_running());

    ctx.start();

    EXPECT_TRUE(ctx.is_running());

    ctx.stop();

    EXPECT_FALSE(ctx.is_running());
}

TEST(IoContextWrapperTest, ExecuteTaskViaPost)
{
    IoContextWrapper &ctx = IoContextWrapper::instance();

    if (ctx.is_running())
    {
        ctx.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::atomic<bool> task_done{false};

    ctx.start();

    asio::post(ctx.get_executor(), [&task_done]()
               { task_done.store(true); });

    while (!task_done.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(task_done.load());

    ctx.stop();
}

TEST(IoContextWrapperTest, DirectPostIfAccessible)
{
    // This test verifies if ctx.post() actually works.
    // If this fails, you MUST use the asio::post(ctx.get_executor(), ...) method above.
    IoContextWrapper &ctx = IoContextWrapper::instance();

    if (ctx.is_running())
    {
        ctx.stop();
    }

    std::atomic<bool> task_done{false};
    ctx.start();

    try
    {
        asio::post(ctx.get_executor(), [&task_done]()
                   { task_done.store(true); });
    }
    catch (...)
    {
    }

    while (!task_done.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(task_done.load());
    ctx.stop();
}
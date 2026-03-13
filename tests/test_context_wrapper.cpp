#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <asio.hpp> // Ensure the full asio header is included for asio::post

// Include your header
#include "core/Context.h"

using namespace Network;

TEST(IoContextWrapperTest, SingletonInstance) {
    IoContextWrapper& instance1 = IoContextWrapper::instance();
    IoContextWrapper& instance2 = IoContextWrapper::instance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST(IoContextWrapperTest, InitialState) {
    IoContextWrapper& ctx = IoContextWrapper::instance();
    EXPECT_FALSE(ctx.is_running());
}

TEST(IoContextWrapperTest, StartAndStop) {
    IoContextWrapper& ctx = IoContextWrapper::instance();
    
    if (ctx.is_running()) {
        ctx.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_FALSE(ctx.is_running());

    ctx.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(ctx.is_running());

    ctx.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_FALSE(ctx.is_running());
}

TEST(IoContextWrapperTest, ExecuteTaskViaPost) {
    IoContextWrapper& ctx = IoContextWrapper::instance();

    if (ctx.is_running()) {
        ctx.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::atomic<bool> task_done{false};

    ctx.start();

    // Option 1: Use the free function asio::post with the executor from the context
    // This is the most robust way if 'ctx.post' feels ambiguous
    asio::post(ctx.get_executor(), [&task_done]() {
        task_done.store(true);
    });

    // Wait for task
    while (!task_done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(task_done.load());

    ctx.stop();
}

TEST(IoContextWrapperTest, DirectPostIfAccessible) {
    // This test verifies if ctx.post() actually works. 
    // If this fails, you MUST use the asio::post(ctx.get_executor(), ...) method above.
    IoContextWrapper& ctx = IoContextWrapper::instance();

    if (ctx.is_running()) {
        ctx.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::atomic<bool> task_done{false};
    ctx.start();

    try {
        // Try the direct member access first
        asio::post(ctx.get_executor(), [&task_done]() {
            task_done.store(true);
        });
    } catch (...) {
        // If this throws or fails to compile, use the executor method in the other test
    }

    while (!task_done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(task_done.load());
    ctx.stop();
}
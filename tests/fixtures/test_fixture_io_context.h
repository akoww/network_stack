#pragma once

#include <gtest/gtest.h>

#include "core/Context.h"

namespace Network::Test
{

class IoContextFixture : public ::testing::Test
{
protected:
  IoContextFixture() = default;
  ~IoContextFixture() override = default;

  void SetUp() override { _io_ctx.start(); }

  void TearDown() override { _io_ctx.stop(); }

  Network::IoContextWrapper& getIoContext() { return _io_ctx; }

private:
  Network::IoContextWrapper _io_ctx;
};

}  // namespace Network::Test

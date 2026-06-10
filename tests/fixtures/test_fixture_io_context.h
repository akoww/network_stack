#pragma once

#include <gtest/gtest.h>

#include "core/Context.h"

namespace Network::Test
{

class IoContextFixture : public ::testing::Test
{
public:
  IoContextFixture() {};
  ~IoContextFixture() override = default;

  void SetUp() override {}

  void TearDown() override {}

  Network::IoContextWrapper& getIoContext() { return _io; }

protected:
  Network::IoContextWrapper _io;
};

}  // namespace Network::Test

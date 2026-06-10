#include "core/Context.h"
#include <spdlog/spdlog.h>

#include <iostream>

namespace Network
{

IoContextWrapper::~IoContextWrapper()
{
}

IoContextWrapper::IoContextWrapper(unsigned int thread_count) : pool_(thread_count)
{
}

}  // namespace Network
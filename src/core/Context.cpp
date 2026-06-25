#include "core/Context.h"
#include "core/details/ContextDetail.h"
#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

namespace Network
{

IoContextWrapper::IoContextWrapper(unsigned int thread_count)
  : _p(std::make_shared<IoContextWrapper::Private>(thread_count))
{
}

void IoContextWrapper::shutdown() noexcept
{
  if (!_p)
    return;
  std::call_once(_p->shutdown_once_,
                 [p = _p]
                 {
                   p->pool_.stop();  // reject new work, let running tasks finish
                   p->pool_.join();  // wait for threads
                 });
}

}  // namespace Network
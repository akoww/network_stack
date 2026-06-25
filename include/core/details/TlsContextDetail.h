#pragma once
#include "../TlsContextWrapper.h"
#include "socket/TlsOptions.h"

#include <asio/ssl/context.hpp>
#include <openssl/ssl.h>

namespace Network
{

struct TlsContextWrapper::Private
{
  explicit Private(const TlsOptions& cfg, const TlsServerOptions* server_opts);

  std::shared_ptr<asio::ssl::context> context_;
};

namespace detail
{

struct TlsContextAccess
{
  static std::shared_ptr<asio::ssl::context> getTlsContext(TlsContextWrapper wrapper) { return wrapper._p->context_; }
};

inline std::shared_ptr<asio::ssl::context> getTlsContext(TlsContextWrapper wrapper)
{
  return TlsContextAccess::getTlsContext(wrapper);
}

}  // namespace detail
}  // namespace Network

#pragma once

#include <expected>
#include <memory>

namespace Network
{

struct TlsOptions;
struct TlsServerOptions;

namespace detail
{
struct TlsContextAccess;
}

class TlsContextWrapper final
{
public:
  explicit TlsContextWrapper(const TlsOptions& cfg, const TlsServerOptions* server_opts = nullptr);

  ~TlsContextWrapper() = default;

  void shutdown() noexcept;

private:
  struct Private;
  std::shared_ptr<Private> _p;

  friend detail::TlsContextAccess;
};

std::expected<Network::TlsContextWrapper, std::error_code> createTlsContextWrapper(
  const TlsOptions& cfg, const TlsServerOptions* server_opts = nullptr);

}  // namespace Network

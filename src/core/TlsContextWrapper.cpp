#include "core/TlsContextWrapper.h"
#include "core/ErrorCodes.h"
#include "core/details/TlsContextDetail.h"

#include <expected>
#include <filesystem>
#include <spdlog/spdlog.h>

#include <system_error>

namespace Network
{

TlsContextWrapper::Private::Private(const TlsOptions& cfg, const TlsServerOptions* server_opts)
{
  asio::ssl::context::method method = asio::ssl::context::tlsv12_client;
  switch (cfg.min_version)
  {
    case TlsVersion::TLS1_2:
      method = server_opts ? asio::ssl::context::tlsv12_server : asio::ssl::context::tlsv12_client;
      break;
    case TlsVersion::TLS1_3:
      method = server_opts ? asio::ssl::context::tlsv13_server : asio::ssl::context::tlsv13_client;
      break;
    case TlsVersion::Auto:
    default:
      method = server_opts ? asio::ssl::context::tlsv12_server : asio::ssl::context::tlsv12_client;
      break;
  }

  context_ = std::make_shared<asio::ssl::context>(method);

#ifndef OPENSSL_API_3
  uint32_t flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
  switch (cfg.min_version)
  {
    case TlsVersion::TLS1_2:
      flags |= SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
      break;
    case TlsVersion::TLS1_3:
      flags |= SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2;
      break;
    default:
      break;
  }
  switch (cfg.max_version)
  {
    case TlsVersion::TLS1_2:
      flags |= SSL_OP_NO_TLSv1_3;
      break;
    default:
      break;
  }
  context_->set_options(flags);
#else
  uint32_t min_ver = SSL_VERSION_TLS_1_2;
  switch (cfg.min_version)
  {
    case TlsVersion::TLS1_2:
      min_ver = SSL_VERSION_TLS_1_2;
      break;
    case TlsVersion::TLS1_3:
      min_ver = SSL_VERSION_TLS_1_3;
      break;
    default:
      min_ver = SSL_VERSION_DEFAULT_MIN;
      break;
  }
  uint32_t max_ver = SSL_VERSION_TLS_1_3;
  switch (cfg.max_version)
  {
    case TlsVersion::TLS1_2:
      max_ver = SSL_VERSION_TLS_1_2;
      break;
    default:
      max_ver = SSL_VERSION_DEFAULT_MAX;
      break;
  }

  if (SSL_CTX_set_min_proto_version(context_->native_handle(), min_ver) != 1)
  {
    spdlog::warn("Failed to set minimum TLS version");
  }
  if (SSL_CTX_set_max_proto_version(context_->native_handle(), max_ver) != 1)
  {
    spdlog::warn("Failed to set maximum TLS version");
  }
#endif

  if (!cfg.cipher_suites.empty())
  {
    std::string cipher_list;
    for (size_t i = 0; i < cfg.cipher_suites.size(); ++i)
    {
      if (i > 0)
        cipher_list += ':';
      cipher_list += cfg.cipher_suites[i];
    }
    SSL_CTX_set_cipher_list(context_->native_handle(), cipher_list.c_str());
  }

  if (!cfg.ecdh_curves.empty())
  {
    std::string curve_list;
    for (size_t i = 0; i < cfg.ecdh_curves.size(); ++i)
    {
      if (i > 0)
        curve_list += ':';
      curve_list += cfg.ecdh_curves[i];
    }
    SSL_CTX_set1_curves_list(context_->native_handle(), curve_list.c_str());
  }

  long ticket_sec = 86400;
  if (cfg.ticket_lifetime.has_value())
  {
    ticket_sec = static_cast<long>(cfg.ticket_lifetime->count() / 1000);
  }
  SSL_CTX_set_timeout(context_->native_handle(), ticket_sec);

  if (!server_opts)
  {
    std::error_code ec;
    context_->set_verify_mode(asio::ssl::verify_peer, ec);
    if (ec)
    {
      spdlog::warn("Failed to set verify mode: {}", ec.message());
    }

    if (cfg.verify_depth > 0)
    {
      context_->set_verify_depth(cfg.verify_depth);
    }

    context_->set_default_verify_paths(ec);
    if (ec)
    {
      spdlog::debug("Failed to load default CA paths: {}", ec.message());
    }
  }
  else
  {
    std::error_code cert_ec;
    if (!server_opts->_cert_chain_path.empty())
    {
      context_->use_certificate_chain_file(server_opts->_cert_chain_path.string(), cert_ec);
      if (cert_ec)
      {
        spdlog::error("Failed to load certificate chain from {}: {}", server_opts->_cert_chain_path.string(),
                      cert_ec.message());
      }
    }

    std::error_code key_ec;
    if (!server_opts->_private_key_path.empty())
    {
      context_->use_private_key_file(server_opts->_private_key_path.string(), asio::ssl::context::pem, key_ec);
      if (key_ec)
      {
        spdlog::error("Failed to load private key from {}: {}", server_opts->_private_key_path.string(),
                      key_ec.message());
      }
    }
  }
}

TlsContextWrapper::TlsContextWrapper(const TlsOptions& cfg, const TlsServerOptions* server_opts)
  : _p(std::make_shared<Private>(cfg, server_opts))
{
}

void TlsContextWrapper::shutdown() noexcept
{
  if (!_p)
    return;

  if (_p->context_)
  {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    SSL_CTX_free(_p->context_->native_handle());
#else
    SSL_cleanup(_p->context_->native_handle());
#endif
    _p->context_.reset();
  }
}

std::expected<Network::TlsContextWrapper, std::error_code> createTlsContextWrapper(const TlsOptions& cfg,
                                                                                   const TlsServerOptions* server_opts)
{
  // If no server options (client mode), just construct - always succeeds
  if (!server_opts)
  {
    return Network::TlsContextWrapper(cfg, nullptr);
  }

  // Server mode: validate that files exist before creating context
  std::error_code file_ec;

  if (!server_opts->_cert_chain_path.empty() && !std::filesystem::exists(server_opts->_cert_chain_path, file_ec))
  {
    return std::unexpected(make_error_code(Error::TLS_CERT_LOAD_FAILURE));
  }

  if (!server_opts->_private_key_path.empty() && !std::filesystem::exists(server_opts->_private_key_path, file_ec))
  {
    return std::unexpected(make_error_code(Error::TLS_CERT_LOAD_FAILURE));
  }

  // All checks passed, create the wrapper
  return Network::TlsContextWrapper(cfg, server_opts);
}

}  // namespace Network


#pragma once

#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <chrono>
#include <filesystem>

namespace Network
{
/// Enumerates supported TLS protocol versions for negotiation limits.
enum class TlsVersion : uint8_t
{
  /// Automatically negotiate the highest mutually supported version with peer.
  Auto = 0,
  /// Enforce TLS 1.2 (and below) as minimum supported version.
  TLS1_2 = 1,
  /// Enforce TLS 1.3 as minimum supported version (drops legacy ciphersuites & renegotiation).
  TLS1_3 = 3
};

/// Configuration structure for SSL/TLS handshake and security parameters.
/// Apply to boost::asio::ssl context before wrapping the stream or calling async_handshake().
struct TlsOptions
{
  // === Protocol Versions ===
  /// Minimum TLS version allowed for handshake negotiation. Determines fallback behavior when connecting to older
  /// servers. Must be set on SSL_CTX before stream creation. Note: TLS 1.3 drops many legacy features; setting
  /// min=TLS1_3 disables session IDs & full renegotiation.
  TlsVersion min_version = TlsVersion::TLS1_2;

  /// Maximum TLS version allowed for handshake negotiation. Often ignored by modern stacks when min_version is TLS 1.3
  /// or Auto. Use to enforce strict version caps (e.g., max=TLS1_2) for environments where TLS 1.3 extensions are not
  /// supported.
  TlsVersion max_version = TlsVersion::TLS1_3;

  // === Cryptography ===
  /// Explicit list of allowed cipher suites in preference order. Overrides stack defaults when populated.
  /// Format: OpenSSL/BoringSSL standard names (e.g., "TLS_AES_256_GCM_SHA384", "ECDHE-ECDSA-AES128-GCM-SHA256").
  /// Empty vector uses stack defaults. Order determines client/server preference. Prefer AEAD suites for security &
  /// performance.
  std::vector<std::string> cipher_suites = {};

  /// Elliptic curves used for ECDHE key exchange to provide forward secrecy. Stacks prefer the first mutually supported
  /// curve. Format: OpenSSL curve identifiers (e.g., "x25519", "secp256r1" / P-256). x25519 is recommended for speed &
  /// security. Order matters when clients advertise multiple curves; first match wins.
  std::vector<std::string> ecdh_curves = {"x25519", "secp256r1"};

  // === Session Resumption ===
  /// Enable RFC 5077 session tickets for fast TLS resumption without server-side state storage.
  /// Reduces handshake to 1-RTT. Requires pairing with ticket_lifetime_sec. Recommended for scale-out deployments.
  bool enable_session_tickets = true;

  /// Maximum lifetime of a session ticket before forcing a full TLS handshake. (Unit: milliseconds)
  /// Typical values: 86,400,000 ms (24h) to 604,800,000 ms (7 days). Limits exposure if server ticket key is
  /// compromised.
  std::optional<std::chrono::milliseconds> ticket_lifetime = std::make_optional(std::chrono::hours(24));  // 24h

  /// Enforce RFC 7507 requirement that resumed sessions must negotiate the exact same cipher suite as the original
  /// handshake. Prevents downgrade attacks on ticket resume. Highly recommended for TLS 1.2 compatibility. TLS 1.3
  /// enforces by default.
  bool restrict_cipher_on_resume = true;

  // === Verification ===
  /// Enable full certificate chain verification against trusted CA store (signatures, expiration, hostname,
  /// revocation). Disable only for testing/self-signed certificates or internal trust stores with custom verify
  /// callback. Default: true.
  bool verify_peer = false;

  /// Maximum number of certificates in the trust/intermediate chain to validate during peer verification.
  /// Recommended: 3–5. Depth 0 validates leaf only; depth 2 validates CA + intermediate chains. Prevents DoS via
  /// excessively long chains.
  int verify_depth = 5;

  /// Maximum time in milliseconds allowed for the TLS handshake to complete before triggering an error/connection drop.
  /// Protects against slow/lucky DoS attacks (e.g., Heartbleed, renegotiation bombs). If nullopt, stack uses
  /// blocking/infinite default.
  std::optional<std::chrono::milliseconds> handshake_timeout_ms = std::nullopt;
};

struct TlsServerOptions
{
  std::filesystem::path _cert_chain_path;
  std::filesystem::path _private_key_path;
};

}  // namespace Network
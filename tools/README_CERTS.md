# TLS Certificate Generation Tool

## Overview

This tool generates self-signed TLS certificates for unit testing the TLS server/client functionality.

## Usage

### Generate Certificates

```bash
python3 tools/generate_certs.py
```

**Options:**
- `--output-dir, -o`: Output directory (default: `tests/certs`)
- `--common-name, -n`: Server certificate CN (default: `localhost`)
- `--days, -d`: Certificate validity in days (default: `365`)
- `--no-client`: Skip client certificate generation

### Example

```bash
# Generate certificates to default location
python3 tools/generate_certs.py

# Generate with custom parameters
python3 tools/generate_certs.py -o tests/certs -n myserver.local -d 730
```

### Generated Files

```
tests/certs/
├── ca.crt          # CA certificate (for verification)
├── ca.key          # CA private key
├── server.crt      # Server certificate
├── server.key      # Server private key
├── client.crt      # Client certificate (optional)
└── client.key      # Client private key (optional)
```

## Using with the Network Stack

### Server Setup

```cpp
#include "server/ServerSync.h"
#include <asio/ssl/context.hpp>

class TlsServer : public Network::ServerSync {
public:
  TlsServer(uint16_t port, asio::io_context& io_ctx)
      : ServerSync(port, io_ctx) {
    // Load server certificate
    auto context = get_ssl_context();
    context->use_certificate_chain_file("tests/certs/server.crt");
    context->use_private_key_file("tests/certs/server.key", asio::ssl::context::pem);
    context->load_verify_file("tests/certs/ca.crt");
    context->set_verify_mode(asio::ssl::verify_peer);
  }

  void handle_client_tls(std::unique_ptr<Network::SslSocket> sock) override {
    // Handle TLS connection
  }
};
```

### Client Setup

```cpp
#include "client/ClientSync.h"
#include <asio/ssl/context.hpp>

asio::io_context io_ctx;
Network::ClientSync client("localhost", 443, io_ctx);

// Configure client SSL context
auto context = client.get_ssl_context();
context->set_verify_mode(asio::ssl::verify_peer);
context->load_verify_file("tests/certs/ca.crt");
// Optionally load client certificate for mutual TLS:
// context->use_certificate_chain_file("tests/certs/client.crt");
// context->use_private_key_file("tests/certs/client.key", asio::ssl::context::pem);

// Connect with TLS
auto result = client.connect_tls();
```

## Certificate Details

- **CA Certificate**: Self-signed, valid for 365 days
- **Server Certificate**: Signed by CA, valid for 365 days
- **Client Certificate**: Signed by CA, valid for 365 days (optional)
- **Key Size**: 2048-bit RSA
- **Signature Algorithm**: SHA-256

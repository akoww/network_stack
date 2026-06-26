# include/core/ - Core Infrastructure

## Key Components
- **`ErrorCodes.h`**: Defines `Network::Error` enum (`NO_ERROR`, `CONNECTION_REFUSED`, `CONNECTION_TIMEOUT`, `CONNECTION_LOST`, `DNS_FAILURE`, `PROTOCOL_ERROR`). Provides `make_error_code()` (snake_case for ADL) and `is_error_code_enum<Network::Error>` for `std::error_code` integration.
- **`Context.h`**: `IoContextWrapper` is a singleton wrapping `asio::thread_pool` with background thread management (`start()`, `stop(), `instance()`). Access executor via `detail::getExecutor(ctx)`.
- **`TlsContextWrapper.h`**: Wrapper around `asio::ssl::context` for TLS configuration. Uses pimpl pattern (private `struct Private` + friend `TlsContextAccess`). For servers, use `createTlsContextWrapper()` which validates cert files before loading.

## Conventions
- Always include `"core/ErrorCodes.h"` before other project headers.
- `IoContextWrapper` manages its own work guard and synchronization.
- Error codes use 0 for `NO_ERROR`.

### TLS Context Lifecycle
- Client mode: Constructor always succeeds. No shutdown needed unless explicitly called.
- Server mode: Use `createTlsContextWrapper()` (validates cert files exist). Call `shutdown()` before destructor to free OpenSSL resources.

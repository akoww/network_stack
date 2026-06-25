# include/core/ - Core Infrastructure

## Key Components
- **`ErrorCodes.h`**: Defines `Network::Error` enum (`NO_ERROR`, `CONNECTION_REFUSED`, `CONNECTION_TIMEOUT`, `CONNECTION_LOST`, `DNS_FAILURE`, `PROTOCOL_ERROR`). Provides `make_error_code()` (snake_case for ADL) and `is_error_code_enum<Network::Error>` for `std::error_code` integration.
- **`Context.h`**: `IoContextWrapper` is a singleton wrapping `asio::io_context` with background thread management (`start()`, `stop()`, `instance()`).
- **`TlsContextWrapper.h`**: Wrapper around `asio::ssl::context` for TLS configuration and lifetime management (similar pattern to `IoContextWrapper`). Access underlying context via `detail::getTlsContext(wrapper)`.

## Conventions
- Always include `"core/ErrorCodes.h"` before other project headers.
- `IoContextWrapper` manages its own work guard and synchronization.
- Error codes use 0 for `NO_ERROR`.

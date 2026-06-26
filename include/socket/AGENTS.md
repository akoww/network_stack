# include/socket/ - Socket Interface Layer

## Key Components
- **`SocketBase.h`**: Defines `SocketBase` (common state: `_id`, `_read_buffer`, `_cancel_signal`), `SyncSocket` (blocking ops with optional timeout), `AsyncSocket` (coroutine ops returning `asio::awaitable<std::expected<std::size_t, std::error_code>>`), and `DualSocket` (inherits all three).
- **`TcpSocket` / `TlsSocket`**: Concrete implementations inheriting `DualSocket`. Uses pimpl pattern with `std::shared_ptr<Private>` and friends in `details/` namespace to hide ASIO. `TlsSocket` requires explicit `asio/ssl/` includes.
- **`*Detail.h` headers**: Friend accessors for private members (not public API).

## Conventions
- Sockets are always returned as `std::unique_ptr<DualSocket>`.
- Private members use `_camelCase` prefix.
- Timeouts default to infinite; explicit timeouts are required for production code.
- `isConnectionClosed()` checks for `eof`, `connection_reset`, `broken_pipe`, `not_connected`.
- Mixed sync/async usage on the same `DualSocket` instance is supported, but not concurrent.

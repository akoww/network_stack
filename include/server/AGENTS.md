# include/server/ - Server Abstractions

## Purpose

Server connection handling: accepts connections and dispatches them to handler methods.

## Key Components

### ServerBase.h

- **Abstract base**: Defines virtual handler methods, not meant for direct use
- **`handle_client(std::unique_ptr<TcpSocket>)`**: Override to handle TCP clients
- **`handle_client_tls(std::unique_ptr<SslSocket>)`**: Override to handle TLS clients
- **Server lifecycle**: `stop()`, `is_stopped()`, acceptor management
- **Configuration**: `host()`, `port()`, `get_io_context()`, `get_ssl_context()`

### ServerSync.h

- **`listen()`**: Blocking accept loop, returns `std::expected<void, std::error_code>`
- **`listen_tls()`**: Blocking TLS accept loop
- **Usage**: Must run in separate thread (blocks indefinitely until `stop()`)
- **Implementation**: Uses `std::promise/std::future` for async start, then blocking `accept()`

### ServerAsync.h

- **`listen()`**: Coroutine accept loop, returns `asio::awaitable<std::expected<void, std::error_code>>`
- **`listen_tls()`**: Coroutine TLS accept loop
- **Usage**: Must be `asio::co_spawn`ed with `asio::detached` (never co_await directly)
- **Implementation**: Uses `async_accept()` with redirect_error

## Conventions

- All server classes inherit from `ServerBase`
- Handlers receive ownership of socket via `std::unique_ptr`
- Server does not manage client logic - just delivers sockets to handlers
- TLS support: separate `listen_tls()` and `handle_client_tls()` methods

## Important Notes

- **Server base is abstract** - use `ServerSync` or `ServerAsync` derived classes
- **Blocking servers require threads** - `listen()` blocks until stopped
- **Async servers require co_spawn** - use `asio::co_spawn(io_ctx, server.listen(), asio::detached)`
- **Stop signal**: Call `stop()` to terminate accept loop gracefully
- **SSL context**: Must be configured via `get_ssl_context()` before calling `listen_tls()`

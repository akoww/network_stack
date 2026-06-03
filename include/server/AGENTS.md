# include/server/ - Server Abstractions

## Purpose

Server connection handling: accepts connections and dispatches them to handler methods.

## Key Components

### ServerBase.h

- **Abstract base**: Defines virtual handler methods, not meant for direct use; holds common state (host, port, acceptor, ssl context)
- **`ClientHandler`**: `std::function<void(std::unique_ptr<DualSocket>)>` callback interface
- **Server lifecycle**: `stop()`, `isStopped()`, acceptor management
- **Configuration**: `host()`, `port()`, `getIoContext()`, `getSslContext()`

### ServerSync (interface, in ServerBase.h)

- **`listen()`**: Blocking accept loop, returns `std::expected<void, std::error_code>`
- **`listenTls()`**: Blocking TLS accept loop (camelCase, no underscore)
- **Usage**: Must run in separate thread (blocks indefinitely until `stop()`)

### ServerAsync (interface, in ServerBase.h)

- **`asyncListen()`**: Coroutine accept loop, returns `asio::awaitable<std::expected<void, std::error_code>>`
- **`asyncListenTls()`**: Coroutine TLS accept loop (camelCase, no underscore)
- **Usage**: Must be `asio::co_spawn`ed with `asio::detached` (never co_await directly)

### Server.h/cpp

- **Unified server class** implementing both `ServerSync` and `ServerAsync` interfaces
- All four methods (`listen`, `listenTls`, `asyncListen`, `asyncListenTls`) available on a single `Server` instance
- Uses `std::promise/std::future` for blocking variants, `async_accept()` with redirect_error for coroutine variants

## Conventions

- All server classes inherit from `ServerBase`
- Handlers receive ownership of socket via `std::unique_ptr<DualSocket>`
- Server does not manage client logic - just delivers sockets to handlers
- TLS support: `listenTls()` and `asyncListenTls()` (no underscore)

## Important Notes

- **Use the unified `Server` class** - it implements both sync and async interfaces simultaneously
- **Blocking servers require threads** - `listen()` blocks until stopped
- **Async servers require co_spawn** - use `asio::co_spawn(io_ctx, server.asyncListen(), asio::detached)`
- **Stop signal**: Call `stop()` to terminate accept loop gracefully
- **SSL context**: Must be configured via `getSslContext()` before calling `listenTls()` or `asyncListenTls()`

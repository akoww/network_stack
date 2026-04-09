# include/client/ - Client Abstractions

## Purpose

High-level client connection orchestration: resolves DNS, establishes connections, supports TLS.

## Key Components

### ClientBase.h

- **Shared configuration**: `host()`, `port()`, `getIoContext()`, `getSslContext()`
- **Options struct**: `timeout` field for connection timeouts
- **Constructor**: `ClientBase(std::string_view host, uint16_t port, asio::io_context&)`
- **SSL context**: lazily-created with `getSslContext()`

### ClientSync.h

- **`connect(Options)`**: Blocking connection, returns `std::unique_ptr<SyncSocket>`
- **`connect_tls(Options)`**: Blocking TLS connection, returns `std::unique_ptr<SyncSocket>`
- **Usage**: Run in main thread or dedicate thread for blocking calls
- **Uses**: `asio::ip::tcp::resolver` and `asio::connect()` for DNS + connection

### ClientAsync.h

- **`connect(Options)`**: Coroutine, returns `asio::awaitable<std::unique_ptr<AsyncSocket>>`
- **`connect_tls(Options)`**: Coroutine TLS connection
- **Usage**: Must be `co_await`ed or `asio::co_spawn`ed
- **Uses**: `asio::async_connect()` with redirect_error

## Conventions

- All client classes inherit from `ClientBase`
- Connection timeout must be explicitly provided (no default)
- TLS connections use same `getSslContext()` from base class
- Sockets ownership transferred to caller via `std::unique_ptr`

## Important Notes

- **Client does not own the socket** - it creates and returns it
- Sockets must be used with matching interface: sync client returns `SyncSocket`, async client returns `AsyncSocket`
- DNS errors are captured via `std::error_code`, not thrown exceptions
- Use `ClientSync` in threads, `ClientAsync` in coroutines - don't mix

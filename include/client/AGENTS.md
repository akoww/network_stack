# include/client/ - Client Abstractions

## Purpose

High-level client connection orchestration: resolves DNS, establishes connections, supports TLS.

## Key Components

### ClientBase.h

- **Shared configuration**: `host()`, `port()`, `getIoContext()`, `getSslContext()`
- **Constructor**: `ClientBase(std::string_view host, uint16_t port, asio::io_context&)`
- **SSL context**: lazily-created with `getSslContext()`

### ClientSync (interface, in ClientBase.h)

- **`connect(timeout)`**: Blocking connection, returns `std::unique_ptr<DualSocket>` - timeout defaults to 500ms, no Options struct needed
- **`connectTls(timeout)`**: Blocking TLS connection, returns `std::unique_ptr<DualSocket>` - timeout defaults to 500ms

### ClientAsync (interface, in ClientBase.h)

- **`asyncConnect(timeout)`**: Coroutine, returns `asio::awaitable<std::unique_ptr<DualSocket>>`
- **`asyncConnectTls(timeout)`**: Coroutine TLS connection
- **Usage**: Must be `co_await`ed or `asio::co_spawn`ed

### Client.h/cpp

- **Unified client class** implementing both `ClientSync` and `ClientAsync` interfaces
- All four methods (`connect`, `connectTls`, `asyncConnect`, `asyncConnectTls`) available on a single `Client` instance
- Timeout parameter defaults to 500ms - no Options struct needed
- Uses `asio::ip::tcp::resolver` and `asio::connect()` for DNS + connection
- Connection timeout must be explicitly provided as milliseconds (e.g. `client.connect(std::chrono::seconds(10))`)
- TLS connections use same `getSslContext()` from base class

## Conventions

- All client classes inherit from `ClientBase`
- Timeout defaults to 500ms when not specified - pass explicit timeout for production code
- TLS connections use same `getSslContext()` from base class
- Sockets ownership transferred to caller via `std::unique_ptr<DualSocket>`

## Important Notes

- **Client does not own the socket** - it creates and returns it
- Use the unified `Client` class - it works for both sync and async usage patterns
- DNS errors are captured via `std::error_code`, not thrown exceptions
- Sockets must be used with matching interface: sync client returns `SyncSocket`, async client returns `AsyncSocket`

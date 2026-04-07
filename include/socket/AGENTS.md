# include/socket/ - Socket Interface Layer

## Purpose

Defines socket interfaces (sync/async) and common base functionality. This is the boundary between core infrastructure and protocol-specific code.

## Key Components

### Interfaces

- **`SyncSocketInterface.h`**: `SyncSocket` interface with blocking operations (`readSome`, `writeAll`, `readExact`, `readUntil`)
- **`AsyncSocketInterface.h`**: `AsyncSocket` interface with coroutine-based operations (returns `asio::awaitable<std::expected<...>>`)
- **Both**: Inherit from `SocketBase` and provide optional timeout support

### Base Classes

- **`SocketBase.h`**: Common interface with `get_id()`, `isConnected()`, `closeSocket()`, `cancelSocket()`, `isConnectionClosed()`
- **`SocketBase.cpp`**: Implements `_id_counter` and `get_id()`

### Implementations

- **`TcpSocket.h/cpp`**: Dual-mode socket implementing both `SyncSocket` and `AsyncSocket`
- **`SslSocket.h/cpp`**: TLS wrapper around.tcp socket implementing both interfaces
- **`TcpImpl.h`**: FTP-specific TCP implementation (not a direct socket interface)

### HelperHeaders

- **`SocketBaseImpl.h`**: **Implementation detail, not public interface** - contains common coroutine logic for async reads/writes
- Defines `socket_detail::asyncReadSome_common()`, `asyncReadExact_common()`, `asyncReadUntil_common()`, `asyncWriteAllCommon()`
- Used by `TcpSocket` and `SslSocket` implementations

## Conventions

- All sockets inherit from `SocketBase` and at least one interface (`SyncSocket` or `AsyncSocket`)
- Memory management: Sockets returned as `std::unique_ptr<SyncSocket>` or `std::unique_ptr<AsyncSocket>`
- Timeout handling: Optional `std::chrono::milliseconds` parameter, defaults to 24 hours if not specified
- Buffer management: Read buffer stored in socket (`get_read_buffer()`) for accumulating partial reads

## Important Notes

- `SocketBaseImpl.h` is a **helper implementation file**, not a public interface
- `TcpSocket` can be used with both sync and async operations (mixed usage supported)
- `isConnectionClosed()` checks for ASIO connection reset errors: `eof`, `connection_reset`, `broken_pipe`, `not_connected`

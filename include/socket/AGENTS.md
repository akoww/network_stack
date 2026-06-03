# include/socket/ - Socket Interface Layer

## Purpose

Defines socket interfaces (sync/async) and common base functionality. This is the boundary between core infrastructure and protocol-specific code.

## Key Components

### Interfaces

- **`SyncSocketInterface.h`**: `SyncSocket` interface with blocking operations (`readSome`, `writeAll`, `readExact`, `readUntil`)
- **`AsyncSocketInterface.h`**: `AsyncSocket` interface with coroutine-based operations (returns `asio::awaitable<std::expected<...>>`)
- **Both**: Inherited by `DualSocket` and provide optional timeout support via `std::chrono::milliseconds`

### Base Classes

- **`SocketBase.h`**: Common interface with `get_id()`, `isConnected()`, `closeSocket()`, `cancelSocket()`, `isConnectionClosed()`; also defines the unified `DualSocket` class
- **`DualSocket`**: Inherits from `SocketBase`, `AsyncSocket`, and `SyncSocket` - all socket types derive from this

### Implementations

- **`TcpSocket.h/cpp`**: Dual-mode socket implementing both interfaces via `DualSocket` base
- **`SslSocket.h/cpp`**: TLS wrapper around tcp socket implementing both interfaces via `DualSocket` base
- **`TcpImpl.h`**: FTP-specific TCP implementation (not a direct socket interface)

### Helper Headers

- **`SocketBaseImpl.h`**: **Implementation detail, not public interface** - contains common coroutine logic for async reads/writes in the `socket_detail` namespace
- Provides `asyncReadSomeCommon()`, `asyncReadExactCommon()`, `asyncReadUntilCommon()`, `asyncWriteAllCommon()` template functions
- Used internally by `TcpSocket` and `SslSocket` implementations

## Conventions

- All sockets inherit from `DualSocket` (which inherits from both sync and async interfaces)
- Memory management: Sockets returned as `std::unique_ptr<DualSocket>` (never raw interface pointers)
- Timeout handling: Optional `std::chrono::milliseconds` parameter, defaults to 24 hours if not specified
- Buffer management: Read buffer stored in socket (`getReadBuffer()`) for accumulating partial reads

## Important Notes

- `SocketBaseImpl.h` is a **helper implementation file**, not a public interface header - include it via the concrete socket headers
- `TcpSocket` can be used with both sync and async operations (mixed usage supported)
- `isConnectionClosed()` checks for ASIO connection reset errors: `eof`, `connection_reset`, `broken_pipe`, `not_connected`

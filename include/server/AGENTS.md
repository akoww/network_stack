# include/server/ - Server Abstractions

## Key Components
- **`ServerBase.h`**: Holds common state (`host`, `port`, acceptor, TLS context) and defines `ClientHandler` (`std::function<void(std::unique_ptr<DualSocket>)>`). Uses pimpl pattern (`std::unique_ptr<Private>`) with `ServerAccess` helper in `details/ServerBaseDetail.h`.
- **`Server.h/cpp`**: Unified class implementing both `ServerSync` (`listen()`, `listenTls()`) and `ServerAsync` (`asyncListen()`, `asyncListenTls()`).

## Conventions
- **Blocking (`listen`)**: Runs indefinitely until `stop()` is called. Must be executed in a separate thread.
- **Async (`asyncListen`)**: Coroutine accept loop. MUST be launched via `asio::co_spawn(io_ctx, server.asyncListen(), asio::detached)`. NEVER `co_await` directly (infinite loop).
- Handlers receive socket ownership via `std::unique_ptr<DualSocket>`.
- TLS context must be configured via `getTlsContext()` before calling TLS variants.
- Server destructor MUST cancel all client sockets and join threads/coroutines to prevent leaks.

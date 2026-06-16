# include/server/ - Server Abstractions

## Key Components
- **`ServerBase.h`**: Holds common state (`host`, `port`, acceptor, SSL context) and defines `ClientHandler` (`std::function<void(std::unique_ptr<DualSocket>)>`).
- **`Server.h/cpp`**: Unified class implementing both `ServerSync` (`listen()`, `listenTls()`) and `ServerAsync` (`asyncListen()`, `asyncListenTls()`).

## Conventions
- **Blocking (`listen`)**: Runs indefinitely until `stop()` is called. Must be executed in a separate thread.
- **Async (`asyncListen`)**: Coroutine accept loop. MUST be launched via `asio::co_spawn(io_ctx, server.asyncListen(), asio::detached)`. NEVER `co_await` directly (infinite loop).
- Handlers receive socket ownership via `std::unique_ptr<DualSocket>`.
- SSL context must be configured via `getSslContext()` before calling TLS variants.
- Server destructor MUST cancel all client sockets and join threads/coroutines to prevent leaks.

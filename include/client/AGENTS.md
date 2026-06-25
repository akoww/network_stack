# include/client/ - Client Abstractions

## Key Components
- **`ClientBase.h`**: Shared config (`host()`, `port()`, `getIoContext()`, `getTlsContext()`). Defines `ClientSync` and `ClientAsync` interfaces.
- **`Client.h/cpp`**: Unified class implementing both `ClientSync` and `ClientAsync`. Provides `connect()`, `connectTls()`, `asyncConnect()`, `asyncConnectTls()`.

## Conventions
- Timeout defaults to 500ms, but explicit timeouts (e.g., `std::chrono::seconds(10)`) should be passed for production code.
- Sockets are returned as `std::unique_ptr<DualSocket>`; the client does not retain ownership.
- DNS and connection errors are captured via `std::error_code`, never thrown.
- TLS connections reuse the lazily-created `getTlsContext()` from the base class.

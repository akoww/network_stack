# Network Stack - Agent Instructions

## Build & Test
```bash
cd build && cmake .. && cmake --build .   # Debug (default)
cmake -DCMAKE_BUILD_TYPE=Release ..       # Release rebuild
ctest                                     # All tests
ctest -V                                  # Verbose
ctest -R pattern                          # Run by regex
```
Run single test:
```bash
cmake --build . --target test_error_code
./build/bin/test_error_code --gtest_filter=NetworkErrorTest.NoErrorIsSuccess
```

## Lint & Format
- **Format**: Chromium style, **Allman braces** (breaks *before* braces), 2-space indent, 120 col limit.
- **Tidy**: `clang-tidy -p build include/**/*.h src/**/*.cpp` (add `-fix` to auto-fix, or enable via `-DENABLE_CLANG_TIDY_FIX=ON`).

## Architecture
Layered dependency: `core` < `socket` < `client`/`server` < `protocol`. Headers may only depend on earlier layers.
- **`DualSocket`**: Unified sync/async socket. Returned as `std::unique_ptr<DualSocket>`.
- **`Client` / `Server`**: Unified classes. No separate `*Sync`/`*Async` classes exist.
- **`IoContextWrapper`**: Singleton wrapping `asio::io_context` with background thread.
- **Protocol**: `TicketPeer` (connection lifecycle), `TicketController`/`TicketWorker` (execution), `FtpFileTransfer` (FTP, depends on nlohmann_json).

## Code Conventions
- **Naming**: `PascalCase` (classes), `camelCase` (functions/locals), `_camelCase` (private members), `ALL_CAPS` (constants/enums). Exception: `make_error_code()` is snake_case for ADL.
- **Types**: `std::expected<T, std::error_code>` for failures (never throw ASIO errors). `std::span<T>` for buffers, `std::string_view` over `const std::string&`.
- **Includes**: System → ASIO → project. Always include `"core/ErrorCodes.h"` first among project headers. TLS requires explicit `asio/ssl/` includes.

## Error Handling
```cpp
#include "core/ErrorCodes.h"
auto result = client.connect();
if (result) { /* *result */ } else { auto ec = result.error(); } // Never throw
// Custom errors: make_error_code(Network::Error::DNS_FAILURE)
```

## Async Rules
- Use `asio::awaitable<T>` returning `std::expected<T, std::error_code>`.
- **Server listen**: ALWAYS use `asio::co_spawn(io_ctx, server.asyncListen(), asio::detached)`. NEVER `co_await` directly (infinite loop).

## Testing Quirks
- Tests are **separate executables** per file (via `add_separate_tests()`), not a monolith.
- **Certs**: `tests/certs/` via `SOURCE_DIR_CERT` compile def. Use build-tree-relative paths.
- **FTP tests**: Require external FTP server (see `tests/fixtures/FtpServerFixture.h`). Not auto-started by CMake.
- **TLS**: Requires OpenSSL. `echo_server` is a standalone binary for external-process testing.

## Critical Pitfalls
1. **Allman braces**: `.clang-format` enforces this. Do NOT use K&R.
2. **Server teardown**: Destructor MUST cancel all client sockets and join coroutines/threads to prevent leaks/dangling pointers.
3. **Socket ownership**: Callers receive `unique_ptr<DualSocket>`; clients do not retain ownership.

## Related Docs
- Layer-specific API details: `include/*/AGENTS.md`
- Tasks/Issues: `TODO.md`, `KNOWN_PROBLEMS.md`

# Network Stack - Agent Documentation

## Project Overview

Modern C++23 Network Stack built on ASIO for asynchronous I/O operations.

## Build System

- **CMake** with C++23 standard (`CMAKE_CXX_STANDARD 23`)
- **Dependencies**: ASIO (headers), spdlog, GTest
- Output directories: `build/bin` (executables), `build/lib` (libraries)

## Build Commands

```bash
cd build
cmake .. && cmake --build .
```

### Build with Debug/Release

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..  # or Release
```

### Build a Single Test Binary

Tests are built as separate executables via `add_separate_tests()`:

```bash
cmake .. && cmake --build .
```

All tests are in `build/bin/` with names like `test_error_code`, `test_socket_interface`, etc.

### Run Tests

```bash
cd build
ctest                                    # Run all tests
ctest -R test_error_code                 # Run specific test by regex
ctest -V                                 # Verbose output
```

To run a single test case within a test binary:

```bash
./build/bin/test_error_code --gtest_filter=NetworkErrorTest.NoErrorIsSuccess
```

### Linting

No linting configuration is currently included. Use `clang-tidy` with these flags:

```bash
clang-tidy -p build -header-filter=include src/**/*.cpp include/**/*.h \
  --checks='-*,modernize-*,bugprone-*,performance-*,readability-*,misc-*'
```

## Code Style Guidelines

### General

- **Language**: C++23 (no extensions)
- **Namespaces**: Use `Network` for library code; `Network::Test` for test code
- **Include order**: System headers → ASIO → project headers
- **Header guards**: Use `#pragma once`

### Files & Directories

- Headers in `include/` match source structure in `src/`
- Test files in `tests/` with pattern `test_*.cpp`
- Implementation files in corresponding `src/` subdirectories

### Naming Conventions

- **Classes/Structs**: `PascalCase` (e.g., `AsioTcpSocket`, `IoContextWrapper`)
- **Interfaces**: End with `Interface` or descriptive name (e.g., `SocketBase`, `AsyncSocket`)
- **Variables/Members**: `_prefixed_lower_snake_case` (e.g., `_host`, `_port`)
- **Functions**: `snake_case` (e.g., `is_connected`, `async_send`)
- **Constants/Enums**: `PascalCase` or `UPPER_SNAKE_CASE` (e.g., `Error::ConnectionTimeout`)
- **Namespaces**: `snake_case` (e.g., `Network`, `asio`)

### Types & Declarations

- Use `std::expected<T, E>` for operations that may fail
- Use `std::span<T>` for buffer parameters
- Prefer `std::string_view` over `const std::string&`
- Use `[[nodiscard]]` for functions where ignoring return value is likely an error
- Use `noexcept` for simple accessors and destructors
- Use `explicit` for single-argument constructors
- Use `override` on virtual function overrides (not `virtual` in derived classes)
- Use `constexpr` where applicable

### Error Handling

- Custom error enum `Network::Error` with values: `NoError`, `ConnectionRefused`, `ConnectionTimeout`, `ConnectionLost`, `DnsFailure`, `ProtocolError`
- Use `std::error_code` with `Network::get_network_category()` for error codes
- Convert with `std::make_error_code(Network::Error::X)`
- Check errors via `if (!ec)` or `if (ec)`

### Asynchronous Code

- Use `asio::awaitable<T>` for async operations returning `std::expected<T, std::error_code>`
- Functions marked `co_await` use `asio::co_spawn` or `co_await` within coroutines
- Use `asio::detached` for fire-and-forget coroutines

### Formatting

- 4-space indentation (no tabs)
- Max line length: 120 characters
- Opening braces on same line as declaration (K&R style)
- Use spaces around operators and after commas
- No spaces before semicolons or after opening/unary operators

### Example Patterns

```cpp
// Header (include/ socket/AsioTcpSocket.h)
#pragma once

#include "socket/BaseInterface.h"
#include <asio/ip/tcp.hpp>

namespace Network {

class AsioTcpSocket : public SyncSocket, public AsyncSocket {
private:
    asio::ip::tcp::socket socket_;

public:
    explicit AsioTcpSocket(asio::io_context& io_ctx);
    ~AsioTcpSocket() override;

    bool is_connected() const noexcept override;

    asio::awaitable<std::expected<std::size_t, std::error_code>>
    async_send(std::span<const std::byte> buffer) override;

    std::expected<std::size_t, std::error_code>
    send(std::span<const std::byte> buffer) override;
};

} // namespace Network

// Implementation (src/ socket/AsioTcpSocket.cpp)
#include "socket/AsioTcpSocket.h"

namespace Network {

AsioTcpSocket::AsioTcpSocket(asio::io_context& io_ctx) : socket_(io_ctx) {}

AsioTcpSocket::~AsioTcpSocket() {
    if (socket_.is_open()) {
        std::error_code ec;
        socket_.close(ec);
    }
}

bool AsioTcpSocket::is_connected() const noexcept {
    return socket_.is_open();
}

asio::awaitable<std::expected<std::size_t, std::error_code>>
AsioTcpSocket::async_send(std::span<const std::byte> buffer) override {
    // TODO: Implement async send
    co_return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
}

std::expected<std::size_t, std::error_code>
AsioTcpSocket::send(std::span<const std::byte> buffer) override {
    // TODO: Implement sync send
    co_return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
}

} // namespace Network
```

### Testing

- Use Google Test (`gtest`)
- Test files: `tests/test_*.cpp`
- Use `TEST()` or `TEST_F()` macros
- Expectations: `EXPECT_*()` for non-fatal, `ASSERT_*()` for fatal
- Names: `ClassNameTest.TestDescription`
- Test code in `Network::Test` namespace

### Known Issues / TODOs

- Socket implementations currently return `function_not_supported` placeholders
- Missing implementation for `ClientAsync::connect()`, `ClientSync`, `ServerAsync`, `ServerSync`
- `is_connected()` in `AsioTcpSocket` returns `false` (stubbed)

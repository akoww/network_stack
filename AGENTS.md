# Network Stack - Agent Documentation

## Project Overview

Modern C++23 Network Stack built on ASIO for asynchronous I/O operations with TLS support and FTP protocol implementation.

## Build System

- **CMake** with C++23 standard (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`)
- **Dependencies**: ASIO (headers), spdlog, GTest, OpenSSL
- Output directories: `build/bin` (executables), `build/lib` (libraries)
- **Compiler flags**: `-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wcast-align -Wconversion -Wsign-conversion -Wnull-dereference -Werror -pedantic` (GCC/Clang); `/W4 /WX /permissive-` (MSVC)
- **Sanitizers in Debug**: address, undefined, leak (`-g -O0 -fsanitize=address,undefined,leak`)

## Build Commands

```bash
cd build && cmake .. && cmake --build .
```

### Build with Debug/Release

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..  # or Release
```

### Build with clang-tidy autofix

```bash
cmake -DENABLE_CLANG_TIDY_FIX=ON ..
```

### Build and Run Tests

```bash
cd build && ctest                        # All tests
ctest -R test_error_code                 # By regex pattern
ctest -V                                 # Verbose output
ctest -N                                 # List all test names
ctest -I 0,1,1                           # Run test index 0 only

# Build and run a single test executable directly
cmake .. && cmake --build . --target test_error_code
./build/bin/test_error_code --gtest_filter=NetworkErrorTest.NoErrorIsSuccess
./build/bin/test_error_code --gtest_list_tests  # List all tests in executable
```

### Linting

```bash
clang-tidy -p build -header-filter=include src/**/*.cpp include/**/*.h \
  --config-file=.clang-tidy
```

## General

- **Language**: C++23 (no extensions)
- **Namespaces**: Use `Network` for library code; `Network::Test` for test code
- **Include order**: System headers → ASIO → project headers
- **Header guards**: Use `#pragma once`

## Files & Directories

- Headers in `include/` match source structure in `src/`
- Test files in `tests/` with pattern `test_*.cpp`
- Implementation files in corresponding `src/` subdirectories
- Test certificates in `tests/certs/` (CA, client, server keys/certs for TLS)
- Test fixtures in `tests/fixtures/` (FTP server, client/server helpers)

## Naming Conventions

- **Classes & Structs**: Use PascalCase (e.g., `CreateScriptedSequenceCommand`).
- **Functions & Methods**: Use camelCase for member functions and free functions (e.g., `createElement`, `destroyElements`).
- **Variables**: Use camelCase for local variables and member variables (e.g., `context`, `params`, `errorLog`). Private variables of classes are always starting with an underscore (e.g. `_my_private_var` while function variables or function parameters are not (e.g. `my_parameter`).
- **Constants & Macros**: Use ALL_CAPS with underscores (e.g., `CMD_CREATE_SCRIPTED_SEQUENCE`).
- **Enum Members**: Use ALL_CAPS with underscores.
- **Global Functions**: Use camelCase.
- **Exception**: `make_error_code()` must remain snake_case for ADL (Argument-Dependent Lookup) compatibility with `std::is_error_code_enum`. This enables implicit conversion from `Network::Error` to `std::error_code`.

## Types & Declarations

- Use `std::expected<T, E>` for operations that may fail
- Use `std::span<T>` for buffer parameters
- Prefer `std::string_view` over `const std::string&`
- Use `[[nodiscard]]` for functions where ignoring return value is likely an error
- Use `noexcept` for simple accessors and destructors
- Use `explicit` for single-argument constructors
- Use `override` on virtual function overrides (not `virtual` in derived classes)
- Use `constexpr` where applicable

## Error Handling

- **Custom error enum** `Network::Error` with values: `NO_ERROR`, `CONNECTION_REFUSED`, `CONNECTION_TIMEOUT`, `CONNECTION_LOST`, `DNS_FAILURE`, `PROTOCOL_ERROR`
- Use `std::error_code` with `Network::getNetworkCategory()` for error codes
- Convert with `makeErrorCode(Network::Error::X)` or implicit conversion via `is_error_code_enum`
- Check errors via `if (!ec)` or `if (ec)`
- **ASIO operations**: Always use `asio::redirect_error(asio::use_awaitable, ec)` for async operations and pass `ec` parameter for sync operations
- **Never throw exceptions**: All ASIO errors must be captured via error codes

## Asynchronous Code

- Use `asio::awaitable<T>` for async operations returning `std::expected<T, std::error_code>`
- Functions marked `co_await` use `asio::co_spawn` or `co_await` within coroutines
- Use `asio::detached` for fire-and-forget coroutines

## Formatting

- 4-space indentation (no tabs)
- Max line length: 120 characters
- Opening braces on same line as declaration (K&R style)
- Use spaces around operators and after commas
- No spaces before semicolons or after opening/unary operators

## Custom CMake Macros

**`add_separate_tests()`** (in `cmake/makro_separate_tests.cmake`):
- Creates individual test executables for each test file
- Automatically links GTest and `network_stack_core`
- Defines `ASIO_STANDALONE=1` and `_HAS_STD_BYTE=0`

## Important Notes

- All include directories have their own `AGENTS.md` files with detailed guidance
- TLS support requires OpenSSL (server and client implementations)
- FTP integration tests require a running FTP server (see `tests/fixtures/FtpServerFixture.h`)
- `IoContextWrapper` provides singleton `io_context` with background thread management
- `TcpSocket` implements both `SyncSocket` and `AsyncSocket` interfaces for mixed usage

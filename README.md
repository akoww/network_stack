# Network Stack

A practice project implementing a basic network stack using modern C++23 and ASIO.

## Overview

This project is educational code to learn and practice modern C++ network programming concepts including:

- Asynchronous I/O with ASIO
- C++23 features and best practices
- Socket programming abstractions
- Error handling with `std::expected`
- Coroutine-based async operations

## Documentation

- **[HOWTO](./HOWTO.md)** - Comprehensive guide to using the network stack
  - Async and sync client/server patterns
  - Lifetime management and thread safety
  - TLS configuration
  - Common patterns and troubleshooting

## Build

```bash
cd build
cmake .. && cmake --build .
```

## Run Tests

```bash
cd build
ctest
```

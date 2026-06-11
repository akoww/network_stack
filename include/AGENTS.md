# include/ - Top-Level Include Directory

## Overview

Root directory for all public headers in the Network Stack library. Contains 5 subdirectories, each representing a major architectural layer.

## Directory Structure

- `core/`: Foundational utilities (error codes, context management)
- `socket/`: Socket interfaces and base classes (`DualSocket`, `TcpSocket`, `TlsSocket`, `SocketBaseImpl`)
- `client/`: Client connection abstractions (unified `Client` class replacing separate sync/async variants)
- `server/`: Server connection handling (unified `Server` class replacing separate sync/async variants)
- `protocol/`: Higher-level protocol implementations (FTP)

## Dependencies

- All headers assume `#include "core/ErrorCodes.h"` before other project headers
- ASIO headers must come before project headers in include order
- TLS features require including `asio/ssl/` headers explicitly

## Public API

All headers in this tree and subdirectories constitute the public API. Each subdirectory has its own `AGENTS.md` with detailed guidance.

## Architecture Notes

- **Unified Client**: `Client.h` combines what was previously `ClientSync.h` and `ClientAsync.h` into a single class that implements both sync and async interfaces simultaneously
- **Unified Server**: `Server.h/cpp` combines what was previously `ServerSync.h`/`ServerAsync.h` into a single class with dual-mode accept loop
- **Interface hierarchies**: `ClientSync`/`ClientAsync` (interfaces) and `ServerSync`/`ServerAsync` (interfaces) are now declared as interface classes within `ClientBase.h`/`ServerBase.h` respectively, inherited by the unified `Client`/`Server`
- **DualSocket pattern**: All sockets inherit from `DualSocket` which combines both sync and async interfaces - no need to track separate socket types

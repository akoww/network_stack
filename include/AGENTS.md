# include/ - Top-Level Include Directory

## Overview

Root directory for all public headers in the Network Stack library. Contains 5 subdirectories, each representing a major architectural layer.

## Directory Structure

- `core/`: Foundational utilities (error codes, context management)
- `socket/`: Socket interfaces and base classes
- `client/`: Client connection abstractions
- `server/`: Server connection handling
- `protocol/`: Higher-level protocol implementations (FTP)

## Dependencies

- All headers assume `#include "core/ErrorCodes.h"` before other project headers
- ASIO headers must come before project headers in include order
- TLS features require including `asio/ssl/` headers explicitly

## Public API

All headers in this tree and subdirectories constitute the public API. Each subdirectory has its own `AGENTS.md` with detailed guidance.

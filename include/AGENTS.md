# include/ - Public Headers

## Overview
Root directory for all public headers. Contains 5 subdirectories representing architectural layers.

## Dependencies
- Always include `"core/ErrorCodes.h"` before other project headers.
- ASIO headers must precede project headers.
- TLS requires explicit `asio/ssl/` includes.

## Architecture Notes
- **Unified Client/Server**: `Client` and `Server` implement both sync and async interfaces simultaneously. No separate `*Sync`/`*Async` classes exist.
- **DualSocket**: All sockets inherit from `DualSocket`, combining sync and async interfaces.
- See subdirectory `AGENTS.md` files for layer-specific API details.

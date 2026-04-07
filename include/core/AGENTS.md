# include/core/ - Core Infrastructure

## Purpose

Foundational utilities used throughout the entire stack: error handling and io_context management.

## Key Components

### ErrorCodes.h

- **`Network::Error`**: Custom error enum with values: `NoError`, `ConnectionRefused`, `ConnectionTimeout`, `ConnectionLost`, `DnsFailure`, `ProtocolError`
- **`Network::get_network_category()`**: Singleton error category for std::error_code integration
- **`make_error_code()`**: Converts `Network::Error` to `std::error_code` via ADL lookup
- **`is_error_code_enum<Network::Error>`**: Enables implicit conversion to `std::error_code`

### Context.h

- **`IoContextWrapper`**: Singleton wrapper around `asio::io_context` with background thread management
- **`start()`/`stop()`**: Manages worker thread lifecycle
- **`instance()`**: Static singleton accessor
- **Note**: Inherits from `asio::io_context`, allowing direct use of all ASIO methods

## Conventions

- No external dependencies beyond standard library and ASIO
- Singleton pattern used for `IoContextWrapper` to enable shared context across modules
- Error codes use 0 for success (standard error code convention)
- Thread-safe: `IoContextWrapper` manages its own synchronization

## Important Notes

- Always include `core/ErrorCodes.h` before other Network headers
- Error code enums start at 0 (reserved for NoError)
- `IoContextWrapper` automatically manages work guard to keep io_context running

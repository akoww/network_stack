# include/protocol/ - Protocol Implementations

## Key Components
- **`FileTransfer.h`**: `IAbstractFileTransfer` interface (`list`, `createDir`, `exists`, `remove`, `read`, `write`, `isDirectory`). Supports streaming via callbacks.
- **`FtpFileTransfer.h/cpp`**: FTP implementation of `IAbstractFileTransfer`. Auto-detects capabilities (MLST, SIZE, EPSV, etc.). Uses `Client::connect()` (explicit timeouts recommended).
- **`FtpUtils.h`**: `SmartDirectoryNavigator` for lexical-only, cross-platform path navigation (`ftpCd`, `ftpSelectDrive`).
- **`FileTransferUtils`**: Helpers `writeFromFile()` and `readToFile()` with progress callbacks.

## Conventions
- All operations return `std::expected<T, std::error_code>`.
- FTP uses the global `IoContextWrapper`, not a raw `asio::io_context`.
- Default timeouts: control channel 10s, data channel matches unless overridden.
- Current FTP implementation is plain FTP (non-TLS). Passive mode is default.

## Testing Quirks
- **FTP tests require external server**: `tests/fixtures/FtpServerFixture.h` starts `busybox tcpsvd` on port 2121. Not auto-started by CMake.

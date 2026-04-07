# include/protocol/ - Protocol Implementations

## Purpose

Higher-level network protocol implementations built on socket interfaces.

## Key Components

### FileTransfer.h

- **`IAbstractFileTransfer`**: Generic file transfer interface (not protocol-specific)
- **Core operations**: `list()`, `createDir()`, `exists()`, `remove()`, `read()`, `write()`, `isDirectory()`
- **Streaming**: Callback variants for `read()` and `write()` enable large file handling without full memory load
- **Data types**: `FileListData` (metadata), `TransferConfig` (timeout options)

### FtpFileTransfer.h

- **`FtpFileTransfer`**: FTP-specific implementation of `IAbstractFileTransfer`
- **Connection**: `connect(ConnectOptions)` with automatic capability detection (FEAT command)
- **Capabilities**: Detects MLST, NLST, SIZE, MDTM, EPSV, PASV, RNFR/RNTO support
- **FTP-specific**: CWD, PWD, PASV/EPSV, PASV/EPSV data channel setup, LIST/NLST/MLSD
- **FTP navigator**: Uses `DefaultFtpNavigator` for directory path navigation

### FtpUtils.h

- **`SmartDirectoryNavigator`**: Protocol-agnostic path navigation helper
- **Features**: Cross-platform (Unix/Windows drives), lexical-only (no filesystem access)
- **Interface**: `ftpCd()` for directory changes, `ftpSelectDrive()` for Windows drives
- **Usage**: Derive and implement FTP-specific directory commands

### Utility Functions

- **`FileTransferUtils::writeFromFile()`**: Upload local file to remote server with progress callback
- **`FileTransferUtils::readToFile()`**: Download remote file to local filesystem with progress callback

## Conventions

- Protocol classes implement `IAbstractFileTransfer` interface
- FTP uses `IoContextWrapper` (global io_context) not raw `asio::io_context`
- All operations return `std::expected<T, std::error_code>`
- Timeout defaults: control channel 10s, data channel same unless overridden

## Important Notes

- **FTP tests require external server**: `tests/fixtures/FtpServerFixture.h` starts `busybox tcpsvd` on port 2121
- **Non-TLS only**: Current FTP implementation is non-TLS (plain FTP, not FTPS)
- **Capability detection**: Server features auto-detected via FEAT command
- **Passive mode**: Default, but active mode supported via `use_passive` option
- **Navigation**: `SmartDirectoryNavigator` is lexical only - no actual filesystem access

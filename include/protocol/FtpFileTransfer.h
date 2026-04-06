#pragma once

#include "FileTransfer.h"
#include "FtpUtils.h"
#include "core/Context.h"
#include "socket/SyncSocketInterface.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace Network {

// fwd
class FtpFileTransfer;

/// @brief Default navigator implementation for FtpFileTransfer.
/// Implements FTP directory navigation by issuing CWD and PASV commands.
class DefaultFtpNavigator : public Utility::SmartDirectoryNavigator {
public:
  /// @brief Construct with parent FtpFileTransfer instance.
  explicit DefaultFtpNavigator(FtpFileTransfer *parent);

  /// @brief Issue FTP CWD command to change directory.
  std::expected<void, std::error_code> ftpCd(const std::string &dir) override;

  /// @brief Issue FTP command to select drive/root (Windows-specific).
  std::expected<void, std::error_code>
  ftpSelectDrive(const std::string &drive) override;

private:
  FtpFileTransfer *_parent;
};

/// @brief FTP implementation of IAbstractFileTransfer interface.
/// Provides file transfer operations over FTP protocol using ASIO coroutines.
/// @section connection Connect Example
/// ```cpp
/// asio::io_context io_ctx;
/// FtpFileTransfer ftp("ftp.example.com", 21, io_ctx);
/// auto result = ftp.connect({
///   .username = "user",
///   .password = "pass",
///   .use_passive = true
/// });
/// if (result) {
///   auto files = ftp.list("/remote/path");
///   // use files...
/// }
/// ```
/// @section capabilities FTP Capabilities Detection
/// The class automatically detects server capabilities via FEAT command and
/// uses advanced commands (MLST, EPSV) when available, falling back to
/// standard commands (LIST, PASV) otherwise.
class FtpFileTransfer : public IAbstractFileTransfer {
public:
  /// @brief FTP connection options.
  struct ConnectOptions {
    std::string username = "anonymous";
    std::string password = "anonymous@";
    std::chrono::milliseconds timeout = std::chrono::seconds(10);
    std::optional<std::chrono::milliseconds> data_timeout = std::nullopt;
    bool use_passive = false;
  };

  /// @brief Construct with host, port, and io_context.
  /// @param host FTP server hostname or IP address.
  /// @param port FTP server control port (default 21).
  /// @param io_ctx ASIO io_context for async operations.
  explicit FtpFileTransfer(std::string_view host, uint16_t port,
                           IoContextWrapper &io_ctx);
  ~FtpFileTransfer() override;

  /// @brief Connect to FTP server and perform initial setup.
  /// @param opts Connection options (credentials, timeout, mode).
  /// @return Success on connection and feature detection, error on failure.
  std::expected<void, std::error_code> connect(const ConnectOptions &opts);

  bool isAlive() const noexcept override;

  std::expected<std::vector<FileListData>, std::error_code>
  list(const std::filesystem::path &path) override;

  std::expected<void, std::error_code>
  createDir(const std::filesystem::path &path) override;

  std::expected<bool, std::error_code>
  exists(const std::filesystem::path &remote_path) override;

  std::expected<void, std::error_code>
  remove(const std::filesystem::path &remote_path) override;

  std::expected<std::vector<std::byte>, std::error_code>
  read(const std::filesystem::path &path) override;

  std::expected<std::size_t, std::error_code>
  read(const std::filesystem::path &path, ReadCallback callback) override;

  std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path,
        std::span<const std::byte> data) override;

  std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path,
        WriteCallback next) override;

  std::expected<bool, std::error_code>
  isDirectory(const std::filesystem::path &path) override;

  /// @brief FTP server capabilities detected during connect.
  /// These indicate which FTP commands the server supports.
  struct FtpCapabilities {
    // Listing commands
    bool mlst = false; // MLST - modern directory listing
    bool nlst = false; // NLST - simple name listing
    bool list = true;  // LIST - standard listing (assume fallback)

    // File info commands
    bool size = false; // SIZE - get file size
    bool mdtm = false; // MDTM - get modification time

    // File operations
    bool rename = false; // RNFR/RNTO - rename file

    // Connection modes
    bool epsv = false; // EPSV - extended passive mode
    bool pasv = true;  // PASV - passive mode

    // Server management
    bool feat = false; // FEAT - feature list
  };

protected:
  struct Answer {
    std::string full_msg;
    int code;
  };

  /// @brief Send command on control connection.
  std::expected<void, std::error_code> sendCommand(std::string_view cmd);
  /// @brief Receive response from control connection.
  std::expected<Answer, std::error_code> receiveResponse();

  std::expected<Answer, std::error_code>
  sendAndReceiveResponse(std::string_view cmd);

  // Static variants for data channel operations

  std::expected<void, std::error_code> sendCommand(SyncSocket &sock,
                                                    std::string_view cmd,
                                                    std::chrono::milliseconds timeout);
  std::expected<Answer, std::error_code> receiveResponse(SyncSocket &sock,
                                                          std::chrono::milliseconds timeout);
  std::expected<std::vector<std::byte>, std::error_code>
  receiveRawResponse(SyncSocket &sock, std::chrono::milliseconds timeout);

  std::expected<Answer, std::error_code>
  sendAndReceiveResponse(SyncSocket &sock, std::string_view cmd,
                          std::chrono::milliseconds timeout);

  /// @brief Open FTP data connection (directory listing or file transfer).
  /// Uses PASV or EPSV based on server capabilities and configuration.
  std::expected<std::unique_ptr<SyncSocket>, std::error_code>
  openDataConnection();

  /// @brief Navigate to directory using navigator.
  void navigateToDirectory(const std::filesystem::path &path);

  /// @brief Parse FEAT response to detect server capabilities.
  void parseFeatures(std::string_view feat_response);

  /// @brief Check if file exists using MLST (preferred).
  std::expected<bool, std::error_code> existsMlst(std::string_view name);
  /// @brief Check if file exists using CWD.
  std::expected<bool, std::error_code> existsCwd(std::string_view name);
  /// @brief Check if file exists using SIZE command.
  std::expected<bool, std::error_code> existsSize(std::string_view name);

  /// @brief Check if path is directory using MLST (preferred).
  std::expected<bool, std::error_code> isDirectoryMlst(std::string_view name);
  /// @brief Check if path is directory using CWD.
  std::expected<bool, std::error_code> isDirectoryCwd(std::string_view name);

  /// @brief Get data timeout (defaults to control timeout if not set).
  std::chrono::milliseconds getDataTimeout() const noexcept;

 private:
  std::string _host;
  uint16_t _port;
  DefaultFtpNavigator _navigator;

  IoContextWrapper &_io_context;
  std::unique_ptr<SyncSocket> _socket;
  ConnectOptions _options;
  FtpCapabilities _capabilities;

  friend DefaultFtpNavigator;
};

/// @brief Factory function to create FTP file transfer connection.
/// @param host FTP server hostname or IP address.
/// @param port FTP server control port (default 21).
/// @param io_ctx ASIO io_context for async operations.
/// @param opts Connection options (credentials, timeout, mode).
/// @return Unique pointer to IAbstractFileTransfer, or error code on failure.
std::expected<std::unique_ptr<IAbstractFileTransfer>, std::error_code>
openFtpConnection(std::string_view host, uint16_t port,
                  IoContextWrapper &io_ctx,
                  const FtpFileTransfer::ConnectOptions &opts = {});

} // namespace Network

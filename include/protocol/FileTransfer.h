#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>

namespace Network {

/// @brief Interface for file transfer operations over network protocols.
/// This interface provides methods to perform common file operations such as
/// listing files, reading/writing files, creating directories, and checking
/// existence on remote servers (e.g., FTP, SFTP).
/// @note All methods use std::expected for error handling. Implementations
/// must handle protocol-specific details.
/// @section ftp_usage FTP Example
/// ```cpp
/// asio::io_context io_ctx;
/// FtpFileTransfer ftp("example.com", 21, io_ctx);
/// auto result = ftp.connect({.username = "user", .password = "pass"});
/// if (result) {
///   auto files = ftp.list("/remote/path");
///   // use files...
/// }
/// ```
class IAbstractFileTransfer {
public:
  virtual ~IAbstractFileTransfer() = default;

  /// @brief Configuration for file transfer operations.
  struct TransferConfig {
    std::chrono::milliseconds timeout = std::chrono::seconds(3);
  };

  /// @brief FTP file list entry data.
  /// Contains metadata about a file or directory on the remote server.
  struct FileListData {
    /// @brief Last modification time of the file.
    std::chrono::system_clock::time_point date;

    /// @brief Size of the file in bytes.
    std::size_t size;

    /// @brief Name of the file (basename only).
    std::string file_name;

    /// @brief Full path of the file on the remote server.
    std::filesystem::path full_path;
  };

  /// @brief Check if the connection to the server is still alive.
  /// @return true if connection is active, false otherwise.
  virtual bool isAlive() const = 0;

  /// @brief List the contents of a remote directory.
  /// @param path Remote directory path.
  /// @return Vector of FileListData for each entry, or error code on failure.
  /// @note Returns only direct children; does not recurse into subdirectories.
  virtual std::expected<std::vector<FileListData>, std::error_code>
  list(const std::filesystem::path &path) = 0;

  /// @brief Create a directory on the remote server.
  /// @param path Directory path to create.
  /// @return Success if directory is created or already exists, error on failure.
  /// @note Creates only the final directory level; parent directories must exist.
  virtual std::expected<void, std::error_code>
  createDir(const std::filesystem::path &path) = 0;

  /// @brief Check if a file or directory exists on the remote server.
  /// @param remote_path Path to check.
  /// @return true if exists, false otherwise. Returns error code on failure.
  virtual std::expected<bool, std::error_code>
  exists(const std::filesystem::path &remote_path) = 0;

  /// @brief Remove a file or directory on the remote server.
  /// @param remote_path Path to remove.
  /// @return Success on deletion, error on failure.
  /// @note For directories, must be empty to succeed.
  virtual std::expected<void, std::error_code>
  remove(const std::filesystem::path &remote_path) = 0;

  /// @brief Read entire remote file into memory.
  /// @param path Remote file path.
  /// @return Vector containing file bytes, or error code on failure.
  virtual std::expected<std::vector<std::byte>, std::error_code>
  read(const std::filesystem::path &path) = 0;

  /// @brief Read remote file incrementally via callback.
  /// @param path Remote file path.
  /// @param callback Function called with each chunk of data.
  /// @return Total bytes processed on success, error code on failure.
  /// @note Callback receives spans of data; must fully consume each chunk.
  /// @note This method enables streaming large files without loading entirely into memory.
  using ReadCallback =
      std::function<std::expected<std::size_t, std::error_code>(
          std::span<const std::byte>)>;
  virtual std::expected<std::size_t, std::error_code>
  read(const std::filesystem::path &path, ReadCallback callback) = 0;

  /// @brief Write data from memory to remote file.
  /// @param remote_dst_path Destination path on remote server.
  /// @param data Data to write as span of bytes.
  /// @return FileListData containing metadata of written file, or error code.
  virtual std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path,
        std::span<const std::byte> data) = 0;

  /// @brief Write to remote file incrementally via callback.
  /// @param remote_dst_path Destination path on remote server.
  /// @param next Function called to provide next chunk of data.
  /// @return FileListData containing metadata of written file, or error code.
  /// @note Callback returns spans of data to write. Return empty span to finish.
  /// @note This method enables streaming data from files or other sources.
  using WriteCallback = std::function<
      std::expected<std::span<const std::byte>, std::error_code>()>;
  virtual std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path, WriteCallback next) = 0;

  /// @brief Check if a path refers to a directory on the remote server.
  /// @param path Remote path to check.
  /// @return true if directory, false if file or doesn't exist, error on failure.
  virtual std::expected<bool, std::error_code>
  isDirectory(const std::filesystem::path &path) = 0;

protected:
};

/// @brief Utility functions for common file transfer operations.
namespace FileTransferUtils {

/// @brief Callback type for progress updates during file transfers.
using ProgressCallback =
    std::function<void(const std::filesystem::path & /*file*/,
                       std::size_t /*transferred*/, std::size_t /*total*/)>;

/// @brief Upload a local file to the remote server.
/// @param transfer File transfer interface instance.
/// @param local_src_path Source path of the local file.
/// @param remote_dst_path Destination path on remote server.
/// @param progress Optional callback for progress updates.
/// @return FileListData with metadata of uploaded file, or error code.
std::expected<IAbstractFileTransfer::FileListData, std::error_code>
writeFromFile(IAbstractFileTransfer &transfer,
              const std::filesystem::path &local_src_path,
              const std::filesystem::path &remote_dst_path,
              ProgressCallback progress);

/// @brief Download a remote file to local filesystem.
/// @param transfer File transfer interface instance.
/// @param remote_src_path Source path on remote server.
/// @param local_dst_path Destination path on local filesystem.
/// @param progress Optional callback for progress updates.
/// @return Number of bytes transferred on success, error code on failure.
std::expected<std::size_t, std::error_code>
readToFile(IAbstractFileTransfer &transfer,
           const std::filesystem::path &remote_src_path,
           const std::filesystem::path &local_dst_path,
           ProgressCallback progress);

} // namespace FileTransferUtils
} // namespace Network

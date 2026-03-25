#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace Network {

/**
 * @class IAbstractFileTransfer
 * @brief Interface for file transfer operations over network protocols, such as
 * FTP and SFTP.
 *
 * This interface provides methods to perform common file operations, such as
 * listing files, reading and writing files, creating directories, and checking
 * the existence of files or directories on a remote server. The operations
 * leverage the `std::expected` type for error handling.
 *
 * Implementations of this interface are expected to handle the underlying
 * protocol-specific details for FTP, SFTP, or other network-based file transfer
 * protocols.
 */
class IAbstractFileTransfer {
public:
  virtual ~IAbstractFileTransfer() = default;

  struct TransferConfig {
    std::chrono::milliseconds timeout = std::chrono::seconds(3);
  };

  /**
   * @brief FTP file list properties.
   */
  struct FileListData {
    /**
     * @brief Last change date of an FTP file.
     */
    std::chrono::system_clock::time_point date;

    /**
     * @brief Current size if the FTP file.
     */
    std::size_t size;

    /**
     * @brief Name of the FTP file.
     */
    std::string file_name;

    /**
     * @brief full path of the file
     */
    std::filesystem::path full_path;
  };

  /**
   * @brief Checks if connection is still alive
   *
   * @return true connection is good
   * @return false connection is not good
   */
  virtual bool isAlive() const = 0;

  /**
   * @brief Lists the contents of a remote directory.
   * @param path The remote directory path.
   * @param opt_filter Optional extension filter that reduce the results and
   * communication
   * @return A vector of `Entry` objects representing the directory contents, or
   * an error code.
   */
  virtual std::expected<std::vector<FileListData>, std::error_code>
  list(const std::filesystem::path &path) = 0;

  /**
   * @brief Creates a directory on the remote server.
   * @param path The remote directory path to create.
   * @return Success if the directory is created (or already exists, depending
   * on server behavior), or an error code on failure. Note: This creates a
   * single directory level; intermediate parent directories are not created.
   */
  virtual std::expected<void, std::error_code>
  createDir(const std::filesystem::path &path) = 0;

  /**
   * @brief Checks if a file or directory exists on the remote server.
   * @param remote_path The remote file or directory path.
   * @return True if the file or directory exists, false otherwise. Returns an
   * error code if the check fails.
   */
  virtual std::expected<bool, std::error_code>
  exists(const std::filesystem::path &remote_path) = 0;

  /**
   * @brief Removes a file or directory on the remote server.
   * @param remote_path The path to the file or directory to remove.
   * @return Success or an error code.
   */
  virtual std::expected<void, std::error_code>
  remove(const std::filesystem::path &remote_path) = 0;

  /**
   * @brief Reads the contents of a remote file into a vector.
   * @param path The path to the remote file.
   * @return A vector containing the file data, or an error code.
   */
  virtual std::expected<std::vector<std::byte>, std::error_code>
  read(const std::filesystem::path &path) = 0;

  /**
   * @brief Reads the contents of a remote file incrementally by chunks using a
   * callback.
   *
   * This method enables reading a remote file in chunks via a callback for
   * customized handling of the data being read. Each call to the callback
   * provides a span of bytes starting from a given offset for processing.
   *
   * @param path The path to the remote file on the server.
   * @param callback A function called to process data as it is read from the
   * remote file.
   *   - Argument: offset - the offset in the file where the reading starts.
   *   - Returns: A span of bytes representing the data read from the file or an
   * error code.
   *
   * @return Returns `std::expected<uint8_t, std::error_code>` indicating the
   * success or failure of the operation. On success, it may return the number
   * of bytes processed (or an appropriate indicator determined by
   * implementation).
   */
  using ReadCallback =
      std::function<std::expected<std::size_t, std::error_code>(
          std::span<std::byte>)>;
  virtual std::expected<std::size_t, std::error_code>
  read(const std::filesystem::path &path, ReadCallback callback) = 0;

  /**
   * @brief Writes raw data from memory to the remote server.
   * @param data The data to write as a span of bytes.
   * @param remote_dst_path The destination path on the remote server.
   * @return An `Entry` object with attributes of the created file, or an error
   * code.
   */
  virtual std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path,
        std::span<const std::byte> data) = 0;

  /**
   * @brief Writes to a remote file from chunks provided by the callback
   * function.
   *
   * This method enables writing to a file on the remote server incrementally by
   * requesting chunks of data from the provided callback function. Each call to
   * the callback is expected to return a span of bytes to write.
   *
   * @param remote_dst_path The destination path of the file on the remote
   * server.
   * @param next A function providing the next chunk of data to write.
   *   - Returns: A span of bytes containing data to be written, or an error
   * code.
   *
   * @return Returns `std::expected<FileListData, std::error_code>` containing
   * metadata about the newly created file on success, or an error code on
   * failure.
   */
  using WriteCallback = std::function<
      std::expected<std::span<const std::byte>, std::error_code>()>;
  virtual std::expected<FileListData, std::error_code>
   write(const std::filesystem::path &remote_dst_path, WriteCallback next) = 0;

   /**
    * @brief Checks if a path is a directory on the remote server.
    * @param path The remote path to check.
    * @return True if path is a directory, false if it's a file or doesn't exist.
    * Returns an error code if the check fails.
    */
   virtual std::expected<bool, std::error_code>
   isDirectory(const std::filesystem::path &path) = 0;

 protected:
};

namespace FileTransferUtils {

using ProgressCallback =
    std::function<void(const std::filesystem::path & /*file*/,
                       std::size_t /*transferred*/, std::size_t /*total*/)>;

/**
 * @brief Writes a local file to the remote server.
 * @param local_src_path The source path of the local file.
 * @param remote_dst_path The destination path on the remote server.
 * @param progress The progress callback to track the file transfer.
 * @return An `Entry` object with attributes of the created file, or an error
 * code.
 */
std::expected<IAbstractFileTransfer::FileListData, std::error_code>
writeFromFile(IAbstractFileTransfer &transfer,
              const std::filesystem::path &local_src_path,
              const std::filesystem::path &remote_dst_path,
              ProgressCallback progress);

/**
 * @brief Reads a remote file and saves it to a local file.
 * @param remote_src_path The source path of the file on the remote server.
 * @param local_dst_path The destination path of the file on the local
 * filesystem.
 * @param progress The progress callback to track the file transfer.
 * @return The number of bytes transferred, or an error code.
 */
std::expected<std::size_t, std::error_code>
readToFile(IAbstractFileTransfer &transfer,
           const std::filesystem::path &remote_src_path,
           const std::filesystem::path &local_dst_path,
           ProgressCallback progress);

} // namespace FileTransferUtils
} // namespace Network
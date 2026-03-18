#pragma once

#include "FileTransfer.h"
#include "socket/TcpSocket.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace Network {

class FtpFileTransfer : public IAbstractFileTransfer {
public:
  struct ConnectOptions {
    std::string username = "anonymous";
    std::string password = "anonymous@";
    std::chrono::milliseconds timeout = std::chrono::seconds(10);
    bool use_passive = false;
  };

  explicit FtpFileTransfer(std::string_view host, uint16_t port,
                           asio::io_context &io_ctx);
  ~FtpFileTransfer() override;

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

  std::expected<std::vector<uint8_t>, std::error_code>
  read(const std::filesystem::path &path) override;

  std::expected<std::size_t, std::error_code>
  read(const std::filesystem::path &path, ReadCallback callback) override;

  std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path,
        std::span<uint8_t> data) override;

  std::expected<FileListData, std::error_code>
  write(const std::filesystem::path &remote_dst_path,
        WriteCallback next) override;

private:
   std::expected<std::string, std::error_code> sendCommand(const std::string &cmd,
                                                           int expected_code = -1);
  std::expected<int, std::error_code> receiveResponse();
  std::expected<void, std::error_code>
  ensureDirectory(const std::filesystem::path &path);
  std::expected<std::unique_ptr<TcpSocket>, std::error_code>
  openDataConnection();

  asio::ip::tcp::endpoint parsePasvResponse(const std::string &response);
  void navigateToDirectory(const std::filesystem::path &path);

  std::string _host;
  uint16_t _port;
  asio::io_context &_io_context;
  std::unique_ptr<TcpSocket> _socket;
  std::filesystem::path _current_dir;
  ConnectOptions _options;
};

std::expected<std::unique_ptr<IAbstractFileTransfer>, std::error_code>
openFtpConnection(std::string_view host, uint16_t port,
                  asio::io_context &io_ctx,
                  const FtpFileTransfer::ConnectOptions &opts = {});
} // namespace Network

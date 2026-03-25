#pragma once

#include "FileTransfer.h"
#include "FtpUtils.h"
#include "socket/TcpSocket.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace Network {

class FtpFileTransfer;

class DefaultFtpNavigator : public Utility::SmartDirectoryNavigator {
public:
  DefaultFtpNavigator(FtpFileTransfer *parent);

  std::expected<void, std::error_code> ftpCd(const std::string &dir) override;
  std::expected<void, std::error_code>
  ftpSelectDrive(const std::string &drive) override;

private:
  FtpFileTransfer *_parent;
};

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

 protected:
  struct FtpCapabilities {
    // Listing
    bool mlst = false;
    bool nlst = false;
    bool list = true; // assume fallback

    // File info
    bool size = false;
    bool mdtm = false;

    // File ops
    bool rename = false;

    // Connection
    bool epsv = false;
    bool pasv = true;

    // Misc
    bool feat = false;
  };

private:
  //--------------------

  struct Answer {
    std::string full_msg;
    int code;
  };

  std::expected<void, std::error_code> sendCommand(std::string_view cmd);
  std::expected<Answer, std::error_code> receiveResponse();

  std::expected<Answer, std::error_code>
  sendAndReceiveResponse(std::string_view cmd);

  // static variants for data channel

  std::expected<void, std::error_code> sendCommand(TcpSocket &sock,
                                                   std::string_view cmd);
  std::expected<Answer, std::error_code> receiveResponse(TcpSocket &sock);
  std::expected<std::vector<std::byte>, std::error_code>
  receiveRawResponse(TcpSocket &sock);

  std::expected<Answer, std::error_code>
  sendAndReceiveResponse(TcpSocket &sock, std::string_view cmd);

  //--------------------

   std::expected<std::unique_ptr<TcpSocket>, std::error_code>
   openDataConnection();

   std::optional<asio::ip::tcp::endpoint>
   parsePasvResponse(std::string_view response) const;

   void navigateToDirectory(const std::filesystem::path &path);

   void parseFeatures(std::string_view feat_response);

   std::expected<bool, std::error_code> existsMlst(std::string_view name);
   std::expected<bool, std::error_code> existsCwd(std::string_view name);
   std::expected<bool, std::error_code> existsSize(std::string_view name);

    std::expected<bool, std::error_code>
    isDirectory(const std::filesystem::path &path) override;

    std::expected<bool, std::error_code> isDirectoryMlst(std::string_view name);
   std::expected<bool, std::error_code> isDirectoryCwd(std::string_view name);

   std::string _host;
  uint16_t _port;
  DefaultFtpNavigator _navigator;

  asio::io_context &_io_context;
  std::unique_ptr<TcpSocket> _socket;
  ConnectOptions _options;
  FtpCapabilities _capabilities;

  friend DefaultFtpNavigator;
};

std::expected<std::unique_ptr<IAbstractFileTransfer>, std::error_code>
openFtpConnection(std::string_view host, uint16_t port,
                  asio::io_context &io_ctx,
                  const FtpFileTransfer::ConnectOptions &opts = {});
} // namespace Network

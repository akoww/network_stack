#include "protocol/FtpFileTransfer.h"

#include "client/ClientSync.h"
#include "socket/TcpSocket.h"
#include "core/ErrorCodes.h"

#include <asio/ip/tcp.hpp>
#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <system_error>
#include <spdlog/spdlog.h>

#include <array>
#include <string>
#include <vector>

namespace Network {

using FileListData = IAbstractFileTransfer::FileListData;

namespace {

} // namespace

FtpFileTransfer::FtpFileTransfer(std::string_view host, uint16_t port, asio::io_context& io_ctx)
    : _host(host), _port(port), _io_context(io_ctx), _current_dir("/") {}

FtpFileTransfer::~FtpFileTransfer() = default;

std::expected<void, std::error_code> FtpFileTransfer::connect(const ConnectOptions& opts) {
    spdlog::info("FTP connecting to {}:{}", _host, _port);

    ClientSync client(_host, _port, _io_context);
    auto socket_result = client.connect({opts.timeout});
    
    if (!socket_result) {
        spdlog::error("FTP control connection failed: {}", socket_result.error().message());
        return std::unexpected(socket_result.error());
    }

    _socket = std::move(*socket_result);

    std::error_code ec;

    // Wait for FTP server greeting (220 response)
    std::array<std::byte, 512> buffer{};
    auto recv_result = _socket->receive(std::span(buffer));
    
    if (!recv_result) {
        spdlog::error("Failed to receive FTP greeting: {}", recv_result.error().message());
        return std::unexpected(recv_result.error());
    }

    auto greeting = std::string_view(reinterpret_cast<const char*>(buffer.data()), *recv_result);
    spdlog::debug("FTP greeting: {}", greeting);

    if (greeting.substr(0, 3) != "220") {
        spdlog::warn("Expected 220 greeting, got: {}", greeting);
        // Continue anyway - some servers don't send greeting
    }

    // Send USER command
    if (!opts.username.empty()) {
        auto cmd_result = sendCommand("USER " + opts.username, -1);
        if (!cmd_result) {
            spdlog::error("USER command failed: {}", cmd_result.error().message());
            _socket.reset();
            return std::unexpected(cmd_result.error());
        }
        
        // USER returns 331 (need password) or 230 (logged in)
        int user_response_code = std::stoi(cmd_result->substr(0, 3));
        if (user_response_code != 331 && user_response_code != 230) {
            spdlog::error("USER command returned unexpected code: {}", user_response_code);
            _socket.reset();
            return std::unexpected(make_error_code(Network::Error::ProtocolError));
        }
    }

    // Send PASS command
    if (!opts.password.empty()) {
        auto cmd_result = sendCommand("PASS " + opts.password, -1);
        if (!cmd_result) {
            spdlog::error("PASS command failed: {}", cmd_result.error().message());
            _socket.reset();
            return std::unexpected(cmd_result.error());
        }

        int pass_response_code = std::stoi(cmd_result->substr(0, 3));
        if (pass_response_code != 230 && pass_response_code != 231) {
            spdlog::error("PASS command returned unexpected code: {}", pass_response_code);
            _socket.reset();
            return std::unexpected(make_error_code(Network::Error::ProtocolError));
        }
    }

    spdlog::info("FTP connected and authenticated successfully");
    return {};
}

bool FtpFileTransfer::isAlive() const noexcept {
    return _socket && _socket->is_connected();
}

std::expected<std::string, std::error_code> FtpFileTransfer::sendCommand(const std::string& cmd, int expected_code) {
    // Send command with CRLF
    std::string command = cmd + "\r\n";
    auto send_result = _socket->send(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(command.data()), command.size()));
    
    if (!send_result) {
        spdlog::error("Failed to send command '{}': {}", cmd, send_result.error().message());
        return std::unexpected(send_result.error());
    }

    // Receive response
    auto response_result = receiveResponse();
    if (!response_result) {
        spdlog::error("Failed to receive response for command '{}': {}", cmd, response_result.error().message());
        return std::unexpected(response_result.error());
    }

    int response_code = *response_result;

    if (expected_code != -1 && response_code != expected_code) {
        spdlog::error("Command '{}' failed: expected code {} got {}", cmd, expected_code, response_code);
        return std::unexpected(make_error_code(Network::Error::ProtocolError));
    }

    spdlog::debug("Command '{}' response: {}", cmd, response_code);
    return std::to_string(response_code);
}

std::expected<int, std::error_code> FtpFileTransfer::receiveResponse() {
    std::string response;
    std::array<std::byte, 1024> buffer{};

    bool complete = false;
    while (!complete) {
        auto recv_result = _socket->receive(std::span(buffer));
        if (!recv_result) {
            spdlog::error("Failed to receive FTP response: {}", recv_result.error().message());
            return std::unexpected(recv_result.error());
        }

        if (*recv_result == 0) {
            spdlog::error("Connection closed while receiving response");
            return std::unexpected(make_error_code(Network::Error::ConnectionLost));
        }

        std::string chunk(reinterpret_cast<const char*>(buffer.data()), *recv_result);
        response += chunk;

        // Check if we have a complete response
        // FTP response ends with \r\n
        // Multi-line responses have format: code-text\r\n ... \r\n code-text\r\n
        // where the last line has code followed by space or hyphen
        size_t pos = response.find("\r\n");
        if (pos != std::string::npos) {
            // Look for the last line to determine if multi-line
            size_t last_crlf = response.rfind("\r\n");
            if (last_crlf != std::string::npos && last_crlf + 2 < response.size()) {
                // Check if it's a multi-line response (code- vs code )
                size_t code_end = response.find(' ', last_crlf);
                if (code_end == std::string::npos) code_end = response.find('-', last_crlf);
                
                if (code_end != std::string::npos) {
                    char sep = response[code_end];
                    if (sep == '-') {
                        // Multi-line response, continue reading
                        continue;
                    }
                }
            }
            complete = true;
        }
    }

    // Remove trailing CRLF
    while (!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
        response.pop_back();
    }

    // Parse the 3-digit response code
    if (response.size() < 3) {
        spdlog::error("Invalid FTP response (too short): {}", response);
        return std::unexpected(make_error_code(Network::Error::ProtocolError));
    }

    int response_code = std::stoi(response.substr(0, 3));
    return response_code;
}

std::expected<std::vector<FileListData>, std::error_code> FtpFileTransfer::list(const std::filesystem::path& path) {
    (void)path; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<void, std::error_code> FtpFileTransfer::createDir(const std::filesystem::path& path) {
    (void)path; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<bool, std::error_code> FtpFileTransfer::exists(const std::filesystem::path& remote_path) {
    (void)remote_path; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<void, std::error_code> FtpFileTransfer::remove(const std::filesystem::path& remote_path) {
    (void)remote_path; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<std::vector<uint8_t>, std::error_code> FtpFileTransfer::read(const std::filesystem::path& path) {
    (void)path; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<std::size_t, std::error_code> FtpFileTransfer::read(const std::filesystem::path& path,
                                                                   ReadCallback callback) {
    (void)path; // unused
    (void)callback; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<FileListData, std::error_code> FtpFileTransfer::write(const std::filesystem::path& remote_dst_path,
                                                                      std::span<uint8_t> data) {
    (void)remote_dst_path; // unused
    (void)data; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<FileListData, std::error_code> FtpFileTransfer::write(const std::filesystem::path& remote_dst_path,
                                                                      WriteCallback next) {
    (void)remote_dst_path; // unused
    (void)next; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<void, std::error_code> FtpFileTransfer::ensureDirectory(const std::filesystem::path& path) {
    (void)path; // unused
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<std::unique_ptr<TcpSocket>, std::error_code> FtpFileTransfer::openDataConnection() {
    return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

asio::ip::tcp::endpoint FtpFileTransfer::parsePasvResponse(const std::string& response) {
    (void)response; // unused
    return asio::ip::tcp::endpoint{};
}

void FtpFileTransfer::navigateToDirectory(const std::filesystem::path& path) {
    (void)path; // unused
}

std::expected<std::unique_ptr<IAbstractFileTransfer>, std::error_code>
openFtpConnection(std::string_view host, uint16_t port, asio::io_context& io_ctx, 
                  const FtpFileTransfer::ConnectOptions& opts) {
    auto ftp = std::make_unique<FtpFileTransfer>(host, port, io_ctx);
    auto connect_result = ftp->connect(opts);
    
    if (!connect_result) {
        return std::unexpected(connect_result.error());
    }

    return ftp;
}

} // namespace Network

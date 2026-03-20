#include "protocol/FtpFileTransfer.h"

#include "client/ClientSync.h"
#include "core/ErrorCodes.h"
#include "socket/TcpSocket.h"

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>
#include <expected>
#include <spdlog/spdlog.h>
#include <system_error>

#include <array>
#include <map>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

namespace Network {

DefaultFtpNavigator::DefaultFtpNavigator(FtpFileTransfer *parent)
    : SmartDirectoryNavigator("/"), _parent(parent) {}

std::expected<void, std::error_code>
DefaultFtpNavigator::ftpCd(const std::string &dir) {
  spdlog::info("cwd {}", dir);
  auto cmd_result = _parent->sendAndReceiveResponse(std::format("CWD {}", dir));
  if (!cmd_result) {
    spdlog::warn("Failed to change directory to '{}': {}", dir,
                 cmd_result.error().message());
    return std::unexpected(cmd_result.error());
  } else if (cmd_result->code != 250) {
    spdlog::warn("Failed to change directory to '{}': code {}", dir,
                 cmd_result->code);
    return std::unexpected(cmd_result.error());
  }
  return {};
}

std::expected<void, std::error_code>
DefaultFtpNavigator::ftpSelectDrive(const std::string &drive) {
  auto cmd_result =
      _parent->sendAndReceiveResponse(std::format("CWD {}", drive));
  if (!cmd_result) {
    spdlog::warn("Failed to change directory to '{}': {}", drive,
                 cmd_result.error().message());
    return std::unexpected(cmd_result.error());
  } else if (cmd_result->code != 250) {
    spdlog::warn("Failed to change directory to '{}': Code {}", drive,
                 cmd_result->code);
    return std::unexpected(cmd_result.error());
  }
  return {};
}

using FileListData = IAbstractFileTransfer::FileListData;

namespace {

template <typename Container> auto as_byte_span(const Container &c) {
  using T = std::ranges::range_value_t<Container>;

  static_assert(sizeof(T) == 1, "Container must hold 1-byte elements");
  static_assert(std::ranges::contiguous_range<Container>,
                "Container must be contiguous");

  return std::span<const std::byte>(
      reinterpret_cast<const std::byte *>(std::ranges::data(c)),
      std::ranges::size(c));
}

std::filesystem::path get_last_component(const std::filesystem::path &p) {
  if (p.filename().empty()) {
    return p.parent_path().filename();
  }
  return p.filename();
}

} // namespace

FtpFileTransfer::FtpFileTransfer(std::string_view host, uint16_t port,
                                 asio::io_context &io_ctx)
    : _host(host), _port(port), _navigator(this), _io_context(io_ctx) {}

FtpFileTransfer::~FtpFileTransfer() = default;

std::expected<void, std::error_code>
FtpFileTransfer::connect(const ConnectOptions &opts) {
  spdlog::info("FTP connecting to {}:{}", _host, _port);

  ClientSync client(_host, _port, _io_context);
  auto socket_result = client.connect({opts.timeout});

  if (!socket_result) {
    spdlog::error("FTP control connection failed: {}",
                  socket_result.error().message());
    return std::unexpected(socket_result.error());
  }

  _socket = std::move(*socket_result);

  std::error_code ec;

  // Wait for FTP server greeting (220 response)
  std::array<std::byte, 512> buffer{};
  auto recv_result = _socket->receive(std::span(buffer));

  if (!recv_result) {
    spdlog::error("Failed to receive FTP greeting: {}",
                  recv_result.error().message());
    return std::unexpected(recv_result.error());
  }

  auto greeting = std::string_view(
      reinterpret_cast<const char *>(buffer.data()), *recv_result);
  spdlog::debug("FTP greeting: {}", greeting);

  if (greeting.substr(0, 3) != "220") {
    spdlog::warn("Expected 220 greeting, got: {}", greeting);
    // Continue anyway - some servers don't send greeting
  }

  // Send USER command
  if (!opts.username.empty()) {
    auto cmd_result = sendAndReceiveResponse("USER " + opts.username);
    if (!cmd_result) {
      spdlog::error("USER command failed: {}", cmd_result.error().message());
      _socket.reset();
      return std::unexpected(cmd_result.error());
    }

    // USER returns 331 (need password) or 230 (logged in)
    int user_response_code = cmd_result->code;
    if (user_response_code != 331 && user_response_code != 230) {
      spdlog::error("USER command returned unexpected code: {}",
                    user_response_code);
      _socket.reset();
      return std::unexpected(make_error_code(Network::Error::ProtocolError));
    }
  }

  // Send PASS command
  if (!opts.password.empty()) {
    auto cmd_result = sendAndReceiveResponse("PASS " + opts.password);
    if (!cmd_result) {
      spdlog::error("PASS command failed: {}", cmd_result.error().message());
      _socket.reset();
      return std::unexpected(cmd_result.error());
    }

    int pass_response_code = cmd_result->code;
    if (pass_response_code != 230 && pass_response_code != 231) {
      spdlog::error("PASS command returned unexpected code: {}",
                    pass_response_code);
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

namespace {

bool isCompleteFtpResponse(const std::vector<std::string> &lines) {
  if (lines.empty())
    return false;

  const auto &first = lines.front();
  if (first.size() < 4 || !std::isdigit(first[0]) || !std::isdigit(first[1]) ||
      !std::isdigit(first[2]))
    return false;

  const std::string code = first.substr(0, 3);

  if (first[3] == ' ')
    return true; // single-line
  if (first[3] != '-')
    return false; // invalid

  for (const auto &l : lines)
    if (l.size() >= 4 && l.substr(0, 3) == code && l[3] == ' ')
      return true;

  return false;
}

void extractLines(std::string &buf, std::vector<std::string> &out) {
  size_t pos;
  while ((pos = buf.find("\r\n")) != std::string::npos) {
    out.emplace_back(buf.substr(0, pos));
    buf.erase(0, pos + 2);
  }
}

} // namespace

std::expected<void, std::error_code>
FtpFileTransfer::sendCommand(std::string_view cmd) {
  return sendCommand(*_socket, cmd); // Success
}

std::expected<FtpFileTransfer::Answer, std::error_code>
FtpFileTransfer::receiveResponse() {
  return receiveResponse(*_socket);
}

std::expected<FtpFileTransfer::Answer, std::error_code>
FtpFileTransfer::sendAndReceiveResponse(std::string_view cmd) {
  return sendAndReceiveResponse(*_socket, cmd);
}

//----

std::expected<void, std::error_code>
FtpFileTransfer::sendCommand(TcpSocket &sock, std::string_view cmd) {
  const std::string command = std::format("{}\r\n", cmd);
  if (auto r = sock.send(as_byte_span(command)); !r) {
    spdlog::error("Failed to send command '{}': {}", cmd, r.error().message());
    return std::unexpected(r.error());
  }
  return {}; // Success
}

std::expected<FtpFileTransfer::Answer, std::error_code>
FtpFileTransfer::receiveResponse(TcpSocket &sock) {

  std::string buf;
  std::vector<std::string> lines;
  std::array<std::byte, 1024> tmp{};

  while (true) {
    auto r = sock.receive(std::span(tmp));
    if (!r) {
      spdlog::error("Failed to receive FTP response: {}", r.error().message());
      return std::unexpected(r.error());
    }
    if (*r == 0) {
      spdlog::error("Connection closed while receiving response");
      return std::unexpected(make_error_code(Network::Error::ConnectionLost));
    }
    buf.append(reinterpret_cast<const char *>(tmp.data()), *r);
    extractLines(buf, lines);
    if (isCompleteFtpResponse(lines))
      break;
  }

  std::string full_msg;
  for (size_t i = 0; i < lines.size(); ++i)
    full_msg += lines[i] + (i + 1 < lines.size() ? "\n" : "");

  if (full_msg.size() < 3)
    return std::unexpected(make_error_code(Network::Error::ProtocolError));

  int code = std::stoi(full_msg.substr(0, 3));

  spdlog::debug("Response code: {}", code);

  return Answer{.full_msg = full_msg, .code = code};
}

std::expected<std::string, std::error_code>
FtpFileTransfer::receiveRawResponse(TcpSocket &sock) {

  std::string buf;
  std::array<std::byte, 1024> tmp{};

  while (true) {
    auto r = sock.receive(std::span(tmp));
    if (!r) {
      spdlog::error("Failed to receive FTP response: {}", r.error().message());
      return std::unexpected(r.error());
    }
    if (*r == 0) {
      break;
    }
    buf.append(reinterpret_cast<const char *>(tmp.data()), *r);
  }
  return buf;
}

std::expected<FtpFileTransfer::Answer, std::error_code>
FtpFileTransfer::sendAndReceiveResponse(TcpSocket &sock, std::string_view cmd) {

  if (auto send_result = sendCommand(sock, cmd)) {
    return std::unexpected(send_result.error());
  }

  return receiveResponse(sock);
}

namespace {

std::chrono::system_clock::time_point
parseFtpTimestamp(const std::string &month, const std::string &day,
                  const std::string &time_or_year) {

  // Map month names to month numbers (0-11)
  static const std::map<std::string, int> monthMap = {
      {"Jan", 0}, {"Feb", 1}, {"Mar", 2}, {"Apr", 3}, {"May", 4},  {"Jun", 5},
      {"Jul", 6}, {"Aug", 7}, {"Sep", 8}, {"Oct", 9}, {"Nov", 10}, {"Dec", 11}};

  // Find and validate month
  auto it = monthMap.find(month);
  if (it == monthMap.end()) {
    throw std::runtime_error("Invalid month name: " + month);
  }
  int monthNum = it->second;

  // Parse day (strip any leading spaces)
  int dayNum = std::stoi(day);
  if (dayNum < 1 || dayNum > 31) {
    throw std::runtime_error("Invalid day: " + day);
  }

  // Get current year for timestamps without year
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_now = *std::localtime(&now_time_t);
  int currentYear = tm_now.tm_year + 1900;

  int year, hour, minute;

  // Determine if we have time (HH:MM) or year (YYYY)
  if (time_or_year.find(':') != std::string::npos) {
    // Format: HH:MM - use current year (modified within last 6 months)
    size_t colonPos = time_or_year.find(':');
    hour = std::stoi(time_or_year.substr(0, colonPos));
    minute = std::stoi(time_or_year.substr(colonPos + 1));
    year = currentYear;
  } else {
    // Format: YYYY - use given year, set time to midnight
    year = std::stoi(time_or_year);
    hour = 0;
    minute = 0;
  }

  // Build tm structure
  std::tm tm = {};
  tm.tm_year = year - 1900;
  tm.tm_mon = monthNum;
  tm.tm_mday = dayNum;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = 0;
  tm.tm_isdst = -1; // Let mktime determine DST

  // Convert to time_t
  std::time_t time_t_value = std::mktime(&tm);
  if (time_t_value == static_cast<std::time_t>(-1)) {
    throw std::runtime_error("Invalid date/time combination");
  }

  return std::chrono::system_clock::from_time_t(time_t_value);
}

std::optional<FileListData> parseFtpLine(std::string_view line) {
  if (line.empty())
    return std::nullopt;

  std::istringstream iss(std::string(line), std::ios_base::in);
  std::string perms, link_count, owner, group, month, day, time_or_year;
  uint64_t size;

  // Read fixed fields. If this fails, the line is not standard ls -l format.
  if (!(iss >> perms >> link_count >> owner >> group >> size >> month >> day >>
        time_or_year)) {
    spdlog::warn("Failed to parse FTP LIST line: {}", line);
    return std::nullopt;
  }

  FileListData file;
  file.size = size;

  // 1. Extract Filename
  // The filename is everything remaining on the line.
  // iss >> will skip leading whitespace automatically.
  std::getline(iss, file.file_name);

  // Trim trailing whitespace from filename
  while (!file.file_name.empty() &&
         std::isspace(static_cast<unsigned char>(file.file_name.back()))) {
    file.file_name.pop_back();
  }
  // Trim leading whitespace (getline might pick it up if there were double
  // spaces)
  while (!file.file_name.empty() &&
         std::isspace(static_cast<unsigned char>(file.file_name.front()))) {
    file.file_name.erase(0, 1);
  }

  if (file.file_name.empty()) {
    spdlog::warn("Parsed file name is empty for line: {}", line);
    return std::nullopt;
  }

  // 2. Parse Date (Simplified)
  // NOTE: Full FTP timestamp parsing is complex (differentiates between 'year'
  // and 'time'). This is a placeholder implementation to demonstrate fixing
  // 'file.date = now()'. In production, use a robust date parser library or
  // specific logic based on time_or_year format.
  file.date = parseFtpTimestamp(month, day, time_or_year);

  return file;
}

auto splitLines(std::string_view lines) {
  return std::views::split(lines, '\n') |
         std::views::transform([](auto &&part) {
           std::string_view s(part); // C++23 range ctor
           while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
             s.remove_suffix(1);
           return s;
         }) |
         std::views::filter([](const auto &l) { return !l.empty(); });
}

} // namespace

std::expected<std::vector<FileListData>, std::error_code>
FtpFileTransfer::list(const std::filesystem::path &path) {

  // 1. Setup
  if (auto result = _navigator.changeDirectory(path); !result) {
    return std::unexpected(result.error());
  }

  auto data_socket_result = openDataConnection();
  if (!data_socket_result) {
    spdlog::error("Failed to open data connection for LIST: {}",
                  data_socket_result.error().message());
    return std::unexpected(data_socket_result.error());
  }

  auto data_socket = std::move(*data_socket_result);

  auto cmd_result = sendAndReceiveResponse("LIST");
  if (!cmd_result) {
    spdlog::error("LIST command failed: {}", cmd_result.error().message());
    return std::unexpected(cmd_result.error());
  }

  auto list_result = receiveRawResponse(*data_socket);
  if (!list_result) {
    spdlog::error("LIST command line did not open");
    return std::unexpected(list_result.error());
  }

  // 2. Receive and Parse
  std::vector<FileListData> files;
  for (auto line : splitLines(*list_result)) {

    if (auto file_data = parseFtpLine(line)) {
      files.push_back(*file_data);
    }
  }

  return files; // Implicit conversion to expected<std::vector, error_code>
}

std::expected<void, std::error_code>
FtpFileTransfer::createDir(const std::filesystem::path &path) {

  // naviagte to parent path
  if (auto change_result = _navigator.changeDirectory(path.parent_path());
      !change_result) {
    spdlog::error("cant change directory to: {}", path.parent_path().string());
    return std::unexpected(change_result.error());
  }

  auto cmd_result = sendAndReceiveResponse(
      std::format("MKD {}", get_last_component(path).string()));
  if (!cmd_result) {
    spdlog::error("MKD command failed for '{}': {}", path.string(),
                  cmd_result.error().message());
    return std::unexpected(cmd_result.error());
  } else if (cmd_result->code != 257) {
    spdlog::warn("Can't create dir '{}' Code: '{}'", path.string(),
                 cmd_result->code);
    return std::unexpected(cmd_result.error());
  }
  return {};
}

std::expected<bool, std::error_code>
FtpFileTransfer::exists(const std::filesystem::path &remote_path) {

  // naviagte to parent path
  if (auto change_result =
          _navigator.changeDirectory(remote_path.parent_path());
      !change_result) {
    spdlog::error("cant change directory to: {}",
                  remote_path.parent_path().string());
    return std::unexpected(change_result.error());
  }

  auto connection_result = openDataConnection();
  if (!connection_result) {
    spdlog::error("cant open data connection");
    return std::unexpected(connection_result.error());
  }

  auto data_connection = std::move(*connection_result);

  auto cmd_result = sendAndReceiveResponse(
      "NLST " + get_last_component(remote_path).string());
  if (!cmd_result) {
    spdlog::error("NLST command failed: {}", cmd_result.error().message());
    return std::unexpected(cmd_result.error());
  }

  int response_code = cmd_result->code;
  if (response_code == 550 || response_code == 553) {
    return false;
  }

  bool exists = false;

  // check if file exists there
  std::array<std::byte, 2048> exist_buffer;
  std::string_view exist_str;
  if (auto exist_result = data_connection->receive(std::span(exist_buffer));
      !exist_result) {
    // maybe not an error?
    spdlog::error("cant read on data connection");
    // return std::unexpected(exist_result.error());
  } else {
    exist_str = std::string_view(
        reinterpret_cast<const char *>(exist_buffer.data()), *exist_result);
    exists = true;
  }

  data_connection.reset();

  std::array<std::byte, 2048> tmp_buffer;
  std::string_view close_str;
  if (auto close_result = _socket->receive(std::span(tmp_buffer));
      !close_result) {
    spdlog::error("cant close on data connection");
    return std::unexpected(close_result.error());
  } else {
    close_str = std::string_view(
        reinterpret_cast<const char *>(tmp_buffer.data()), *close_result);
  }
  return exists;
}

std::expected<void, std::error_code>
FtpFileTransfer::remove(const std::filesystem::path &remote_path) {
  std::string cmd;
  if (exists(remote_path).value_or(false)) {
    cmd = "RMD";
  } else {
    cmd = "DELE";
  }
  auto cmd_result =
      sendAndReceiveResponse(std::format("{} {}", cmd, remote_path.string()));
  if (!cmd_result) {
    spdlog::error("{} command failed for '{}': {}", cmd, remote_path.string(),
                  cmd_result.error().message());
    return std::unexpected(cmd_result.error());
  } else if (cmd_result->code != 250) {
    spdlog::warn("Failed to remove file '{}' Code: '{}'", remote_path.string(),
                 cmd_result->code);
    return std::unexpected(cmd_result.error());
  }

  return {};
}

std::expected<std::vector<uint8_t>, std::error_code>
FtpFileTransfer::read(const std::filesystem::path &path) {
  (void)path; // unused
  return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<std::size_t, std::error_code>
FtpFileTransfer::read(const std::filesystem::path &path,
                      ReadCallback callback) {
  (void)path;     // unused
  (void)callback; // unused
  return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<FileListData, std::error_code>
FtpFileTransfer::write(const std::filesystem::path &remote_dst_path,
                       std::span<uint8_t> data) {
  (void)remote_dst_path; // unused
  (void)data;            // unused
  return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<FileListData, std::error_code>
FtpFileTransfer::write(const std::filesystem::path &remote_dst_path,
                       WriteCallback next) {
  (void)remote_dst_path; // unused
  (void)next;            // unused
  return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<void, std::error_code>
FtpFileTransfer::ensureDirectory(const std::filesystem::path &path) {
  (void)path; // unused
  return std::unexpected(make_error_code(Network::Error::ProtocolError));
}

std::expected<std::unique_ptr<TcpSocket>, std::error_code>
FtpFileTransfer::openDataConnection() {
  auto pasv_result = sendAndReceiveResponse("PASV");
  if (!pasv_result) {
    spdlog::error("PASV command failed: {}", pasv_result.error().message());
    return std::unexpected(pasv_result.error());
  } else if (pasv_result->code != 227) {
    spdlog::error("Failed to open connection. Code: '{}'", pasv_result->code);
    return std::unexpected(pasv_result.error());
  }

  auto endpoint = parsePasvResponse(pasv_result->full_msg);

  auto data_socket = std::make_unique<ClientSync>(
      endpoint->address().to_string(), endpoint->port(), _io_context);
  auto connect_result = data_socket->connect({std::chrono::seconds(10)});
  if (!connect_result) {
    spdlog::error("Failed to connect data socket: {}",
                  connect_result.error().message());
    return std::unexpected(connect_result.error());
  }

  return connect_result;
}

namespace {
// Helper to skip whitespace at the start of a view
inline std::string_view trim_leading_space(std::string_view str) {
  while (!str.empty() &&
         std::isspace(static_cast<unsigned char>(str.front()))) {
    str.remove_prefix(1);
  }
  return str;
}

// Helper to parse a single integer (0-255) from string_view safely
std::error_code parse_uint8(std::string_view str, uint32_t &out) {
  str = trim_leading_space(str);
  if (str.empty()) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  uint32_t val = 0;
  // std::from_chars works with const char*, compatible with
  // string_view::data()
  auto result = std::from_chars(str.data(), str.data() + str.size(), val);

  // Check for parsing errors (non-digit chars, etc.)
  if (result.ec != std::errc{}) {
    return std::make_error_code(result.ec);
  }
  // Check that we consumed the whole string (no trailing junk)
  if (result.ptr != str.data() + str.size()) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  // Validate IPv4 octet range (0-255)
  if (val > 255) {
    return std::make_error_code(std::errc::result_out_of_range);
  }

  out = val;
  return {}; // No error
}
} // namespace

std::optional<asio::ip::tcp::endpoint>
FtpFileTransfer::parsePasvResponse(std::string_view response) const {
  std::error_code ec;
  size_t start = response.find('(');
  if (start == std::string_view::npos) {
    spdlog::error("Invalid PASV response: missing '('");
    return std::nullopt;
  }

  size_t end = response.find(')', start);
  if (end == std::string_view::npos || end <= start) {
    spdlog::error("Invalid PASV response: missing ')'");
    return std::nullopt;
  }

  std::string_view params = response.substr(start + 1, end - start - 1);

  // We know FTP PASV always has exactly 6 integers
  std::array<uint32_t, 6> parts;
  size_t count = 0;
  size_t pos = 0;

  while (pos < params.size() && count < 6) {
    // Find comma
    size_t comma = params.find(',', pos);
    size_t len = (comma == std::string_view::npos) ? (params.size() - pos)
                                                   : (comma - pos);

    std::string_view part = params.substr(pos, len);
    uint32_t val;

    // Use helper to parse safely without exceptions
    ec = parse_uint8(part, val);
    if (ec) {
      spdlog::error("PASV parameter parse error at '{}' : {}", part,
                    ec.message());
      return std::nullopt;
    }

    parts[count++] = val;

    if (comma == std::string_view::npos)
      break;
    pos = comma + 1;
  }

  if (count != 6) {
    spdlog::error("Invalid PASV parameters: expected 6 values, got {}", count);
    return std::nullopt;
  }

  // Reconstruct IP address
  // Note: parts[0] is the high-order byte in network order logic for address_v4
  uint32_t ip =
      (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
  uint16_t port = static_cast<uint16_t>((parts[4] << 8) | parts[5]);

  // Address construction is safe here because we validated 0-255 ranges
  asio::ip::address_v4 addr(ip);
  return asio::ip::tcp::endpoint(addr, port);
}

std::expected<std::unique_ptr<IAbstractFileTransfer>, std::error_code>
openFtpConnection(std::string_view host, uint16_t port,
                  asio::io_context &io_ctx,
                  const FtpFileTransfer::ConnectOptions &opts) {
  auto ftp = std::make_unique<FtpFileTransfer>(host, port, io_ctx);
  auto connect_result = ftp->connect(opts);

  if (!connect_result) {
    return std::unexpected(connect_result.error());
  }

  return ftp;
}

} // namespace Network

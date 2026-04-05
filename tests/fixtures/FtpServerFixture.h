#pragma once

#include <cstdlib>
#include <string>

#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __unix__
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Network::Test {

class FtpServerFixture {
public:
  FtpServerFixture();
  ~FtpServerFixture();

  FtpServerFixture(const FtpServerFixture &) = delete;
  FtpServerFixture &operator=(const FtpServerFixture &) = delete;
  FtpServerFixture(FtpServerFixture &&) = delete;
  FtpServerFixture &operator=(FtpServerFixture &&) = delete;

  bool start();
  void stop();

  bool isRunning() const;

  static const char *ftpBaseDir() { return kFtpBaseDir; }
  static const char *uploadDir() { return kUploadDir; }
  static const char *readmeFile() { return kReadmeFile; }
  static const char *ftpdProgram() { return kFtpdProgram; }

private:
  bool setupDirectoryStructure();
  bool writeReadmeFile();
  bool startServerProcess();
  void cleanupServerProcess();
  void cleanupDirectoryStructure();

  static constexpr const char *kFtpBaseDir = "/tmp/ftpd";
  static constexpr const char *kUploadDir = "/tmp/ftpd/upload";
  static constexpr const char *kReadmeFile = "/tmp/ftpd/readme.txt";
  static constexpr const char *kFtpdProgram = "/usr/bin/busybox";
  static constexpr const char *kTcpsvdArgs =
      "/usr/bin/busybox tcpsvd -vE 0.0.0.0 2121";

  std::string serverArgs_{};
  pid_t serverPid_{-1};
  bool running_{false};
};

FtpServerFixture::FtpServerFixture() {
  serverArgs_ = std::string(kTcpsvdArgs) + " " +
                std::string(FtpServerFixture::kFtpdProgram) +
                " ftpd -A -w /tmp/ftpd";
}

FtpServerFixture::~FtpServerFixture() { stop(); }

bool FtpServerFixture::start() {
  if (running_) {
    return true;
  }

  if (!setupDirectoryStructure()) {
    return false;
  }

  if (!startServerProcess()) {
    cleanupDirectoryStructure();
    return false;
  }

  running_ = true;
  return true;
}

void FtpServerFixture::stop() {
  if (!running_) {
    return;
  }

  cleanupServerProcess();
  cleanupDirectoryStructure();

  running_ = false;
}

bool FtpServerFixture::isRunning() const { return running_; }

bool FtpServerFixture::setupDirectoryStructure() {
  if (mkdir(kFtpBaseDir, 0755) != 0 && errno != EEXIST) {
    return false;
  }

  if (mkdir(kUploadDir, 0755) != 0 && errno != EEXIST) {
    rmdir(kFtpBaseDir);
    return false;
  }

  if (!writeReadmeFile()) {
    rmdir(kUploadDir);
    rmdir(kFtpBaseDir);
    return false;
  }

  return true;
}

bool FtpServerFixture::writeReadmeFile() {
  std::ofstream file(kReadmeFile);
  if (!file.is_open()) {
    return false;
  }

  file << "Welcome to the FTP Test Server\n";
  file << "================================\n";
  file << "\n";
  file << "This is a test file created by the FTP integration test fixture.\n";
  file << "\n";
  file << "Uploaded files should be placed in the /upload directory.\n";

  return file.good();
}

bool FtpServerFixture::startServerProcess() {
  std::stringstream cmd;
  cmd << kTcpsvdArgs << " " << FtpServerFixture::kFtpdProgram
      << " ftpd -A -w /tmp/ftpd &";

  int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    return false;
  }

  sleep(2);

  std::string pgrepCmd =
      "pgrep -f \"" + std::string(kTcpsvdArgs) + "\" | head -1";
  std::array<char, 32> buffer;
  std::string pidStr;

  FILE *pipe = popen(pgrepCmd.c_str(), "r");
  if (!pipe) {
    return false;
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    pidStr += buffer.data();
  }
  pclose(pipe);

  if (!pidStr.empty()) {
    serverPid_ = std::stoi(pidStr);
    return true;
  }

  return false;
}

void FtpServerFixture::cleanupServerProcess() {
  if (serverPid_ > 0) {
    kill(serverPid_, SIGTERM);
    int status;
    waitpid(serverPid_, &status, 0);
    serverPid_ = -1;
  }
}

void FtpServerFixture::cleanupDirectoryStructure() {
  std::string rmCmd = "rm -rf " + std::string(kFtpBaseDir);
  std::system(rmCmd.c_str());
}

} // namespace Network::Test

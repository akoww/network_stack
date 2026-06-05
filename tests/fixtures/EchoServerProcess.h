#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#ifdef __unix__
  #include <sys/types.h>
  #include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace Network::Test
{

class EchoServerProcessFixture
{
public:
  explicit EchoServerProcessFixture(uint16_t port);

  ~EchoServerProcessFixture();

  EchoServerProcessFixture(const EchoServerProcessFixture&) = delete;
  EchoServerProcessFixture& operator=(const EchoServerProcessFixture&) = delete;

  bool start(std::string_view serverBinary,
             std::optional<fs::path> certChain = std::nullopt,
             std::optional<fs::path> privateKey = std::nullopt);
  void stop();
  bool isRunning() const;

private:
  uint16_t _port;
  pid_t _pid{-1};
  bool _running{false};
};

inline EchoServerProcessFixture::EchoServerProcessFixture(uint16_t port) : _port(port)
{
}

inline EchoServerProcessFixture::~EchoServerProcessFixture()
{
  stop();
}

inline bool EchoServerProcessFixture::isRunning() const
{
  return _running;
}

static inline pid_t findPidByPort(uint16_t port)
{
  std::string pgrepCmd = "pgrep -f \"echo_server.*--port " + std::to_string(port) + "\" | head -1";
  std::array<char, 32> buffer;
  FILE* pipe = popen(pgrepCmd.c_str(), "r");
  if (!pipe)
    return -1;

  std::string pidStr;
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
  {
    pidStr += buffer.data();
  }
  pclose(pipe);

  if (pidStr.empty())
    return -1;
  try
  {
    return static_cast<pid_t>(std::stoi(pidStr));
  }
  catch (...)
  {
    return -1;
  }
}

inline bool EchoServerProcessFixture::start(std::string_view serverBinary,
                                            std::optional<fs::path> certChain,
                                            std::optional<fs::path> privateKey)
{
  if (_running)
    return true;

  if (!fs::exists(fs::path(serverBinary)))
  {
    return false;
  }

  std::string cmd = std::string(serverBinary);
  cmd += " --port " + std::to_string(_port);
  if (certChain.has_value() && privateKey.has_value())
  {
    cmd += " --cert-chain \"" + certChain->string() + "\"";
    cmd += " --private-key \"" + privateKey->string() + "\"";
  }
  cmd += " &";

  int ret = std::system(cmd.c_str());
  if (ret != 0)
    return false;

  // Wait for the server process to appear via pgrep
  for (int i = 0; i < 30; ++i)
  {
    pid_t p = findPidByPort(_port);
    if (p > 0)
    {
      _pid = p;
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      // Verify process is still alive
      if (kill(_pid, 0) == 0)
      {
        _running = true;
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  return false;
}

inline void EchoServerProcessFixture::stop()
{
  if (!_running && _pid <= 0)
    return;

  // Kill any remaining echo_server processes on our port
  pid_t p = findPidByPort(_port);
  while (p > 0)
  {
    kill(p, SIGTERM);
    usleep(250000);
    int status;
    waitpid(p, &status, WNOHANG);
    if (kill(p, 0) == 0)
    {
      kill(p, SIGKILL);
      waitpid(p, &status, 0);
    }
    pid_t next = findPidByPort(_port);
    if (next <= 0 || next == p)
      break;  // done or found a different PID (shouldn't happen)
    p = next;
  }

  _pid = -1;
  _running = false;
}

}  // namespace Network::Test

#include "protocol/FtpUtils.h"

#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace Network::Utility {

namespace {

bool hasWindowsDrive(const fs::path &p) {
  static const std::regex driveRegex(R"(^[A-Za-z]:)");
  return std::regex_search(p.generic_string(), driveRegex);
}

std::string extractWindowsDrive(const fs::path &p) {
  std::string s = p.generic_string();
  if (s.size() >= 2 && std::isalpha(s[0]) && s[1] == ':')
    return s.substr(0, 2);
  return {};
}

bool isPosixAbsolute(const fs::path &p) {
  return !hasWindowsDrive(p) && !p.empty() && p.generic_string()[0] == '/';
}

fs::path stripWindowsDrive(const fs::path &p) {
  std::string s = p.generic_string();

  if (s.size() >= 2 && std::isalpha(s[0]) && s[1] == ':') {
    // Remove "D:" or "D:/"
    if (s.size() > 2 && (s[2] == '/' || s[2] == '\\'))
      return fs::path(s.substr(3));
    else
      return fs::path(s.substr(2));
  }

  return p;
}

} // namespace

SmartDirectoryNavigator::SmartDirectoryNavigator(const fs::path &startPath)
    : current(normalize(startPath)) {}

std::expected<void, std::error_code>
SmartDirectoryNavigator::changeDirectory(const fs::path &targetPath) {
  fs::path target = normalize(targetPath);

  bool currentHasDrive = hasWindowsDrive(current);
  bool targetHasDrive = hasWindowsDrive(target);

  bool currentIsPosixRoot = isPosixAbsolute(current);
  bool targetIsPosixRoot = isPosixAbsolute(target);

  // Windows drive change
  if (currentHasDrive || targetHasDrive) {
    std::string currentDrive = extractWindowsDrive(current);
    std::string targetDrive = extractWindowsDrive(target);

    if (currentDrive != targetDrive) {
      if (!targetDrive.empty()) {
        if (auto result = ftpSelectDrive(targetDrive); !result)
          return std::unexpected(result.error());
      }

      if (auto result = descendFromRoot(target); !result)
        return std::unexpected(result.error());
      current = target;
      return {};
    }
  }

  // POSIX absolute root jump
  if (targetIsPosixRoot && !currentIsPosixRoot) {
    if (auto result = descendFromRoot(target); !result)
      return std::unexpected(result.error());
    current = target;
    return {};
  }

  auto currentParts = split(current.relative_path());
  auto targetParts = split(target.relative_path());

  size_t common = 0;
  while (common < currentParts.size() && common < targetParts.size() &&
         currentParts[common] == targetParts[common]) {
    ++common;
  }

  for (size_t i = common; i < currentParts.size(); ++i) {
    if (auto result = ftpCd(".."); !result)
      return std::unexpected(result.error());
  }

  for (size_t i = common; i < targetParts.size(); ++i) {
    if (auto result = ftpCd(targetParts[i].string()); !result)
      return std::unexpected(result.error());
  }

  current = target;
  return {};
}

fs::path SmartDirectoryNavigator::normalize(const fs::path &p) {
  // Lexical only — no filesystem access
  return p.lexically_normal();
}

std::vector<fs::path> SmartDirectoryNavigator::split(const fs::path &p) {
  std::vector<fs::path> parts;
  for (const auto &part : p) {
    if (part != "." && part != "..") {
      parts.push_back(part);
    }
  }
  return parts;
}

std::expected<void, std::error_code>
SmartDirectoryNavigator::descendFromRoot(const fs::path &target) {
  fs::path clean = stripWindowsDrive(target);

  for (const auto &part : clean) {
    if (part != "/" && part != "\\" && part != "")

      if (auto result = ftpCd(part.string()); !result)
        return std::unexpected(result.error());
  }
  return {};
}
} // namespace Network::Utility

// ===== Default FTP stubs =====
// Replace or override in derived class

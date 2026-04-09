#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace Network::Utility
{

/// @brief Path navigator for changing directories incrementally.
/// This class translates absolute paths into incremental navigation commands
/// (e.g., FTP "cd" steps) needed to move between directory locations.
/// It is protocol-agnostic: derive from this class and implement ftpCd and
/// ftpSelectDrive to integrate with your transport (FTP, SFTP, etc.).
///
/// @section characteristics Features
/// - Purely lexical: uses std::filesystem::path::lexically_normal; no filesystem access
/// - Cross-platform: handles Unix roots ("/") and Windows drives ("C:")
/// - Minimal navigation: goes up to common ancestor then down to target
///
/// @section thread_safety Thread Safety
/// Not thread-safe. Create one instance per session/connection.
///
/// @section usage Usage
/// ```cpp
/// class MyNavigator : public SmartDirectoryNavigator {
///   std::expected<void, std::error_code> ftpCd(const std::string &dir) override {
///     // Issue FTP "CWD" or "CDUP" command...
///   }
///   std::expected<void, std::error_code> ftpSelectDrive(const std::string &drive) override {
///     // Issue FTP drive change command if needed...
///   }
/// };
/// ```
class SmartDirectoryNavigator
{
public:
  /// @brief Construct with initial location.
  /// @param startPath Initial path. May be absolute or relative.
  ///                  Path is lexically normalized and stored as current location.
  explicit SmartDirectoryNavigator(const std::filesystem::path& startPath);
  virtual ~SmartDirectoryNavigator() = default;

  /// @brief Navigate from current path to target path using incremental steps.
  /// @param targetPath Desired path to navigate to.
  /// @return Success on navigation completion, or error code on failure.
  ///
  /// Behavior:
  /// - If target has different root_name (e.g., Windows drive change), issues
  ///   ftpSelectDrive then descends from root via ftpCd for each component.
  /// - Otherwise, computes common prefix: issues ftpCd("..") for each level up
  ///   to common ancestor, then ftpCd() for each remaining component to target.
  ///
  /// The internal current path is updated to the normalized targetPath.
  std::expected<void, std::error_code> changeDirectory(const std::filesystem::path& targetPath);

protected:
  /// @brief Issue a single directory change on the underlying protocol.
  /// @param dir Directory name (one path element) or ".." to move up.
  /// @return Success on navigation completion, or error code on failure.
  ///
  /// Called with exactly one component at a time. Implementations should
  /// perform "cd" into dir or handle ".." to move up one level.
  virtual std::expected<void, std::error_code> ftpCd(const std::string& dir) = 0;

  /// @brief Switch session to a different drive/root when needed (Windows).
  /// @param drive Drive/root identifier, e.g., "D:".
  /// @return Success on drive change, or error code on failure.
  ///
  /// Called when changeDirectory detects target.root_name() differs from
  /// current root and target.root_name() is non-empty.
  virtual std::expected<void, std::error_code> ftpSelectDrive(const std::string& drive) = 0;

private:
  std::filesystem::path _current;

  /// @brief Lexically normalize a path without touching the filesystem.
  /// @param p Input path.
  /// @return Normalized path with "." and ".." segments removed.
  static std::filesystem::path normalize(const std::filesystem::path& p);

  /// @brief Split a path into components, filtering out "." and "..".
  /// @param p Path to split.
  /// @return Vector of non-dot path elements.
  static std::vector<std::filesystem::path> split(const std::filesystem::path& p);

  /// @brief Descend from root into target by issuing ftpCd per component.
  /// @param target Absolute target path.
  /// @return Success on completion, or error code on failure.
  ///
  /// Used after a root/drive change. Iterates target.relative_path() and
  /// calls ftpCd for each element in order.
  std::expected<void, std::error_code> descendFromRoot(const std::filesystem::path& target);
};

}  // namespace Network::Utility

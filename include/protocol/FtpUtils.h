#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace Network::Utility {

/**
 * @brief Computes and issues step-wise directory changes to move between paths.
 *
 * SmartDirectoryNavigator translates absolute paths into incremental navigation
 * commands (e.g., FTP "cd" steps) needed to move from a current location to a
 * target location. It is protocol-agnostic: derive from this class and
 * implement ftpCd and ftpSelectDrive to hook into your transport (FTP, SFTP,
 * etc.).
 *
 * Characteristics:
 * - Purely lexical: paths are normalized using
 * std::filesystem::path::lexically_normal; no filesystem access is performed
 * and symlinks are not resolved.
 * - Cross-platform: handles Unix-like roots ("/") and Windows drive roots
 * ("C:").
 * - Minimal navigation: goes up to the common ancestor and then down to the
 * target.
 *
 * Thread-safety: not thread-safe. Create one instance per session/connection.
 */
class SmartDirectoryNavigator {
public:
  /**
   * @brief Construct a navigator with an initial location.
   *
   * @param startPath Initial path. May be absolute (recommended) or relative.
   *                  The path is lexically normalized and stored as the current
   * location.
   */
  explicit SmartDirectoryNavigator(const std::filesystem::path &startPath);
  virtual ~SmartDirectoryNavigator() = default;

  /**
   * @brief Navigate from the current path to targetPath using incremental
   * steps.
   *
   * Behavior:
   * - If the target has a different root_name (e.g., Windows drive change) and
   *   target.root_name() is non-empty, ftpSelectDrive is called with that root,
   *   then the navigator descends from the root by issuing ftpCd for each
   * component.
   * - Otherwise, computes the common prefix of current and target:
   *   - Calls ftpCd("..") once per level to go up from current to the common
   * ancestor.
   *   - Calls ftpCd(component) for each remaining component to reach the
   * target.
   *
   * The internal current path is updated to the normalized targetPath.
   *
   * @param targetPath Desired path to navigate to.
   */
  std::expected<void, std::error_code>
  changeDirectory(const std::filesystem::path &targetPath);

protected:
  /**
   * @brief Issue a single directory change on the underlying protocol.
   *
   * Implementations should perform a "cd" into dir or handle ".." to move up
   * one level. This is called with exactly one path component at a time or
   * "..".
   *
   * @param dir Either a directory name (one path element) or ".." to go up.
   */
  virtual std::expected<void, std::error_code>
  ftpCd(const std::string &dir) = 0;

  /**
   * @brief Switch the session to a different drive/root when needed (Windows).
   *
   * Called when changeDirectory detects that target.root_name() differs from
   * the current root and target.root_name() is non-empty.
   *
   * @param drive The drive/root identifier, e.g., "D:".
   */
  virtual std::expected<void, std::error_code>
  ftpSelectDrive(const std::string &drive) = 0;

private:
  // Normalized current location
  std::filesystem::path current;

  /**
   * @brief Lexically normalize a path without touching the filesystem.
   *
   * Removes "." and ".." segments and collapses redundant separators using
   * std::filesystem::path::lexically_normal.
   *
   * @param p Input path.
   * @return Normalized path.
   */
  static std::filesystem::path normalize(const std::filesystem::path &p);

  /**
   * @brief Split a path into components, filtering out "." and "..".
   *
   * @param p Path to split.
   * @return Vector of non-dot path elements.
   */
  static std::vector<std::filesystem::path>
  split(const std::filesystem::path &p);

  /**
   * @brief Descend from root into the target by issuing ftpCd per component.
   *
   * Used after a root/drive change. Iterates target.relative_path() and calls
   * ftpCd for each element in order.
   *
   * @param target Absolute target path.
   */
  std::expected<void, std::error_code>
  descendFromRoot(const std::filesystem::path &target);
};

} // namespace Network::Utility
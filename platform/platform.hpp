#pragma once

#include "platform/country_defines.hpp"

#include "coding/reader.hpp"

#include "base/exception.hpp"
#include "base/macros.hpp"
#include "base/task_loop.hpp"
#include "base/thread_pool_delayed.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "defines.hpp"

DECLARE_EXCEPTION(FileAbsentException, RootException);
DECLARE_EXCEPTION(FileSystemException, RootException);

class Platform;

Platform & GetPlatform();

class Platform
{
public:

  enum EError
  {
    ERR_OK = 0,
    ERR_FILE_DOES_NOT_EXIST,
    ERR_ACCESS_FAILED,
    ERR_DIRECTORY_NOT_EMPTY,
    ERR_FILE_ALREADY_EXISTS,
    ERR_NAME_TOO_LONG,
    ERR_NOT_A_DIRECTORY,
    ERR_SYMLINK_LOOP,
    ERR_IO_ERROR,
    ERR_UNKNOWN
  };

  enum EFileType
  {
    FILE_TYPE_UNKNOWN = 0x1,
    FILE_TYPE_REGULAR = 0x2,
    FILE_TYPE_DIRECTORY = 0x4
  };

  using TFilesWithType = std::vector<std::pair<std::string, EFileType>>;

protected:
  /// Usually read-only directory for application resources
  std::string m_resourcesDir = "./../geocore/data/";
  /// Writable directory to store downloaded map data
  /// @note on some systems it can point to external ejectable storage
  std::string m_writableDir = "./";
  /// Temporary directory, can be cleaned up by the system
  std::string m_tmpDir = "/tmp/";

  /// Returns last system call error as EError.
  static EError ErrnoToError();

public:
  Platform();
  virtual ~Platform() = default;

  static bool IsFileExistsByFullPath(std::string const & filePath);

  static bool RemoveFileIfExists(std::string const & filePath);

  /// @returns path to current working directory.
  /// @note In case of an error returns an empty std::string.
  static std::string GetCurrentWorkingDirectory() noexcept;
  /// @return always the same writable dir for current user with slash at the end
  std::string const & WritableDir() const { return m_writableDir; }
  /// Set writable dir
  void SetWritableDir(std::string const & path);
  /// @return full path to file in user's writable directory
  std::string WritablePathForFile(std::string const & file) const { return WritableDir() + file; }
  /// Uses m_writeableDir [w], m_resourcesDir [r]
  std::string ReadPathForFile(std::string const & file,
                              std::string searchScope = std::string()) const;

  /// @return resource dir (on some platforms it's differ from Writable dir)
  std::string const & ResourcesDir() const { return m_resourcesDir; }
  /// @note! This function is used in generator_tool and unit tests.
  /// Client app should not replace default resource dir.
  void SetResourceDir(std::string const & path);

  /// Creates the directory in the filesystem.
  WARN_UNUSED_RESULT static EError MkDir(std::string const & dirName);

  /// Creates the directory. Returns true on success.
  /// Returns false and logs the reason on failure.
  WARN_UNUSED_RESULT static bool MkDirChecked(std::string const & dirName);

  // Creates the directory path dirName.
  // The function will create all parent directories necessary to create the directory.
  // Returns true if successful; otherwise returns false.
  // If the path already exists when this function is called, it will return true.
  // If it was possible to create only a part of the directories, the function will returns false
  // and will not restore the previous state of the file system.
  WARN_UNUSED_RESULT static bool MkDirRecursively(std::string const & dirName);

  /// Removes empty directory from the filesystem.
  static EError RmDir(std::string const & dirName);

  /// Removes directory from the filesystem.
  /// @note Directory can be non empty.
  /// @note If function fails, directory can be partially removed.
  static bool RmDirRecursively(std::string const & dirName);

  /// @return path for directory with temporary files with slash at the end
  std::string const & TmpDir() const { return m_tmpDir; }
  /// @return full path to file in the temporary directory
  std::string TmpPathForFile(std::string const & file) const { return TmpDir() + file; }
  /// @return full random path to temporary file.
  std::string TmpPathForFile() const;

  /// @return reader for file decriptor.
  /// @throws FileAbsentException
  /// @param[in] file name or full path which we want to read
  /// @param[in] searchScope looks for file in dirs in given order: \n
  /// [w]ritable, [r]esources, by [f]ull path, [e]xternal resources,
  std::unique_ptr<ModelReader> GetReader(std::string const & file,
                                         std::string const & searchScope = std::string()) const;

  /// @name File operations
  //@{
  using FilesList = std::vector<std::string>;
  /// Retrieves files list contained in given directory
  /// @param directory directory path with slash at the end
  //@{
  /// @param ext files extension to find, like ".mwm".
  static void GetFilesByExt(std::string const & directory, std::string const & ext,
                            FilesList & outFiles);
  static void GetFilesByRegExp(std::string const & directory, std::string const & regexp,
                               FilesList & outFiles);

  static void GetFilesByType(std::string const & directory, unsigned typeMask,
                             TFilesWithType & outFiles);

  static void GetFilesRecursively(std::string const & directory, FilesList & filesList);

  static bool IsDirectoryEmpty(std::string const & directory);
  // Returns true if |path| refers to a directory. Returns false otherwise or on error.
  static bool IsDirectory(std::string const & path);

  static EError GetFileType(std::string const & path, EFileType & type);

  /// @return false if file is not exist
  /// @note Check files in Writable dir first, and in ReadDir if not exist in Writable dir
  bool GetFileSizeByName(std::string const & fileName, uint64_t & size) const;
  /// @return false if file is not exist
  /// @note Try do not use in client production code
  static bool GetFileSizeByFullPath(std::string const & filePath, uint64_t & size);
  //@}

  // Please note, that number of active cores can vary at runtime.
  // DO NOT assume for the same return value between calls.
  static unsigned CpuCores();
};

std::string DebugPrint(Platform::EError err);

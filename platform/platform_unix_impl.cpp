#include "platform/platform.hpp"
#include "platform/platform_unix_impl.hpp"

#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/scope_guard.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <cstring>
#include <unistd.h>

#include <memory>
#include <regex>

using namespace std;

#if defined(GEOCORE_OS_MAC)
  #include <sys/mount.h>
#else
  #include <sys/vfs.h>
#endif

namespace
{
struct CloseDir
{
  void operator()(DIR * dir) const
  {
    if (dir)
      closedir(dir);
  }
};
}  // namespace


// static
Platform::EError Platform::RmDir(string const & dirName)
{
  if (rmdir(dirName.c_str()) != 0)
    return ErrnoToError();
  return ERR_OK;
}

// static
Platform::EError Platform::GetFileType(string const & path, EFileType & type)
{
  struct stat stats;
  if (stat(path.c_str(), &stats) != 0)
    return ErrnoToError();
  if (S_ISREG(stats.st_mode))
    type = FILE_TYPE_REGULAR;
  else if (S_ISDIR(stats.st_mode))
    type = FILE_TYPE_DIRECTORY;
  else
    type = FILE_TYPE_UNKNOWN;
  return ERR_OK;
}

// static
bool Platform::IsFileExistsByFullPath(string const & filePath)
{
  struct stat s;
  return stat(filePath.c_str(), &s) == 0;
}

//static
void Platform::DisableBackupForFile(string const & /*filePath*/) {}

// static
string Platform::GetCurrentWorkingDirectory() noexcept
{
  char path[PATH_MAX];
  char const * const dir = getcwd(path, PATH_MAX);
  if (dir == nullptr)
    return {};
  return dir;
}

bool Platform::IsDirectoryEmpty(string const & directory)
{
  unique_ptr<DIR, CloseDir> dir(opendir(directory.c_str()));
  if (!dir)
    return true;

  struct dirent * entry;

  // Invariant: all files met so far are "." or "..".
  while ((entry = readdir(dir.get())) != nullptr)
  {
    // A file is not a special UNIX file. Early exit here.
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
      return false;
  }
  return true;
}

bool Platform::GetFileSizeByFullPath(string const & filePath, uint64_t & size)
{
  struct stat s;
  if (stat(filePath.c_str(), &s) == 0)
  {
    size = s.st_size;
    return true;
  }
  else return false;
}

Platform::TStorageStatus Platform::GetWritableStorageStatus(uint64_t neededSize) const
{
  struct statfs st;
  int const ret = statfs(m_writableDir.c_str(), &st);

  LOG(LDEBUG, ("statfs return =", ret,
               "; block size =", st.f_bsize,
               "; blocks available =", st.f_bavail));

  if (ret != 0)
  {
    LOG(LERROR, ("Path:", m_writableDir, "statfs error:", ErrnoToError()));
    return STORAGE_DISCONNECTED;
  }

  /// @todo May be add additional storage space.
  if (st.f_bsize * st.f_bavail < neededSize)
    return NOT_ENOUGH_SPACE;

  return STORAGE_OK;
}

uint64_t Platform::GetWritableStorageSpace() const
{
  struct statfs st;
  int const ret = statfs(m_writableDir.c_str(), &st);

  LOG(LDEBUG, ("statfs return =", ret,
               "; block size =", st.f_bsize,
               "; blocks available =", st.f_bavail));

  if (ret != 0)
    LOG(LERROR, ("Path:", m_writableDir, "statfs error:", ErrnoToError()));

  return (ret != 0) ? 0 : st.f_bsize * st.f_bavail;
}

namespace pl
{
void EnumerateFilesByRegExp(string const & directory, string const & regexp,
                            vector<string> & res)
{
  unique_ptr<DIR, CloseDir> dir(opendir(directory.c_str()));
  if (!dir)
    return;

  regex exp(regexp);

  struct dirent * entry;
  while ((entry = readdir(dir.get())) != 0)
  {
    string const name(entry->d_name);
    if (regex_search(name.begin(), name.end(), exp))
      res.push_back(name);
  }
}
}  // namespace pl

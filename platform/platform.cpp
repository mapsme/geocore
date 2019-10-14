#include "platform/platform.hpp"

#include "coding/file_reader.hpp"
#include "coding/internal/file_data.hpp"
#include "coding/writer.hpp"

#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <random>
#include <thread>

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>

#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>

#include "platform/constants.hpp"

namespace fs = boost::filesystem;
using namespace std;

namespace
{
string RandomString(size_t length)
{
  static string const kCharset =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  random_device rd;
  mt19937 gen(rd());
  uniform_int_distribution<size_t> dis(0, kCharset.size() - 1);
  string str(length, 0);
  generate_n(str.begin(), length, [&]() {
    return kCharset[dis(gen)];
  });
  return str;
}

bool IsSpecialDirName(string const & dirName)
{
  return dirName == "." || dirName == "..";
}

bool GetFileTypeChecked(string const & path, Platform::EFileType & type)
{
  Platform::EError const ret = Platform::GetFileType(path, type);
  if (ret != Platform::ERR_OK)
  {
    LOG(LERROR, ("Can't determine file type for", path, ":", ret));
    return false;
  }
  return true;
}
} // namespace

// static
Platform::EError Platform::ErrnoToError()
{
  switch (errno)
  {
  case ENOENT:
    return ERR_FILE_DOES_NOT_EXIST;
  case EACCES:
    return ERR_ACCESS_FAILED;
  case ENOTEMPTY:
    return ERR_DIRECTORY_NOT_EMPTY;
  case EEXIST:
    return ERR_FILE_ALREADY_EXISTS;
  case ENAMETOOLONG:
    return ERR_NAME_TOO_LONG;
  case ENOTDIR:
    return ERR_NOT_A_DIRECTORY;
  case ELOOP:
    return ERR_SYMLINK_LOOP;
  case EIO:
    return ERR_IO_ERROR;
  default:
    return ERR_UNKNOWN;
  }
}

// static
bool Platform::RmDirRecursively(string const & dirName)
{
  if (dirName.empty() || IsSpecialDirName(dirName))
    return false;

  bool res = true;

  FilesList allFiles;
  GetFilesByRegExp(dirName, ".*", allFiles);
  for (string const & file : allFiles)
  {
    string const path = base::JoinPath(dirName, file);

    EFileType type;
    if (GetFileType(path, type) != ERR_OK)
      continue;

    if (type == FILE_TYPE_DIRECTORY)
    {
      if (!IsSpecialDirName(file) && !RmDirRecursively(path))
        res = false;
    }
    else
    {
      if (!base::DeleteFileX(path))
        res = false;
    }
  }

  if (RmDir(dirName) != ERR_OK)
    res = false;

  return res;
}

void Platform::SetSettingsDir(string const & path)
{
  m_settingsDir = base::AddSlashIfNeeded(path);
}

string Platform::ReadPathForFile(string const & file, string searchScope) const
{
  if (searchScope.empty())
    searchScope = "wrf";

  string fullPath;
  for (size_t i = 0; i < searchScope.size(); ++i)
  {
    switch (searchScope[i])
    {
    case 'w': fullPath = m_writableDir + file; break;
    case 'r': fullPath = m_resourcesDir + file; break;
    case 's': fullPath = m_settingsDir + file; break;
    case 'f': fullPath = file; break;
    default : CHECK(false, ("Unsupported searchScope:", searchScope)); break;
    }
    if (IsFileExistsByFullPath(fullPath))
      return fullPath;
  }

  string const possiblePaths = m_writableDir  + "\n" + m_resourcesDir + "\n" + m_settingsDir;
  MYTHROW(FileAbsentException, ("File", file, "doesn't exist in the scope", searchScope,
                                "Have been looking in:\n", possiblePaths));
}

bool Platform::RemoveFileIfExists(string const & filePath)
{
  return IsFileExistsByFullPath(filePath) ? base::DeleteFileX(filePath) : true;
}

string Platform::TmpPathForFile() const
{
  size_t const kNameLen = 32;
  return TmpDir() + RandomString(kNameLen);
}

void Platform::GetFilesByExt(string const & directory, string const & ext, FilesList & outFiles)
{
  // Transform extension mask to regexp (.mwm -> \.mwm$)
  ASSERT ( !ext.empty(), () );
  ASSERT_EQUAL ( ext[0], '.' , () );

  GetFilesByRegExp(directory, '\\' + ext + '$', outFiles);
}

// static
void Platform::GetFilesByType(string const & directory, unsigned typeMask,
                              TFilesWithType & outFiles)
{
  FilesList allFiles;
  GetFilesByRegExp(directory, ".*", allFiles);
  for (string const & file : allFiles)
  {
    EFileType type;
    if (GetFileType(base::JoinPath(directory, file), type) != ERR_OK)
      continue;
    if (typeMask & type)
      outFiles.emplace_back(file, type);
  }
}

// static
bool Platform::IsDirectory(string const & path)
{
  EFileType fileType;
  if (GetFileType(path, fileType) != ERR_OK)
    return false;
  return fileType == FILE_TYPE_DIRECTORY;
}

// static
void Platform::GetFilesRecursively(string const & directory, FilesList & filesList)
{
  TFilesWithType files;

  GetFilesByType(directory, Platform::FILE_TYPE_REGULAR, files);
  for (auto const & p : files)
  {
    auto const & file = p.first;
    CHECK_EQUAL(p.second, Platform::FILE_TYPE_REGULAR, ("dir:", directory, "file:", file));
    filesList.push_back(base::JoinPath(directory, file));
  }

  TFilesWithType subdirs;
  GetFilesByType(directory, Platform::FILE_TYPE_DIRECTORY, subdirs);

  for (auto const & p : subdirs)
  {
    auto const & subdir = p.first;
    CHECK_EQUAL(p.second, Platform::FILE_TYPE_DIRECTORY, ("dir:", directory, "subdir:", subdir));
    if (subdir == "." || subdir == "..")
      continue;

    GetFilesRecursively(base::JoinPath(directory, subdir), filesList);
  }
}

void Platform::SetWritableDirForTests(string const & path)
{
  m_writableDir = base::AddSlashIfNeeded(path);
}

void Platform::SetResourceDir(string const & path)
{
  m_resourcesDir = base::AddSlashIfNeeded(path);
}

// static
bool Platform::MkDirChecked(string const & dirName)
{
  Platform::EError const ret = MkDir(dirName);
  switch (ret)
  {
  case Platform::ERR_OK: return true;
  case Platform::ERR_FILE_ALREADY_EXISTS:
  {
    Platform::EFileType type;
    if (!GetFileTypeChecked(dirName, type))
      return false;
    if (type != Platform::FILE_TYPE_DIRECTORY)
    {
      LOG(LERROR, (dirName, "exists, but not a dirName:", type));
      return false;
    }
    return true;
  }
  default: LOG(LERROR, (dirName, "can't be created:", ret)); return false;
  }
}

// static
bool Platform::MkDirRecursively(string const & dirName)
{
  auto const sep = base::GetNativeSeparator();
  string path = strings::StartsWith(dirName, sep) ? sep : "";
  auto const tokens = strings::Tokenize(dirName, sep.c_str());
  for (auto const & t : tokens)
  {
    path = base::JoinPath(path, t);
    if (!IsFileExistsByFullPath(path))
    {
      auto const ret = MkDir(path);
      switch (ret)
      {
      case ERR_OK: break;
      case ERR_FILE_ALREADY_EXISTS:
      {
        if (!IsDirectory(path))
          return false;
        break;
      }
      default: return false;
      }
    }
  }

  return true;
}

unsigned Platform::CpuCores() const
{
  unsigned const cores = thread::hardware_concurrency();
  return cores > 0 ? cores : 1;
}

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

namespace
{

// Returns directory where binary resides, including slash at the end.
bool GetBinaryDir(string & outPath)
{
  char path[4096] = {};
  if (::readlink("/proc/self/exe", path, ARRAY_SIZE(path)) <= 0)
    return false;
  outPath = path;
  outPath.erase(outPath.find_last_of('/') + 1);
  return true;
}

// Returns true if EULA file exists in directory.
bool IsEulaExist(string const & directory)
{
  return Platform::IsFileExistsByFullPath(base::JoinPath(directory, "eula.html"));
}

// Makes base::JoinPath(path, dirs) and all intermediate dirs.
// The directory |path| is assumed to exist already.
bool MkDirsChecked(string path, initializer_list<string> const & dirs)
{
  string accumulatedDirs = path;
  // Storing full paths is redundant but makes the implementation easier.
  vector<string> madeDirs;
  bool ok = true;
  for (auto const & dir : dirs)
  {
    accumulatedDirs = base::JoinPath(accumulatedDirs, dir);
    auto const result = Platform::MkDir(accumulatedDirs);
    switch (result)
    {
    case Platform::ERR_OK: madeDirs.push_back(accumulatedDirs); break;
    case Platform::ERR_FILE_ALREADY_EXISTS:
    {
      Platform::EFileType type;
      if (Platform::GetFileType(accumulatedDirs, type) != Platform::ERR_OK ||
          type != Platform::FILE_TYPE_DIRECTORY)
      {
        ok = false;
      }
    }
      break;
    default: ok = false; break;
    }
  }

  if (ok)
    return true;

  for (; !madeDirs.empty(); madeDirs.pop_back())
    Platform::RmDir(madeDirs.back());

  return false;
}

string HomeDir()
{
  char const * homePath = ::getenv("HOME");
  if (homePath == nullptr)
    MYTHROW(RootException, ("The environment variable HOME is not set"));
  return homePath;
}

// Returns the default path to the writable dir, creating the dir if needed.
// An exception is thrown if the default dir is not already there and we were unable to create it.
string DefaultWritableDir()
{
  initializer_list<string> const dirs = {".local", "share", "MapsWithMe"};
  string result;
  for (auto const & dir : dirs)
    result = base::JoinPath(result, dir);
  result = base::AddSlashIfNeeded(result);

  auto const home = HomeDir();
  if (!MkDirsChecked(home, dirs))
    MYTHROW(FileSystemException, ("Cannot create directory:", result));
  return result;
}
}  // namespace


Platform::Platform()
{
  // Init directories.
  string path;
  CHECK(GetBinaryDir(path), ("Can't retrieve path to executable"));

  m_settingsDir = base::JoinPath(HomeDir(), ".config", "MapsWithMe");

  if (!IsFileExistsByFullPath(base::JoinPath(m_settingsDir, SETTINGS_FILE_NAME)))
  {
    auto const configDir = base::JoinPath(HomeDir(), ".config");
    if (!MkDirChecked(configDir))
      MYTHROW(FileSystemException, ("Can't create directory", configDir));
    if (!MkDirChecked(m_settingsDir))
      MYTHROW(FileSystemException, ("Can't create directory", m_settingsDir));
  }

  char const * resDir = ::getenv("MWM_RESOURCES_DIR");
  char const * writableDir = ::getenv("MWM_WRITABLE_DIR");
  if (resDir && writableDir)
  {
    m_resourcesDir = resDir;
    m_writableDir = writableDir;
  }
  else if (resDir)
  {
    m_resourcesDir = resDir;
    m_writableDir = DefaultWritableDir();
  }
  else
  {
    string const devBuildWithSymlink = base::JoinPath(path, "..", "..", "data");
    string const devBuildWithoutSymlink = base::JoinPath(path, "..", "..", "..", "geocore", "data");
    string const installedVersionWithPackages = base::JoinPath(path, "..", "share");
    string const installedVersionWithoutPackages = base::JoinPath(path, "..", "MapsWithMe");
    string const customInstall = path;

    if (IsEulaExist(devBuildWithSymlink))
    {
      m_resourcesDir = devBuildWithSymlink;
      m_writableDir = writableDir != nullptr ? writableDir : m_resourcesDir;
    }
    else if (IsEulaExist(devBuildWithoutSymlink))
    {
      m_resourcesDir = devBuildWithoutSymlink;
      m_writableDir = writableDir != nullptr ? writableDir : m_resourcesDir;
    }
    else if (IsEulaExist(installedVersionWithPackages))
    {
      m_resourcesDir = installedVersionWithPackages;
      m_writableDir = writableDir != nullptr ? writableDir : DefaultWritableDir();
    }
    else if (IsEulaExist(installedVersionWithoutPackages))
    {
      m_resourcesDir = installedVersionWithoutPackages;
      m_writableDir = writableDir != nullptr ? writableDir : DefaultWritableDir();
    }
    else if (IsEulaExist(customInstall))
    {
      m_resourcesDir = path;
      m_writableDir = writableDir != nullptr ? writableDir : DefaultWritableDir();
    }
  }
  m_resourcesDir += '/';
  m_settingsDir += '/';
  m_writableDir += '/';

  char const * tmpDir = ::getenv("TMPDIR");
  if (tmpDir)
    m_tmpDir = tmpDir;
  else
    m_tmpDir = "/tmp";
  m_tmpDir += '/';

  m_privateDir = m_settingsDir;

  LOG(LDEBUG, ("Resources directory:", m_resourcesDir));
  LOG(LDEBUG, ("Writable directory:", m_writableDir));
  LOG(LDEBUG, ("Tmp directory:", m_tmpDir));
  LOG(LDEBUG, ("Settings directory:", m_settingsDir));
}


unique_ptr<ModelReader> Platform::GetReader(string const & file, string const & searchScope) const
{
  return make_unique<FileReader>(ReadPathForFile(file, searchScope), READER_CHUNK_LOG_SIZE,
                                 READER_CHUNK_LOG_COUNT);
}

bool Platform::GetFileSizeByName(string const & fileName, uint64_t & size) const
{
  try
  {
    return GetFileSizeByFullPath(ReadPathForFile(fileName), size);
  }
  catch (RootException const &)
  {
    return false;
  }
}

void Platform::GetFilesByRegExp(string const & directory, string const & regexp,
                                FilesList & outFiles)
{
  boost::system::error_code ec{};
  regex exp(regexp);

  for (auto const & entry : boost::make_iterator_range(fs::directory_iterator(directory, ec), {}))
  {
    string const name = entry.path().filename().string();
    if (regex_search(name.begin(), name.end(), exp))
      outFiles.push_back(name);
  }
}

// static
Platform::EError Platform::MkDir(string const & dirName)
{
  boost::system::error_code ec{};

  fs::path dirPath{dirName};
  if (fs::exists(dirPath, ec))
    return Platform::ERR_FILE_ALREADY_EXISTS;
  if (!fs::create_directory(dirPath, ec))
  {
    LOG(LWARNING, ("Can't create directory: ", dirName));
    return Platform::ERR_UNKNOWN;
  }
  return Platform::ERR_OK;
}

Platform & GetPlatform()
{
  static Platform platform;
  return platform;
}

string DebugPrint(Platform::EError err)
{
  switch (err)
  {
  case Platform::ERR_OK: return "Ok";
  case Platform::ERR_FILE_DOES_NOT_EXIST: return "File does not exist.";
  case Platform::ERR_ACCESS_FAILED: return "Access failed.";
  case Platform::ERR_DIRECTORY_NOT_EMPTY: return "Directory not empty.";
  case Platform::ERR_FILE_ALREADY_EXISTS: return "File already exists.";
  case Platform::ERR_NAME_TOO_LONG:
    return "The length of a component of path exceeds {NAME_MAX} characters.";
  case Platform::ERR_NOT_A_DIRECTORY:
    return "A component of the path prefix of Path is not a directory.";
  case Platform::ERR_SYMLINK_LOOP:
    return "Too many symbolic links were encountered in translating path.";
  case Platform::ERR_IO_ERROR: return "An I/O error occurred.";
  case Platform::ERR_UNKNOWN: return "Unknown";
  }
  UNREACHABLE();
}

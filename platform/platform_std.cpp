#include "platform/constants.hpp"
#include "platform/measurement_utils.hpp"
#include "platform/platform.hpp"
#include "platform/settings.hpp"

#include "coding/file_reader.hpp"

#include "base/logging.hpp"

#include "platform/target_os.hpp"

#include <algorithm>
#include <future>
#include <memory>
#include <regex>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>

namespace fs = boost::filesystem;

using namespace std;

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

extern Platform & GetPlatform()
{
  static Platform platform;
  return platform;
}

#include "coding/files_merger.hpp"

#include "coding/internal/file_data.hpp"

#include "platform/platform.hpp"

FilesMerger::FilesMerger(std::string const & intoFilename)
  : m_targetFilename{intoFilename}
{ }

FilesMerger::~FilesMerger() noexcept(false)
{
  Merge();
}

void FilesMerger::DeferMergeAndDelete(std::string const & filename)
{
  m_mergeFiles.push_back(filename);
}

void FilesMerger::Merge()
{
  while (!m_mergeFiles.empty())
  {
    auto const & filename = m_mergeFiles.front();
    if (Platform::IsFileExistsByFullPath(filename))
    {
      base::AppendFileToFile(filename, m_targetFilename);
      base::DeleteFileX(filename);
    }

    m_mergeFiles.pop_front();
  }
}

#include <string>
#include <list>

class FilesMerger
{
public:
  FilesMerger(std::string const & intoFilename);
  ~FilesMerger() noexcept(false);

  void DeferMergeAndDelete(std::string const & filename);
  void Merge();

private:
  std::string m_targetFilename;
  std::list<std::string> m_mergeFiles;
};

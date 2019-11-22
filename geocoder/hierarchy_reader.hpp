#pragma once

#include "geocoder/hierarchy.hpp"
#include "geocoder/name_dictionary.hpp"

#include "base/exception.hpp"
#include "base/geo_object_id.hpp"

#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace geocoder
{
class HierarchyReader
{
  static constexpr const char * kVersionKey = "version";

public:
  using Entry = Hierarchy::Entry;
  using ParsingStats = Hierarchy::ParsingStats;

  DECLARE_EXCEPTION(Exception, RootException);
  DECLARE_EXCEPTION(OpenException, Exception);
  DECLARE_EXCEPTION(NoVersion, Exception);

  explicit HierarchyReader(std::string const & pathToJsonHierarchy,
                           bool dataVersionHeadline = false);
  explicit HierarchyReader(std::istream & jsonHierarchy, bool dataVersionHeadline = false);

  // Read hierarchy file/stream concurrently in |readersCount| threads.
  Hierarchy Read(unsigned int readersCount = 1);

  static std::string ReadDataVersion(std::string const & pathToJsonHierarchy);

private:
  struct ParsingResult
  {
    std::vector<Entry> m_entries;
    NameDictionary m_nameDictionary;
    ParsingStats m_stats;
  };

  static std::unique_ptr<std::istream> CreateDataStream(std::string const & pathToJsonHierarchy);
  static std::string ReadDataVersion(std::istream & stream);
  ParsingResult ReadEntries(size_t count);
  ParsingResult DeserializeEntries(std::vector<std::string> const & linesBuffer,
                                   std::size_t const bufferSize);
  static bool DeserializeId(std::string const & str, uint64_t & id);
  static std::string SerializeId(uint64_t id);

  void CheckDuplicateOsmIds(std::vector<Entry> const & entries, ParsingStats & stats);

  std::unique_ptr<std::istream> m_fileStream;
  std::istream & m_in;
  bool m_eof{false};
  std::mutex m_mutex;
  std::atomic<std::uint64_t> m_totalNumLoaded{0};
  std::string m_dataVersion;
};
} // namespace geocoder

#include "generator/final_processor_intermediate_mwm.hpp"

#include "generator/feature_builder.hpp"
#include "generator/feature_merger.hpp"
#include "generator/type_helper.hpp"

#include "indexer/classificator.hpp"

#include "platform/platform.hpp"

#include "base/assert.hpp"
#include "base/file_name_utils.hpp"
#include "base/geo_object_id.hpp"
#include "base/string_utils.hpp"
#include "base/thread_pool_computational.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <future>
#include <iterator>
#include <memory>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "defines.hpp"

using namespace base::thread_pool::computational;
using namespace feature;
using namespace serialization_policy;

namespace generator
{
namespace
{
template <typename ToDo>
void ForEachCountry(std::string const & temporaryMwmPath, ToDo && toDo)
{
  Platform::FilesList fileList;
  Platform::GetFilesByExt(temporaryMwmPath, DATA_FILE_EXTENSION_TMP, fileList);
  for (auto const & filename : fileList)
    toDo(filename);
}

// Writes |fbs| to countries tmp.mwm files that |fbs| belongs to according to |affiliations|.
template <class SerializationPolicy = MaxAccuracy>
void AppendToCountries(std::vector<FeatureBuilder> const & fbs,
                       std::vector<std::vector<std::string>> const & affiliations,
                       std::string const & temporaryMwmPath, size_t threadsCount)
{
  std::unordered_map<std::string, std::vector<size_t>> countryToFbsIndexes;
  for (size_t i = 0; i < fbs.size(); ++i)
  {
    for (auto const & country : affiliations[i])
      countryToFbsIndexes[country].emplace_back(i);
  }

  ThreadPool pool(threadsCount);
  for (auto && p : countryToFbsIndexes)
  {
    pool.SubmitWork([&, country{std::move(p.first)}, indexes{std::move(p.second)}]() {
      auto const path = base::JoinPath(temporaryMwmPath, country + DATA_FILE_EXTENSION_TMP);
      FeatureBuilderWriter<SerializationPolicy> collector(path, FileWriter::Op::OP_APPEND);
      for (auto const index : indexes)
        collector.Write(fbs[index]);
    });
  }
}
}  // namespace

FinalProcessorIntermediateMwmInterface::FinalProcessorIntermediateMwmInterface(FinalProcessorPriority priority)
  : m_priority(priority)
{
}

bool FinalProcessorIntermediateMwmInterface::operator<(FinalProcessorIntermediateMwmInterface const & other) const
{
  return m_priority < other.m_priority;
}

bool FinalProcessorIntermediateMwmInterface::operator==(FinalProcessorIntermediateMwmInterface const & other) const
{
  return !(*this < other || other < *this);
}

bool FinalProcessorIntermediateMwmInterface::operator!=(FinalProcessorIntermediateMwmInterface const & other) const
{
  return !(*this == other);
}
}  // namespace generator

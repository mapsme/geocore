#pragma once

#include "generator/feature_builder.hpp"

#include "base/thread_safe_queue.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace generator
{
size_t static const kAffiliationsBufferSize = 2'000;

struct ProcessedData
{
  feature::FeatureBuilder::Buffer m_buffer;
  std::shared_ptr<std::vector<std::string>> m_affiliations;
};

using FeatureProcessorChunk =
    base::threads::DataWrapper<std::shared_ptr<std::vector<ProcessedData>>>;
using FeatureProcessorQueue = base::threads::ThreadSafeQueue<FeatureProcessorChunk>;
}  // namespace generator

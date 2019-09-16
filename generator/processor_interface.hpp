#pragma once

#include "generator/feature_builder.hpp"

#include "base/assert.hpp"
#include "base/thread_safe_queue.hpp"

#include <memory>
#include <string>
#include <vector>

class FeatureParams;

namespace generator
{
class ProcessorNoop;
class ProcessorSimple;

// Implementing this interface allows an object to process FeatureBuilder objects and broadcast them.
class FeatureProcessorInterface
{
public:
  virtual ~FeatureProcessorInterface() = default;

  virtual std::shared_ptr<FeatureProcessorInterface> Clone() const = 0;

  // This method is used by OsmTranslator to pass |fb| to Processor for further processing.
  virtual void Process(feature::FeatureBuilder & fb) = 0;
  virtual void Finish() = 0;

  virtual void Merge(FeatureProcessorInterface const &) = 0;

  virtual void MergeInto(ProcessorNoop &) const { FailIfMethodUnsupported(); }
  virtual void MergeInto(ProcessorSimple &) const { FailIfMethodUnsupported(); }

private:
  void FailIfMethodUnsupported() const { CHECK(false, ("This method is unsupported.")); }
};
}  // namespace generator

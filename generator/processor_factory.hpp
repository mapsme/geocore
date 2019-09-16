#pragma once

#include "generator/factory_utils.hpp"
#include "generator/processor_interface.hpp"
#include "generator/processor_noop.hpp"
#include "generator/processor_simple.hpp"

#include "base/assert.hpp"

#include <memory>
#include <utility>

namespace generator
{
enum class ProcessorType
{
  Simple,
  Noop
  //  Booking
};

template <class... Args>
std::shared_ptr<FeatureProcessorInterface> CreateProcessor(ProcessorType type, Args&&... args)
{
  switch (type)
  {
  case ProcessorType::Simple:
    return create<ProcessorSimple>(std::forward<Args>(args)...);
  case ProcessorType::Noop:
    return create<ProcessorNoop>(std::forward<Args>(args)...);
  }
  UNREACHABLE();
}
}  // namespace generator

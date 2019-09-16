#pragma once

#include "generator/coastlines_generator.hpp"
#include "generator/feature_generator.hpp"
#include "generator/world_map_generator.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace generator
{
enum class FinalProcessorPriority : uint8_t
{
  CountriesOrWorld = 1,
  WorldCoasts = 2
};

// Classes that inherit this interface implement the final stage of intermediate mwm processing.
// For example, attempt to merge the coastline or adding external elements.
// Each derived class has a priority. This is done to comply with the order of processing intermediate mwm,
// taking into account the dependencies between them. For example, before adding a coastline to
// a country, we must build coastline. Processors with higher priority will be called first.
// Processors with the same priority can run in parallel.
class FinalProcessorIntermediateMwmInterface
{
public:
  explicit FinalProcessorIntermediateMwmInterface(FinalProcessorPriority priority);
  virtual ~FinalProcessorIntermediateMwmInterface() = default;

  virtual bool Process() = 0;

  bool operator<(FinalProcessorIntermediateMwmInterface const & other) const;
  bool operator==(FinalProcessorIntermediateMwmInterface const & other) const;
  bool operator!=(FinalProcessorIntermediateMwmInterface const & other) const;

protected:
  FinalProcessorPriority m_priority;
};
}  // namespace generator

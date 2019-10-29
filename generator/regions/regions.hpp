#pragma once

#include <string>

namespace generator
{
namespace regions
{
void GenerateRegions(std::string const & pathRegionsTmpMwm,
                     std::string const & pathInRegionsCollector,
                     std::string const & pathOutRegionsKv,
                     bool verbose,
                     unsigned int threadsCount = 1);
}  // namespace regions
}  // namespace generator

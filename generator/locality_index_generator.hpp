#pragma once

#include <string>

#include <boost/optional.hpp>

namespace generator
{
bool GenerateRegionsIndex(
    std::string const & outPath, std::string const & featuresFile, unsigned int threadsCount);

bool GenerateGeoObjectsIndex(
    std::string const & outPath, std::string const & geoObjectsFeaturesFile,
    unsigned int threadsCount,
    boost::optional<std::string> const & nodesFile = {},
    boost::optional<std::string> const & streetsFeaturesFile = {});

// Generates borders section for server-side reverse geocoder from input feature-dat-files.
bool GenerateBorders(std::string const & outPath, std::string const & featuresDir);

void WriteDataVersionSection(std::string const & outPath, std::string const & dataVersionJson);
}  // namespace generator

#pragma once

#include <string>

#include <boost/optional.hpp>

namespace feature
{
// Generates data for GeoObjectsIndexBuilder from input feature-dat-files.
bool GenerateGeoObjectsData(std::string const & toDataFile,
                            std::string const & geoObjectsFeaturesFile,
                            boost::optional<std::string> const & nodesFile = {},
                            boost::optional<std::string> const & streetsFeaturesFile = {});

// Generates data for RegionsIndexBuilder from input feature-dat-files.
bool GenerateRegionsData(std::string const & toDataFile, std::string const & featuresFile);

// Generates borders section for server-side reverse geocoder from input feature-dat-files.
bool GenerateBorders(std::string const & toDataFile, std::string const & featuresDir);
}  // namespace feature

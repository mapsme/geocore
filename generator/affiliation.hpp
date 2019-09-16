#pragma once

#include "generator/feature_builder.hpp"

#include <string>
#include <vector>

namespace feature
{
class AffiliationInterface
{
public:
  virtual ~AffiliationInterface() = default;

  // The method will return the names of the buckets to which the fb belongs.
  virtual std::vector<std::string> GetAffiliations(FeatureBuilder const & fb) const = 0;
  virtual bool HasRegionByName(std::string const & name) const = 0;
};

class SingleAffiliation : public AffiliationInterface
{
public:
  SingleAffiliation(std::string const & filename);

  // AffiliationInterface overrides:
  std::vector<std::string> GetAffiliations(FeatureBuilder const &) const override;
  bool HasRegionByName(std::string const & name) const override;

private:
  std::string m_filename;
};
}  // namespace feature

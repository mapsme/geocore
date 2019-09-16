#pragma once

#include "generator/feature_builder.hpp"
#include "generator/filter_interface.hpp"

#include <memory>
#include <string>

namespace generator
{
class FilterWorld : public FilterInterface
{
public:
  // FilterInterface overrides:
  std::shared_ptr<FilterInterface> Clone() const override;

  bool IsAccepted(feature::FeatureBuilder const & feature) override;

  static bool IsInternationalAirport(feature::FeatureBuilder const & fb);
  static bool IsGoodScale(feature::FeatureBuilder const & fb);
};
}  // namespace generator

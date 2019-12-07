#pragma once

#include "generator/processor_interface.hpp"
#include "generator/translator.hpp"

#include <memory>

namespace feature
{
class FeatureBuilder;
}  // namespace feature

namespace cache
{
class IntermediateData;
}  // namespace cache

namespace generator
{
// TranslatorRegion class is responsible for processing regions.
class TranslatorRegion : public Translator
{
public:
  explicit TranslatorRegion(std::shared_ptr<FeatureProcessorInterface> const & processor,
                            std::shared_ptr<cache::IntermediateData const> const & cache,
                            std::string const & regionsInfoPath);

  // TranslatorInterface overrides:
  std::shared_ptr<TranslatorInterface> Clone() const override;

  void Merge(TranslatorInterface const & other) override;
  void MergeInto(TranslatorRegion & other) const override;

protected:
  using Translator::Translator;
};

class FilterRegions : public FilterInterface
{
public:
  // FilterInterface overrides:
  std::shared_ptr<FilterInterface> Clone() const override;
  bool IsAccepted(OsmElement const & element) override;
  bool IsAccepted(feature::FeatureBuilder const & feature) override;

protected:
  bool IsEnclaveBoundaryWay(OsmElement const & element) const;
  bool IsGeometryClosed(OsmElement const & element) const;
};
}  // namespace generator

#pragma once

#include "generator/features_processing_helpers.hpp"
#include "generator/final_processor_intermediate_mwm.hpp"
#include "generator/generate_info.hpp"
#include "generator/intermediate_data.hpp"
#include "generator/translator_collection.hpp"
#include "generator/translator_interface.hpp"

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/optional.hpp>

namespace generator
{
class RawGeneratorWriter;
class ProcessorOsmElementsInterface;

class RawGenerator
{
public:
  explicit RawGenerator(feature::GenerateInfo & genInfo, size_t chunkSize = 1024);

  void GenerateRegionFeatures(
      std::string const & regionsFeaturesPath, std::string const & regionsInfoPath);
  void GenerateStreetsFeatures(std::string const & filename);
  void GenerateGeoObjectsFeatures(std::string const & filename);
  void GenerateCustom(std::shared_ptr<TranslatorInterface> const & translator);
  void GenerateCustom(std::shared_ptr<TranslatorInterface> const & translator,
                      std::shared_ptr<FinalProcessorIntermediateMwmInterface> const & finalProcessor);
  bool Execute();
  std::vector<std::string> const & GetNames() const;
  std::shared_ptr<FeatureProcessorQueue> GetQueue();
  void ForceReloadCache();

private:
  using FinalProcessorPtr = std::shared_ptr<FinalProcessorIntermediateMwmInterface>;

  struct FinalProcessorPtrCmp
  {
    bool operator()(FinalProcessorPtr const & l, FinalProcessorPtr const & r)
    {
      return *l < *r;
    }
  };

  bool GenerateFilteredFeatures();
  bool GenerateFeatures(unsigned int threadsCount, RawGeneratorWriter & rawGeneratorWriter);
  static void TranslateToFeatures(ProcessorOsmElementsInterface & sourceProcessor,
                                  TranslatorInterface & translator);
  bool FinishTranslation(std::vector<std::shared_ptr<TranslatorInterface>> & translators);
  boost::iostreams::mapped_file_source MakeFileMap(std::string const & filename);

  feature::GenerateInfo & m_genInfo;
  size_t m_chunkSize;
  std::shared_ptr<cache::IntermediateData> m_cache;
  std::shared_ptr<FeatureProcessorQueue> m_queue;
  std::shared_ptr<TranslatorCollection> m_translators;
  std::priority_queue<FinalProcessorPtr, std::vector<FinalProcessorPtr>, FinalProcessorPtrCmp> m_finalProcessors;
  std::vector<std::string> m_names;
};
}  // namespace generator

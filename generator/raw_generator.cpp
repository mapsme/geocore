#include "generator/raw_generator.hpp"

#include "generator/osm_source.hpp"
#include "generator/processor_factory.hpp"
#include "generator/raw_generator_writer.hpp"
#include "generator/translator_factory.hpp"
#include "generator/translators_pool.hpp"

#include "base/thread_pool_computational.hpp"


#include <string>
#include <vector>

using namespace std;

namespace generator
{
RawGenerator::RawGenerator(feature::GenerateInfo & genInfo, size_t threadsCount, size_t chunkSize)
  : m_genInfo(genInfo)
  , m_threadsCount(threadsCount)
  , m_chunkSize(chunkSize)
  , m_cache(std::make_shared<generator::cache::IntermediateData>(genInfo))
  , m_queue(std::make_shared<FeatureProcessorQueue>())
  , m_translators(std::make_shared<TranslatorCollection>())
{
}

void RawGenerator::ForceReloadCache()
{
  m_cache = std::make_shared<cache::IntermediateData>(m_genInfo, true /* forceReload */);
}

std::shared_ptr<FeatureProcessorQueue> RawGenerator::GetQueue()
{
  return m_queue;
}

void RawGenerator::GenerateRegionFeatures(
    string const & regionsFeaturesPath, std::string const & regionsInfoPath)
{
  auto processor = CreateProcessor(ProcessorType::Simple, m_queue, regionsFeaturesPath);
  m_translators->Append(
      CreateTranslator(TranslatorType::Regions, processor, m_cache, regionsInfoPath));
}

void RawGenerator::GenerateStreetsFeatures(string const & filename)
{
  auto processor = CreateProcessor(ProcessorType::Simple, m_queue, filename);
  m_translators->Append(CreateTranslator(TranslatorType::Streets, processor, m_cache));
}

void RawGenerator::GenerateGeoObjectsFeatures(string const & filename)
{
  auto processor = CreateProcessor(ProcessorType::Simple, m_queue, filename);
  m_translators->Append(CreateTranslator(TranslatorType::GeoObjects, processor, m_cache));
}

void RawGenerator::GenerateCustom(std::shared_ptr<TranslatorInterface> const & translator)
{
  m_translators->Append(translator);
}

void RawGenerator::GenerateCustom(std::shared_ptr<TranslatorInterface> const & translator,
                                  std::shared_ptr<FinalProcessorIntermediateMwmInterface> const & finalProcessor)
{
  m_translators->Append(translator);
  m_finalProcessors.emplace(finalProcessor);
}

bool RawGenerator::Execute()
{
  if (!GenerateFilteredFeatures())
    return false;

  while (!m_finalProcessors.empty())
  {
    base::thread_pool::computational::ThreadPool threadPool(m_threadsCount);
    while (true)
    {
      auto const finalProcessor = m_finalProcessors.top();
      m_finalProcessors.pop();
      threadPool.SubmitWork([finalProcessor{finalProcessor}]() {
        finalProcessor->Process();
      });
      if (m_finalProcessors.empty() || *finalProcessor != *m_finalProcessors.top())
        break;
    }
  }

  LOG(LINFO, ("Final processing is finished."));
  return true;
}

std::vector<std::string> const & RawGenerator::GetNames() const
{
  return m_names;
}

bool RawGenerator::GenerateFilteredFeatures()
{
  SourceReader reader = m_genInfo.m_osmFileName.empty() ? SourceReader()
                                                        : SourceReader(m_genInfo.m_osmFileName);

  std::unique_ptr<ProcessorOsmElementsInterface> sourceProcessor;
  switch (m_genInfo.m_osmFileType) {
  case feature::GenerateInfo::OsmSourceType::O5M:
    sourceProcessor = std::make_unique<ProcessorOsmElementsFromO5M>(reader);
    break;
  case feature::GenerateInfo::OsmSourceType::XML:
    sourceProcessor = std::make_unique<ProcessorOsmElementsFromXml>(reader);
    break;
  }
  CHECK(sourceProcessor, ());

  TranslatorsPool translators(m_translators, m_threadsCount);
  RawGeneratorWriter rawGeneratorWriter(m_queue);
  rawGeneratorWriter.Run();

  size_t element_pos = 0;
  std::vector<OsmElement> elements(m_chunkSize);
  while (sourceProcessor->TryRead(elements[element_pos]))
  {
    if (++element_pos != m_chunkSize)
      continue;

    translators.Emit(std::move(elements));
    elements = vector<OsmElement>(m_chunkSize);
    element_pos = 0;
  }
  elements.resize(element_pos);
  translators.Emit(std::move(elements));

  LOG(LINFO, ("Input was processed."));
  if (!translators.Finish())
    return false;

  rawGeneratorWriter.ShutdownAndJoin();
  m_names = rawGeneratorWriter.GetNames();
  LOG(LINFO, ("Names:", m_names));
  return true;
}
}  // namespace generator

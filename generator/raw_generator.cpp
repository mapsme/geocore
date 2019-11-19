#include "generator/raw_generator.hpp"

#include "generator/osm_source.hpp"
#include "generator/processor_factory.hpp"
#include "generator/raw_generator_writer.hpp"
#include "generator/translator_factory.hpp"

#include "base/thread_pool_computational.hpp"

#include <future>
#include <string>
#include <vector>

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <sys/mman.h>

using namespace std;

namespace generator
{
RawGenerator::RawGenerator(feature::GenerateInfo & genInfo, size_t chunkSize)
  : m_genInfo(genInfo)
  , m_chunkSize(chunkSize)
  , m_cache(std::make_shared<generator::cache::IntermediateData>(genInfo))
  , m_queue(std::make_shared<FeatureProcessorQueue>())
  , m_translators(std::make_shared<TranslatorCollection>())
{
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
    base::thread_pool::computational::ThreadPool threadPool(m_genInfo.m_threadsCount);
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
  RawGeneratorWriter rawGeneratorWriter(m_queue);
  rawGeneratorWriter.Run();

  auto processorThreadsCount = std::max(m_genInfo.m_threadsCount, 2u) - 1 /* writer */;
  if (m_genInfo.m_osmFileName.empty())  // stdin
    processorThreadsCount = 1;

  if (!GenerateFeatures(processorThreadsCount, rawGeneratorWriter))
    return false;

  rawGeneratorWriter.ShutdownAndJoin();
  m_names = rawGeneratorWriter.GetNames();
  LOG(LINFO, ("Names:", m_names));
  return true;
}

bool RawGenerator::GenerateFeatures(
    unsigned int threadsCount, RawGeneratorWriter & /* rawGeneratorWriter */)
{
  auto translators = std::vector<std::shared_ptr<TranslatorInterface>>{};
  auto sourceMap = boost::optional<boost::iostreams::mapped_file_source>{};
  if (!m_genInfo.m_osmFileName.empty())
  {
    sourceMap = MakeFileMap(m_genInfo.m_osmFileName);
    LOG_SHORT(LINFO, ("Reading OSM data from", m_genInfo.m_osmFileName));
  }

  std::vector<std::thread> threads;
  for (unsigned int i = 0; i < threadsCount; ++i)
  {
    auto translator = m_translators->Clone();
    translators.push_back(translator);

    constexpr size_t chunkSize = 10'000;
    auto processorMaker =
        [osmFileType = m_genInfo.m_osmFileType, threadsCount, i, chunkSize] (auto & reader)
            -> std::unique_ptr<ProcessorOsmElementsInterface>
    {
      switch (osmFileType)
      {
      case feature::GenerateInfo::OsmSourceType::O5M:
        return std::make_unique<ProcessorOsmElementsFromO5M>(reader, threadsCount, i, chunkSize);
      case feature::GenerateInfo::OsmSourceType::XML:
        return std::make_unique<ProcessorOsmElementsFromXml>(reader);
      }
      UNREACHABLE();
    };

    threads.emplace_back([translator, processorMaker, &sourceMap] {
      if (!sourceMap)
      {
        auto reader = SourceReader{};
        auto processor = processorMaker(reader);
        TranslateToFeatures(*processor, *translator);
        return;
      }

      namespace io = boost::iostreams;
      auto && sourceArray = io::array_source{sourceMap->data(), sourceMap->size()};
      auto && stream = io::stream<io::array_source>{sourceArray, std::ios::binary};
      auto && reader = SourceReader(stream);
      auto processor = processorMaker(reader);
      TranslateToFeatures(*processor, *translator);
    });
  }
  for (auto & thread : threads)
    thread.join();
  LOG(LINFO, ("Input was processed."));

  return FinishTranslation(translators);
}

// static
void RawGenerator::TranslateToFeatures(ProcessorOsmElementsInterface & sourceProcessor,
                                       TranslatorInterface & translator)
{
  OsmElement osmElement{};
  while (sourceProcessor.TryRead(osmElement))
    translator.Emit(osmElement);
}

bool RawGenerator::FinishTranslation(
    std::vector<std::shared_ptr<TranslatorInterface>> & translators)
{
  using TranslatorPtr = std::shared_ptr<TranslatorInterface>;

  base::threads::ThreadSafeQueue<std::future<TranslatorPtr>> queue;
  for (auto const & translator : translators)
  {
    std::promise<TranslatorPtr> p;
    p.set_value(translator);
    queue.Push(p.get_future());
  }
  CHECK_GREATER_OR_EQUAL(queue.Size(), 1, ());

  base::thread_pool::computational::ThreadPool pool(queue.Size() / 2 + 1);
  while (queue.Size() != 1)
  {
    std::future<TranslatorPtr> left;
    std::future<TranslatorPtr> right;
    queue.WaitAndPop(left);
    queue.WaitAndPop(right);
    queue.Push(pool.Submit([left{move(left)}, right{move(right)}]() mutable {
      auto leftTranslator = left.get();
      auto rigthTranslator = right.get();
      rigthTranslator->Finish();
      leftTranslator->Finish();
      leftTranslator->Merge(*rigthTranslator);
      return leftTranslator;
    }));
  }

  std::future<TranslatorPtr> translatorFuture;
  queue.WaitAndPop(translatorFuture);
  auto translator = translatorFuture.get();
  translator->Finish();
  return translator->Save();
}

boost::iostreams::mapped_file_source RawGenerator::MakeFileMap(std::string const & filename)
{
  CHECK(!filename.empty(), ());
  auto fileMap = boost::iostreams::mapped_file_source{filename};
  if (!fileMap.is_open())
    MYTHROW(Writer::OpenException, ("Failed to open", filename));

  // Try aggressively (MADV_WILLNEED) and asynchronously read ahead the o5m-file.
  auto readaheadTask = std::thread([data = fileMap.data(), size = fileMap.size()] {
    ::madvise(const_cast<char*>(data), size, MADV_WILLNEED);
  });
  readaheadTask.detach();

  return fileMap;
}

}  // namespace generator

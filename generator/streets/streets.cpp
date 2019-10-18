#include "generator/streets/streets.hpp"

#include "generator/regions/region_info_getter.hpp"
#include "generator/streets/streets_builder.hpp"

#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/timer.hpp"


namespace generator
{
namespace streets
{
void GenerateStreets(std::string const & pathInRegionsIndex, std::string const & pathInRegionsKv,
                     std::string const & pathInStreetsTmpMwm,
                     std::string const & pathInGeoObjectsTmpMwm,
                     std::string const & pathOutStreetsKv,
                     bool /*verbose*/, size_t threadsCount)
{
  LOG(LINFO, ("Start generating streets..."));
  auto timer = base::Timer();
  SCOPE_GUARD(finishGeneratingStreets, [&timer]() {
    LOG(LINFO, ("Finish generating streets.", timer.ElapsedSeconds(), "seconds."));
  });

  regions::RegionInfoGetter regionInfoGetter{pathInRegionsIndex, pathInRegionsKv};
  LOG(LINFO, ("Size of regions key-value storage:", regionInfoGetter.GetStorage().Size()));

  auto const regionFinder = [&regionInfoGetter] (auto && point, auto && selector) {
    return regionInfoGetter.FindDeepest(point, selector);
  };
  StreetsBuilder streetsBuilder{regionFinder, threadsCount};

  streetsBuilder.AssembleStreets(pathInStreetsTmpMwm);
  LOG(LINFO, ("Streets were built."));

  streetsBuilder.AssembleBindings(pathInGeoObjectsTmpMwm);
  LOG(LINFO, ("Binding's streets were built."));

  streetsBuilder.RegenerateAggregatedStreetsFeatures(pathInStreetsTmpMwm);
  LOG(LINFO, ("Streets features are aggreated into", pathInStreetsTmpMwm));

  std::ofstream streamStreetsKv(pathOutStreetsKv);
  auto const regionGetter = [&regionStorage = regionInfoGetter.GetStorage()](uint64_t id) {
    return regionStorage.Find(id);
  };
  streetsBuilder.SaveStreetsKv(regionGetter, streamStreetsKv);
  LOG(LINFO, ("Streets key-value storage saved to", pathOutStreetsKv));
}
}  // namespace streets
}  // namespace generator

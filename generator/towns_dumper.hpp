#pragma once

#include "geometry/latlon.hpp"
#include "geometry/mercator.hpp"
#include "geometry/rect2d.hpp"

#include "generator/osm_element.hpp"

#include "base/string_utils.hpp"

#include <mutex>
#include <string>
#include <vector>

class TownsDumper
{
public:
  TownsDumper();

  void CheckElement(OsmElement const & em, bool concurrent);

  void Dump(std::string const & filePath);

private:
  void FilterTowns();

  struct Town
  {
    ms::LatLon point;
    uint64_t id;
    bool capital;
    uint64_t population;

    Town(double lat, double lon, uint64_t id, bool isCapital, uint64_t population)
      : point(lat, lon), id(id), capital(isCapital), population(population)
    {
    }

    bool operator<(Town const & rhs) const { return population < rhs.population; }
    m2::RectD GetLimitRect() const
    {
      return m2::RectD(MercatorBounds::FromLatLon(point), MercatorBounds::FromLatLon(point));
    }
  };

  std::vector<Town> m_records;
  std::mutex m_updateMutex;
};

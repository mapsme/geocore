#include "geocoder/types.hpp"

#include "base/assert.hpp"

using namespace std;

namespace geocoder
{
string ToString(Type type)
{
  switch (type)
  {
  case Type::Country: return "country";
  case Type::Region: return "region";
  case Type::Subregion: return "subregion";
  case Type::Locality: return "locality";
  case Type::Suburb: return "suburb";
  case Type::Sublocality: return "sublocality";
  case Type::Street: return "street";
  case Type::Building: return "building";
  case Type::Count: return "count";
  }
  UNREACHABLE();
}

string DebugPrint(Type type)
{
  return ToString(type);
}

char const * ToString(Kind kind)
{
  switch (kind)
  {
  case Kind::Country:
    return "country";
  case Kind::State:
    return "state";
  case Kind::Province:
    return "province";
  case Kind::District:
    return "district";
  case Kind::County:
    return "county";
  case Kind::Municipality:
    return "municipality";
  case Kind::City:
    return "city";
  case Kind::Town:
    return "town";
  case Kind::Village:
    return "village";
  case Kind::Hamlet:
    return "hamlet";
  case Kind::IsolatedDwelling:
    return "isolated_dwelling";
  case Kind::Suburb:
    return "suburb";
  case Kind::Quarter:
    return "quarter";
  case Kind::Neighbourhood:
    return "neighbourhood";
  case Kind::Street:
    return "street";
  case Kind::Building:
    return "building";
  case Kind::Unknown:
  case Kind::Count:
    UNREACHABLE();
  };
  UNREACHABLE();
}

Kind KindFromString(std::string const & str)
{
  static auto const string2kindMap = std::map<std::string, Kind>{
    {"country", Kind::Country},
    {"state", Kind::State},
    {"province", Kind::Province},
    {"district", Kind::District},
    {"county", Kind::County},
    {"municipality", Kind::Municipality},
    {"city", Kind::City},
    {"town", Kind::Town},
    {"village", Kind::Village},
    {"hamlet", Kind::Hamlet},
    {"isolated_dwelling", Kind::IsolatedDwelling},
    {"suburb", Kind::Suburb},
    {"quarter", Kind::Quarter},
    {"neighbourhood", Kind::Neighbourhood},
    {"street", Kind::Street},
    {"building", Kind::Building},
  };
  auto it = string2kindMap.find(str);
  return it != string2kindMap.end() ? it->second : Kind::Unknown;
}
}  // namespace geocoder

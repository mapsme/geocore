#pragma once

#include "base/string_utils.hpp"

#include <string>
#include <vector>

namespace geocoder
{
enum : unsigned int { kIndexFormatVersion = 2 };

using Tokens = std::vector<std::string>;

enum class Type
{
  // It is important that the types are ordered from
  // the more general to the more specific.
  Country,
  Region,
  Subregion,
  Locality,
  Suburb,
  Sublocality,
  Street,
  Building,

  Count
};

std::string ToString(Type type);
std::string DebugPrint(Type type);

enum class Kind
{
  Unknown = 0,

  Country,
  State,
  Province,
  District,
  County,
  Municipality,
  City,
  Town,
  Village,
  Hamlet,
  IsolatedDwelling,
  Suburb,
  Quarter,
  Neighbourhood,
  Street,
  Building,

  Count
};

char const * ToString(Kind kind);
Kind KindFromString(std::string const & str);
}  // namespace geocoder

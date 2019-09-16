#include "base/gmtime.hpp"

namespace base
{
std::tm GmTime(time_t const time)
{
  std::tm result{};
  gmtime_r(&time, &result);

  return result;
}
}  // namespace base

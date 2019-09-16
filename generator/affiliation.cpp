#include "generator/affiliation.hpp"

namespace feature
{
SingleAffiliation::SingleAffiliation(std::string const & filename)
  : m_filename(filename)
{
}

std::vector<std::string> SingleAffiliation::GetAffiliations(FeatureBuilder const &) const
{
  return {m_filename};
}

bool SingleAffiliation::HasRegionByName(std::string const & name) const
{
  return name == m_filename;
}
}  // namespace feature

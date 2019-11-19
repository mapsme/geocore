#include "generator/affiliation.hpp"

namespace feature
{
SingleAffiliation::SingleAffiliation(std::string const & filename)
  : m_filename(filename)
  , m_affilations{std::make_shared<std::vector<std::string>>(std::vector<std::string>{m_filename})}
{
}

std::shared_ptr<std::vector<std::string>> SingleAffiliation::GetAffiliations(
    FeatureBuilder const &) const
{
  return m_affilations;
}

bool SingleAffiliation::HasRegionByName(std::string const & name) const
{
  return name == m_filename;
}
}  // namespace feature

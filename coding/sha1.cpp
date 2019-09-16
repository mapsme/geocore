#include "coding/sha1.hpp"


namespace coding
{
// static
SHA1::Hash SHA1::Calculate(std::string const & /*filePath*/)
{
  return {};
}

// static
std::string SHA1::CalculateBase64(std::string const & /*filePath*/)
{
  return {};
}

// static
SHA1::Hash SHA1::CalculateForString(std::string const & /*str*/)
{
  return {};
}

// static
std::string SHA1::CalculateForStringFormatted(std::string const & /*str*/)
{
  return {};
}

// static
std::string SHA1::CalculateBase64ForString(std::string const & /*str*/)
{
  return {};
}
}  // coding

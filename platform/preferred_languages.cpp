#include "platform/preferred_languages.hpp"

#include "base/string_utils.hpp"
#include "base/logging.hpp"
#include "base/macros.hpp"

#include "platform/target_os.hpp"
#include "std/set.hpp"
#include "std/vector.hpp"

#if defined(GEOCORE_OS_MAC)
  #include <CoreFoundation/CFLocale.h>
  #include <CoreFoundation/CFString.h>
#elif defined(GEOCORE_OS_LINUX)
  #include <cstdlib>
#endif

namespace languages
{

void GetSystemPreferred(vector<string> & languages)
{
#if defined(GEOCORE_OS_MAC) || defined(GEOCORE_OS_LINUX)
  // check environment variables
  char const * p = getenv("LANGUAGE");
  if (p && strlen(p))  // LANGUAGE can contain several values divided by ':'
  {
    string const str(p);
    strings::SimpleTokenizer iter(str, ":");
    for (; iter; ++iter)
      languages.push_back(*iter);
  }
  else if ((p = getenv("LC_ALL")))
    languages.push_back(p);
  else if ((p = getenv("LC_MESSAGES")))
    languages.push_back(p);
  else if ((p = getenv("LANG")))
    languages.push_back(p);
#if defined(GEOCORE_OS_MAC)
  else
  {
    // Mac and iOS implementation
    CFArrayRef langs = CFLocaleCopyPreferredLanguages();
    char buf[30];
    for (CFIndex i = 0; i < CFArrayGetCount(langs); ++i)
    {
      CFStringRef strRef = (CFStringRef)CFArrayGetValueAtIndex(langs, i);
      CFStringGetCString(strRef, buf, 30, kCFStringEncodingUTF8);
      languages.push_back(buf);
    }
    CFRelease(langs);
  }
#endif // defined(GEOCORE_OS_MAC)
#endif // defined(GEOCORE_OS_MAC) || defined(GEOCORE_OS_LINUX)
}

string GetPreferred()
{
  vector<string> arr;
  GetSystemPreferred(arr);

  // generate output string
  string result;
  for (size_t i = 0; i < arr.size(); ++i)
  {
    result.append(arr[i]);
    result.push_back('|');
  }

  if (result.empty())
    result = "default";
  else
    result.resize(result.size() - 1);
  return result;
}

string GetCurrentOrig()
{
  vector<string> arr;
  GetSystemPreferred(arr);
  if (arr.empty())
    return "en";
  else
    return arr[0];
}

string Normalize(string const & lang)
{
  strings::SimpleTokenizer const iter(lang, "-_ ");
  ASSERT(iter, (lang));
  return *iter;
}

string GetCurrentNorm()
{
  return Normalize(GetCurrentOrig());
}

string GetCurrentTwine()
{
  string const lang = GetCurrentOrig();
  // Special cases for different Chinese variations.
  if (lang.find("zh") == 0)
  {
    string lower = lang;
    strings::AsciiToLower(lower);

    // Traditional Chinese.
    for (char const * s : {"hant", "tw", "hk", "mo"})
    {
      if (lower.find(s) != string::npos)
        return "zh-Hant";
    }

    // Simplified Chinese by default for all other cases.
    return "zh-Hans";
  }
  // Use short (2 or 3 chars) versions for all other languages.
  return Normalize(lang);
}

}  // namespace languages

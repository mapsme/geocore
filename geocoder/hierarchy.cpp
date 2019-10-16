#include "geocoder/hierarchy.hpp"

#include "indexer/search_string_utils.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"
#include "base/macros.hpp"
#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <utility>

using namespace std;

namespace geocoder
{
// Hierarchy::Entry --------------------------------------------------------------------------------
bool Hierarchy::Entry::DeserializeFromJSON(string const & jsonStr,
                                           NameDictionaryBuilder & normalizedNameDictionaryBuilder,
                                           ParsingStats & stats)
{
  try
  {
    coding::JsonDocument document;
    document.Parse(jsonStr);

    return DeserializeFromJSONImpl(document, jsonStr, normalizedNameDictionaryBuilder, stats);
  }
  catch (coding::JsonException const & e)
  {
    LOG(LDEBUG, ("Can't parse entry:", e.Msg(), jsonStr));
  }
  return false;
}

// todo(@m) Factor out to geojson.hpp? Add geojson to myjansson?
bool Hierarchy::Entry::DeserializeFromJSONImpl(
    coding::JsonDocument const & root, string const & jsonStr,
    NameDictionaryBuilder & normalizedNameDictionaryBuilder, ParsingStats & stats)
{
  if (!root.IsObject())
  {
    ++stats.m_badJsons;
    MYTHROW(coding::JsonException, ("Not a json object."));
  }

  if (!DeserializeAddressFromJSON(root, normalizedNameDictionaryBuilder, stats))
    return false;

  coding::JsonValue const & defaultLocale =
      coding::GetJsonObligatoryFieldByPath(root, "properties", "locales", "default");
  coding::FromJsonObjectOptionalField(defaultLocale, "name", m_name);

  if (m_name.empty())
    ++stats.m_emptyNames;

  if (m_type == Type::Count)
  {
    LOG(LDEBUG, ("No address in an hierarchy entry:", jsonStr));
    ++stats.m_emptyAddresses;
  }
  return true;
}

bool Hierarchy::Entry::DeserializeAddressFromJSON(
    coding::JsonDocument const & root, NameDictionaryBuilder & normalizedNameDictionaryBuilder,
    ParsingStats & stats)
{
  coding::JsonValue const & properties = coding::GetJsonObligatoryField(root, "properties");
  coding::JsonValue const & locales = coding::GetJsonObligatoryField(properties, "locales");

  m_normalizedAddress = {};
  for (size_t i = 0; i < static_cast<size_t>(Type::Count); ++i)
  {
    Type const type = static_cast<Type>(i);
    MultipleNames multipleNames;
    if (!FetchAddressFieldNames(locales, type, multipleNames))
    {
      return false;
    }

    if (!multipleNames.GetMainName().empty())
    {
      m_normalizedAddress[i] = normalizedNameDictionaryBuilder.Add(move(multipleNames));
      m_type = static_cast<Type>(i);
    }
  }
  coding::JsonValue const & rank = coding::GetJsonOptionalField(properties, "rank");

  if (!rank.IsNull())
  {
    auto const type = RankToType(rank.GetInt());
    if (type != Type::Count &&
        m_normalizedAddress[static_cast<size_t>(type)] != NameDictionary::kUnspecifiedPosition)
    {
      m_type = type;
    }
  }

  auto const & subregion = m_normalizedAddress[static_cast<size_t>(Type::Subregion)];
  auto const & locality = m_normalizedAddress[static_cast<size_t>(Type::Locality)];
  if (m_type == Type::Street && locality == NameDictionary::kUnspecifiedPosition &&
      subregion == NameDictionary::kUnspecifiedPosition)
  {
    ++stats.m_noLocalityStreets;
    return false;
  }
  if (m_type == Type::Building && locality == NameDictionary::kUnspecifiedPosition &&
      subregion == NameDictionary::kUnspecifiedPosition)
  {
    ++stats.m_noLocalityBuildings;
    return false;
  }

  return true;
}

// static
bool Hierarchy::Entry::FetchAddressFieldNames(coding::JsonValue const & locales, Type type,
                                              MultipleNames & multipleNames)
{
  string const & levelKey = ToString(type);
  Tokens tokens;

  for (coding::JsonValue::ConstMemberIterator itr = locales.MemberBegin();
       itr != locales.MemberEnd(); ++itr)
  {
    coding::JsonValue const & address = coding::GetJsonObligatoryField(itr->value, "address");
    coding::JsonValue const & levelJson = coding::GetJsonOptionalField(address, levelKey);

    if (levelJson.IsNull())
      continue;

    std::string levelValue;
    coding::FromJson(levelJson, levelValue);
    if (levelValue.empty())
      continue;

    search::NormalizeAndTokenizeAsUtf8(levelValue, tokens);
    if (tokens.empty())
      continue;

    auto normalizedValue = strings::JoinStrings(tokens, " ");
    static std::string defaultLocale = "default";
    if (itr->name == defaultLocale)
      multipleNames.SetMainName(normalizedValue);
    else
      multipleNames.AddAltName(normalizedValue);
  }

  return true;
}

MultipleNames const & Hierarchy::Entry::GetNormalizedMultipleNames(
    Type type, NameDictionary const & normalizedNameDictionary) const
{
  auto const & addressField = m_normalizedAddress[static_cast<size_t>(type)];
  return normalizedNameDictionary.Get(addressField);
}

bool Hierarchy::Entry::HasFieldInAddress(Type type) const
{
  return m_normalizedAddress[static_cast<size_t>(type)] != NameDictionary::kUnspecifiedPosition;
}

// static
Type Hierarchy::Entry::RankToType(uint8_t rank)
{
  switch (rank)
  {
  case 1: return Type::Country;
  case 2: return Type::Region;
  case 3: return Type::Subregion;
  case 4: return Type::Locality;
  }

  return Type::Count;
}

// Hierarchy ---------------------------------------------------------------------------------------
Hierarchy::Hierarchy(vector<Entry> && entries, NameDictionary && normalizedNameDictionary,
                     std::string && dataVersion)
  : m_entries{move(entries)}
  , m_normalizedNameDictionary{move(normalizedNameDictionary)}
  , m_dataVersion(move(dataVersion))
{
  if (!is_sorted(m_entries.begin(), m_entries.end()))
  {
    LOG(LINFO, ("Sorting entries..."));
    sort(m_entries.begin(), m_entries.end());
  }
}

vector<Hierarchy::Entry> const & Hierarchy::GetEntries() const { return m_entries; }

NameDictionary const & Hierarchy::GetNormalizedNameDictionary() const
{
  return m_normalizedNameDictionary;
}

Hierarchy::Entry const * Hierarchy::GetEntryForOsmId(base::GeoObjectId const & osmId) const
{
  auto const cmp = [](Hierarchy::Entry const & e, base::GeoObjectId const & id) {
    return e.m_osmId < id;
  };

  auto const it = lower_bound(m_entries.begin(), m_entries.end(), osmId, cmp);

  if (it == m_entries.end() || it->m_osmId != osmId)
    return nullptr;

  return &(*it);
}

bool Hierarchy::IsParentTo(Hierarchy::Entry const & entry, Hierarchy::Entry const & toEntry) const
{
  for (size_t i = 0; i < static_cast<size_t>(geocoder::Type::Count); ++i)
  {
    if (entry.m_normalizedAddress[i] == NameDictionary::kUnspecifiedPosition)
      continue;

    if (toEntry.m_normalizedAddress[i] == NameDictionary::kUnspecifiedPosition)
      return false;

    auto const pos1 = entry.m_normalizedAddress[i];
    auto const pos2 = toEntry.m_normalizedAddress[i];
    if (pos1 == pos2)
      continue;

    auto const & name1 = m_normalizedNameDictionary.Get(pos1).GetMainName();
    auto const & name2 = m_normalizedNameDictionary.Get(pos2).GetMainName();
    if (name1 != name2)
      return false;
  }
  return true;
}
}  // namespace geocoder

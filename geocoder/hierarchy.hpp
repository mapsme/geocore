#pragma once

#include "geocoder/name_dictionary.hpp"
#include "geocoder/types.hpp"

#include "base/assert.hpp"
#include "base/geo_object_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <boost/serialization/array.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/version.hpp>

#include "3party/jansson/myjansson.hpp"

namespace geocoder
{
class Hierarchy
{
public:
  struct ParsingStats
  {
    // Number of entries that the hierarchy was constructed from.
    uint64_t m_numLoaded = 0;

    // Number of corrupted json lines.
    uint64_t m_badJsons = 0;

    // Number of entries with unreadable base::GeoObjectIds.
    uint64_t m_badOsmIds = 0;

    // Number of base::GeoObjectsIds that occur as keys in at least two entries.
    uint64_t m_duplicateOsmIds = 0;

    // Number of entries with duplicate subfields in the address field.
    uint64_t m_duplicateAddresses = 0;

    // Number of entries whose address field either does
    // not exist or consists of empty lines.
    uint64_t m_emptyAddresses = 0;

    // Number of entries without the name field or with an empty one.
    uint64_t m_emptyNames = 0;

    // Number of street entries without a locality name.
    uint64_t m_noLocalityStreets = 0;

    // Number of building entries without a locality name.
    uint64_t m_noLocalityBuildings = 0;

    // Number of entries whose names do not match the most
    // specific parts of their addresses.
    // This is expected from POIs but not from regions or streets.
    uint64_t m_mismatchedNames = 0;
  };

  // A single entry in the hierarchy directed acyclic graph.
  // Currently, this is more or less the "properties"-"address"
  // part of the geojson entry.
  struct Entry
  {
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
      CHECK_EQUAL(version, kIndexFormatVersion, ());
      ar & m_osmId;
      ar & m_name;
      ar & m_type;
      ar & m_normalizedAddress;
    }

    bool DeserializeFromJSON(std::string const & jsonStr,
                             NameDictionaryBuilder & normalizedNameDictionaryBuilder,
                             ParsingStats & stats);
    bool DeserializeFromJSONImpl(json_t * const root, std::string const & jsonStr,
                                 NameDictionaryBuilder & normalizedNameDictionaryBuilder,
                                 ParsingStats & stats);
    bool DeserializeAddressFromJSON(json_t * const root,
                                    NameDictionaryBuilder & normalizedNameDictionaryBuilder,
                                    ParsingStats & stats);
    static bool FetchAddressFieldNames(json_t * const locales, Type type,
                                       MultipleNames & multipleNames,
                                       NameDictionaryBuilder & normalizedNameDictionaryBuilder,
                                       ParsingStats & stats);
    // See generator::regions::LevelRegion::GetRank().
    static Type RankToType(uint8_t rank);

    MultipleNames const & GetNormalizedMultipleNames(
        Type type, NameDictionary const & normalizedNameDictionary) const;
    bool operator<(Entry const & rhs) const { return m_osmId < rhs.m_osmId; }

    base::GeoObjectId m_osmId = base::GeoObjectId(base::GeoObjectId::kInvalid);

    // Original name of the entry. Useful for debugging.
    std::string m_name;

    Type m_type = Type::Count;

    // The positions of entry address fields in normalized name dictionary, one per Type.
    std::array<NameDictionary::Position, static_cast<size_t>(Type::Count)> m_normalizedAddress{};
  };

  Hierarchy() = default;
  Hierarchy(std::vector<Entry> && entries, NameDictionary && normalizeNameDictionary,
            std::string && dataVersion);

  template<class Archive>
  void serialize(Archive & ar, const unsigned int version)
  {
    CHECK_EQUAL(version, kIndexFormatVersion, ());
    ar & m_entries;
    ar & m_normalizedNameDictionary;
    ar & m_dataVersion;
  }

  std::vector<Entry> const & GetEntries() const;
  NameDictionary const & GetNormalizedNameDictionary() const;

  Entry const * GetEntryForOsmId(base::GeoObjectId const & osmId) const;
  bool IsParentTo(Hierarchy::Entry const & entry, Hierarchy::Entry const & toEntry) const;

  std::string const & GetDataVersion() const
  {
    return m_dataVersion;
  }

private:
  std::vector<Entry> m_entries;
  NameDictionary m_normalizedNameDictionary;
  std::string m_dataVersion;
};
}  // namespace geocoder

BOOST_CLASS_VERSION(geocoder::Hierarchy, geocoder::kIndexFormatVersion)
BOOST_CLASS_VERSION(geocoder::Hierarchy::Entry, geocoder::kIndexFormatVersion)

namespace boost
{
namespace serialization
{
template<class Archive>
inline void serialize(Archive & ar, base::GeoObjectId & t, const unsigned int version)
{
  split_free(ar, t, version);
}

template<class Archive>
inline void save(Archive & ar, base::GeoObjectId const & t, const unsigned int /*version*/)
{
  ar & t.GetEncodedId();
}

template<class Archive>
inline void load(Archive & ar, base::GeoObjectId & t, const unsigned int /*version*/)
{
  uint64_t encodedId = 0;
  ar & encodedId;
  t = base::GeoObjectId(encodedId);
}
}  // namespace serialization
}  // namespace boost

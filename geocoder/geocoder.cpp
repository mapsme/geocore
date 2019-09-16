#include "geocoder/geocoder.hpp"

#include "geocoder/hierarchy_reader.hpp"

#include "geocoder/house_numbers_matcher.hpp"

#include "indexer/search_string_utils.hpp"

#include "base/assert.hpp"
#include "base/exception.hpp"
#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"
#include "base/timer.hpp"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <set>
#include <thread>
#include <utility>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/optional.hpp>

using namespace std;

namespace geocoder
{
namespace
{
size_t const kMaxResults = 100;

// While Result's |m_certainty| is deliberately vaguely defined,
// current implementation is a log-prob type measure of our belief
// that the labeling of tokens is correct, provided the labeling is
// possible with respect to the IsParentTo relation on entries.
// In other words, non-scaled post-probabilities are
//   log(Prob(Country|token)) ~ 10
//   log(Prob(Region|token)) ~ 5
//   etc.
// The greater their sum, the more likely it is that we guessed the
// token types right.
//
// The reasoning is as follows. A naïve weighing would look how many query tokens
// are covered with the current parse and assign this fraction to certainty.
// Turns out, it works badly since a single matched long street in the query
// (i.e., wrong city, wrong region, wrong locality, correct street) can shadow a more
// relevant result (correct city, correct locality, wrong street) in the case where
// the database does not contain an exact match. So let's make some parts of the
// query heavier (heuristically). This turns out to work more predictable.
double GetWeight(Type t)
{
  switch (t)
  {
  case Type::Country: return 10.0;
  case Type::Region: return 5.0;
  case Type::Subregion: return 4.0;
  case Type::Locality: return 3.0;
  case Type::Suburb: return 3.0;
  case Type::Sublocality: return 2.0;
  case Type::Street: return 1.0;
  case Type::Building: return 0.1;
  case Type::Count: return 0.0;
  }
  UNREACHABLE();
}

// todo(@m) This is taken from search/geocoder.hpp. Refactor.
class ScopedMarkTokens
{
public:
  // The range is [l, r).
  ScopedMarkTokens() = delete;
  ScopedMarkTokens(Geocoder::Context & context, Type type, size_t l, size_t r)
    : m_context(context), m_type(type), m_l(l), m_r(r)
  {
    CHECK_LESS_OR_EQUAL(l, r, ());
    CHECK_LESS_OR_EQUAL(r, context.GetNumTokens(), ());

    for (size_t i = m_l; i < m_r; ++i)
      m_context.MarkToken(i, m_type);
  }

  ~ScopedMarkTokens()
  {
    for (size_t i = m_l; i < m_r; ++i)
      m_context.MarkToken(i, Type::Count);
  }

private:
  Geocoder::Context & m_context;
  Type const m_type;
  size_t m_l = 0;
  size_t m_r = 0;
};

void MarkStreetSynonym(Geocoder::Context & ctx, boost::optional<ScopedMarkTokens> & mark)
{
  for (size_t tokId = 0; tokId < ctx.GetNumTokens(); ++tokId)
  {
    auto const t = ctx.GetTokenType(tokId);
    if (t == Type::Count && search::IsStreetSynonym(strings::MakeUniString(ctx.GetToken(tokId))))
    {
      mark.emplace(ctx, Type::Street, tokId, tokId + 1);
      return;
    }
  }
}

Type NextType(Type type)
{
  CHECK_NOT_EQUAL(type, Type::Count, ());
  auto t = static_cast<size_t>(type);
  return static_cast<Type>(t + 1);
}

strings::UniString MakeHouseNumber(Tokens const & tokens)
{
  return strings::MakeUniString(strings::JoinStrings(tokens, " "));
}
}  // namespace

// Geocoder::Context -------------------------------------------------------------------------------
Geocoder::Context::Context(string const & query) : m_beam(kMaxResults)
{
  search::NormalizeAndTokenizeAsUtf8(query, m_tokens);
  m_tokenTypes.assign(m_tokens.size(), Type::Count);
  m_numUsedTokens = 0;
}

vector<Type> & Geocoder::Context::GetTokenTypes() { return m_tokenTypes; }

size_t Geocoder::Context::GetNumTokens() const { return m_tokens.size(); }

size_t Geocoder::Context::GetNumUsedTokens() const
{
  CHECK_LESS_OR_EQUAL(m_numUsedTokens, m_tokens.size(), ());
  return m_numUsedTokens;
}

Type Geocoder::Context::GetTokenType(size_t id) const
{
  CHECK_LESS(id, m_tokenTypes.size(), ());
  return m_tokenTypes[id];
}

string const & Geocoder::Context::GetToken(size_t id) const
{
  CHECK_LESS(id, m_tokens.size(), ());
  return m_tokens[id];
}

void Geocoder::Context::MarkToken(size_t id, Type type)
{
  CHECK_LESS(id, m_tokens.size(), ());
  bool wasUsed = m_tokenTypes[id] != Type::Count;
  m_tokenTypes[id] = type;
  bool nowUsed = m_tokenTypes[id] != Type::Count;

  if (wasUsed && !nowUsed)
    --m_numUsedTokens;
  if (!wasUsed && nowUsed)
    ++m_numUsedTokens;
}

bool Geocoder::Context::IsTokenUsed(size_t id) const
{
  CHECK_LESS(id, m_tokens.size(), ());
  return m_tokenTypes[id] != Type::Count;
}

bool Geocoder::Context::AllTokensUsed() const { return m_numUsedTokens == m_tokens.size(); }

void Geocoder::Context::AddResult(base::GeoObjectId const & osmId, double certainty, Type type,
                                  vector<size_t> const & tokenIds, vector<Type> const & allTypes)
{
  m_beam.Add(BeamKey(osmId, type, tokenIds, allTypes), certainty);
}

void Geocoder::Context::FillResults(vector<Result> & results) const
{
  results.clear();
  results.reserve(m_beam.GetEntries().size());

  set<base::GeoObjectId> seen;
  bool const hasPotentialHouseNumber = !m_houseNumberPositionsInQuery.empty();
  for (auto const & e : m_beam.GetEntries())
  {
    if (!seen.insert(e.m_key.m_osmId).second)
      continue;

    if (hasPotentialHouseNumber && !IsGoodForPotentialHouseNumberAt(e.m_key, m_houseNumberPositionsInQuery))
      continue;

    results.emplace_back(e.m_key.m_osmId, e.m_value /* certainty */);
  }

  if (!results.empty())
  {
    auto const by = results.front().m_certainty;
    for (auto & r : results)
    {
      r.m_certainty /= by;
      ASSERT_GREATER_OR_EQUAL(r.m_certainty, 0.0, ());
      ASSERT_LESS_OR_EQUAL(r.m_certainty, 1.0, ());
    }
  }

  ASSERT(is_sorted(results.rbegin(), results.rend(), base::LessBy(&Result::m_certainty)), ());
  ASSERT_LESS_OR_EQUAL(results.size(), kMaxResults, ());
}

vector<Geocoder::Layer> & Geocoder::Context::GetLayers() { return m_layers; }

vector<Geocoder::Layer> const & Geocoder::Context::GetLayers() const { return m_layers; }

void Geocoder::Context::MarkHouseNumberPositionsInQuery(vector<size_t> const & tokenIds)
{
  m_houseNumberPositionsInQuery.insert(tokenIds.begin(), tokenIds.end());
}

bool Geocoder::Context::IsGoodForPotentialHouseNumberAt(BeamKey const & beamKey,
                                                        set<size_t> const & tokenIds) const
{
  if (beamKey.m_tokenIds.size() == m_tokens.size())
    return true;

  if (IsBuildingWithAddress(beamKey))
    return true;

  // Pass street, locality or region with number in query address parts.
  if (HasLocalityOrRegion(beamKey) && ContainsTokenIds(beamKey, tokenIds))
    return true;

  return false;
}

bool Geocoder::Context::IsBuildingWithAddress(BeamKey const & beamKey) const
{
  if (beamKey.m_type != Type::Building)
    return false;

  bool gotLocality = false;
  bool gotStreet = false;
  bool gotBuilding = false;
  for (Type t : beamKey.m_allTypes)
  {
    if (t == Type::Region || t == Type::Subregion || t == Type::Locality)
      gotLocality = true;
    if (t == Type::Street)
      gotStreet = true;
    if (t == Type::Building)
      gotBuilding = true;
  }
  return gotLocality && gotStreet && gotBuilding;
}

bool Geocoder::Context::HasLocalityOrRegion(BeamKey const & beamKey) const
{
  for (Type t : beamKey.m_allTypes)
  {
    if (t == Type::Region || t == Type::Subregion || t == Type::Locality)
      return true;
  }

  return false;
}

bool Geocoder::Context::ContainsTokenIds(BeamKey const & beamKey, set<size_t> const & needTokenIds) const
{
  auto const & keyTokenIds = beamKey.m_tokenIds;
  return base::Includes(keyTokenIds.begin(), keyTokenIds.end(), needTokenIds.begin(), needTokenIds.end());
}

// Geocoder ----------------------------------------------------------------------------------------
void Geocoder::LoadFromJsonl(std::string const & pathToJsonHierarchy, unsigned int loadThreadsCount)
try
{
  using namespace boost::iostreams;
  filtering_istreambuf fileStreamBuf;

  if (strings::EndsWith(pathToJsonHierarchy, ".gz"))
    fileStreamBuf.push(gzip_decompressor());

  file_source file(pathToJsonHierarchy);
  if (!file.is_open())
    MYTHROW(OpenException, ("Failed to open file", pathToJsonHierarchy));
  fileStreamBuf.push(file);

  std::istream fileStream(&fileStreamBuf);
  m_hierarchy = HierarchyReader{fileStream}.Read(loadThreadsCount);
  m_index.BuildIndex(loadThreadsCount);
}
catch (boost::exception const & err)
{
  MYTHROW(Exception, ("Failed to load jsonl:", boost::diagnostic_information(err)));
}
catch (std::exception const & err)
{
  MYTHROW(Exception, ("Failed to load jsonl:", err.what()));
}

void Geocoder::LoadFromBinaryIndex(std::string const & pathToTokenIndex)
try
{
  std::ifstream ifs{pathToTokenIndex};
  if (!ifs)
    MYTHROW(OpenException, ("Failed to open file", pathToTokenIndex));
  ifs.exceptions(std::ifstream::badbit | std::ifstream::failbit);

  boost::archive::binary_iarchive ia{ifs};
  ia >> *this;
}
catch (boost::exception const & err)
{
  MYTHROW(Exception, ("Failed to load geocoder index:", boost::diagnostic_information(err)));
}
catch (std::exception const & err)
{
  MYTHROW(Exception, ("Failed to load geocoder index:", err.what()));
}

void Geocoder::SaveToBinaryIndex(std::string const & pathToTokenIndex) const
try
{
  std::ofstream ofs{pathToTokenIndex};
  if (!ofs)
    MYTHROW(OpenException, ("Failed to open file", pathToTokenIndex));
  ofs.exceptions(std::ifstream::badbit | std::ifstream::failbit);

  boost::archive::binary_oarchive oa{ofs};
  oa << *this;
}
catch (boost::exception const & err)
{
  MYTHROW(Exception, ("Failed to save geocoder index:", boost::diagnostic_information(err)));
}
catch (std::exception const & err)
{
  MYTHROW(Exception, ("Failed to save geocoder index:", err.what()));
}

void Geocoder::ProcessQuery(string const & query, vector<Result> & results) const
{
#if defined(DEBUG)
  base::Timer timer;
  SCOPE_GUARD(printDuration, [&timer]() {
    LOG(LINFO, ("Total geocoding time:", timer.ElapsedSeconds(), "seconds"));
  });
#endif

  Context ctx(query);
  Go(ctx, Type::Country);
  ctx.FillResults(results);
}

Hierarchy const & Geocoder::GetHierarchy() const { return m_hierarchy; }

Index const & Geocoder::GetIndex() const { return m_index; }

void Geocoder::Go(Context & ctx, Type type) const
{
  if (ctx.GetNumTokens() == 0)
    return;

  if (ctx.AllTokensUsed())
    return;

  if (type == Type::Count)
    return;

  Tokens subquery;
  vector<size_t> subqueryTokenIds;
  for (size_t i = 0; i < ctx.GetNumTokens(); ++i)
  {
    subquery.clear();
    subqueryTokenIds.clear();
    for (size_t j = i; j < ctx.GetNumTokens(); ++j)
    {
      if (ctx.IsTokenUsed(j))
        break;

      subquery.push_back(ctx.GetToken(j));
      subqueryTokenIds.push_back(j);

      Layer curLayer;
      curLayer.m_type = type;

      // Buildings are indexed separately.
      if (type == Type::Building)
      {
        FillBuildingsLayer(ctx, subquery, subqueryTokenIds, curLayer);
      }
      else
      {
        FillRegularLayer(ctx, type, subquery, curLayer);
      }

      if (curLayer.m_entries.empty())
        continue;

      ScopedMarkTokens mark(ctx, type, i, j + 1);

      auto streetSynonymMark = boost::make_optional(false, ScopedMarkTokens(ctx, type, 0, 0));
      if (type == Type::Street)
        MarkStreetSynonym(ctx, streetSynonymMark);

      AddResults(ctx, curLayer.m_entries);

      ctx.GetLayers().emplace_back(move(curLayer));
      SCOPE_GUARD(pop, [&] { ctx.GetLayers().pop_back(); });

      Go(ctx, NextType(type));
    }
  }

  Go(ctx, NextType(type));
}

void Geocoder::FillBuildingsLayer(Context & ctx, Tokens const & subquery, vector<size_t> const & subqueryTokenIds,
                                  Layer & curLayer) const
{
  if (ctx.GetLayers().empty())
    return;

  auto const & subqueryHN = MakeHouseNumber(subquery);

  if (!search::house_numbers::LooksLikeHouseNumber(subqueryHN, false /* isPrefix */))
    return;

  for_each(ctx.GetLayers().rbegin(), ctx.GetLayers().rend(), [&, this] (auto const & layer) {
    if (layer.m_type != Type::Street && layer.m_type != Type::Locality)
      return;

    // We've already filled a street/location layer and now see something that resembles
    // a house number. While it still can be something else (a zip code, for example)
    // let's stay on the safer side and mark the tokens as potential house number.
    ctx.MarkHouseNumberPositionsInQuery(subqueryTokenIds);

    for (auto const & docId : layer.m_entries)
    {
      m_index.ForEachRelatedBuilding(docId, [&](Index::DocId const & buildingDocId) {
        auto const & bld = m_index.GetDoc(buildingDocId);
        auto const & multipleHN = bld.GetNormalizedMultipleNames(
            Type::Building, m_hierarchy.GetNormalizedNameDictionary());
        auto const & realHN = multipleHN.GetMainName();
        auto const & realHNUniStr = strings::MakeUniString(realHN);
        if (search::house_numbers::HouseNumbersMatch(realHNUniStr, subqueryHN,
                                                     false /* queryIsPrefix */))
        {
          curLayer.m_entries.emplace_back(buildingDocId);
        }
      });
    }
  });
}

void Geocoder::FillRegularLayer(Context const & ctx, Type type, Tokens const & subquery,
                                Layer & curLayer) const
{
  m_index.ForEachDocId(subquery, [&](Index::DocId const & docId) {
    auto const & d = m_index.GetDoc(docId);
    if (d.m_type != type)
      return;

    if (ctx.GetLayers().empty() || HasParent(ctx.GetLayers(), d))
    {
      if (type > Type::Locality && !IsRelevantLocalityMember(ctx, d, subquery))
        return;

      curLayer.m_entries.emplace_back(docId);
    }
  });
}

void Geocoder::AddResults(Context & ctx, std::vector<Index::DocId> const & entries) const
{
  double certainty = 0;
  vector<size_t> tokenIds;
  vector<Type> allTypes;
  for (size_t tokId = 0; tokId < ctx.GetNumTokens(); ++tokId)
  {
    auto const t = ctx.GetTokenType(tokId);
    certainty += GetWeight(t);
    if (t != Type::Count)
    {
      tokenIds.push_back(tokId);
      allTypes.push_back(t);
    }
  }

  for (auto const & docId : entries)
  {
    auto const & entry = m_index.GetDoc(docId);

    auto entryCertainty = certainty;
    if (entry.m_type == Type::Locality)
    {
      auto const localityName = entry.m_normalizedAddress[static_cast<size_t>(Type::Locality)];

      if (entry.m_normalizedAddress[static_cast<size_t>(Type::Region)] == localityName)
        entryCertainty += GetWeight(Type::Region);

      if (entry.m_normalizedAddress[static_cast<size_t>(Type::Subregion)] == localityName)
        entryCertainty += GetWeight(Type::Subregion);
    }

    ctx.AddResult(entry.m_osmId, entryCertainty, entry.m_type, tokenIds, allTypes);
  }
}

bool Geocoder::HasParent(vector<Geocoder::Layer> const & layers, Hierarchy::Entry const & e) const
{
  CHECK(!layers.empty(), ());
  auto const & layer = layers.back();
  for (auto const & docId : layer.m_entries)
  {
    // Note that the relationship is somewhat inverted: every ancestor
    // is stored in the address but the nodes have no information
    // about their children.
    if (m_hierarchy.IsParentTo(m_index.GetDoc(docId), e))
      return true;
  }
  return false;
}

bool Geocoder::IsRelevantLocalityMember(Context const & ctx, Hierarchy::Entry const & member,
                                        Tokens const & subquery) const
{
  auto const isNumeric = subquery.size() == 1 && strings::IsASCIINumeric(subquery.front());
  return !isNumeric || HasMemberLocalityInMatching(ctx, member);
}

bool Geocoder::HasMemberLocalityInMatching(Context const & ctx, Hierarchy::Entry const & member) const
{
  for (auto const & layer : ctx.GetLayers())
  {
    auto const layerType = layer.m_type;
    if (layerType > Type::Locality)
      break;
    if (layerType != Type::Locality)
      continue;

    for (auto const docId : layer.m_entries)
    {
      auto const & matchedEntry = m_index.GetDoc(docId);
      if (m_hierarchy.IsParentTo(matchedEntry, member))
        return true;
    }
  }

  return false;
}
}  // namespace geocoder

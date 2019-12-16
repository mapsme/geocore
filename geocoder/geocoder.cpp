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
#include <boost/optional.hpp>
#include <boost/range/adaptor/reversed.hpp>

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
//   log(Prob(Region|token)) ~ 4
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
  case Type::Locality: return 5.0;
  case Type::Region: return 4.0;
  case Type::Subregion: return 4.0;
  case Type::Street: return 2.0;
  case Type::Suburb: return 1.0;
  case Type::Sublocality: return 1.0;
  case Type::Building: return 0.1;
  case Type::Count: return 0.0;
  }
  UNREACHABLE();
}

double GetWeight(Kind kind)
{
  switch (kind)
  {
  case Kind::Country: return 10.0;
  case Kind::City: return 5.05;
  case Kind::Town: return 5.04;
  case Kind::State: return 4.05;
  case Kind::Province: return 4.01;
  case Kind::District: return 4.01;
  case Kind::County: return 4.01;
  case Kind::Municipality: return 4.0;
  case Kind::Village: return 3.0;
  case Kind::Street: return 2.0;
  case Kind::Hamlet: return 1.06;
  case Kind::Suburb: return 1.05;
  case Kind::Quarter: return 1.01;
  case Kind::Neighbourhood: return 1.0;
  case Kind::IsolatedDwelling: return 0.5;
  case Kind::Building: return 0.1;
  case Kind::Unknown: return 0.0;
  case Kind::Count: return 0.0;
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

strings::UniString & AppendToHouseNumber(strings::UniString & houseNumber, std::string const & token)
{
  houseNumber += strings::MakeUniString(" ");
  houseNumber += strings::MakeUniString(token);
  return houseNumber;
}
}  // namespace

// Geocoder::Layer ---------------------------------------------------------------------------------
Geocoder::Layer::Layer(Index const & index, Type type)
  : m_index{index}, m_type{type}
{
}

void Geocoder::Layer::SetCandidates(std::vector<Candidate> && candidates)
{
  std::sort(candidates.begin(), candidates.end(), [this](auto const & a, auto const & b) {
    if (a.m_totalCertainty < b.m_totalCertainty)
      return true;
    if (a.m_totalCertainty > b.m_totalCertainty)
      return false;
    return m_index.GetDoc(a.m_entry).m_osmId < m_index.GetDoc(b.m_entry).m_osmId;
  });
  m_candidatesByCertainty = std::move(candidates);
}

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
                                  vector<size_t> const & tokenIds, vector<Type> const & allTypes,
                                  bool isOtherSimilar)
{
  m_beam.Add(BeamKey(osmId, type, tokenIds, allTypes, isOtherSimilar), certainty);
}

void Geocoder::Context::FillResults(vector<Result> & results) const
{
  results.clear();
  results.reserve(m_beam.GetEntries().size());

  auto normalizationCertainty = 0.0;

  set<base::GeoObjectId> seen;
  bool const hasPotentialHouseNumber = !m_houseNumberPositionsInQuery.empty();
  for (auto const & e : m_beam.GetEntries())
  {
    if (!seen.insert(e.m_key.m_osmId).second)
      continue;

    if (hasPotentialHouseNumber && !IsGoodForPotentialHouseNumberAt(e.m_key, m_houseNumberPositionsInQuery))
      continue;

    if (!normalizationCertainty)
    {
      normalizationCertainty = e.m_value;
      // Normalize other-similar candidate certaintly to 0.95 in the best results.
      if (e.m_key.m_isOtherSimilar)
        normalizationCertainty /= 0.95;
    }

    ASSERT_GREATER_OR_EQUAL(normalizationCertainty, e.m_value, ());

    auto resultCertainty = e.m_value / normalizationCertainty;
    ASSERT_GREATER_OR_EQUAL(resultCertainty, 0.0, ());
    ASSERT_LESS_OR_EQUAL(resultCertainty, 1.0, ());

    results.emplace_back(e.m_key.m_osmId, resultCertainty);
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
void Geocoder::LoadFromJsonl(std::string const & pathToJsonHierarchy, bool dataVersionHeadline,
                             unsigned int loadThreadsCount)
try
{
  m_hierarchy = HierarchyReader{pathToJsonHierarchy, dataVersionHeadline}.Read(loadThreadsCount);
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

      Layer curLayer{m_index, type};

      // Buildings are indexed separately.
      if (type == Type::Building)
      {
        // House building parser has specific tokenizer.
        // Pass biggest house number token sequence to house number matcher.
        if (IsValidHouseNumberWithNextUnusedToken(ctx, subquery, subqueryTokenIds))
          continue;

        FillBuildingsLayer(ctx, subquery, subqueryTokenIds, curLayer);
      }
      else
      {
        FillRegularLayer(ctx, type, subquery, curLayer);
      }

      if (curLayer.GetCandidatesByCertainty().empty())
        continue;

      ScopedMarkTokens mark(ctx, type, i, j + 1);

      auto streetSynonymMark = boost::make_optional(false, ScopedMarkTokens(ctx, type, 0, 0));
      if (type == Type::Street)
        MarkStreetSynonym(ctx, streetSynonymMark);

      AddResults(ctx, curLayer.GetCandidatesByCertainty());

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

  for (auto const & layer : boost::adaptors::reverse(ctx.GetLayers()))
  {
    if (layer.GetType() != Type::Street && layer.GetType() != Type::Locality)
      continue;

    // We've already filled a street/location layer and now see something that resembles
    // a house number. While it still can be something else (a zip code, for example)
    // let's stay on the safer side and mark the tokens as potential house number.
    ctx.MarkHouseNumberPositionsInQuery(subqueryTokenIds);

    auto subqueryNumberParse = std::vector<search::house_numbers::Token>{};
    ParseQuery(subqueryHN, false /* queryIsPrefix */, subqueryNumberParse);

    auto candidates = std::vector<Candidate>{};

    auto const & lastLayer = ctx.GetLayers().back();
    auto const forSublocalityLayer =
        lastLayer.GetType() == Type::Suburb || lastLayer.GetType() == Type::Sublocality;
    for (auto const & buildingOwnerCandidate : layer.GetCandidatesByCertainty())
    {
      auto const & docId = buildingOwnerCandidate.m_entry;
      m_index.ForEachRelatedBuilding(docId, [&](Index::DocId const & buildingDocId) {
        auto const & building = m_index.GetDoc(buildingDocId);
        auto const & multipleHN = building.GetNormalizedMultipleNames(
            Type::Building, m_hierarchy.GetNormalizedNameDictionary());
        auto const & realHN = multipleHN.GetMainName();
        auto const & realHNUniStr = strings::MakeUniString(realHN);
        auto matchResult = search::house_numbers::MatchResult{};
        if (search::house_numbers::HouseNumbersMatch(realHNUniStr, subqueryNumberParse, matchResult))
        {
          auto && parentCandidateCertainty =
              forSublocalityLayer ? FindMaxCertaintyInParentCandidates(ctx.GetLayers(), building)
                                  : boost::optional<double>{buildingOwnerCandidate.m_totalCertainty};
          if (!parentCandidateCertainty)
            return;

          auto totalCertainty =
              *parentCandidateCertainty + SumHouseNumberSubqueryCertainty(matchResult);
          auto const isOtherSimilar =
              matchResult.queryMismatchedTokensCount || matchResult.houseNumberMismatchedTokensCount;
          candidates.push_back({buildingDocId, totalCertainty, isOtherSimilar});
        }
      });
    }

    if (!candidates.empty())
      curLayer.SetCandidates(std::move(candidates));
    break;
  }
}

void Geocoder::FillRegularLayer(Context const & ctx, Type type, Tokens const & subquery,
                                Layer & curLayer) const
{
  auto candidates = std::vector<Candidate>{};

  m_index.ForEachDocId(subquery, [&](Index::DocId const & docId) {
    auto const & d = m_index.GetDoc(docId);
    if (d.m_type != type)
      return;

    auto && parentCandidateCertainty = FindMaxCertaintyInParentCandidates(ctx.GetLayers(), d);
    if (!parentCandidateCertainty)
      return;

    if (type > Type::Locality && !IsRelevantLocalityMember(ctx, d, subquery))
      return;

    auto subqueryWeight =
        (d.m_kind != Kind::Unknown ? GetWeight(d.m_kind) : GetWeight(d.m_type)) * subquery.size();
    auto totalCertainty = *parentCandidateCertainty + subqueryWeight;

    candidates.push_back({docId, totalCertainty, false /* m_isOtherSimilar */});
  });

  if (!candidates.empty())
    curLayer.SetCandidates(std::move(candidates));
}

void Geocoder::AddResults(Context & ctx, std::vector<Candidate> const & candidates) const
{
  vector<size_t> tokenIds;
  vector<Type> allTypes;
  for (size_t tokId = 0; tokId < ctx.GetNumTokens(); ++tokId)
  {
    auto const t = ctx.GetTokenType(tokId);
    if (t != Type::Count)
    {
      tokenIds.push_back(tokId);
      allTypes.push_back(t);
    }
  }

  for (auto const & candidate : candidates)
  {
    auto const & docId = candidate.m_entry;
    auto const & entry = m_index.GetDoc(docId);
    auto entryCertainty = candidate.m_totalCertainty;

    if (InCityState(entry))
    {
      constexpr auto kCityStateExtraWeight = 0.05;
      ASSERT_LESS(kCityStateExtraWeight, GetWeight(Type::Building),
                  ("kCityStateExtraWeight must be smallest"));
      // Prefer city-state (Moscow, Istambul) to other city types.
      entryCertainty += kCityStateExtraWeight;
    }

    ctx.AddResult(entry.m_osmId, entryCertainty, entry.m_type, tokenIds, allTypes,
                  candidate.m_isOtherSimilar);
  }
}

bool Geocoder::IsValidHouseNumberWithNextUnusedToken(
    Context const & ctx, Tokens const & subquery, vector<size_t> const & subqueryTokenIds) const
{
  auto const nextTokenId = subqueryTokenIds.back() + 1;
  if (nextTokenId >= ctx.GetNumTokens() || ctx.IsTokenUsed(nextTokenId))
    return false;

  auto subqueryHouseNumber = MakeHouseNumber(subquery);
  AppendToHouseNumber(subqueryHouseNumber, ctx.GetToken(nextTokenId));

  return search::house_numbers::LooksLikeHouseNumber(subqueryHouseNumber, false /* isPrefix */);
}

double Geocoder::SumHouseNumberSubqueryCertainty(
    search::house_numbers::MatchResult const & matchResult) const
{
  static auto const buildingTokenWeight = GetWeight(Kind::Building);
  auto const matchedTokensCount = matchResult.matchedTokensCount;
  auto certainty = matchedTokensCount * buildingTokenWeight;

  // Candidate don't have all query tokens.
  if (matchResult.queryMismatchedTokensCount)
  {
    auto const missingTokensCount = matchResult.queryMismatchedTokensCount;
    // Missing tokens in the candidate are more penalty than extra tokents
    // in other candidates.
    auto missingTokenRelativeWeight = 4.0; // <missing token weight> / <extra token weight>
    auto const penaltyRatio =
        missingTokenRelativeWeight * missingTokensCount /
          (missingTokenRelativeWeight * missingTokensCount + matchedTokensCount);
    certainty -= penaltyRatio * buildingTokenWeight;
  }

  // Candidate has extra tokens.
  if (matchResult.houseNumberMismatchedTokensCount)
  {
    auto const extraTokensCount = matchResult.houseNumberMismatchedTokensCount;
    auto const penaltyRatio =
      double(extraTokensCount) / (matchedTokensCount + extraTokensCount);
    certainty -= penaltyRatio * buildingTokenWeight;
  }

  return certainty;
}

bool Geocoder::InCityState(Hierarchy::Entry const & entry) const
{
  if (!entry.HasFieldInAddress(Type::Locality))
    return false;

  auto const & nameDictionary = m_hierarchy.GetNormalizedNameDictionary();
  auto const & localityMultipleName = entry.GetNormalizedMultipleNames(Type::Locality,
                                                                       nameDictionary);
  auto const & localityName = localityMultipleName.GetMainName();

  for (auto const type : {Type::Region, Type::Subregion})
  {
    if (!entry.HasFieldInAddress(type))
      continue;

    auto const & multipleName = entry.GetNormalizedMultipleNames(type, nameDictionary);
    auto const & name = multipleName.GetMainName();
    if (name == localityName)
      return true;
  }

  return false;
}

boost::optional<double> Geocoder::FindMaxCertaintyInParentCandidates(
    vector<Geocoder::Layer> const & layers, Hierarchy::Entry const & e) const
{
  if (layers.empty())
    return 0;

  auto const & layer = layers.back();
  for (auto const & candidate : layer.GetCandidatesByCertainty())
  {
    auto const & docId = candidate.m_entry;
    // Note that the relationship is somewhat inverted: every ancestor
    // is stored in the address but the nodes have no information
    // about their children.
    if (m_hierarchy.IsParentTo(m_index.GetDoc(docId), e))
      return candidate.m_totalCertainty;
  }
  return {};
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
    auto const layerType = layer.GetType();
    if (layerType > Type::Locality)
      break;
    if (layerType != Type::Locality)
      continue;

    for (auto const & candidate : layer.GetCandidatesByCertainty())
    {
      auto const & docId = candidate.m_entry;
      auto const & matchedEntry = m_index.GetDoc(docId);
      if (m_hierarchy.IsParentTo(matchedEntry, member))
        return true;
    }
  }

  return false;
}
}  // namespace geocoder

#pragma once

#include "geocoder/hierarchy.hpp"
#include "geocoder/house_numbers_matcher.hpp"
#include "geocoder/index.hpp"
#include "geocoder/result.hpp"
#include "geocoder/types.hpp"

#include "base/beam.hpp"
#include "base/geo_object_id.hpp"
#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/optional.hpp>
#include <boost/serialization/version.hpp>

namespace geocoder
{
// This class performs geocoding by using the data that we are currently unable
// to distribute to mobile devices. Therefore, the class is intended to be used
// on the server side.
// On the other hand, the design is largely experimental and when the dust
// settles we may reuse some parts of it in the offline mobile application.
// In this case, a partial merge with search/ and in particular with
// search/geocoder.hpp is possible.
//
// Geocoder receives a search query and returns the osm ids of the features
// that match it. Currently, the only data source for the geocoder is
// the hierarchy of features, that is, for every feature that can be found
// the geocoder expects to have the total information about this feature
// in the region subdivision graph (e.g., country, city, street that contain a
// certain house). This hierarchy is to be obtained elsewhere.
//
// Note that search index, locality index, scale index, and, generally, mwm
// features are currently not used at all.
class Geocoder
{
public:
  DECLARE_EXCEPTION(Exception, RootException);
  DECLARE_EXCEPTION(OpenException, Exception);

  // Candidate contain matched entry with certainty of all matched tokens.
  struct Candidate
  {
    Index::DocId m_entry;
    double m_totalCertainty;
    bool m_isOtherSimilar;
  };

  // A Layer contains all entries matched by a subquery of consecutive tokens.
  class Layer
  {
  public:
    Layer(Index const & index, Type type);

    Type GetType() const noexcept { return m_type; }
    std::vector<Candidate> const & GetCandidatesByCertainty() const noexcept
    {
      return m_candidatesByCertainty;
    }
    void SetCandidates(std::vector<Candidate> && candidates);

  private:
    Index const & m_index;
    Type m_type{Type::Count};
    std::vector<Candidate> m_candidatesByCertainty;
  };

  // This class is very similar to the one we use in search/.
  // See search/geocoder_context.hpp.
  class Context
  {
  public:
    struct BeamKey
    {
      BeamKey(base::GeoObjectId osmId, Type type, std::vector<size_t> const & tokensPositions,
              std::vector<Type> const & allTypes, bool isOtherSimilar)
        : m_osmId(osmId)
        , m_type(type)
        , m_tokensPositions{tokensPositions}
        , m_allTypes(allTypes)
        , m_isOtherSimilar(isOtherSimilar)
      {
        base::SortUnique(m_allTypes);
      }

      base::GeoObjectId m_osmId;
      Type m_type;
      std::vector<size_t> m_tokensPositions;
      std::vector<Type> m_allTypes;
      bool m_isOtherSimilar;
    };

    Context(std::string const & query);

    void Clear();

    std::vector<Type> & GetTokenTypes();
    size_t GetNumTokens() const;
    size_t GetNumUsedTokens() const;

    Type GetTokenType(size_t id) const;

    std::string const & GetToken(size_t id) const;

    void MarkToken(size_t id, Type type);

    // Returns true if |token| is marked as used.
    bool IsTokenUsed(size_t id) const;

    // Returns true iff all tokens are used.
    bool AllTokensUsed() const;

    void AddResult(base::GeoObjectId const & osmId, double certainty, Type type,
                   std::vector<size_t> const & tokensPositions, std::vector<Type> const & allTypes,
                   bool isOtherSimilar);

    void FillResults(std::vector<Result> & results) const;

    std::vector<Layer> & GetLayers();

    std::vector<Layer> const & GetLayers() const;

    void MarkHouseNumberPositionsInQuery(std::vector<size_t> const & tokensPositions);

  private:
    bool IsGoodForPotentialHouseNumberAt(BeamKey const & beamKey,
                                         std::set<size_t> const & tokensPositions) const;
    bool IsBuildingWithAddress(BeamKey const & beamKey) const;
    bool HasLocalityOrRegion(BeamKey const & beamKey) const;
    bool ContainsTokens(BeamKey const & beamKey, std::set<size_t> const & needTokensPostions) const;

    Tokens m_tokens;
    std::vector<Type> m_tokenTypes;

    size_t m_numUsedTokens = 0;

    // |m_houseNumberPositionsInQuery| has indexes of query tokens which are placed on
    // context-dependent positions of house number.
    // The rationale is that we must only emit buildings in this case
    // and implement a fallback to a more powerful geocoder if we
    // could not find a building.
    std::set<size_t> m_houseNumberPositionsInQuery;

    // The highest value of certainty for a fixed amount of
    // the most relevant retrieved osm ids.
    base::Beam<BeamKey, double> m_beam;

    std::vector<Layer> m_layers;
  };

  void LoadFromJsonl(std::string const & pathToJsonHierarchy, bool dataVersionHeadline = false,
                     unsigned int loadThreadsCount = 1);

  void LoadFromBinaryIndex(std::string const & pathToTokenIndex);
  void SaveToBinaryIndex(std::string const & pathToTokenIndex) const;

  template<class Archive>
  void serialize(Archive & ar, const unsigned int version)
  {
    CHECK_EQUAL(version, kIndexFormatVersion, ());
    ar & m_hierarchy;
    ar & m_index;
  }

  void ProcessQuery(std::string const & query, std::vector<Result> & results) const;

  Hierarchy const & GetHierarchy() const;

  Index const & GetIndex() const;

private:
  void Go(Context & ctx, Type type) const;

  void FillBuildingsLayer(Context & ctx, Tokens const & subquery,
                          std::vector<size_t> const & subqueryTokensPositions,
                          Layer & curLayer) const;
  void FillRegularLayer(Context const & ctx, Type type, Tokens const & subquery,
                        Layer & curLayer) const;
  void AddResults(Context & ctx, std::vector<Candidate> const & candidates) const;

  bool IsValidHouseNumberWithNextUnusedToken(
      Context const & ctx, Tokens const & subquery,
      std::vector<size_t> const & subqueryTokensPositions) const;
  double SumHouseNumberSubqueryCertainty(
      search::house_numbers::MatchResult const & matchResult) const;

  bool InCityState(Hierarchy::Entry const & entry) const;

  // Find max certainty in parent candidates.
  // 0 - first candidate.
  // none - there is no parent from candidates.
  boost::optional<double> FindMaxCertaintyInParentCandidates(
      std::vector<Geocoder::Layer> const & layers, Hierarchy::Entry const & e) const;

  bool IsRelevantLocalityMember(Context const & ctx, Hierarchy::Entry const & member,
                                Tokens const & subquery) const;
  bool HasMemberLocalityInMatching(Context const & ctx, Hierarchy::Entry const & member) const;

  Hierarchy m_hierarchy;
  Index m_index{m_hierarchy};
};
}  // namespace geocoder

BOOST_CLASS_VERSION(geocoder::Geocoder, geocoder::kIndexFormatVersion)

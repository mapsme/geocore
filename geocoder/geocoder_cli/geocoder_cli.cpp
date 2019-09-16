#include "geocoder/geocoder.hpp"
#include "geocoder/result.hpp"

#include "base/internal/message.hpp"
#include "base/string_utils.hpp"

#include <boost/program_options.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace geocoder;
using namespace std;

namespace po = boost::program_options;

void PrintResults(Hierarchy const & hierarchy, vector<Result> const & results, int32_t top)
{
  cout << "Found results: " << results.size() << endl;
  if (results.empty())
    return;
  cout << "Top results:" << endl;

  auto const & dictionary = hierarchy.GetNormalizedNameDictionary();
  for (size_t i = 0; i < results.size(); ++i)
  {
    if (top >= 0 && static_cast<int32_t>(i) >= top)
      break;
    cout << "  " << DebugPrint(results[i]);
    if (auto const && e = hierarchy.GetEntryForOsmId(results[i].m_osmId))
    {
      cout << " [";
      auto const * delimiter = "";
      for (size_t i = 0; i < static_cast<size_t>(Type::Count); ++i)
      {
        if (e->m_normalizedAddress[i] != NameDictionary::kUnspecifiedPosition)
        {
          auto type = static_cast<Type>(i);
          auto multipleNames = e->GetNormalizedMultipleNames(type, dictionary);
          cout << delimiter << ToString(type) << ": " << multipleNames.GetMainName();
          delimiter = ", ";
        }
      }
      cout << "]";
    }
    cout << endl;
  }
}

void ProcessQueriesFromFile(Geocoder const & geocoder, string const & path, int32_t top)
{
  ifstream stream(path.c_str());
  CHECK(stream.is_open(), ("Can't open", path));

  vector<Result> results;
  string s;
  while (getline(stream, s))
  {
    strings::Trim(s);
    if (s.empty())
      continue;

    cout << s << endl;
    geocoder.ProcessQuery(s, results);
    PrintResults(geocoder.GetHierarchy(), results, top);
    cout << endl;
  }
}

void ProcessQueriesFromCommandLine(Geocoder const & geocoder, int32_t top)
{
  string query;
  vector<Result> results;
  while (true)
  {
    cout << "> ";
    if (!getline(cin, query))
      break;
    if (query == "q" || query == ":q" || query == "quit")
      break;
    geocoder.ProcessQuery(query, results);
    PrintResults(geocoder.GetHierarchy(), results, top);
  }
}

struct CliCommandOptions
{
  std::string m_hierarchy_path;
  std::string m_queries_path;
  int32_t m_top;
};

CliCommandOptions DefineOptions(int argc, char * argv[])
{
  CliCommandOptions o;
  po::options_description optionsDescription;

  optionsDescription.add_options()
    ("hierarchy_path", po::value(&o.m_hierarchy_path), "Path to the hierarchy file for the geocoder")
    ("queries_path", po::value(&o.m_queries_path)->default_value(""), "Path to the file with queries")
    ("top", po::value(&o.m_top)->default_value(5), "Number of top results to show for every query, -1 to show all results")
    ("help", "produce help message");

  po::variables_map vm;

  po::store(po::parse_command_line(argc, argv, optionsDescription), vm);
  po::notify(vm);

  if (vm.count("help"))
  {
    std::cout << optionsDescription << std::endl;
    exit(1);
  }

  return o;
}

int main(int argc, char * argv[])
{
  ios_base::sync_with_stdio(false);
  CliCommandOptions options;
  try
  {
    options = DefineOptions(argc, argv);
  }
  catch(po::error& e)
  {
    std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
    return 1;
  }

  Geocoder geocoder;
  if (strings::EndsWith(options.m_hierarchy_path, ".jsonl") ||
      strings::EndsWith(options.m_hierarchy_path, ".jsonl.gz"))
  {
    geocoder.LoadFromJsonl(options.m_hierarchy_path);
  }
  else
  {
    geocoder.LoadFromBinaryIndex(options.m_hierarchy_path);
  }

  if (!options.m_queries_path.empty())
  {
    ProcessQueriesFromFile(geocoder, options.m_queries_path, options.m_top);
    return 0;
  }

  ProcessQueriesFromCommandLine(geocoder, options.m_top);
  return 0;
}

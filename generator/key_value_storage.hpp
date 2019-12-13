#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>

#include <boost/variant.hpp>

#include "3party/jansson/myjansson.hpp"

namespace generator
{
class JsonValue
{
public:
  explicit JsonValue(json_t * value = nullptr) : m_handle{value} {}
  explicit JsonValue(base::JSONPtr && value) : m_handle{std::move(value)} {}

  JsonValue(JsonValue &&) = default;
  JsonValue & operator=(JsonValue &&) = default;

  operator json_t const *() const noexcept { return m_handle.get(); }
  operator base::JSONPtr const &() const noexcept { return m_handle; };
  base::JSONPtr MakeDeepCopyJson() const { return base::JSONPtr{json_deep_copy(m_handle.get())}; }

private:
  base::JSONPtr m_handle;
};

using KeyValue = std::pair<uint64_t, std::shared_ptr<JsonValue>>;

class KeyValueStorage
{
public:
  // Longitude and latitude has maximum 3 digits before comma. So we have minimum 6 digits after
  // comma. Nautical mile is good approximation for one angle minute, so we can rely, that final
  // precision is 60 (minutes in degree) * 1852 (meters in one mile) / 1000000 = 0.111 = 111
  // millimeters. Also, if you are quizzed by nautical mile, just forget, precision was defined in
  // https://jira.mail.ru/browse/MAPSB2B-41
  static uint32_t constexpr kDefaultPrecision = 9;

  explicit KeyValueStorage(std::string const & kvPath);

  KeyValueStorage(KeyValueStorage &&) = default;
  KeyValueStorage & operator=(KeyValueStorage &&) = default;

  KeyValueStorage(KeyValueStorage const &) = delete;
  KeyValueStorage & operator=(KeyValueStorage const &) = delete;

  static std::string SerializeFullLine(uint64_t key, JsonValue const & valueJson);
  static void SerializeFullLine(std::ostream & out, uint64_t key, JsonValue const & jsonValue);

  std::shared_ptr<JsonValue> Find(uint64_t key) const;
  size_t Size() const;

  static std::string Serialize(base::JSONPtr const & ptr)
  {
    return base::DumpToString(ptr, JSON_COMPACT | JSON_REAL_PRECISION(kDefaultPrecision));
  }

  static std::string SerializeDref(uint64_t number);

private:
  static bool ParseKeyValueLine(std::string const & line, std::streamoff lineNumber, uint64_t & key,
                                std::string & value);
  std::unordered_map<uint64_t, std::shared_ptr<JsonValue>> m_values;
};
}  // namespace generator

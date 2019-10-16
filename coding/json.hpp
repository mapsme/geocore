#pragma once

#include "base/exception.hpp"
#include "base/macros.hpp"

#define RAPIDJSON_SSE2 1
#define RAPIDJSON_HAS_STDSTRING 1
#define RAPIDJSON_HAS_CXX11_TYPETRAITS 1

#include "3party/rapidjson/document.h"
#include "3party/rapidjson/rapidjson.h"
#include "3party/rapidjson/writer.h"

#include <iomanip>
#include <sstream>

namespace coding
{
DECLARE_EXCEPTION(JsonException, RootException);

using JsonValue = rapidjson::Value;
using JsonDocument = rapidjson::Document;
using JsonParseResult = rapidjson::ParseResult;

template <typename T>
inline T FromJson(JsonDocument const & root)
{
  T result{};
  FromJson(root, result);
  return result;
}

inline void FromJson(JsonValue const & root, double & result)
{
  if (!root.IsNumber())
    MYTHROW(coding::JsonException, ("Object must contain a json number."));
  result = root.GetDouble();
}

inline void FromJson(JsonValue const & root, bool & result)
{
  if (!root.IsBool())
    MYTHROW(coding::JsonException, ("Object must contain a boolean value."));
  result = root.GetBool();
}

inline void FromJson(JsonValue const & root, std::string & result)
{
  if (!root.IsString())
    MYTHROW(coding::JsonException, ("The field must contain a json string."));
  result = root.GetString();
}

static const coding::JsonValue nullValue;

inline coding::JsonValue const & GetJsonOptionalField(coding::JsonValue const & root,
                                                      std::string const & field)
{
  if (!root.IsObject())
    MYTHROW(coding::JsonException, ("Bad json object while parsing", field));

  coding::JsonValue::ConstMemberIterator it = root.FindMember(field);

  if (it == root.MemberEnd())
    return nullValue;

  return it->value;
}

inline coding::JsonValue const & GetJsonObligatoryField(coding::JsonValue const & root,
                                                        std::string const & field)
{
  coding::JsonValue const & value = GetJsonOptionalField(root, field);
  if (value.IsNull())
    MYTHROW(coding::JsonException, ("Obligatory field", field, "is absent."));

  return value;
}

template <typename T>
void FromJsonObjectOptionalField(JsonValue const & root, std::string const & field, T & result)
{
  coding::JsonValue const & value = GetJsonOptionalField(root, field);

  if (value.IsNull())
  {
    result = T{};
    return;
  }
  FromJson(value, result);
}

template <class First>
inline coding::JsonValue const & GetJsonObligatoryFieldByPath(coding::JsonValue const & root,
                                                              First && path)
{
  return GetJsonObligatoryField(root, std::forward<First>(path));
}

template <class First, class... Paths>
inline coding::JsonValue const & GetJsonObligatoryFieldByPath(coding::JsonValue const & root,
                                                              First && path, Paths &&... paths)
{
  coding::JsonValue const & newRoot = GetJsonObligatoryFieldByPath(root, std::forward<First>(path));
  return GetJsonObligatoryFieldByPath(newRoot, std::forward<Paths>(paths)...);
}

template <typename Stream>
class JsonCustomPrecisionWriter : public rapidjson::Writer<Stream>
{
public:
  // 6 digits after comma. Nautical mile is good approximation for one angle minute, so we can rely,
  // that final precision is 60 (minutes in degree) * 1852 (meters in one mile)
  // 1000000 = 0.111 = 111 millimeters.

  static uint32_t constexpr kDefaultPrecision = 6;
  JsonCustomPrecisionWriter(Stream & stream, size_t precision)
        : rapidjson::Writer<Stream>(stream)
        , m_precision(precision)
  {
  }
  JsonCustomPrecisionWriter & Double(double d)
  {
    this->Prefix(rapidjson::kNumberType);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(m_precision) << d;
    std::string number = ss.str();

    for (char c : number)
      this->stream_.Put(c);

    return *this;
  }

private:
  size_t m_precision = kDefaultPrecision;
};

}
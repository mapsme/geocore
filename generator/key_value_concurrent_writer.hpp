#include "generator/key_value_storage.hpp"

#include "base/geo_object_id.hpp"

#include <string>
#include <sstream>

namespace generator
{
// |KeyValueConcurrentWriter| allow concurrent write to the same KV-file by multiple instance of
// this class from threads.
class KeyValueConcurrentWriter
{
public:
  KeyValueConcurrentWriter(std::string const & keyValuePath, size_t bufferSize = 1'000'000);
  KeyValueConcurrentWriter(KeyValueConcurrentWriter && other);
  KeyValueConcurrentWriter & operator=(KeyValueConcurrentWriter && other);
  ~KeyValueConcurrentWriter();

  // No thread-safety.
  void Write(base::GeoObjectId const & id, JsonValue const & jsonValue);

private:
  int m_keyValueFile{-1};
  std::ostringstream m_keyValueBuffer;
  size_t m_bufferSize{1'000'000};

  void FlushBuffer();
};
}  // namespace generator

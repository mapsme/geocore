#include "generator/key_value_concurrent_writer.hpp"

#include "generator/key_value_storage.hpp"

#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <sys/stat.h>

namespace generator
{
KeyValueConcurrentWriter::KeyValueConcurrentWriter(
    std::string const & keyValuePath, size_t bufferSize)
  : m_bufferSize{bufferSize}
{
  // Posix API are used for concurrent atomic write from threads.
  ::mode_t mode{S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH};
  m_keyValueFile = ::open(keyValuePath.c_str(), O_CREAT | O_WRONLY | O_APPEND, mode);
  if (m_keyValueFile == -1)
  {
    throw std::runtime_error("failed to open file " + keyValuePath + ": " +
                             std::strerror(errno));
  }
}
  
KeyValueConcurrentWriter::KeyValueConcurrentWriter(KeyValueConcurrentWriter && other)
{
  *this = std::move(other);
}

KeyValueConcurrentWriter & KeyValueConcurrentWriter::operator=(
    KeyValueConcurrentWriter && other)
{
  if (m_keyValueFile != -1)
  {
    FlushBuffer();
    ::close(m_keyValueFile);
    m_keyValueFile = -1;
  }

  std::swap(m_keyValueFile, other.m_keyValueFile);
  m_keyValueBuffer = std::move(other.m_keyValueBuffer);
  m_bufferSize = other.m_bufferSize;
  return *this;
}

KeyValueConcurrentWriter::~KeyValueConcurrentWriter()
{
  if (m_keyValueFile == -1)
    return;

  FlushBuffer();
  ::close(m_keyValueFile);
}

void KeyValueConcurrentWriter::Write(base::GeoObjectId const & id, JsonValue const & jsonValue)
{
  KeyValueStorage::SerializeFullLine(m_keyValueBuffer, id.GetEncodedId(), jsonValue);

  if (size_t{m_keyValueBuffer.tellp()} + 1'000 >= m_bufferSize)
    FlushBuffer();
}

void KeyValueConcurrentWriter::FlushBuffer()
{
  auto const & data = m_keyValueBuffer.str();
  if (data.empty())
    return;

  auto writed = ::write(m_keyValueFile, data.data(), data.size());
  // Error if ::write() interrupted by a signal.
  CHECK(static_cast<size_t>(writed) == data.size(), ());
  m_keyValueBuffer.str({});
}
}  // namespace generator

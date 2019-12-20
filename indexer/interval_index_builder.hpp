#pragma once

#include "indexer/interval_index.hpp"

#include "coding/byte_stream.hpp"
#include "coding/endianness.hpp"
#include "coding/varint.hpp"
#include "coding/write_to_sink.hpp"
#include "coding/writer.hpp"

#include "base/assert.hpp"
#include "base/base.hpp"
#include "base/bits.hpp"
#include "base/checked_cast.hpp"
#include "base/logging.hpp"

#include <cstdint>
#include <limits>
#include <vector>

// +------------------------------+
// |            Header            |
// +------------------------------+
// |        Leaves  offset        |
// +------------------------------+
// |        Level 1 offset        |
// +------------------------------+
// |             ...              |
// +------------------------------+
// |        Level N offset        |
// +------------------------------+
// |        Leaves  data          |
// +------------------------------+
// |        Level 1 data          |
// +------------------------------+
// |             ...              |
// +------------------------------+
// |        Level N data          |
// +------------------------------+
class IntervalIndexBuilder
{
public:
  IntervalIndexBuilder(uint32_t keyBits, uint32_t leafBytes, uint32_t bitsPerLevel = 8)
    : IntervalIndexBuilder(IntervalIndexVersion::V1, keyBits, leafBytes, bitsPerLevel)
  { }

  IntervalIndexBuilder(IntervalIndexVersion version, uint32_t keyBits, uint32_t leafBytes,
                       uint32_t bitsPerLevel = 8)
    : m_version{version}, m_BitsPerLevel(bitsPerLevel), m_LeafBytes(leafBytes)
  {
    CHECK_GREATER_OR_EQUAL(
        static_cast<uint8_t>(version), static_cast<uint8_t>(IntervalIndexVersion::V1), ());
    CHECK_LESS_OR_EQUAL(
        static_cast<uint8_t>(version), static_cast<uint8_t>(IntervalIndexVersion::V2), ());
    CHECK_GREATER(leafBytes, 0, ());
    CHECK_LESS(keyBits, 63, ());
    int const nodeKeyBits = keyBits - (m_LeafBytes << 3);
    CHECK_GREATER(nodeKeyBits, 0, (keyBits, leafBytes));
    m_Levels = (nodeKeyBits + m_BitsPerLevel - 1) / m_BitsPerLevel;
    m_LastBitsMask = (1 << m_BitsPerLevel) - 1;
  }

  uint32_t GetLevelCount() const { return m_Levels; }

  template <class Writer, typename CellIdValueIter>
  void BuildIndex(Writer & writer, CellIdValueIter const & beg, CellIdValueIter const & end)
  {
    if (beg == end)
    {
      IntervalIndexBase::Header header;
      header.m_Version = static_cast<uint8_t>(m_version);
      header.m_BitsPerLevel = 0;
      header.m_Levels = 0;
      header.m_LeafBytes = 0;
      writer.Write(&header, sizeof(header));
      return;
    }

    m_levelsAssembly.clear();
    for (int i = 0; i <= static_cast<int>(m_Levels); ++i)
      m_levelsAssembly.emplace_back(*this, i);

    uint64_t const initialPos = writer.Pos();
    WriteZeroesToSink(writer, sizeof(IntervalIndexBase::Header));
    WriteZeroesToSink(writer, (m_version == IntervalIndexVersion::V1 ? 4 : 8) * (m_Levels + 2));
    uint64_t const afterHeaderPos = writer.Pos();

    std::vector<uint64_t> levelOffset;

    levelOffset.push_back(writer.Pos());
    BuildAllLevels(writer, beg, end);
    levelOffset.push_back(writer.Pos());

    // Write levels.
    for (int i = 1; i <= static_cast<int>(m_Levels); ++i)
    {
      auto const & levelAssembly = m_levelsAssembly[i];
      auto const & levelData = levelAssembly.GetLevelData();
      writer.Write(levelData.data(), levelData.size());

      levelOffset.push_back(writer.Pos());
    }

    uint64_t const lastPos = writer.Pos();
    writer.Seek(initialPos);

    // Write header.
    {
      IntervalIndexBase::Header header;
      header.m_Version = static_cast<uint8_t>(m_version);
      header.m_BitsPerLevel = static_cast<uint8_t>(m_BitsPerLevel);
      ASSERT_EQUAL(header.m_BitsPerLevel, m_BitsPerLevel, ());
      header.m_Levels = static_cast<uint8_t>(m_Levels);
      ASSERT_EQUAL(header.m_Levels, m_Levels, ());
      header.m_LeafBytes = static_cast<uint8_t>(m_LeafBytes);
      ASSERT_EQUAL(header.m_LeafBytes, m_LeafBytes, ());
      writer.Write(&header, sizeof(header));
    }

    // Write level offsets.
    for (size_t i = 0; i < levelOffset.size(); ++i)
    {
      if (m_version == IntervalIndexVersion::V1)
        WriteToSink(writer, base::checked_cast<uint32_t>(levelOffset[i]));
      else
        WriteToSink(writer, levelOffset[i]);
    }

    uint64_t const pos = writer.Pos();
    CHECK_EQUAL(pos, afterHeaderPos, ());
    writer.Seek(lastPos);
  }

  template <class SinkT>
  uint64_t WriteNode(SinkT & sink, uint64_t offset, uint64_t * childSizes)
  {
    std::vector<uint8_t> bitmapSerial, listSerial;
    bitmapSerial.reserve(1024);
    listSerial.reserve(1024);
    PushBackByteSink<std::vector<uint8_t> > bitmapSink(bitmapSerial), listSink(listSerial);
    WriteBitmapNode(bitmapSink, offset, childSizes);
    WriteListNode(listSink, offset, childSizes);
    if (bitmapSerial.size() <= listSerial.size())
    {
      sink.Write(&bitmapSerial[0], bitmapSerial.size());
      ASSERT_EQUAL(bitmapSerial.size(), static_cast<uint32_t>(bitmapSerial.size()), ());
      return bitmapSerial.size();
    }
    else
    {
      sink.Write(&listSerial[0], listSerial.size());
      ASSERT_EQUAL(listSerial.size(), static_cast<uint32_t>(listSerial.size()), ());
      return listSerial.size();
    }
  }

  template <class SinkT>
  void WriteBitmapNode(SinkT & sink, uint64_t offset, uint64_t * childSizes)
  {
    ASSERT_GREATER_OR_EQUAL(m_BitsPerLevel, 3, ());

    if (m_version == IntervalIndexVersion::V1)
      CHECK_LESS_OR_EQUAL(offset, std::numeric_limits<uint32_t>::max() >> 1, ());
    else
      CHECK_LESS_OR_EQUAL(offset, std::numeric_limits<uint64_t>::max() >> 1, ());
    uint64_t const offsetAndFlag = (offset << 1) + 1;
    WriteVarUint(sink, offsetAndFlag);

    buffer_vector<uint8_t, 32> bitMask(1 << (m_BitsPerLevel - 3));
    for (uint32_t i = 0; i < static_cast<uint32_t>(1 << m_BitsPerLevel); ++i)
      if (childSizes[i])
        bits::SetBitTo1(&bitMask[0], i);
    sink.Write(&bitMask[0], bitMask.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(1 << m_BitsPerLevel); ++i)
    {
      uint64_t size = childSizes[i];
      if (!size)
        continue;

      if (m_version == IntervalIndexVersion::V1)
        CHECK_LESS_OR_EQUAL(size, std::numeric_limits<uint32_t>::max(), ());
      WriteVarUint(sink, size);
    }
  }

  template <class SinkT>
  void WriteListNode(SinkT & sink, uint64_t offset, uint64_t * childSizes)
  {
    ASSERT_LESS_OR_EQUAL(m_BitsPerLevel, 8, ());

    if (m_version == IntervalIndexVersion::V1)
      CHECK_LESS_OR_EQUAL(offset, std::numeric_limits<uint32_t>::max() >> 1, ());
    else
      CHECK_LESS_OR_EQUAL(offset, std::numeric_limits<uint64_t>::max() >> 1, ());
    uint64_t const offsetAndFlag = offset << 1;
    WriteVarUint(sink, offsetAndFlag);

    for (uint32_t i = 0; i < static_cast<uint32_t>(1 << m_BitsPerLevel); ++i)
    {
      uint64_t size = childSizes[i];
      if (!size)
        continue;

      WriteToSink(sink, static_cast<uint8_t>(i));

      if (m_version == IntervalIndexVersion::V1)
        CHECK_LESS_OR_EQUAL(size, std::numeric_limits<uint32_t>::max(), ());
      WriteVarUint(sink, size);
    }
  }

private:
  class LevelAssembly
  {
  public:
    LevelAssembly(IntervalIndexBuilder & indexBuilder, int level)
      : m_indexBuilder{indexBuilder}
      , m_level{level}
      , m_expandedSizes(1 << indexBuilder.m_BitsPerLevel)
    { }

    void NewChildNode(uint64_t childNodeKey, uint64_t childNodeSize, bool last = false)
    {
      CHECK(childNodeKey != m_prevChildNodeKey, ());

      auto const bitsPerLevel = m_indexBuilder.m_BitsPerLevel;
      if ((childNodeKey >> bitsPerLevel) != (m_prevChildNodeKey >> bitsPerLevel) &&
          m_nextChildOffset)
      {
        auto nodeSize = m_indexBuilder.WriteNode(m_writer, m_childOffset, &m_expandedSizes[0]);
        m_indexBuilder.NewNode(m_level, m_prevChildNodeKey >> bitsPerLevel, nodeSize);

        m_childOffset = m_nextChildOffset;
        m_expandedSizes.assign(m_expandedSizes.size(), 0);
      }

      m_nextChildOffset += childNodeSize;
      auto const lastBitsMask = m_indexBuilder.m_LastBitsMask;
      CHECK_EQUAL(m_expandedSizes[childNodeKey & lastBitsMask], 0, ());
      m_expandedSizes[childNodeKey & lastBitsMask] = childNodeSize;
      m_prevChildNodeKey = childNodeKey;

      if (last)
      {
        auto nodeSize = m_indexBuilder.WriteNode(m_writer, m_childOffset, &m_expandedSizes[0]);
        m_indexBuilder.NewNode(m_level, childNodeKey >> bitsPerLevel, nodeSize, true /* last */);
      }
    }

    std::vector<char> const & GetLevelData() const
    {
      return *m_buffer;
    }

  private:
    IntervalIndexBuilder & m_indexBuilder;
    int m_level;
    uint64_t m_prevChildNodeKey = std::numeric_limits<uint64_t>::max();
    uint64_t m_childOffset{0};
    uint64_t m_nextChildOffset{0};
    std::vector<uint64_t> m_expandedSizes;
    // |m_buffer| are allocated because of reference to buffer must be fixed for |m_writer|.
    std::unique_ptr<std::vector<char>> m_buffer = std::make_unique<std::vector<char>>();
    MemWriter<std::vector<char>> m_writer{*m_buffer};
  };

  void NewNode(int nodeLevel, uint64_t nodeKey, uint64_t nodeSize, bool last = false)
  {
    if (nodeLevel == static_cast<int>(m_Levels))
      return;

    m_levelsAssembly[nodeLevel + 1].NewChildNode(nodeKey, nodeSize, last);
  }

  template <class Writer, typename CellIdValueIter>
  void BuildAllLevels(Writer & writer, CellIdValueIter const & beg, CellIdValueIter const & end)
  {
    using Value = typename CellIdValueIter::value_type::ValueType;

    uint32_t const keyBits = 8 * m_LeafBytes + m_Levels * m_BitsPerLevel;
    uint32_t const skipBits = 8 * m_LeafBytes;
    uint64_t prevKey = 0;
    uint64_t prevValue = 0;
    uint64_t prevPos = writer.Pos();
    for (CellIdValueIter it = beg; it != end; ++it)
    {
      uint64_t const key = it->GetCell();
      CHECK_GREATER(key, 0, ());
      CHECK_LESS(key, 1ULL << keyBits, ());
      CHECK_GREATER_OR_EQUAL(key, prevKey, ());

      Value const value = it->GetValue();
      if (key == prevKey && value == prevValue)
        continue;

      if ((key >> skipBits) != (prevKey >> skipBits) && prevKey)
      {
        auto const nodeSize = writer.Pos() - prevPos;
        NewNode(0 /* nodeLevel */, prevKey >> skipBits, nodeSize);

        prevValue = 0;
        prevPos = writer.Pos();
      }
      uint64_t const keySerial = SwapIfBigEndianMacroBased(key);
      writer.Write(&keySerial, m_LeafBytes);
      WriteVarInt(writer, static_cast<int64_t>(value) - static_cast<int64_t>(prevValue));
      prevKey = key;
      prevValue = value;
    }

    auto const nodeSize = writer.Pos() - prevPos;
    NewNode(0 /* nodeLevel */, prevKey >> skipBits, nodeSize, true /* last */);
  }

  IntervalIndexVersion m_version;
  uint32_t m_Levels, m_BitsPerLevel, m_LeafBytes, m_LastBitsMask;
  std::vector<LevelAssembly> m_levelsAssembly;
};

template <class Writer, typename CellIdValueIter>
void BuildIntervalIndex(CellIdValueIter const & beg, CellIdValueIter const & end, Writer & writer,
                        uint32_t keyBits,
                        IntervalIndexVersion version = IntervalIndexVersion::V1)
{
  IntervalIndexBuilder(version, keyBits, 1).BuildIndex(writer, beg, end);
}

#pragma once

#include "geometry/point2d.hpp"
#include "geometry/segment2d.hpp"
#include "geometry/triangle2d.hpp"

#include "base/assert.hpp"
#include "base/base.hpp"
#include "base/buffer_vector.hpp"
#include "base/logging.hpp"
#include "base/math.hpp"
#include "base/thread_pool_computational.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace covering
{
// Result of an intersection between object and cell.
enum CellObjectIntersection
{
  // No intersection. It is important, that its value is 0, so one can do if (intersection) ... .
  CELL_OBJECT_NO_INTERSECTION = 0,
  CELL_OBJECT_INTERSECT = 1,
  CELL_INSIDE_OBJECT = 2,
  OBJECT_INSIDE_CELL = 3
};

template <class CellId>
CellObjectIntersection IntersectCellWithLine(CellId const cell, m2::PointD const & a,
                                             m2::PointD const & b)
{
  std::pair<uint32_t, uint32_t> const xy = cell.XY();
  uint32_t const r = cell.Radius();
  m2::PointD const cellCorners[4] = {
      m2::PointD(xy.first - r, xy.second - r), m2::PointD(xy.first - r, xy.second + r),
      m2::PointD(xy.first + r, xy.second + r), m2::PointD(xy.first + r, xy.second - r)};
  for (int i = 0; i < 4; ++i)
  {
    if (m2::SegmentsIntersect(a, b, cellCorners[i], cellCorners[i == 0 ? 3 : i - 1]))
      return CELL_OBJECT_INTERSECT;
  }
  if (xy.first - r <= a.x && a.x <= xy.first + r && xy.second - r <= a.y && a.y <= xy.second + r)
    return OBJECT_INSIDE_CELL;
  return CELL_OBJECT_NO_INTERSECTION;
}

template <class CellId>
CellObjectIntersection IntersectCellWithTriangle(CellId const cell, m2::PointD const & a,
                                                 m2::PointD const & b, m2::PointD const & c)
{
  CellObjectIntersection const i1 = IntersectCellWithLine(cell, a, b);
  if (i1 == CELL_OBJECT_INTERSECT)
    return CELL_OBJECT_INTERSECT;
  CellObjectIntersection const i2 = IntersectCellWithLine(cell, b, c);
  if (i2 == CELL_OBJECT_INTERSECT)
    return CELL_OBJECT_INTERSECT;
  CellObjectIntersection const i3 = IntersectCellWithLine(cell, c, a);
  if (i3 == CELL_OBJECT_INTERSECT)
    return CELL_OBJECT_INTERSECT;
  // At this point either:
  // 1. Triangle is inside cell.
  // 2. Cell is inside triangle.
  // 3. Cell and triangle do not intersect.
  ASSERT_EQUAL(i1, i2, (cell, a, b, c));
  ASSERT_EQUAL(i2, i3, (cell, a, b, c));
  ASSERT_EQUAL(i3, i1, (cell, a, b, c));
  if (i1 == OBJECT_INSIDE_CELL || i2 == OBJECT_INSIDE_CELL || i3 == OBJECT_INSIDE_CELL)
    return OBJECT_INSIDE_CELL;
  std::pair<uint32_t, uint32_t> const xy = cell.XY();
  if (m2::IsPointStrictlyInsideTriangle(m2::PointD(xy.first, xy.second), a, b, c))
    return CELL_INSIDE_OBJECT;
  return CELL_OBJECT_NO_INTERSECTION;
}

template <class CellId, class CellIdContainerT, typename IntersectF>
void CoverObject(IntersectF const & intersect, uint64_t cellPenaltyArea, CellIdContainerT & out,
                 int cellDepth, CellId cell)
{
  uint64_t const cellArea = std::pow(uint64_t(1 << (cellDepth - 1 - cell.Level())), 2);
  CellObjectIntersection const intersection = intersect(cell);

  if (intersection == CELL_OBJECT_NO_INTERSECTION)
    return;
  if (intersection == CELL_INSIDE_OBJECT || cellPenaltyArea >= cellArea)
  {
    out.push_back(cell);
    return;
  }

  if (cell.Level() == cellDepth - 1)
  {
    out.push_back(cell);
    return;
  }

  buffer_vector<CellId, 32> subdiv;
  for (uint8_t i = 0; i < 4; ++i)
    CoverObject(intersect, cellPenaltyArea, subdiv, cellDepth, cell.Child(i));

  uint64_t subdivArea = 0;
  for (size_t i = 0; i < subdiv.size(); ++i)
    subdivArea += std::pow(uint64_t(1 << (cellDepth - 1 - subdiv[i].Level())), 2);

  ASSERT(!subdiv.empty(), (cellPenaltyArea, out, cell));

  // This criteria is more clear for me. Let's divide if we can save more than cellPenaltyArea.
  if (subdiv.size() > 1 && cellPenaltyArea >= cellArea - subdivArea)
  {
    out.push_back(cell);
  }
  else
  {
    for (size_t i = 0; i < subdiv.size(); ++i)
      out.push_back(subdiv[i]);
  }
}

// ObjectCoverer -----------------------------------------------------------------------------------
template <typename CellId, typename IntersectionInspector>
class ObjectCoverer
{
public:
  ObjectCoverer(IntersectionInspector const & intersectionInspector, int cellDepth,
                base::thread_pool::computational::ThreadPool & threadPool)
    : m_intersectionInspector{intersectionInspector}
    , m_cellDepth{cellDepth}
    , m_threadPool{threadPool}
  { }

  std::vector<CellId> Cover() const
  {
    std::vector<CellId> result;

    auto covering = std::vector<ObjectCovering>{{result, CellId::Root(), {}}};
    Cover(0, covering);

    return result;
  }

private:
  struct ObjectCovering
  {
    std::vector<CellId> & m_out;
    CellId m_cell;
    std::vector<CellId> m_subCells;
  };

  void Cover(int level, std::vector<ObjectCovering> & levelCovering) const
  {
    auto const uptoLevel = m_cellDepth - 1;
    if (level < uptoLevel)
      CoverBySubCells(level, levelCovering);

    ForwardLevelCoveringToOut(level, levelCovering);
  }

  void ForwardLevelCoveringToOut(int level, std::vector<ObjectCovering> & levelCovering) const
  {
    for (auto & cellCovering : levelCovering)
    {
      auto & out = cellCovering.m_out;

      auto const & subCells = cellCovering.m_subCells;

      bool allSubcellsAreChildren =
        std::all_of(subCells.begin(), subCells.end(),
                    [level](auto const & subCell) { return subCell.Level() + 1 == level; });

      if (subCells.empty())
        out.push_back(cellCovering.m_cell);
      else if (allSubcellsAreChildren && subCells.size() == 4)
        out.push_back(cellCovering.m_cell);
      else
        out.insert(out.end(), subCells.begin(), subCells.end());
    }
  }

  void CoverBySubCells(int level, std::vector<ObjectCovering> & levelCovering) const
  {
    if (level == m_parallelingLevel && levelCovering.size() / m_tasksPerThread > 1)
      CoverParallelBySubCells(level, levelCovering);
    else
      CoverSequencedBySubCells(level, levelCovering.begin(), levelCovering.end());
  }

  void CoverParallelBySubCells(int level, std::vector<ObjectCovering> & levelCovering) const
  {
    std::atomic_size_t unprocessedIndex{0};
    auto processor = [&]() {
      while (true)
      {
        auto const i = unprocessedIndex++;
        if (i >= levelCovering.size())
          return;

        CoverSequencedBySubCells(level, levelCovering.begin() + i, levelCovering.begin() + i + 1);
      }
    };

    auto const tasksCount = levelCovering.size() / m_tasksPerThread;
    m_threadPool.PerformParallelWorks(processor, tasksCount);
  }

  void CoverSequencedBySubCells(int level, auto levelCoveringBegin, auto levelCoveringEnd) const
  {
    auto const childrenLevel = level + 1;

    auto childrenLevelCovering = std::vector<ObjectCovering>{};
    childrenLevelCovering.reserve(std::distance(levelCoveringBegin, levelCoveringEnd));
    for (auto cellCovering = levelCoveringBegin; cellCovering != levelCoveringEnd; ++cellCovering)
    {
      auto & cell = cellCovering->m_cell;
      auto & subCells = cellCovering->m_subCells;

      for (uint8_t i = 0; i < 4; ++i)
      {
        auto childCell = cell.Child(i);

        CellObjectIntersection const intersection = m_intersectionInspector(childCell);

        if (intersection == CELL_OBJECT_NO_INTERSECTION)
          continue;

        if (intersection == CELL_INSIDE_OBJECT)
        {
          subCells.push_back(childCell);
          continue;
        }

        if (childrenLevel == m_cellDepth - 1)
          subCells.push_back(childCell);
        else
          childrenLevelCovering.push_back({subCells, childCell, {}});
      }
    }

    if (!childrenLevelCovering.empty())
      Cover(childrenLevel, childrenLevelCovering);
  }

  IntersectionInspector const & m_intersectionInspector;
  int m_cellDepth;
  base::thread_pool::computational::ThreadPool & m_threadPool;
  // |m_parallelingLevel| is checking level for parallelization.
  // This level has 87380 subcells (~100'000) and let this number is task unit complexity.
  int const m_parallelingLevel{m_cellDepth - std::min(m_cellDepth, 9)};
  unsigned const m_tasksPerThread = 10;  // ~1'000'000  == 10 * ~100'000 (see |m_parallelingLevel|)
};

template <class CellId, typename IntersectF>
std::vector<CellId> CoverObject(
    IntersectF const & intersect, int cellDepth,
    base::thread_pool::computational::ThreadPool & threadPool)
{
  ObjectCoverer<CellId, IntersectF> coverer{intersect, cellDepth, threadPool};
  return coverer.Cover();
}

}  // namespace covering

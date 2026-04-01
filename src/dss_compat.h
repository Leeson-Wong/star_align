#pragma once
// Lightweight compatibility layer to replace Qt/Workspace/ztrace dependencies
// used by DSS core files (MatchingStars, BilinearParameters, matrix.h).

#include <vector>
#include <cstdint>

// ---------- QPointF replacement ----------
struct QPointF {
    double m_x = 0.0;
    double m_y = 0.0;

    QPointF() = default;
    QPointF(double x, double y) : m_x(x), m_y(y) {}

    double x()  const { return m_x; }
    double y()  const { return m_y; }
    double& rx()      { return m_x; }
    double& ry()      { return m_y; }
};

using QPointFVector = std::vector<QPointF>;
using qreal = double;

// ---------- ztrace replacement (empty macros) ----------
#define ZFUNCTRACE_RUNTIME()
#define ZTRACE_RUNTIME(...)

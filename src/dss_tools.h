#pragma once
// Simplified DSSTools.h - only the functions needed by MatchingStars/BilinearParameters
// Extracted from DSS DeepSkyStackerKernel/DSSTools.h

#include "dss_compat.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>
#include <vector>

inline double Distance(const double fX1, const double fY1, const double fX2, const double fY2) noexcept
{
	constexpr auto SquareOf = [](const double x) { return x * x; };
	return std::sqrt(SquareOf(fX1 - fX2) + SquareOf(fY1 - fY2));
}

inline double Distance(const QPointF& pt1, const QPointF& pt2) noexcept
{
	return Distance(pt1.x(), pt1.y(), pt2.x(), pt2.y());
}

template <class T>
inline double Average(const std::vector<T>& values)
{
	return std::accumulate(values.cbegin(), values.cend(), 0.0) / values.size();
}

template <class T>
inline double Sigma2(const std::vector<T>& values, double& average)
{
	double result = 0.0;
	double squareDiff = 0.0;

	average = Average(values);

	for (double val : values)
		squareDiff += std::pow(val - average, 2);

	if (values.size())
		result = sqrt(squareDiff / values.size());

	return result;
}

template <class T>
inline double Sigma(const std::vector<T>& values)
{
	double average;
	return Sigma2(values, average);
}

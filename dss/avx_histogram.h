#pragma once
// avx_histogram.h - stub for AVX histogram
#include "pch.h"
#include <vector>

class CMemoryBitmap;

class AvxHistogram
{
public:
	static constexpr int HistogramSize = 65536;
	using HistogramVectorType = std::vector<int>;

	std::vector<int> m_red;
	std::vector<int> m_green;
	std::vector<int> m_blue;

	AvxHistogram() : m_red(HistogramSize, 0), m_green(HistogramSize, 0), m_blue(HistogramSize, 0) {}
	explicit AvxHistogram(const CMemoryBitmap&) : m_red(HistogramSize, 0), m_green(HistogramSize, 0), m_blue(HistogramSize, 0) {}
	void reset() {
		std::fill(m_red.begin(), m_red.end(), 0);
		std::fill(m_green.begin(), m_green.end(), 0);
		std::fill(m_blue.begin(), m_blue.end(), 0);
	}
};

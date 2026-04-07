#pragma once
// histogram.h - stub for histogram
#include "pch.h"
#include <vector>

class CHistogram
{
public:
	std::vector<int> m_vRed;
	std::vector<int> m_vGreen;
	std::vector<int> m_vBlue;

	CHistogram() = default;
	void Reset() {
		m_vRed.clear();
		m_vGreen.clear();
		m_vBlue.clear();
	}
};

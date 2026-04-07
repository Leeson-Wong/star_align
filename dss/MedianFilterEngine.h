#pragma once
// MedianFilterEngine.h - stub
#include "pch.h"
class CMemoryBitmap;

template <class T> class CColorMedianFilterEngineT;
template <class T> class CGrayMedianFilterEngineT;

class CMedianFilterEngine
{
public:
	virtual ~CMedianFilterEngine() = default;
};

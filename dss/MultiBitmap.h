#pragma once
// MultiBitmap.h - stub
#include "pch.h"
class CMemoryBitmap;

template <class T> class CColorMultiBitmapT;
template <class T> class CGrayMultiBitmapT;

template <class T> class CColorMedianFilterEngineT;
template <class T> class CGrayMedianFilterEngineT;

class CMultiBitmap
{
public:
	virtual ~CMultiBitmap() = default;
	virtual void SetNrBitmaps(int n) {}
	virtual void AddBitmap(const CMemoryBitmap*) {}
	virtual void SetBitmapModel(bool) {}
	virtual void RemoveBitmap(int) {}
	template <class T> void SetBitmapModel(T model) {}
};
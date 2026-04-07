#pragma once
// ColorMultiBitmap.h - stub for color multi-bitmap
#include "pch.h"
#include "MemoryBitmap.h"

class CColorMultiBitmap
{
public:
	virtual ~CColorMultiBitmap() = default;
	virtual void SetNrBitmaps(int) {}
	virtual void AddBitmap(CMemoryBitmap*) {}
};

#pragma once
// GreyMultiBitmap.h - stub for grey multi-bitmap
#include "pch.h"
#include "MemoryBitmap.h"

class CGreyMultiBitmap
{
public:
	virtual ~CGreyMultiBitmap() = default;
	virtual void SetNrBitmaps(int) {}
	virtual void AddBitmap(CMemoryBitmap*) {}
};

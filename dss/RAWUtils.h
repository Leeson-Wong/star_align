#pragma once
// RAWUtils.h - stub for RAW file utilities
#include "pch.h"
class CMemoryBitmap;
class CBitmapInfo;

inline bool IsRAWFile(const QString&) { return false; }
inline bool LoadRAWPicture(const fs::path&, std::shared_ptr<CMemoryBitmap>&, class DSS::OldProgressBase*) { return false; }
inline bool GetRAWPictureInfo(const fs::path&, CBitmapInfo&) { return false; }

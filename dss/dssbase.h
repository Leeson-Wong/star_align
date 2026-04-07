#pragma once
// dssbase stub - provides interfaces used everywhere

#include "dss_qt.h"

class CMemoryBitmap;

// Forward declare DSS::OldProgressBase
namespace DSS { class OldProgressBase; }

bool FetchPicture(const fs::path& filePath, std::shared_ptr<CMemoryBitmap>& pBitmap, DSS::OldProgressBase* pProgress, std::shared_ptr<QImage>& pQImage);
bool DebayerPicture(CMemoryBitmap* pInBitmap, std::shared_ptr<CMemoryBitmap>& pOutBitmap, DSS::OldProgressBase* pProgress);

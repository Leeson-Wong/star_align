#pragma once
// dssbase stub - provides error reporting interfaces used everywhere

#include "dss_qt.h"

// Bitmap info class used for picture loading
class CBitmapInfo
{
public:
	int m_lWidth{ 0 };
	int m_lHeight{ 0 };
	int m_lBitsPerChannel{ 16 };
	int m_lNrChannels{ 3 };
	double m_fExposure{ 0.0 };
	int m_lISOSpeed{ 0 };
	int m_lGain{ -1 };
	double m_fAperture{ 0.0 };
	QString m_strFileName;
	QString m_strFilterName;

	CBitmapInfo() = default;

	bool CanLoad() const { return true; }
	void GetDescription(QString& desc) const { desc = ""; }
};

// Picture loading function stubs
class CMemoryBitmap;

bool GetPictureInfo(const fs::path& filePath, CBitmapInfo& info);
bool FetchPicture(const fs::path& filePath, std::shared_ptr<CMemoryBitmap>& pBitmap, class DSS::OldProgressBase* pProgress, std::shared_ptr<QImage>& pQImage);
bool DebayerPicture(CMemoryBitmap* pInBitmap, std::shared_ptr<CMemoryBitmap>& pOutBitmap, class DSS::OldProgressBase* pProgress);

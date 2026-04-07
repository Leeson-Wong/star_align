#pragma once
// BitmapInfo.h - CBitmapInfo class definition
// Contains full bitmap metadata

#include "dss_qt.h"
#include "DSSCommon.h"
#include "cfa.h"
#include "BitmapExtraInfo.h"

class CBitmapInfo
{
public:
	QString m_strFileName;
	QString m_strFileType;
	QString m_strModel;
	int m_lWidth{ 0 };
	int m_lHeight{ 0 };
	int m_lBitsPerChannel{ 0 };
	int m_lNrChannels{ 0 };
	int m_lISOSpeed{ 0 };
	int m_lGain{ -1 };
	double m_fExposure{ 0.0 };
	double m_fAperture{ 0.0 };
	bool m_bCanLoad{ false };
	bool m_bFloat{ false };
	bool m_bMaster{ false };
	bool m_bFITS16bit{ false };
	CFATYPE m_CFAType{ CFATYPE_NONE };
	int m_xBayerOffset{ 0 };
	int m_yBayerOffset{ 0 };
	QString m_strDateTime;
	QDateTime m_DateTime;
	QDateTime m_InfoTime;
	CBitmapExtraInfo m_ExtraInfo;
	QString m_filterName;

	CBitmapInfo();
	CBitmapInfo(const CBitmapInfo& bi);
	explicit CBitmapInfo(const fs::path& fileName);

	CBitmapInfo& operator=(const CBitmapInfo& bi);
	bool operator<(const CBitmapInfo& other) const;
	bool operator==(const CBitmapInfo& other) const;

	bool CanLoad() const;
	bool IsCFA();
	bool IsMaster();
	void GetDescription(QString& strDescription);
	bool IsInitialized();

private:
	void CopyFrom(const CBitmapInfo& bi);
	void Init();
};

#pragma once
#include "ExtraInfo.h"

class CBitmapExtraInfo
{
public:
	std::vector<ExtraInfo> m_vExtras;

public:
	CBitmapExtraInfo() = default;
	CBitmapExtraInfo(const CBitmapExtraInfo& rhs) = default;
	CBitmapExtraInfo(CBitmapExtraInfo&& rhs) = default;

	CBitmapExtraInfo& operator = (const CBitmapExtraInfo& rhs) = default;
	CBitmapExtraInfo& operator = (CBitmapExtraInfo&& rhs) = default;

	~CBitmapExtraInfo() = default;

	void AddInfo(const ExtraInfo& ei)
	{
		m_vExtras.push_back(ei);
	}

	void AddInfo(const QString& szName, const QString& szValue, const QString& szComment, bool bPropagate = false)
	{
		ExtraInfo ei;
		ei.m_Type = ExtraInfo::EIT_STRING;
		ei.m_strName = szName;
		ei.m_strValue = szValue;
		ei.m_strComment = szComment;
		ei.m_bPropagate = bPropagate;
		m_vExtras.push_back(ei);
	}
	void AddInfo(const QString& szName, int lValue, const QString& szComment, bool bPropagate = false)
	{
		ExtraInfo ei;
		ei.m_Type = ExtraInfo::EIT_LONG;
		ei.m_strName = szName;
		ei.m_strComment = szComment;
		ei.m_lValue = lValue;
		ei.m_bPropagate = bPropagate;
		m_vExtras.push_back(ei);
	}
	void AddInfo(const QString& szName, double fValue, const QString& szComment = nullptr, bool bPropagate = false)
	{
		ExtraInfo ei;
		ei.m_Type = ExtraInfo::EIT_DOUBLE;
		ei.m_strName = szName;
		ei.m_strComment = szComment;
		ei.m_fValue = fValue;
		ei.m_bPropagate = bPropagate;
		m_vExtras.push_back(ei);
	}

	void Clear()
	{
		m_vExtras.clear();
	}
};


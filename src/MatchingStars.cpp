#include "dss_compat.h"
#include "MatchingStars.h"

#define _NO_EXCEPTION
#include "matrix.h"

constexpr int MINPAIRSTOBISQUARED = 25;
constexpr int MINPAIRSTOBICUBIC	= 40;
constexpr double TriangleMaxRatio = 0.9;


namespace {
	TRANSFORMATIONTYPE GetTransformationType(const size_t lNrVotingPairs)
	{
		std::uint32_t dwAlignmentTransformation = 2;
		TRANSFORMATIONTYPE		TTResult = TT_BILINEAR;

		// Hardcoded: AlignmentTransformation = 2 (auto)

		if (dwAlignmentTransformation > TT_LAST)
			dwAlignmentTransformation = 0;

		if (dwAlignmentTransformation == 0)
		{
			// Automatic - no forcing
			if (lNrVotingPairs >= MINPAIRSTOBICUBIC)
				TTResult = TT_BICUBIC;
			else if (lNrVotingPairs >= MINPAIRSTOBISQUARED)
				TTResult = TT_BISQUARED;
			else
				TTResult = TT_BILINEAR;
		}
		else
		{
			if (dwAlignmentTransformation == 5)
				TTResult = TT_NONE;
			else if (dwAlignmentTransformation == 4 && lNrVotingPairs >= MINPAIRSTOBICUBIC)
				TTResult = TT_BICUBIC;
			else if (dwAlignmentTransformation >= 3 && lNrVotingPairs >= MINPAIRSTOBISQUARED)
				TTResult = TT_BISQUARED;
			else if (dwAlignmentTransformation >= 2)
				TTResult = TT_BILINEAR;
			else
				TTResult = TT_LINEAR;
		}

		return TTResult;
	}

	TRANSFORMATIONTYPE GetTransformationType()
	{
		return GetTransformationType(99999);
	}

	bool AreCornersLocked()
	{
		// Hardcoded: LockCorners = true
		return true;
	}
}


STARDISTVECTOR CMatchingStars::ComputeStarDistances(const QPointFVector& vStars)
{
	STARDISTVECTOR vStarDist;

	vStarDist.reserve((vStars.size() * (vStars.size() - 1)) / 2);

	for (size_t i = 0; i < vStars.size(); ++i)
	{
		for (size_t j = i + 1; j < vStars.size(); ++j)
		{
			const double fDistance = Distance(vStars[i].x(), vStars[i].y(), vStars[j].x(), vStars[j].y());

			vStarDist.emplace_back(i, j, fDistance);
		}
	}

	static_assert(CStarDist{ 1, 2 } < CStarDist{ 2, 2 } && CStarDist{ 1, 1 } < CStarDist{ 1, 2 }
		&& !(CStarDist{ 2, 6 } < CStarDist{ 1, 6 }) && !(CStarDist{ 2, 6 } < CStarDist{ 2, 5 }) && !(CStarDist{ 6, 6 } < CStarDist{ 6, 6 })
	);

	std::sort(vStarDist.begin(), vStarDist.end());
	return vStarDist;
}


void CMatchingStars::ComputeTriangles(const QPointFVector& vStars, STARTRIANGLEVECTOR& vTriangles)
{
	ZFUNCTRACE_RUNTIME();

	const STARDISTVECTOR vStarDist = ComputeStarDistances(vStars);

	for (size_t i = 0; i < vStars.size(); i++)
	{
		for (size_t j = i + 1; j < vStars.size(); j++)
		{
			std::array<float, 3> vDistances;
			auto it = std::lower_bound(vStarDist.begin(), vStarDist.end(), CStarDist{ i, j });
			vDistances[0] = it->m_fDistance;

			for (size_t k = j + 1; k < vStars.size(); k++)
			{
				it = std::lower_bound(vStarDist.begin(), vStarDist.end(), CStarDist{ j, k });
				vDistances[1] = it->m_fDistance;
				it = std::lower_bound(vStarDist.begin(), vStarDist.end(), CStarDist{ i, k });
				vDistances[2] = it->m_fDistance;

				std::sort(vDistances.begin(), vDistances.end());

				if (vDistances[2] > 0)
				{
					const float fX = vDistances[1] / vDistances[2];
					const float fY = vDistances[0] / vDistances[2];

					if (fX < TriangleMaxRatio)
					{
						vTriangles.emplace_back(i, j, k, fX, fY);
					}
				}
			}
		}
	}

	std::sort(vTriangles.begin(), vTriangles.end());
}


void CMatchingStars::InitVotingGrid(VOTINGPAIRVECTOR& vVotingPairs)
{
	vVotingPairs.clear();
	vVotingPairs.reserve(m_vRefStars.size() * m_vTgtStars.size());

	for (int i = 0; i < static_cast<int>(m_vRefStars.size()); i++)
	{
		for (int j = 0; j < static_cast<int>(m_vTgtStars.size()); j++)
		{
			vVotingPairs.emplace_back(i, j);
		}
	}
}

namespace {
	static void AddVote(const std::uint32_t RefStar, const std::uint32_t TgtStar, VOTINGPAIRVECTOR& vVotingPairs, const size_t lNrTgtStars)
	{
		const size_t offset = RefStar * lNrTgtStars + TgtStar;
		vVotingPairs[offset].m_lNrVotes++;
	}

	static void AddAllVotes(const STARTRIANGLEVECTOR::const_iterator itRef, const STARTRIANGLEVECTOR::const_iterator itTgt, VOTINGPAIRVECTOR& vVotingPairs, const size_t nrTargetStars)
	{
		AddVote(itRef->m_Star1, itTgt->m_Star1, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star1, itTgt->m_Star2, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star1, itTgt->m_Star3, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star2, itTgt->m_Star1, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star2, itTgt->m_Star2, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star2, itTgt->m_Star3, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star3, itTgt->m_Star1, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star3, itTgt->m_Star2, vVotingPairs, nrTargetStars);
		AddVote(itRef->m_Star3, itTgt->m_Star3, vVotingPairs, nrTargetStars);
	}
}


using DMATRIX = math::matrix<double>;

bool CMatchingStars::ComputeTransformation(const VOTINGPAIRVECTOR& vVotingPairs, CBilinearParameters& BilinearParameters, const TRANSFORMATIONTYPE TType)
{
	bool bResult = false;
	const double fXWidth = m_lWidth;
	const double fYWidth = m_lHeight;

	BilinearParameters.fXWidth = fXWidth;
	BilinearParameters.fYWidth = fYWidth;

	if (TType == TT_BICUBIC)
	{
		DMATRIX M(vVotingPairs.size(), 16);
		DMATRIX X(vVotingPairs.size(), 1);
		DMATRIX Y(vVotingPairs.size(), 1);

		for (size_t i = 0; i < vVotingPairs.size(); i++)
		{
			const QPointF& Star = RefStar(vVotingPairs[i]);
			X(i, 0) = Star.x() / fXWidth;
			Y(i, 0) = Star.y() / fYWidth;
		}

		for (size_t i = 0; i < vVotingPairs.size(); i++)
		{
			const QPointF& Star = TgtStar(vVotingPairs[i]);
			const double X1 = Star.x() / fXWidth;
			const double X2 = X1 * X1;
			const double X3 = X1 * X1 * X1;
			const double Y1 = Star.y() / fYWidth;
			const double Y2 = Y1 * Y1;
			const double Y3 = Y1 * Y1 * Y1;

			M(i, 0) = 1;
			M(i, 1) = X1;
			M(i, 2) = Y1;
			M(i, 3) = X1*Y1;
			M(i, 4) = X2;
			M(i, 5) = Y2;
			M(i, 6) = X2*Y1;
			M(i, 7) = X1*Y2;
			M(i, 8) = X2*Y2;
			M(i, 9) = X3;
			M(i, 10) = Y3;
			M(i, 11) = X3*Y1;
			M(i, 12) = X1*Y3;
			M(i, 13) = X3*Y2;
			M(i, 14) = X2*Y3;
			M(i, 15) = X3*Y3;
		}

		const DMATRIX MT = ~M;
		DMATRIX TM = MT * M;

		try
		{
			if (!TM.IsSingular())
			{
				const DMATRIX A = !TM * MT * X;
				const DMATRIX B = !TM * MT * Y;

				BilinearParameters.Type = TType;
				BilinearParameters.a0 = A(0, 0);
				BilinearParameters.a1 = A(1, 0);
				BilinearParameters.a2 = A(2, 0);
				BilinearParameters.a3 = A(3, 0);
				BilinearParameters.a4 = A(4, 0);
				BilinearParameters.a5 = A(5, 0);
				BilinearParameters.a6 = A(6, 0);
				BilinearParameters.a7 = A(7, 0);
				BilinearParameters.a8 = A(8, 0);
				BilinearParameters.a9 = A(9, 0);
				BilinearParameters.a10 = A(10, 0);
				BilinearParameters.a11 = A(11, 0);
				BilinearParameters.a12 = A(12, 0);
				BilinearParameters.a13 = A(13, 0);
				BilinearParameters.a14 = A(14, 0);
				BilinearParameters.a15 = A(15, 0);

				BilinearParameters.b0 = B(0, 0);
				BilinearParameters.b1 = B(1, 0);
				BilinearParameters.b2 = B(2, 0);
				BilinearParameters.b3 = B(3, 0);
				BilinearParameters.b4 = B(4, 0);
				BilinearParameters.b5 = B(5, 0);
				BilinearParameters.b6 = B(6, 0);
				BilinearParameters.b7 = B(7, 0);
				BilinearParameters.b8 = B(8, 0);
				BilinearParameters.b9 = B(9, 0);
				BilinearParameters.b10 = B(10, 0);
				BilinearParameters.b11 = B(11, 0);
				BilinearParameters.b12 = B(12, 0);
				BilinearParameters.b13 = B(13, 0);
				BilinearParameters.b14 = B(14, 0);
				BilinearParameters.b15 = B(15, 0);

				bResult = true;
			}
		}
		catch(math::matrix_error const&)
		{
			bResult = false;
		}
	}
	else if (TType == TT_BISQUARED)
	{
		DMATRIX M(vVotingPairs.size(), 9);
		DMATRIX X(vVotingPairs.size(), 1);
		DMATRIX Y(vVotingPairs.size(), 1);

		for (size_t i = 0; i < vVotingPairs.size(); i++)
		{
			const QPointF& Star = RefStar(vVotingPairs[i]);
			X(i, 0) = Star.x() / fXWidth;
			Y(i, 0) = Star.y() / fYWidth;
		}

		for (size_t i = 0; i < vVotingPairs.size(); i++)
		{
			const QPointF& Star = TgtStar(vVotingPairs[i]);

			const double X1 = Star.x() / fXWidth;
			const double X2 = X1 * X1;
			const double Y1 = Star.y() / fYWidth;
			const double Y2 = Y1 * Y1;

			M(i, 0) = 1;
			M(i, 1) = X1;
			M(i, 2) = Y1;
			M(i, 3) = X1*Y1;
			M(i, 4) = X2;
			M(i, 5) = Y2;
			M(i, 6) = X2 * Y1;
			M(i, 7) = X1 * Y2;
			M(i, 8) = X2 * Y2;
		}

		const DMATRIX MT = ~M;
		DMATRIX TM = MT * M;

		try
		{
			if (!TM.IsSingular())
			{
				const DMATRIX A = !TM * MT * X;
				const DMATRIX B = !TM * MT * Y;

				BilinearParameters.Type = TType;
				BilinearParameters.a0 = A(0, 0);
				BilinearParameters.a1 = A(1, 0);
				BilinearParameters.a2 = A(2, 0);
				BilinearParameters.a3 = A(3, 0);
				BilinearParameters.a4 = A(4, 0);
				BilinearParameters.a5 = A(5, 0);
				BilinearParameters.a6 = A(6, 0);
				BilinearParameters.a7 = A(7, 0);
				BilinearParameters.a8 = A(8, 0);

				BilinearParameters.b0 = B(0, 0);
				BilinearParameters.b1 = B(1, 0);
				BilinearParameters.b2 = B(2, 0);
				BilinearParameters.b3 = B(3, 0);
				BilinearParameters.b4 = B(4, 0);
				BilinearParameters.b5 = B(5, 0);
				BilinearParameters.b6 = B(6, 0);
				BilinearParameters.b7 = B(7, 0);
				BilinearParameters.b8 = B(8, 0);

				bResult = true;
			}
		}
		catch(math::matrix_error const&)
		{
			bResult = false;
		}
	}
	else
	{
		DMATRIX M(vVotingPairs.size(), 4);
		DMATRIX X(vVotingPairs.size(), 1);
		DMATRIX Y(vVotingPairs.size(), 1);

		for (size_t i = 0; i < vVotingPairs.size(); i++)
		{
			const QPointF& Star = RefStar(vVotingPairs[i]);
			X(i, 0) = Star.x() / fXWidth;
			Y(i, 0) = Star.y() / fYWidth;
		}

		for (size_t i = 0; i < vVotingPairs.size(); i++)
		{
			const QPointF& Star = TgtStar(vVotingPairs[i]);
			const double X1 = Star.x() / fXWidth;
			const double Y1 = Star.y() / fYWidth;

			M(i, 0) = 1;
			M(i, 1) = X1;
			M(i, 2) = Y1;
			M(i, 3) = X1 * Y1;
		}

		const DMATRIX MT = ~M;
		DMATRIX TM = MT * M;

		try
		{
			if (!TM.IsSingular())
			{
				const DMATRIX A = !TM * MT * X;
				const DMATRIX B = !TM * MT * Y;

				BilinearParameters.a0 = A(0, 0);
				BilinearParameters.a1 = A(1, 0);
				BilinearParameters.a2 = A(2, 0);
				BilinearParameters.a3 = A(3, 0);
				BilinearParameters.b0 = B(0, 0);
				BilinearParameters.b1 = B(1, 0);
				BilinearParameters.b2 = B(2, 0);
				BilinearParameters.b3 = B(3, 0);

				bResult = true;
			}
		}
		catch(math::matrix_error const&)
		{
			bResult = false;
		}
	}

	return bResult;
}

template <typename... DistanceVector>
std::pair<double, size_t> CMatchingStars::ComputeDistanceBetweenStars(const VOTINGPAIRVECTOR& vTestedPairs, const CBilinearParameters& projection, DistanceVector&... distances)
{
	double maxDistance = 0.0;
	size_t maxDistanceIndex = 0;
	auto vdistance = std::tie(distances...);

	for (size_t i = 0; i < vTestedPairs.size(); i++)
	{
		const auto& testedPair = vTestedPairs[i];
		if (!testedPair.IsCorner())
		{
			const double distance = Distance(
				projection.transform(TgtStar(testedPair)),
				RefStar(testedPair)
			);

			if constexpr (sizeof...(DistanceVector) == 1)
			{
				std::get<0>(vdistance).push_back(distance);
			}

			if (distance > maxDistance)
			{
				maxDistance = distance;
				maxDistanceIndex = i;
			}
		}
	}

	return { maxDistance, maxDistanceIndex };
}


bool CMatchingStars::ComputeCoordinatesTransformation(VOTINGPAIRVECTOR& vVotingPairs, CBilinearParameters& BilinearParameters, const TRANSFORMATIONTYPE MaxTType)
{
	bool bResult = false;
	bool bEnd = false;
	TRANSFORMATIONTYPE TType = TT_BILINEAR;
	TRANSFORMATIONTYPE OkTType = TT_LINEAR;

	CBilinearParameters OkTransformation;

	std::vector<int> vAddedPairs;
	std::vector<int> vOkAddedPairs;
	VOTINGPAIRVECTOR vPairs = vVotingPairs;
	VOTINGPAIRVECTOR vTestedPairs;
	VOTINGPAIRVECTOR vOkPairs;

	const size_t nrExtraPairs = !vVotingPairs.empty() && vVotingPairs[0].IsCorner() ? 4 : 0;

	while (!bEnd && !bResult)
	{
		const size_t nrPairs = nrExtraPairs + (TType == TT_BICUBIC ? 32 : (TType == TT_BISQUARED ? 18 : 8));

		vAddedPairs.clear();
		vTestedPairs.clear();
		for (int i = 0; i < static_cast<int>(vPairs.size()); i++)
		{
			if (vPairs[i].IsActive() && vPairs[i].IsLocked())
			{
				vTestedPairs.push_back(vPairs[i]);
				vAddedPairs.push_back(i);
			}
		}

		for (size_t i = 0; i < vPairs.size() && vTestedPairs.size() < nrPairs; i++)
		{
			if (vPairs[i].IsActive() && !vPairs[i].IsLocked())
			{
				vTestedPairs.push_back(vPairs[i]);
				vAddedPairs.push_back(static_cast<int>(i));
			}
		}

		if (vTestedPairs.size() == nrPairs)
		{
			CBilinearParameters projection;

			if (ComputeTransformation(vTestedPairs, projection, TType))
			{
				std::vector<double> vDistances;
				const auto [fMaxDistance, maxDistanceIndex] = ComputeDistanceBetweenStars(vTestedPairs, projection, vDistances);

				if (fMaxDistance > 3)
				{
					int lDeactivatedIndice = 0;
					bool bOneDeactivated = false;

					const double fAverage = Average(vDistances);
					const double fSigma = Sigma(vDistances);

					for (size_t i = 0; i < vDistances.size(); i++)
					{
						if (std::abs(vDistances[i] - fAverage) > 2 * fSigma)
						{
							lDeactivatedIndice = vAddedPairs[i];
							if (vPairs[lDeactivatedIndice].IsCorner())
							{
							}
							else
							{
								vPairs[lDeactivatedIndice].SetActive(false);
								if (vDistances[i] < 7)
									vPairs[lDeactivatedIndice].SetPossible(true);
								bOneDeactivated = true;
							}
						}
					}

					if (!bOneDeactivated)
					{
						for (size_t i = 0; i < vDistances.size(); i++)
						{
							if (fabs(vDistances[i] - fAverage) > fSigma)
							{
								lDeactivatedIndice = vAddedPairs[i];
								if (vPairs[lDeactivatedIndice].IsCorner())
								{
								}
								else
								{
									vPairs[lDeactivatedIndice].SetActive(false);
									bOneDeactivated = true;
								}
							}
						}
					}

					if (!bOneDeactivated)
					{
						lDeactivatedIndice = vAddedPairs[maxDistanceIndex];
						if (vPairs[lDeactivatedIndice].IsCorner())
						{
						}
						else
						{
							vPairs[lDeactivatedIndice].SetActive(false);
						}
					}
				}
				else
				{
					OkTransformation = projection;
					vOkPairs = vTestedPairs;
					vOkAddedPairs = vAddedPairs;
					OkTType = TType;
					bResult = (TType == MaxTType);

					if (TType < MaxTType)
					{
						TType = getNextHigherTransformationType(TType);

						for (auto& votingPair : vPairs)
						{
							if (votingPair.IsPossible())
							{
								votingPair.SetActive(true);
								votingPair.SetPossible(false);
							}
						}

						for (const size_t index : vAddedPairs)
							vPairs[index].SetLocked(true);
					}
				}
			}
			else
			{
				vPairs[vAddedPairs[nrPairs - 1]].SetActive(false);
			}
		}
		else
			bEnd = true;
	}

	if (!vOkPairs.empty())
		bResult = true;

	if (bResult)
	{
		bEnd = false;
		int lNrFails = 0;

		BilinearParameters = OkTransformation;

		vTestedPairs = vOkPairs;
		vAddedPairs = vOkAddedPairs;
		TType = OkTType;

		for (const auto index : vAddedPairs)
			vVotingPairs[index].SetUsed(true);

		while (!bEnd)
		{
			bool bTransformOk = false;
			int lAddedPair = -1;
			VOTINGPAIRVECTOR vTempPairs = vTestedPairs;

			for (size_t i = 0; i < vVotingPairs.size() && lAddedPair < 0; i++)
			{
				if (vVotingPairs[i].IsActive() && !vVotingPairs[i].IsUsed())
				{
					lAddedPair = static_cast<int>(i);
					vTempPairs.push_back(vVotingPairs[i]);
					vVotingPairs[lAddedPair].SetUsed(true);
				}
			}

			if (lAddedPair >= 0)
			{
				CBilinearParameters projection;
				if (ComputeTransformation(vTempPairs, projection, TType))
				{
					const double maxDistance = ComputeDistanceBetweenStars(vTempPairs, projection).first;
					if (maxDistance <= 2)
					{
						vTestedPairs = vTempPairs;
						BilinearParameters = projection;
						vAddedPairs.push_back(lAddedPair);
						bTransformOk = true;
					}
					else
						vVotingPairs[lAddedPair].SetActive(false);
				}

				if (!bTransformOk)
				{
					lNrFails++;
					if (lNrFails > 3)
						bEnd = true;
				}
			}
			else
				bEnd = true;
		}
	}

	if (bResult)
		vVotingPairs = vTestedPairs;

	return bResult;
}


bool CMatchingStars::ComputeSigmaClippingTransformation(const VOTINGPAIRVECTOR& vVotingPairs, CBilinearParameters& BilinearParameters, const TRANSFORMATIONTYPE TType)
{
	bool bResult = false;
	VOTINGPAIRVECTOR vPairs = vVotingPairs;

	if (AreCornersLocked())
	{
		CBilinearParameters BaseTransformation;

		bResult = ComputeCoordinatesTransformation(vPairs, BaseTransformation, TT_BILINEAR);
		if (bResult)
		{
			m_vRefCorners.clear();
			m_vTgtCorners.clear();

			m_vTgtCorners.push_back(QPointF(0, 0));
			m_vTgtCorners.push_back(QPointF(m_lWidth - 1, 0));
			m_vTgtCorners.push_back(QPointF(0, m_lHeight - 1));
			m_vTgtCorners.push_back(QPointF(m_lWidth - 1, m_lHeight - 1));

			for (const QPointF& targetCorner : m_vTgtCorners)
			{
				m_vRefCorners.push_back(BaseTransformation.transform(targetCorner));
			}

			vPairs = vVotingPairs;
			VotingPair vp;

			vp.m_Flags = VPFLAG_ACTIVE | VPFLAG_CORNER_TOPLEFT;
			vp.m_lNrVotes = 10000000;
			vp.m_RefStar = vp.m_TgtStar = 0;
			vPairs.push_back(vp);

			vp.m_Flags = VPFLAG_ACTIVE | VPFLAG_CORNER_TOPRIGHT;
			vp.m_RefStar = vp.m_TgtStar = 1;
			vPairs.push_back(vp);

			vp.m_Flags = VPFLAG_ACTIVE | VPFLAG_CORNER_BOTTOMLEFT;
			vp.m_RefStar = vp.m_TgtStar = 2;
			vPairs.push_back(vp);

			vp.m_Flags = VPFLAG_ACTIVE | VPFLAG_CORNER_BOTTOMRIGHT;
			vp.m_RefStar = vp.m_TgtStar = 3;
			vPairs.push_back(vp);

			std::sort(vPairs.begin(), vPairs.end(), std::greater<VotingPair>{});
			bResult = ComputeCoordinatesTransformation(vPairs, BilinearParameters, TType);

			VOTINGPAIRVECTOR vOutPairs;
			for (const auto& pair : vPairs)
			{
				if (pair.IsActive() && !pair.IsCorner())
					vOutPairs.push_back(pair);
			}

			vPairs = vOutPairs;
		}
	}
	else
		bResult = ComputeCoordinatesTransformation(vPairs, BilinearParameters, TType);

	if (bResult)
		m_vVotedPairs = vPairs;

	return bResult;
}


bool CMatchingStars::ComputeMatchingTriangleTransformation(CBilinearParameters & BilinearParameters)
{
	constexpr float TRIANGLETOLERANCE = 0.002f;
	bool bResult = false;

	if (m_vRefTriangles.empty())
		ComputeTriangles(m_vRefStars, m_vRefTriangles);

	ComputeTriangles(m_vTgtStars, m_vTgtTriangles);

	bool bEnd = false;
	VOTINGPAIRVECTOR vVotingPairs;

	InitVotingGrid(vVotingPairs);

	auto itLastUsedRef = m_vRefTriangles.cbegin();

	for (auto itTgt = m_vTgtTriangles.cbegin(); itTgt != m_vTgtTriangles.cend() && !bEnd; ++itTgt)
	{
		while (itLastUsedRef != m_vRefTriangles.cend() && (itTgt->m_fX > itLastUsedRef->m_fX + TRIANGLETOLERANCE))
		{
			++itLastUsedRef;
		}

		if (itLastUsedRef == m_vRefTriangles.cend())
			bEnd = true;
		else
		{
			auto itRef = itLastUsedRef;
			while (itRef != m_vRefTriangles.cend() && (itRef->m_fX < itTgt->m_fX + TRIANGLETOLERANCE))
			{
				const auto distance = Distance(itRef->m_fX, itRef->m_fY, itTgt->m_fX, itTgt->m_fY);
				if (distance <= TRIANGLETOLERANCE)
				{
					AddAllVotes(itRef, itTgt, vVotingPairs, m_vTgtStars.size());
				}
				++itRef;
			}
		}
	}

	std::sort(vVotingPairs.begin(), vVotingPairs.end(), std::greater<VotingPair>{});

	if (vVotingPairs.size() >= m_vTgtStars.size())
	{
		int lMinNrVotes = vVotingPairs[m_vTgtStars.size() * 2 - 1].m_lNrVotes;
		if (lMinNrVotes == 0)
			lMinNrVotes = 1;

		int lCut = 0;
		while (vVotingPairs[lCut].m_lNrVotes >= lMinNrVotes)
			lCut++;
		vVotingPairs.resize(lCut);

		const TRANSFORMATIONTYPE TType = GetTransformationType(vVotingPairs.size());

		bResult = ComputeSigmaClippingTransformation(vVotingPairs, BilinearParameters, TType);

		if (bResult && (TType == TT_LINEAR))
		{
			BilinearParameters.a3 = 0;
			BilinearParameters.b3 = 0;
		}
	}

	return bResult;
}


bool CMatchingStars::ComputeLargeTriangleTransformation(CBilinearParameters& BilinearParameters)
{
	constexpr double MAXSTARDISTANCEDELTA = 2.0;

	auto createIota = [](const size_t size) -> std::vector<int>
	{
		std::vector<int> result(size);
		for (int i = 0; i < static_cast<int>(size); i++)
			result[i] = i;
		return result;
	};

	if (m_vRefStarDistances.empty())
	{
		m_vRefStarDistances = ComputeStarDistances(m_vRefStars);
		m_vRefStarIndices = createIota(m_vRefStarDistances.size());
	}

	const STARDISTVECTOR targetStarDistances = ComputeStarDistances(m_vTgtStars);
	std::vector<int> targetStarIndices = createIota(targetStarDistances.size());

	std::sort(m_vRefStarIndices.begin(), m_vRefStarIndices.end(), [this](const int a, const int b) { return m_vRefStarDistances[a].m_fDistance > m_vRefStarDistances[b].m_fDistance; });
	std::sort(targetStarIndices.begin(), targetStarIndices.end(), [&targetStarDistances](const int a, const int b) { return targetStarDistances[a].m_fDistance > targetStarDistances[b].m_fDistance; });

	VOTINGPAIRVECTOR vVotingPairs;
	InitVotingGrid(vVotingPairs);

	const auto TargetStar = [&targetStarDistances, &targetStarIndices](const size_t ndx) { return targetStarDistances[targetStarIndices[ndx]]; };
	const auto ReferenceStar = [&dist = m_vRefStarDistances, &ind = m_vRefStarIndices](const size_t ndx) { return dist[ind[ndx]]; };

	const auto GetRefStarDistance = [this](const int star1, const int star2) -> float
	{
		const auto it = std::lower_bound(m_vRefStarDistances.begin(), m_vRefStarDistances.end(), CStarDist(star1, star2));
		return it == m_vRefStarDistances.end() ? 0.0 : it->m_fDistance;
	};

	for (size_t i = 0, j = 0; i < targetStarDistances.size() && j < m_vRefStarDistances.size();)
	{
		if (std::fabs(TargetStar(i).m_fDistance - ReferenceStar(j).m_fDistance) <= static_cast<decltype(CStarDist::m_fDistance)>(MAXSTARDISTANCEDELTA))
		{
			const double fTgtDistance12 = TargetStar(i).m_fDistance;

			const int lRefStar1 = ReferenceStar(j).m_Star1;
			const int lRefStar2 = ReferenceStar(j).m_Star2;

			const int lTgtStar1 = TargetStar(i).m_Star1;
			const int lTgtStar2 = TargetStar(i).m_Star2;

			for (int lTgtStar3 = 0; lTgtStar3 < m_vTgtStars.size(); lTgtStar3++)
			{
				if ((lTgtStar3 != lTgtStar1) && (lTgtStar3 != lTgtStar2))
				{
					auto it = std::lower_bound(targetStarDistances.begin(), targetStarDistances.end(), CStarDist(lTgtStar1, lTgtStar3));
					const double fTgtDistance13 = it == targetStarDistances.end() ? 0.0 : it->m_fDistance;

					it = std::lower_bound(targetStarDistances.begin(), targetStarDistances.end(), CStarDist(lTgtStar2, lTgtStar3));
					const double fTgtDistance23 = it == targetStarDistances.end() ? 0.0 : it->m_fDistance;

					const double fRatio = std::max(fTgtDistance13, fTgtDistance23) / fTgtDistance12;
					if (fRatio < TriangleMaxRatio)
					{
						for (int lRefStar3 = 0; lRefStar3 < m_vRefStars.size(); lRefStar3++)
						{
							if ((lRefStar3 != lRefStar1) && (lRefStar3 != lRefStar2))
							{
								const double fRefDistance13 = GetRefStarDistance(lRefStar1, lRefStar3);
								const double fRefDistance23 = GetRefStarDistance(lRefStar2, lRefStar3);

								if (std::abs(fRefDistance13 - fTgtDistance13) < MAXSTARDISTANCEDELTA && std::fabs(fRefDistance23 - fTgtDistance23) < MAXSTARDISTANCEDELTA)
								{
									AddVote(lRefStar1, lTgtStar1, vVotingPairs, m_vTgtStars.size());
									AddVote(lRefStar2, lTgtStar2, vVotingPairs, m_vTgtStars.size());
									AddVote(lRefStar3, lTgtStar3, vVotingPairs, m_vTgtStars.size());
								}
								else if (std::abs(fRefDistance23 - fTgtDistance13) < MAXSTARDISTANCEDELTA && std::fabs(fRefDistance13 - fTgtDistance23) < MAXSTARDISTANCEDELTA)
								{
									AddVote(lRefStar1, lTgtStar2, vVotingPairs, m_vTgtStars.size());
									AddVote(lRefStar2, lTgtStar1, vVotingPairs, m_vTgtStars.size());
									AddVote(lRefStar3, lTgtStar3, vVotingPairs, m_vTgtStars.size());
								}
							}
						}
					}
				}
			}
		}

		if (TargetStar(i).m_fDistance < ReferenceStar(j).m_fDistance)
			++j;
		else
			++i;
	}

	std::sort(vVotingPairs.begin(), vVotingPairs.end(), std::greater<VotingPair>{});

	bool bResult = false;
	if (vVotingPairs.size() >= m_vTgtStars.size())
	{
		int lMinNrVotes = vVotingPairs[m_vTgtStars.size() * 2 - 1].m_lNrVotes;
		if (lMinNrVotes == 0)
			lMinNrVotes = 1;

		size_t lCut = 0;
		while (vVotingPairs[lCut].m_lNrVotes >= lMinNrVotes)
			lCut++;
		vVotingPairs.resize(lCut + 1);

		const TRANSFORMATIONTYPE TType = GetTransformationType(vVotingPairs.size());

		bResult = ComputeSigmaClippingTransformation(vVotingPairs, BilinearParameters, TType);

		if (bResult && (TType == TT_LINEAR))
		{
			BilinearParameters.a3 = 0;
			BilinearParameters.b3 = 0;
		}
	}

	return bResult;
}


void CMatchingStars::AdjustSize()
{
	bool				bAllInTopLeft = true;

	for (int i = 0;(i<m_vTgtStars.size()) && bAllInTopLeft;i++)
	{
		if ((m_vTgtStars[i].x() > m_lWidth/2) || (m_vTgtStars[i].y() > m_lHeight/2))
			bAllInTopLeft = false;
	}

	if (bAllInTopLeft)
	{
		m_lWidth *= 2;
		m_lHeight *= 2;
	}
	else
	{
		bool			bOutside = false;
		for (int i = 0;i<m_vTgtStars.size() && !bOutside;i++)
		{
			if ((m_vTgtStars[i].x() > m_lWidth) || (m_vTgtStars[i].y() > m_lHeight))
				bOutside = true;
		}
		if (bOutside)
		{
			m_lWidth  /= 2;
			m_lHeight /= 2;
		}
	}
}


bool CMatchingStars::ComputeCoordinateTransformation(CBilinearParameters& BilinearParameters)
{
	if (GetTransformationType() == TT_NONE)
		return true;

	//AdjustSize();
	if (m_vRefStars.size() >= 8 && m_vTgtStars.size() >= 8)
	{
		bool bResult = ComputeLargeTriangleTransformation(BilinearParameters);
		if (!bResult)
			bResult = ComputeMatchingTriangleTransformation(BilinearParameters);
		return bResult;
	}
	return false;
}

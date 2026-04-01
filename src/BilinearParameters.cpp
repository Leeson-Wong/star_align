#include "dss_compat.h"
#include "BilinearParameters.h"

void CBilinearParameters::Clear() noexcept
{
	Type = TT_BILINEAR;
	a0 = a1 = a2 = a3 = a4 = a5 = a6 = a7 = a8 = 0.0;
	a9 = a10 = a11 = a12 = a13 = a14 = a15 = 0.0;
	b0 = b1 = b2 = b3 = b4 = b5 = b6 = b7 = b8 = 0.0;
	b9 = b10 = b11 = b12 = b13 = b14 = b15 = 0.0;
	a1 = 1.0;	// to have x' = x
	b2 = 1.0;	// to have y' = y

	fXWidth = fYWidth = 1.0;
}

QPointF CBilinearParameters::transform(const QPointF& pt) const noexcept
{
	QPointF ptResult;
	double& x = ptResult.rx();
	double& y = ptResult.ry();


	if (Type == TT_BICUBIC)
	{
		double			X = pt.x() / fXWidth;
		double			X2 = X * X;
		double			X3 = X * X * X;
		double			Y = pt.y() / fYWidth;
		double			Y2 = Y * Y;
		double			Y3 = Y * Y * Y;

		x = a0 + a1 * X + a2 * Y + a3 * X * Y
			+ a4 * X2 + a5 * Y2 + a6 * X2 * Y + a7 * X * Y2 + a8 * X2 * Y2
			+ a9 * X3 + a10 * Y3 + a11 * X3 * Y + a12 * X * Y3 + a13 * X3 * Y2 + a14 * X2 * Y3 + a15 * X3 * Y3;
		y = b0 + b1 * X + b2 * Y + b3 * X * Y
			+ b4 * X2 + b5 * Y2 + b6 * X2 * Y + b7 * X * Y2 + b8 * X2 * Y2
			+ b9 * X3 + b10 * Y3 + b11 * X3 * Y + b12 * X * Y3 + b13 * X3 * Y2 + b14 * X2 * Y3 + b15 * X3 * Y3;
	}
	else if (Type == TT_BISQUARED)
	{
		double			X = pt.x() / fXWidth;
		double			X2 = X * X;
		double			Y = pt.y() / fYWidth;
		double			Y2 = Y * Y;

		x = a0 + a1 * X + a2 * Y + a3 * X * Y
			+ a4 * X2 + a5 * Y2 + a6 * X2 * Y + a7 * X * Y2 + a8 * X2 * Y2;
		y = b0 + b1 * X + b2 * Y + b3 * X * Y
			+ b4 * X2 + b5 * Y2 + b6 * X2 * Y + b7 * X * Y2 + b8 * X2 * Y2;
	}
	else
	{
		double			X = pt.x() / fXWidth;
		double			Y = pt.y() / fYWidth;

		x = a0 + a1 * X + a2 * Y + a3 * X * Y;
		y = b0 + b1 * X + b2 * Y + b3 * X * Y;
	};

	x *= fXWidth;
	y *= fYWidth;

	return ptResult;
}

double CBilinearParameters::Angle(int lWidth) const noexcept
{
	double		fAngle;
	QPointF	pt1(0, 0),
		pt2(lWidth, 0);

	pt1 = transform(pt1);
	pt2 = transform(pt2);

	fAngle = atan2(pt2.y() - pt1.y(), pt2.x() - pt1.x());

	return fAngle;
}

void CBilinearParameters::Offsets(double& dX, double& dY) const noexcept
{
	dX = a0 * fXWidth;
	dY = b0 * fYWidth;
}

void CBilinearParameters::Footprint(QPointF& pt1, QPointF& pt2, QPointF& pt3, QPointF& pt4) const noexcept
{
	pt1.rx() = pt1.ry() = 0;
	pt2.rx() = fXWidth;	pt2.ry() = 0;
	pt3.rx() = fXWidth;	pt3.ry() = fYWidth;
	pt4.rx() = 0;		pt4.ry() = fYWidth;

	pt1 = transform(pt1);
	pt2 = transform(pt2);
	pt3 = transform(pt3);
	pt4 = transform(pt4);
}

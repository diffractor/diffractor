// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

quadd quadd::crop(const rectd limit, const int active_point) const noexcept
{
	const auto t = limit.top();
	const auto b = limit.bottom();
	const auto l = limit.left();
	const auto r = limit.right();
	const auto center = center_point();
	const auto anchor_point = (active_point + 2) % 4;
	const auto angle = this->angle();

	quadd result;
	const auto abs_angle = fabs(fmod(angle, 90.0));
	const auto angle_epsilon = 0.001;
	const auto is_right_angle = abs_angle < angle_epsilon || abs_angle >(90.0 - angle_epsilon);

	if (is_right_angle)
	{
		for (auto i = 0; i < 4; ++i)
		{
			result.pts[i].X = std::clamp(pts[i].X, l, r);
			result.pts[i].Y = std::clamp(pts[i].Y, t, b);
		}
	}
	else
	{
		const auto has_anchor = active_point != -1;
		const auto anchor = has_anchor ? pts[anchor_point] : center;

		for (auto i = 0; i < 4; ++i)
		{
			if (!has_anchor || i != anchor_point)
			{
				auto x = pts[i].X;
				auto y = pts[i].Y;

				// x
				const auto dx = std::clamp(x, l, r) - x;

				auto cx = anchor.X - x;
				auto cy = anchor.Y - y;

				x = x + dx;
				y = y + (dx * cy / cx);

				// y
				const auto dy = std::clamp(y, t, b) - y;

				cx = anchor.X - x;
				cy = anchor.Y - y;

				result.pts[i].X = x + (dy * cx / cy);
				result.pts[i].Y = y + dy;
			}
			else
			{
				result.pts[i] = pts[i];
			}
		}
	}

	result = result.rotate(-angle, center);
	result = result.inside_bounds(1.0);
	result = result.rotate(angle, center);
	result = result.limit(limit);

	return result;
}

sized quadd::actual_extent() const noexcept
{
	const auto angle = -this->angle();
	const auto theta = to_radian(angle);
	const auto s = sin(theta);
	const auto c = cos(theta);

	const auto center = pts[0];
	const auto px = pts[1];
	const auto py = pts[3];

	const auto x = ((px.X - center.X) * c) - ((px.Y - center.Y) * s);
	const auto y = ((py.X - center.X) * s) + ((py.Y - center.Y) * c);

	return { fabs(x), fabs(y) };
}

quadd quadd::limit(const rectd limit) const noexcept
{
	auto cx = 0.0;
	auto cy = 0.0;

	const auto l = left();
	const auto t = top();
	const auto r = right();
	const auto b = bottom();

	const auto ll = limit.left();
	const auto lt = limit.top();
	const auto lr = limit.right();
	const auto lb = limit.bottom();

	if (l < ll && r < lr) cx = std::min(ll - l, lr - r);
	if (t < lt && b < lb) cy = std::min(lt - t, lb - b);
	if (r > lr && l > ll) cx = std::max(lr - r, ll - l);
	if (b > lb && t > lt) cy = std::max(lb - b, lt - t);

	return offset(cx, cy);
}

quadd quadd::transform(const simple_transform t) const noexcept
{
	quadd result;

	switch (t)
	{
	default:
	case simple_transform::none:
		result = *this;
		break;

	case simple_transform::flip_h:
		result.pts[0] = pts[1];
		result.pts[1] = pts[0];
		result.pts[2] = pts[3];
		result.pts[3] = pts[2];
		break;

	case simple_transform::rot_180:
		result.pts[0] = pts[2];
		result.pts[1] = pts[3];
		result.pts[2] = pts[0];
		result.pts[3] = pts[1];
		break;

	case simple_transform::flip_v:
		result.pts[0] = pts[3];
		result.pts[1] = pts[2];
		result.pts[2] = pts[1];
		result.pts[3] = pts[0];
		break;

	case simple_transform::transpose:
		result.pts[0] = pts[0];
		result.pts[1] = pts[3];
		result.pts[2] = pts[2];
		result.pts[3] = pts[1];
		break;

	case simple_transform::rot_90:
		result.pts[1] = pts[0];
		result.pts[2] = pts[1];
		result.pts[3] = pts[2];
		result.pts[0] = pts[3];
		break;

	case simple_transform::transverse:
		result.pts[0] = pts[2];
		result.pts[1] = pts[1];
		result.pts[2] = pts[0];
		result.pts[3] = pts[3];
		break;

	case simple_transform::rot_270:
		result.pts[3] = pts[0];
		result.pts[0] = pts[1];
		result.pts[1] = pts[2];
		result.pts[2] = pts[3];
		break;
	}

	return result;
}


quadd quadd::rotate(const double angle, const pointd center) const noexcept
{
	quadd result;

	const auto theta = to_radian(angle);
	const auto s = sin(theta);
	const auto c = cos(theta);

	for (int i = 0; i < 4; ++i)
	{
		const auto point = pts[i];
		const auto x = ((point.X - center.X) * c) - ((point.Y - center.Y) * s);
		const auto y = ((point.X - center.X) * s) + ((point.Y - center.Y) * c);
		result.pts[i] = pointd(center.X + x, center.Y + y);
	}

	return result;
}

rectd quadd::inside_bounds(const double min_size) const noexcept
{
	double xx[] = { pts[0].X, pts[1].X, pts[2].X, pts[3].X };
	double yy[] = { pts[0].Y, pts[1].Y, pts[2].Y, pts[3].Y };

	std::sort(xx, xx + 4);
	std::sort(yy, yy + 4);

	const auto cx = (xx[1] + xx[2]) / 2.0;
	const auto cy = (yy[1] + yy[2]) / 2.0;
	const auto cxy = min_size / 2.0;

	return {
		std::min(xx[1], cx - cxy),
		std::min(yy[1], cy - cxy),
		std::max(xx[2] - xx[1], min_size),
		std::max(yy[2] - yy[1], min_size)
	};
}

affined quadd::calc_transform(const quadd& other) const noexcept
{
	const auto s = other.pts[0].dist(other.pts[1]) / pts[0].dist(pts[1]);
	return affined().translate(-pts[0]).rotate(other.angle() - angle()).scale(s).translate(other.pts[0]);
}

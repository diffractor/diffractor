// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"


class spline_interpolator : public df::no_copy
{
	using point_t = std::pair<double, double>;

	std::vector<point_t> points;
	std::vector<double> y2;

public:
	spline_interpolator() = default;

	int size() const
	{
		return static_cast<int>(points.size());
	}

	void add(const double x, const double y)
	{
		points.emplace_back(x, y);
	}

	void clear()
	{
		points.clear();
	}

	// Interpolate() and PreCompute() are adapted from:
	// NUMERICAL RECIPES IN C: THE ART OF SCIENTIFIC COMPUTING
	// ISBN 0-521-43108-5, page 113, section 3.3.

	double interpolate(const double x)
	{
		if (y2.empty())
		{
			pre_compute();
		}

		// We will find the right place in the table by means of
		// bisection. This is optimal if sequential calls to this
		// routine are at random values of x. If sequential calls
		const int n = static_cast<int>(points.size());
		auto klo = 0;
		auto khi = n - 1;

		while (khi - klo > 1)
		{
			const auto k = (khi + klo) >> 1; // are in order, and closely spaced, one would do better

			if (points[k].first > x)
			{
				khi = k; // to store previous values of klo and khi and test if
			}
			else
			{
				klo = k;
			}
		}

		const auto kvhi = points[khi];
		const auto kvlo = points[klo];

		const auto h = kvhi.first - kvlo.first;
		const auto a = (kvhi.first - x) / h;
		const auto b = (x - kvlo.first) / h;

		// Cubic spline polynomial is now evaluated.
		return a * kvlo.second + b * kvhi.second + ((a * a * a - a) * y2[klo] + (b * b * b - b) * y2[khi]) * (h * h) /
			6.0;
		//return a*ya[klo] + b*ya[khi] + ((a*a*a - a)*y2[klo] + (b*b*b - b)*y2[khi])*(h*h)/6.0;
	}

private:
	void pre_compute()
	{
		std::ranges::sort(points, [](auto&& l, auto&& r) { return l.first < r.first; });

		const int n = static_cast<int>(points.size());
		std::vector<double> u;
		u.resize(n);
		y2.resize(n);

		u[0] = 0;
		y2[0] = 0;

		for (auto i = 1; i < n - 1; ++i)
		{
			const auto kv = points[i];
			const auto kv1 = points[i + 1];
			const auto kv2 = points[i - 1];

			// This is the decomposition loop of the tridiagonal algorithm. 
			// y2 and u are used for temporary storage of the decomposed factors.
			const auto wx = kv1.first - kv2.first;
			const auto sig = (kv.first - kv2.first) / wx;
			const auto p = sig * y2[i - 1] + 2.0;

			y2[i] = (sig - 1.0) / p;

			const auto ddydx = (kv1.second - kv.second) / (kv1.first - kv.first) - (kv.second - kv2.second) / (kv.first
				- kv2.
				first);

			u[i] = (6.0 * ddydx / wx - sig * u[i - 1]) / p;
		}

		y2[n - 1] = 0;

		// This is the backsubstitution loop of the tridiagonal algorithm
		for (auto i = n - 2; i >= 0; --i)
		{
			y2[i] = y2[i] * y2[i + 1] + u[i];
		}
	}
};


void ui::color_adjust::color_params(const double vibrance, const double saturation,
                                    const double darks, const double midtones, const double lights,
                                    const double contrast, const double brightness)
{
	_saturation = static_cast<float>(saturation + 1.0);
	_vibrance = static_cast<float>(vibrance);

	const auto cc = (contrast / (contrast < 0.0 ? 2 : 1.5)) + 1.0;
	const auto bb = brightness / 3.0;

	const double t = 0.0;
	const double d = (((0.2 + darks * 0.1) - 0.5) * cc) + 0.5 + bb;
	const double m = (((0.5 + midtones * 0.1) - 0.5) * cc) + 0.5 + bb;
	const double l = (((0.8 + lights * 0.1) - 0.5) * cc) + 0.5 + bb;
	const double b = 1.0;

	spline_interpolator interpolator;
	interpolator.add(0.0, std::clamp(t, 0.0, 1.0));
	interpolator.add(0.2, std::clamp(d, 0.0, 1.0));
	interpolator.add(0.5, std::clamp(m, 0.0, 1.0));
	interpolator.add(0.8, std::clamp(l, 0.0, 1.0));
	interpolator.add(1.0, std::clamp(b, 0.0, 1.0));

	for (auto i = 0; i < curve_len; ++i)
	{
		_curve[i] = static_cast<float>(interpolator.interpolate(i / static_cast<double>(curve_len)));
	}
}

ui::color32 ui::color_adjust::adjust_color(double y, double u, double v, double a) const
{
	y = _curve[std::clamp(static_cast<int>(y * curve_len), 0, curve_len - 1)];

	if (!df::is_zero(_vibrance))
	{
		const auto xx = -0.105 - u;
		const auto yy = 0.227 - v;
		const auto d = sqrt(xx * xx + yy * yy);
		const auto sat = _saturation * (1.0 + (_vibrance * d * 4.0));

		u *= sat;
		v *= sat;
	}
	else
	{
		u *= _saturation;
		v *= _saturation;
	}

	u = std::clamp(u, -1.0, 1.0);
	v = std::clamp(v, -1.0, 1.0);

	const auto r = y + 1.140 * v;
	const auto g = y - 0.396 * u - 0.581 * v;
	const auto b = y + 2.029 * u;

	return saturate_rgba(b, g, r, a);
}

void ui::color_adjust::apply(const const_surface_ptr& src, uint8_t* dst, const size_t dst_stride,
                             df::cancel_token token) const
{
	const auto dims = src->dimensions();


	// TODO sse optimization of this
	/*while(d < e)
	{
	__m128i src128 = _mm_loadu_si128(s);
	// _mm_mul_ps
	_mm_cvtps_epi32 
	}*/

	for (auto yy = 0; yy < dims.cy; ++yy)
	{
		const auto* s = std::bit_cast<const color32*>(src->pixels() + yy * src->stride());
		auto* d = std::bit_cast<color32*>(dst + yy * dst_stride);

		for (auto xx = 0; xx < dims.cx; ++xx)
		{
			const auto c = *s++;

			const auto r = get_b(c) / 255.0;
			const auto g = get_g(c) / 255.0;
			const auto b = get_r(c) / 255.0;
			const auto a = get_a(c) / 255.0;

			const auto y = 0.299 * r + 0.587 * g + 0.114 * b;
			const auto u = -0.147 * r - 0.289 * g + 0.436 * b;
			const auto v = 0.615 * r - 0.515 * g - 0.100 * b;
			
			*d++ = adjust_color(y, u, v, a);
		}

		if (token.is_cancelled())
		{
			break;
		}
	}
}

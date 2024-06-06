// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

using real = double;

class sizei;
class pointi;
class sized;
class pointd;
class recti;
class rectd;

constexpr double geom_pi = 3.14159265358979323846;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr bool is_equal(const double l, const double r)
{
	return df::equiv(l, r);
}

constexpr double to_radian(const double theta) noexcept
{
	return theta * (geom_pi / 180.0);
}

constexpr double to_degrees(const double theta) noexcept
{
	return theta / (geom_pi / 180.0);
}

class sizei
{
public:
	int cx = 0;
	int cy = 0;

	constexpr sizei() noexcept = default;
	constexpr sizei(const sizei& other) noexcept = default;

	constexpr sizei(int x, int y) noexcept : cx(x), cy(y)
	{
	}

	explicit constexpr sizei(pointi other) noexcept;

	constexpr bool operator ==(const sizei size) const noexcept
	{
		return (cx == size.cx && cy == size.cy);
	}

	constexpr bool operator !=(const sizei size) const noexcept
	{
		return (cx != size.cx || cy != size.cy);
	}

	constexpr bool is_empty() const noexcept
	{
		return cx == 0 || cy == 0;
	}

	constexpr sizei operator +(const sizei size) const noexcept
	{
		return {cx + size.cx, cy + size.cy};
	}

	constexpr sizei operator -(const sizei size) const noexcept
	{
		return {cx - size.cx, cy - size.cy};
	}

	constexpr sizei operator -() const noexcept
	{
		return {-cx, -cy};
	}

	constexpr sizei operator /(const int i) const noexcept
	{
		return {cx / i, cy / i};
	}

	constexpr sizei operator *(const int i) const noexcept
	{
		return {cx * i, cy * i};
	}

	constexpr sizei operator *(const double d) const noexcept
	{
		return {df::round(cx * d), df::round(cy * d)};
	}

	constexpr sizei inflate(const int n) const noexcept
	{
		return {cx + n, cy + n};
	}

	constexpr sizei flip() const noexcept
	{
		return {cy, cx};
	}

	constexpr int area() const noexcept
	{
		return cx * cy;
	}

	constexpr pointi operator +(pointi point) const noexcept;
	constexpr pointi operator -(pointi point) const noexcept;
};

class pointi
{
public:
	int x = 0;
	int y = 0;

	constexpr pointi() noexcept = default;
	constexpr pointi(const pointi& other) noexcept = default;

	constexpr pointi(const int xx, const int yy) noexcept : x(xx), y(yy)
	{
	}

	constexpr pointi(const sizei other) noexcept : x(other.cx), y(other.cy)
	{
	}

	constexpr bool operator ==(const pointi other) const noexcept
	{
		return x == other.x && y == other.y;
	}

	constexpr bool operator !=(const pointi other) const noexcept
	{
		return x != other.x || y != other.y;
	}

	constexpr pointi operator +(const sizei other) const noexcept
	{
		return {x + other.cx, y + other.cy};
	}

	constexpr pointi operator -(const sizei other) const noexcept
	{
		return {x - other.cx, y - other.cy};
	}

	constexpr pointi operator -() const noexcept
	{
		return {-x, -y};
	}

	constexpr pointi operator +(const pointi other) const noexcept
	{
		return {x + other.x, y + other.y};
	}

	constexpr pointi operator -(const pointi other) const noexcept
	{
		return {x - other.x, y - other.y};
	}

	constexpr pointi clamp(recti limit) const noexcept;

	int dist_sqrd(const pointi other) const
	{
		const auto dx = x - other.x;
		const auto dy = y - other.y;
		return df::isqrt((dx * dx) + (dy * dy));
	}
};

class recti
{
public:
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	constexpr recti() noexcept = default;

	constexpr recti(const int l, const int t, const int r, const int b) noexcept : left(l), top(t), right(r), bottom(b)
	{
	}

	constexpr recti(const int r, const int b) noexcept : right(r), bottom(b)
	{
	}

	constexpr recti(const sizei size) noexcept : right(size.cx), bottom(size.cy)
	{
	}

	constexpr recti(const pointi point, const sizei size) noexcept : left(point.x), top(point.y),
	                                                                 right(point.x + size.cx), bottom(point.y + size.cy)
	{
	}

	constexpr recti(const pointi tl, const pointi br) noexcept : left(tl.x), top(tl.y), right(br.x), bottom(br.y)
	{
	}

	constexpr int width() const noexcept
	{
		return right - left;
	}

	constexpr int height() const noexcept
	{
		return bottom - top;
	}

	constexpr int area() const noexcept
	{
		return (right - left) * (bottom - top);
	}

	constexpr sizei extent() const noexcept
	{
		return {right - left, bottom - top};
	}

	constexpr pointi top_left() const noexcept
	{
		return {left, top};
	}

	constexpr pointi top_center() const noexcept
	{
		return {(left + right) / 2, top};
	}

	constexpr pointi bottom_center() const noexcept
	{
		return {(left + right) / 2, bottom};
	}

	constexpr pointi bottom_right() const noexcept
	{
		return {right, bottom};
	}

	constexpr pointi center() const noexcept
	{
		return {(left + right) / 2, (top + bottom) / 2};
	}

	constexpr recti operator+(const sizei offset) const noexcept
	{
		return {left + offset.cx, top + offset.cy, right + offset.cx, bottom + offset.cy};
	}

	constexpr bool is_empty() const noexcept
	{
		return left >= right || top >= bottom;
	}

	constexpr bool is_null() const noexcept
	{
		return left == 0 && right == 0 && top == 0 && bottom == 0;
	}

	constexpr bool contains(const pointi point) const noexcept
	{
		return left <= point.x && right >= point.x && top <= point.y && bottom >= point.y;
	}

	constexpr void clear() noexcept
	{
		left = right = top = bottom = 0;
	}

	constexpr void set(int x1, int y1, int x2, int y2) noexcept
	{
		left = x1;
		top = y1;
		right = x2;
		bottom = y2;
	}

	constexpr void set(const pointi topLeft, const pointi bottomRight) noexcept
	{
		set(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
	}

	constexpr recti inflate(const int xy) const noexcept
	{
		return {left - xy, top - xy, right + xy, bottom + xy};
	}

	constexpr recti inflate(const int x, const int y) const noexcept
	{
		return {left - x, top - y, right + x, bottom + y};
	}

	constexpr recti inflate(const int ll, const int tt, const int rr, const int bb) const noexcept
	{
		return {left - ll, top - tt, right + rr, bottom + bb};
	}

	constexpr recti inflate(const sizei s) const noexcept
	{
		return {left - s.cx, top - s.cy, right + s.cx, bottom + s.cy};
	}

	constexpr recti extend(const int x, const int y) const noexcept
	{
		return {left, top, right + x, bottom + y};
	}

	constexpr bool intersects(const recti other) const noexcept
	{
		return left < other.right &&
			top < other.bottom &&
			right > other.left &&
			bottom > other.top;
	}

	constexpr recti intersection(const recti other) const noexcept
	{
		if (!intersects(other)) return {};
		return {
			std::max(left, other.left), std::max(top, other.top), std::min(right, other.right),
			std::min(bottom, other.bottom)
		};
	}

	constexpr recti make_union(const recti other) const noexcept
	{
		if (is_empty()) return other;
		if (other.is_empty()) return *this;
		return {
			std::min(left, other.left), std::min(top, other.top), std::max(right, other.right),
			std::max(bottom, other.bottom)
		};
	}

	constexpr recti clamp(const recti limit) const noexcept
	{
		sizei offset(0, 0);

		if (top < limit.top)
			offset.cy = limit.top - top;

		if (left < limit.left)
			offset.cx = limit.left - left;

		if (bottom > limit.bottom)
			offset.cy = limit.bottom - bottom;

		if (right > limit.right)
			offset.cx = limit.right - right;

		return this->offset(offset);
	}

	constexpr recti crop(const recti limit) const noexcept
	{
		recti result(*this);

		if (top < limit.top) result.top = limit.top;
		if (left < limit.left) result.left = limit.left;
		if (bottom > limit.bottom) result.bottom = limit.bottom;
		if (right > limit.right) result.right = limit.right;

		return result;
	}

	constexpr recti offset(const pointi pt) const noexcept
	{
		return {left + pt.x, top + pt.y, right + pt.x, bottom + pt.y};
	}

	constexpr recti offset(const int x, const int y) const noexcept
	{
		return {left + x, top + y, right + x, bottom + y};
	}

	constexpr recti& operator =(const recti& other) noexcept = default;

	constexpr bool operator ==(const recti other) const noexcept
	{
		return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
	}

	constexpr bool operator !=(const recti other) const noexcept
	{
		return left != other.left || top != other.top || right != other.right || bottom != other.bottom;
	}

	constexpr recti normalise() const
	{
		return {std::min(left, right), std::min(top, bottom), std::max(left, right), std::max(top, bottom)};
	}

	constexpr void exclude(const pointi loc, const recti bounds)
	{
		if (intersects(bounds))
		{
			// calculate the white space rect
			// min bounding rect before over an element with a tooltip
			if (bounds.right < loc.x)
			{
				left = std::max(left, bounds.right);
			}

			if (bounds.left > loc.x)
			{
				right = std::min(right, bounds.left);
			}

			if (bounds.bottom < loc.y)
			{
				top = std::max(top, bounds.bottom);
			}

			if (bounds.top > loc.y)
			{
				bottom = std::min(bottom, bounds.top);
			}
		}
	}
};

constexpr sizei::sizei(const pointi other) noexcept : cx(other.x), cy(other.y)
{
}

constexpr pointi sizei::operator +(const pointi point) const noexcept
{
	return {cx + point.x, cy + point.y};
}

constexpr pointi sizei::operator -(const pointi point) const noexcept
{
	return {cx - point.x, cy - point.y};
}

constexpr pointi pointi::clamp(const recti limit) const noexcept
{
	return {std::clamp(x, limit.left, limit.right), std::clamp(y, limit.top, limit.bottom)};
}

constexpr recti center_rect(const sizei s, const int xx, const int yy) noexcept
{
	const auto x = xx - (s.cx / 2);
	const auto y = yy - (s.cy / 2);
	return {x, y, x + s.cx, y + s.cy};
}

constexpr recti center_rect(const sizei s, const recti limit) noexcept
{
	const auto center = limit.center();
	return center_rect(s, center.x, center.y);
}

constexpr recti center_rect(const sizei s, const sizei limit) noexcept
{
	return center_rect(s, limit.cx / 2, limit.cy / 2);
}

constexpr recti center_rect(const sizei s, const pointi limit) noexcept
{
	return center_rect(s, limit.x, limit.y);
}

constexpr recti center_rect(const recti r, const recti limit) noexcept
{
	return center_rect(r.extent(), limit);
}

constexpr recti round_rect(const double x, const double y, const double cx, const double cy) noexcept
{
	return {df::round(x), df::round(y), df::round(x + cx), df::round(y + cy)};
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class sized
{
public:
	constexpr sized() noexcept = default;
	constexpr sized(const sized& other) noexcept = default;
	constexpr sized& operator=(const sized& other) noexcept = default;

	constexpr sized(const double w, const double h) noexcept : Width(w), Height(h)
	{
	}

	constexpr sized(const sizei other) noexcept : Width(other.cx), Height(other.cy)
	{
	}

	constexpr explicit sized(const pointi other) noexcept : Width(other.x), Height(other.y)
	{
	}

	constexpr sized(pointd other) noexcept;

	constexpr sized operator+(const sized other) const noexcept
	{
		return {
			Width + other.Width,
			Height + other.Height
		};
	}

	constexpr sized operator-(const sized other) const noexcept
	{
		return {Width - other.Width, Height - other.Height};
	}

	constexpr sized operator/(const sized other) const noexcept
	{
		return {Width / other.Width, Height / other.Height};
	}

	constexpr sized operator/(const double other) const noexcept
	{
		return {Width / other, Height / other};
	}

	constexpr sized operator*(const sized other) const noexcept
	{
		return {Width * other.Width, Height * other.Height};
	}

	constexpr sized operator*(const double other) const noexcept
	{
		return {Width * other, Height * other};
	}

	constexpr bool operator==(const sized other) const noexcept
	{
		return equals(other);
	}

	constexpr bool operator!=(const sized other) const noexcept
	{
		return !equals(other);
	}

	constexpr bool equals(const sized other) const noexcept
	{
		return is_equal(Width, other.Width) && is_equal(Height, other.Height);
	}

	constexpr bool is_empty() const noexcept
	{
		return (Width == 0.0 && Height == 0.0);
	}

	constexpr sizei round() const noexcept
	{
		return {df::round(Width), df::round(Height)};
	}

	constexpr sizei ceil() const noexcept
	{
		return {static_cast<int>(std::ceil(Width)), static_cast<int>(std::ceil(Height))};
	}

	double Width = 0.0;
	double Height = 0.0;
};

class pointd
{
public:
	constexpr pointd() noexcept = default;
	constexpr pointd(const pointd& point) noexcept = default;

	constexpr pointd(double x, double y) noexcept : X(x), Y(y)
	{
	}

	constexpr pointd(const pointi loc) noexcept : X(loc.x), Y(loc.y)
	{
	}

	constexpr pointd& operator=(const pointd& other) noexcept = default;

	constexpr bool operator==(const pointd other) const noexcept
	{
		return equals(other);
	}

	constexpr bool operator!=(const pointd other) const noexcept
	{
		return !equals(other);
	}

	double dist(const pointd other) const noexcept
	{
		const auto x = other.X - X;
		const auto y = other.Y - Y;

		return sqrt(x * x + y * y);
	}

	constexpr pointd scale(const double x, const double y) const noexcept
	{
		return {X / x, Y / y};
	}

	constexpr pointd offset(const double x, const double y) const noexcept
	{
		return {X + x, Y + y};
	}

	constexpr pointi round() const noexcept
	{
		return {df::round(X), df::round(Y)};
	}

	constexpr pointd operator+(const pointd point) const noexcept
	{
		return {X + point.X, Y + point.Y};
	}

	constexpr pointd operator+(const sized size) const noexcept
	{
		return {X + size.Width, Y + size.Height};
	}

	constexpr pointd operator-(const pointd point) const noexcept
	{
		return {X - point.X, Y - point.Y};
	}

	constexpr pointd operator/(const double d) const noexcept
	{
		return {X / d, Y / d};
	}

	constexpr pointd operator*(const double d) const noexcept
	{
		return {X * d, Y * d};
	}

	constexpr pointd mult(const double d) const noexcept
	{
		return {X * d, Y * d};
	}

	constexpr pointd mult(const double x, const double y) const noexcept
	{
		return {X * x, Y * y};
	}

	constexpr pointd clamp(const sized limit) const noexcept
	{
		const auto x = std::clamp(X, -limit.Width, limit.Width);
		const auto y = std::clamp(Y, -limit.Height, limit.Height);
		return {x, y};
	}

	constexpr double dot(const pointd other) const noexcept
	{
		return X * other.X + Y * other.Y;
	}

	pointd rotate(const double theta, const pointd center) const noexcept
	{
		const auto s = sin(theta);
		const auto c = cos(theta);

		const auto x = ((X - center.X) * c) - ((center.Y - Y) * s);
		const auto y = ((center.Y - Y) * c) - ((X - center.X) * s);

		return {center.X + x, center.Y + y};
	}

	constexpr pointd operator -() const noexcept
	{
		return {-X, -Y};
	}

	constexpr pointd operator /(const pointd other) const noexcept
	{
		return {X / other.X, Y / other.Y};
	}

	constexpr pointd& operator/=(const double d) noexcept
	{
		X /= d;
		Y /= d;
		return *this;
	}

	constexpr pointd& operator*=(const double d) noexcept
	{
		X *= d;
		Y *= d;
		return *this;
	}

	constexpr bool equals(const pointd other) const noexcept
	{
		return is_equal(X, other.X) && is_equal(Y, other.Y);
	}

	double X = 0.0;
	double Y = 0.0;
};

constexpr sized::sized(const pointd other) noexcept
{
	Width = other.X;
	Height = other.Y;
}

class rectd
{
public:
	constexpr rectd() noexcept = default;

	constexpr rectd(const recti r) noexcept : X(r.left), Y(r.top), Width(r.width()), Height(r.height())
	{
	}

	constexpr rectd(const double x, const double y, const double width,
	                const double height) noexcept : X(x), Y(y), Width(width), Height(height)
	{
	}

	constexpr rectd(const pointd location, const sized size) noexcept : X(location.X), Y(location.Y),
	                                                                    Width(size.Width), Height(size.Height)
	{
	}

	constexpr rectd(const rectd& other) noexcept = default;
	constexpr rectd& operator=(const rectd& other) noexcept = default;

	constexpr rectd round_d() const noexcept
	{
		return {
			static_cast<double>(df::round(X)),
			static_cast<double>(df::round(Y)),
			static_cast<double>(df::round(Width)),
			static_cast<double>(df::round(Height))
		};
	}

	constexpr recti round() const noexcept
	{
		return {df::round(X), df::round(Y), df::round(X + Width), df::round(Y + Height)};
	}


	constexpr pointd location() const noexcept
	{
		return {X, Y};
	}

	constexpr sized extent() const noexcept
	{
		return {Width, Height};
	}

	constexpr void bounds(rectd* rect) const noexcept
	{
		rect->X = X;
		rect->Y = Y;
		rect->Width = Width;
		rect->Height = Height;
	}

	constexpr double left() const noexcept
	{
		return X;
	}

	constexpr double top() const noexcept
	{
		return Y;
	}

	constexpr double right() const
	{
		return X + Width;
	}

	constexpr double bottom() const
	{
		return Y + Height;
	}

	constexpr pointd top_left() const noexcept
	{
		return {left(), top()};
	}

	constexpr pointd top_right() const noexcept
	{
		return {right(), top()};
	}

	constexpr pointd bottom_right() const noexcept
	{
		return {right(), bottom()};
	}

	constexpr pointd bottom_left() const noexcept
	{
		return {left(), bottom()};
	}

	constexpr bool is_empty() const
	{
		return df::equiv(Width, 0.0) || df::equiv(Height, 0.0);
	}

	constexpr bool operator==(const rectd other) const noexcept
	{
		return equals(other);
	}

	constexpr bool operator!=(const rectd other) const noexcept
	{
		return !equals(other);
	}

	constexpr bool equals(const rectd other) const noexcept
	{
		return is_equal(X, other.X) &&
			is_equal(Y, other.Y) &&
			is_equal(Width, other.Width) &&
			is_equal(Height, other.Height);
	}

	constexpr bool contains(const double x, const double y) const noexcept
	{
		return x >= X && x < (X + Width) && y >= Y && y < (Y + Height);
	}

	constexpr bool contains(const pointd pt) const noexcept
	{
		return contains(pt.X, pt.Y);
	}

	constexpr bool contains(const rectd rect) const noexcept
	{
		return (X <= rect.X) && (rect.right() <= right()) && (Y <= rect.Y) && (rect.bottom() <= bottom());
	}

	constexpr rectd clamp(const rectd limit) const noexcept
	{
		double x = 0;
		double y = 0;

		if (Y < limit.Y)
			y = limit.Y - Y;

		if (X < limit.X)
			x = limit.X - X;

		if (bottom() > limit.bottom())
			y = limit.bottom() - bottom();

		if (right() > limit.right())
			x = limit.right() - right();

		return offset(x, y);
	}

	constexpr pointd center() const noexcept
	{
		return {X + Width / 2.0, Y + Height / 2.0};
	}

	constexpr rectd scale(const double dx, const double dy) const noexcept
	{
		return {X / dx, Y / dy, Width / dx, Height / dy};
	}

	constexpr rectd scale(const sized s) const noexcept
	{
		return scale(s.Width, s.Height);
	}

	constexpr rectd inflate(const double d) const noexcept
	{
		return {X - d, Y - d, Width + 2 * d, Height + 2 * d};
	}

	constexpr rectd inflate(const double dx, const double dy) const noexcept
	{
		return {X - dx, Y - dy, Width + 2 * dx, Height + 2 * dy};
	}

	constexpr rectd inflate(const pointd point) const noexcept
	{
		return inflate(point.X, point.Y);
	}

	constexpr bool intersect(const rectd rect) noexcept
	{
		return intersect(*this, *this, rect);
	}

	constexpr void set(const double x1, const double y1, const double x2, const double y2)
	{
		X = x1;
		Y = y1;
		Width = x2 - x1;
		Height = y2 - y1;
	}

	static constexpr bool intersect(rectd& c, const rectd a, const rectd b) noexcept
	{
		const auto right = std::min(a.right(), b.right());
		const auto bottom = std::min(a.bottom(), b.bottom());
		const auto left = std::min(a.left(), b.left());
		const auto top = std::min(a.top(), b.top());

		c.X = left;
		c.Y = top;
		c.Width = right - left;
		c.Height = bottom - top;
		return !c.is_empty();
	}

	constexpr bool intersects(const rectd rect) const noexcept
	{
		return (left() < rect.right() &&
			top() < rect.bottom() &&
			right() > rect.left() &&
			bottom() > rect.top());
	}

	constexpr rectd offset(const pointd point) const noexcept
	{
		return offset(point.X, point.Y);
	}

	constexpr rectd offset(const double dx, const double dy) const noexcept
	{
		return {X + dx, Y + dy, Width, Height};
	}

public:
	double X{0};
	double Y{0};
	double Width{0};
	double Height{0};
};


class affined
{
protected:
	double _trans[6]{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};

public:
	affined() noexcept = default;
	affined(const affined& src) noexcept = default;
	affined& operator=(const affined& other) noexcept = default;
	~affined() = default;

	affined(const double d0, const double d1, const double d2, const double d3, const double d4,
	        const double d5) noexcept
	{
		_trans[0] = d0;
		_trans[1] = d1;
		_trans[2] = d2;
		_trans[3] = d3;
		_trans[4] = d4;
		_trans[5] = d5;
	}

	affined(const double aff[]) noexcept
	{
		_trans[0] = aff[0];
		_trans[1] = aff[1];
		_trans[2] = aff[2];
		_trans[3] = aff[3];
		_trans[4] = aff[4];
		_trans[5] = aff[5];
	}

	pointd transform(const pointd p) const noexcept
	{
		pointd result;
		result.X = p.X * _trans[0] + p.Y * _trans[2] + _trans[4];
		result.Y = p.X * _trans[1] + p.Y * _trans[3] + _trans[5];
		return result;
	}

	affined mult(const affined& other) const noexcept
	{
		const auto d0 = _trans[0] * other._trans[0] + _trans[1] * other._trans[2];
		const auto d1 = _trans[0] * other._trans[1] + _trans[1] * other._trans[3];
		const auto d2 = _trans[2] * other._trans[0] + _trans[3] * other._trans[2];
		const auto d3 = _trans[2] * other._trans[1] + _trans[3] * other._trans[3];
		const auto d4 = _trans[4] * other._trans[0] + _trans[5] * other._trans[2] + other._trans[4];
		const auto d5 = _trans[4] * other._trans[1] + _trans[5] * other._trans[3] + other._trans[5];

		return affined(d0, d1, d2, d3, d4, d5);
	}

	/*void Transform(float &x, float &y) const
	{
	double xx = x * _trans[0] + y * _trans[2] + _trans[4];
	double yy = x * _trans[1] + y * _trans[3] + _trans[5];

	x = (float)xx;
	y = (float)yy;
	}*/

	/*void TransformPoints(pointd pts[], int len) const
	{
	for(int i = 0; i < len; ++i)
	{
	pts[i] = Mult(pts[i]);
	}
	}*/

	bool equal(const affined& other) const noexcept
	{
		return is_equal(_trans[0], other._trans[0]) &&
			is_equal(_trans[1], other._trans[1]) &&
			is_equal(_trans[2], other._trans[2]) &&
			is_equal(_trans[3], other._trans[3]) &&
			is_equal(_trans[4], other._trans[4]) &&
			is_equal(_trans[5], other._trans[5]);
	}

	pointd operator*(const pointd p) const noexcept
	{
		return transform(p);
	}

	affined operator*(const affined& other) const noexcept
	{
		return mult(other);
	}

	bool operator==(const affined& other) const noexcept
	{
		return equal(other);
	}

	bool operator!=(const affined& other) const noexcept
	{
		return !equal(other);
	}

	affined invert() const noexcept
	{
		//const auto r_det = 1.0 / (_trans[0] * _trans[3] - _trans[1] * _trans[2]);

		affined result;
		//result._trans[0] = _trans[3] * r_det;
		//result._trans[1] = -_trans[1] * r_det;
		//result._trans[2] = -_trans[2] * r_det;
		//result._trans[3] = _trans[0] * r_det;
		//result._trans[4] = -_trans[4] * result._trans[0] - _trans[5] * result._trans[2];
		//result._trans[5] = -_trans[4] * result._trans[1] - _trans[5] * result._trans[3];

		const auto det = _trans[0] * _trans[3] - _trans[1] * _trans[2];
		result._trans[0] = _trans[3] / det;
		result._trans[1] = -_trans[1] / det;
		result._trans[2] = -_trans[2] / det;
		result._trans[3] = _trans[0] / det;
		result._trans[4] = (_trans[2] * _trans[5] - _trans[3] * _trans[4]) / det;
		result._trans[5] = -(_trans[0] * _trans[5] - _trans[1] * _trans[4]) / det;

		return result;
	}

	affined flip(const bool horiz, const bool vert) const noexcept
	{
		affined result;
		result._trans[0] = horiz ? - _trans[0] : _trans[0];
		result._trans[1] = horiz ? - _trans[1] : _trans[1];
		result._trans[2] = vert ? - _trans[2] : _trans[2];
		result._trans[3] = vert ? - _trans[3] : _trans[3];
		result._trans[4] = horiz ? - _trans[4] : _trans[4];
		result._trans[5] = vert ? - _trans[5] : _trans[5];
		return result;
	}

	bool is_rectilinear() const noexcept
	{
		return (df::equiv(_trans[1], 0.0) && df::equiv(_trans[2], 0.0)) ||
			(df::equiv(_trans[0], 0.0) && df::equiv(_trans[3], 0.0));
	}

	affined rotate(const double theta) const noexcept
	{
		const auto s = sin(to_radian(theta));
		const auto c = cos(to_radian(theta));

		return mult(affined(c, s, -s, c, 0, 0));
	}

	affined scale(const double s) const noexcept
	{
		return mult(affined(s, 0, 0, s, 0, 0));
	}

	affined translate(const double tx, const double ty) const noexcept
	{
		return mult(affined(1, 0, 0, 1, tx, ty));
	}

	affined translate(const pointd p) const noexcept
	{
		return translate(p.X, p.Y);
	}

	affined translate(const sized p) const noexcept
	{
		return translate(p.Width, p.Height);
	}

	affined shear(const double theta) const noexcept
	{
		const auto t = tan(to_radian(theta));
		return mult(affined(1, 0, t, 1, 0, 0));
	}

	affined shear(const double x, const double y) const noexcept
	{
		return mult(affined(1, 0, x, 1, 0, y));
	}
};

enum class simple_transform
{
	none = 0,
	flip_h,
	rot_180,
	flip_v,
	transpose,
	rot_90,
	transverse,
	rot_270,
};


class quadd
{
private:
	pointd pts[4];

public:
	quadd() noexcept = default;

	quadd(const double w, const double h) noexcept
	{
		pts[0] = pointd(0, 0);
		pts[1] = pointd(w, 0);
		pts[2] = pointd(w, h);
		pts[3] = pointd(0, h);
	}

	quadd(const double l, const double t, const double r, const double b) noexcept
	{
		pts[0] = pointd(l, t);
		pts[1] = pointd(r, t);
		pts[2] = pointd(r, b);
		pts[3] = pointd(l, b);
	}

	quadd(const recti r) noexcept
	{
		pts[0] = pointd(r.left, r.top);
		pts[1] = pointd(r.right, r.top);
		pts[2] = pointd(r.right, r.bottom);
		pts[3] = pointd(r.left, r.bottom);
	}

	quadd(const sizei s) noexcept
	{
		pts[0] = pointd(0, 0);
		pts[1] = pointd(s.cx, 0);
		pts[2] = pointd(s.cx, s.cy);
		pts[3] = pointd(0, s.cy);
	}

	quadd(const rectd r) noexcept
	{
		pts[0] = pointd(r.X, r.Y);
		pts[1] = pointd(r.X + r.Width, r.Y);
		pts[2] = pointd(r.X + r.Width, r.Y + r.Height);
		pts[3] = pointd(r.X, r.Y + r.Height);
	}

	quadd(const quadd& other) noexcept = default;
	quadd& operator=(const quadd& other) noexcept = default;

	quadd& operator=(const sizei s) noexcept
	{
		pts[0] = pointd(0, 0);
		pts[1] = pointd(s.cx, 0);
		pts[2] = pointd(s.cx, s.cy);
		pts[3] = pointd(0, s.cy);
		return *this;
	}

	bool operator==(const quadd& other) const noexcept
	{
		return equals(other);
	}

	bool operator!=(const quadd& other) const noexcept
	{
		return !equals(other);
	}

	bool equals(const quadd& other) const noexcept
	{
		return pts[0] == other.pts[0] && pts[1] == other.pts[1] && pts[2] == other.pts[2] && pts[3] == other.pts[3];
	}

	bool contains(const pointi point) const noexcept
	{
		return bounding_rect_i().contains(point);
	}

	rectd bounding_rect() const noexcept
	{
		const auto l = left();
		const auto t = top();

		return {l, t, right() - l, bottom() - t};
	}

	recti bounding_rect_i() const noexcept
	{
		return {df::round(left()), df::round(top()), df::round(right()), df::round(bottom())};
	}

	bool is_empty() const noexcept
	{
		return df::equiv(left(), right()) || df::equiv(top(), bottom());
	}

	quadd rotate(double angle, pointd center) const noexcept;

	quadd rotate(const double angle) const noexcept
	{
		return rotate(angle, pointd(0, 0));
	};

	rectd inside_bounds(double min_size) const noexcept;

	void clear()
	{
		pts[0].X = pts[1].X = pts[2].X = pts[3].X = 0.0;
		pts[0].Y = pts[1].Y = pts[2].Y = pts[3].Y = 0.0;
	}

	quadd transform(simple_transform t) const noexcept;

	quadd transform(const affined& aff) const noexcept
	{
		quadd result;
		result.pts[0] = aff.transform(pts[0]);
		result.pts[1] = aff.transform(pts[1]);
		result.pts[2] = aff.transform(pts[2]);
		result.pts[3] = aff.transform(pts[3]);
		return result;
	}

	quadd scale(const double x, const double y) const noexcept
	{
		quadd result;
		result.pts[0] = pts[0].scale(x, y);
		result.pts[1] = pts[1].scale(x, y);
		result.pts[2] = pts[2].scale(x, y);
		result.pts[3] = pts[3].scale(x, y);
		return result;
	}

	quadd mult(const double x, const double y) const noexcept
	{
		quadd result;
		result.pts[0] = pts[0].mult(x, y);
		result.pts[1] = pts[1].mult(x, y);
		result.pts[2] = pts[2].mult(x, y);
		result.pts[3] = pts[3].mult(x, y);
		return result;
	}

	quadd scale(const sized s) const noexcept
	{
		return scale(s.Width, s.Height);
	}

	quadd scale(const double s) const noexcept
	{
		return scale(s, s);
	}

	quadd offset(const double x, const double y) const noexcept
	{
		quadd result;
		result.pts[0] = pts[0].offset(x, y);
		result.pts[1] = pts[1].offset(x, y);
		result.pts[2] = pts[2].offset(x, y);
		result.pts[3] = pts[3].offset(x, y);
		return result;
	}

	quadd offset(const pointd pt) const noexcept
	{
		return offset(pt.X, pt.Y);
	}

	quadd crop(rectd limit, int active_point = -1) const noexcept;
	quadd limit(rectd limit) const noexcept;

	double grad(const pointd p1, const pointd p2) const noexcept
	{
		const auto dx = std::abs(p2.X - p1.X);
		const auto dy = std::abs(p2.Y - p1.Y);

		return dx / dy;
	}

	pointd center_point() const noexcept
	{
		const auto x = (pts[0].X + pts[1].X + pts[2].X + pts[3].X) / 4.0;
		const auto y = (pts[0].Y + pts[1].Y + pts[2].Y + pts[3].Y) / 4.0;
		return {x, y};
	}

	static constexpr double min(const double a, const double b, const double c, const double d) noexcept
	{
		return std::min(std::min(a, b), std::min(c, d));
	}

	static constexpr double max(const double a, const double b, const double c, const double d) noexcept
	{
		return std::max(std::max(a, b), std::max(c, d));
	}

	constexpr double left() const noexcept
	{
		return min(pts[0].X, pts[1].X, pts[2].X, pts[3].X);
	}

	constexpr double top() const noexcept
	{
		return min(pts[0].Y, pts[1].Y, pts[2].Y, pts[3].Y);
	}

	constexpr double right() const noexcept
	{
		return max(pts[0].X, pts[1].X, pts[2].X, pts[3].X);
	}

	constexpr double bottom() const noexcept
	{
		return max(pts[0].Y, pts[1].Y, pts[2].Y, pts[3].Y);
	}

	constexpr double width() const noexcept
	{
		return right() - left();
	}

	constexpr double height() const noexcept
	{
		return bottom() - top();
	}

	sized actual_extent() const noexcept;

	sized extent() const noexcept
	{
		return {width(), height()};
	}

	int origin_point() const
	{
		auto cur_dist = pts[0].dist({0, 0});
		auto result = 0;

		for (int i = 1; i < 4; ++i)
		{
			const auto d = pts[i].dist({0, 0});

			if (d < cur_dist)
			{
				cur_dist = d;
				result = i;
			}
		}

		return result;
	}

	double angle(const int n = 0) const noexcept
	{
		const auto x = pts[(n + 1) % 4].X - pts[n].X;
		const auto y = pts[(n + 1) % 4].Y - pts[n].Y;
		return to_degrees(atan2(y, x));
	}

	bool has_point(const pointd pt) const noexcept
	{
		return pts[0] == pt || pts[1] == pt || pts[2] == pt || pts[3] == pt;
	}

	const pointd operator[](const int i) const noexcept
	{
		return pts[i];
	}

	pointd& operator[](const int i) noexcept
	{
		return pts[i];
	}

	affined calc_transform(const quadd& other) const noexcept;
};

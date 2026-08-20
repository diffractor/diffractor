// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Core UI framework and rendering abstractions. Defines colors, surfaces, textures,
// drawing contexts, controls, frames, and platform-independent UI primitives.

#pragma once

class gps_coordinate;
class view_elements;
enum class icon_index;
struct view_hover_element;
enum class icon_index;
class av_frame;
class files;
class image_edits;

using av_frame_ptr = std::shared_ptr<av_frame>;
using view_elements_ptr = std::shared_ptr<view_elements>;

static constexpr int normal_font_size = 16;
static constexpr int large_font_size = 21;

enum class command_group
{
	none,
	navigation,
	selection,
	open,
	tools,
	rate_flag,
	options,
	media_playback,
	help,
	file_management,
	group_by,
	sort_by,
	edit_item
};

namespace keys
{
	extern char32_t APPS;
	extern char32_t BACK;
	extern char32_t BROWSER_BACK;
	extern char32_t BROWSER_FAVORITES;
	extern char32_t BROWSER_FORWARD;
	extern char32_t BROWSER_HOME;
	extern char32_t BROWSER_REFRESH;
	extern char32_t BROWSER_SEARCH;
	extern char32_t BROWSER_STOP;
	extern char32_t DEL;
	extern char32_t DOWN;
	extern char32_t ESCAPE;
	extern char32_t F1;
	extern char32_t F10;
	extern char32_t F11;
	extern char32_t F2;
	extern char32_t F3;
	extern char32_t F4;
	extern char32_t F5;
	extern char32_t F6;
	extern char32_t F7;
	extern char32_t F8;
	extern char32_t F9;
	extern char32_t INSERT;
	extern char32_t LEFT;
	extern char32_t MEDIA_NEXT_TRACK;
	extern char32_t MEDIA_PLAY_PAUSE;
	extern char32_t MEDIA_PREV_TRACK;
	extern char32_t MEDIA_STOP;
	extern char32_t NEXT;
	extern char32_t OEM_4;
	extern char32_t OEM_6;
	extern char32_t OEM_MINUS;
	extern char32_t OEM_PLUS;
	extern char32_t PRIOR;
	extern char32_t RETURN;
	extern char32_t RIGHT;
	extern char32_t SPACE;
	extern char32_t TAB;
	extern char32_t UP;
	extern char32_t VOLUME_DOWN;
	extern char32_t VOLUME_MUTE;
	extern char32_t VOLUME_UP;
	extern char32_t HOME;
	extern char32_t END;

	std::string_view format(int key);

	// Numeric keypad digit virtual-key codes (VK_NUMPAD0..VK_NUMPAD9 = 0x60..0x69) are
	// distinct from the top-row digit codes ('0'..'9' = 0x30..0x39). With NumLock on, the
	// keypad sends the VK_NUMPAD* codes, so keyboard accelerators bound to '0'..'9' (rating
	// and label shortcuts) never fired from the numeric keypad (issue #135). Map the keypad
	// digits onto the equivalent top-row digit; all other keys pass through unchanged.
	constexpr char32_t numpad0 = 0x60;
	constexpr char32_t numpad9 = 0x69;

	constexpr char32_t normalize_numpad(const char32_t key)
	{
		return (key >= numpad0 && key <= numpad9) ? U'0' + (key - numpad0) : key;
	}
};

struct keyboard_accelerator_t
{
	char32_t key = 0;
	uint32_t key_state = 0;

	static constexpr auto shift = 0x04;
	static constexpr auto control = 0x08;
	static constexpr auto alt = 0x10;
};

std::string format_keyboard_accelerator(const std::vector<keyboard_accelerator_t>& keyboard_accelerators);

namespace ui
{
	extern int ticks_since_last_user_action;

	class app;
	class bubble_frame;
	class button;
	class edit;
	class command;
	class control_frame;
	class toolbar;
	class trackbar;
	class date_time_control;
	class frame_host;
	class frame;
	class measure_context;
	class web_window;
	class control_base;
	class platform_app;

	using app_ptr = std::shared_ptr<app>;
	using weak_app_ptr = std::weak_ptr<app>;
	using bubble_window_ptr = std::shared_ptr<bubble_frame>;
	using button_ptr = std::shared_ptr<button>;
	using edit_ptr = std::shared_ptr<edit>;
	using command_ptr = std::shared_ptr<command>;
	using control_frame_ptr = std::shared_ptr<control_frame>;
	using control_frame_weak_ptr = std::weak_ptr<control_frame>;
	using toolbar_ptr = std::shared_ptr<toolbar>;
	using trackbar_ptr = std::shared_ptr<trackbar>;
	using date_time_control_ptr = std::shared_ptr<date_time_control>;
	using frame_host_ptr = std::shared_ptr<frame_host>;
	using frame_host_weak_ptr = std::weak_ptr<frame_host>;
	using frame_ptr = std::shared_ptr<frame>;
	using frame_weak_ptr = std::weak_ptr<frame>;
	using measure_context_ptr = std::shared_ptr<measure_context>;
	using web_window_ptr = std::shared_ptr<web_window>;
	using control_base_ptr = std::shared_ptr<control_base>;
	using plat_app_ptr = std::shared_ptr<platform_app>;

	using surface_ptr = std::shared_ptr<surface>;
	using const_surface_ptr = std::shared_ptr<const surface>;
	using image_ptr = std::shared_ptr<image>;
	using const_image_ptr = std::shared_ptr<const image>;

	static constexpr int default_ticks_per_second = 5;

	struct screen_units
	{
		int n;

		int operator*(const double scale) const noexcept
		{
			return df::round(n * scale * 10.0);
		}
	};

	struct coll_widths
	{
		int c1 = 0;
		int c2 = 0;
		int c3 = 0;
	};

	class context;
	class image;

	enum class image_format
	{
		Unknown = 0,
		JPEG,
		PNG,
		WEBP,
	};

	enum class texture_format
	{
		None,
		RGB,
		ARGB,
		NV12,
		P010
	};

	// Selects the YUV->RGB conversion applied to NV12/P010 textures. A single value
	// encodes both the colour matrix (BT.601/709/2020) and the signal range: the
	// *_limited variants are the usual video ranges (Y 16-235), rec601_full is
	// JPEG/JFIF (Y 0-255). See compute_yuv_matrix().
	enum class color_space : uint8_t
	{
		rec601_limited,
		rec601_full,
		rec709_limited,
		rec709_full,
		rec2020_limited,
		rec2020_full,
	};

	// Affine YUV->RGB transform: a 3x3 matrix plus a bias column, stored row-major.
	struct yuv_rgb_matrix
	{
		float m[12];
	};

	// Builds the transform for the requested colour space and range, so one matrix handles
	// BT.601/709/2020 and limited/full range. Both draw backends share it: the D3D11 path
	// feeds it to the yuv_params constant buffer, the software path applies it per pixel.
	// The limited-range BT.601 case reproduces the previously hard-coded coefficients.
	inline yuv_rgb_matrix compute_yuv_matrix(const color_space cs, const bool is_p010 = false) noexcept
	{
		double kr, kb;
		bool full;

		switch (cs)
		{
		case color_space::rec709_limited: kr = 0.2126;
			kb = 0.0722;
			full = false;
			break;
		case color_space::rec709_full: kr = 0.2126;
			kb = 0.0722;
			full = true;
			break;
		case color_space::rec2020_limited: kr = 0.2627;
			kb = 0.0593;
			full = false;
			break;
		case color_space::rec2020_full: kr = 0.2627;
			kb = 0.0593;
			full = true;
			break;
		case color_space::rec601_full: kr = 0.299;
			kb = 0.114;
			full = true;
			break;
		case color_space::rec601_limited:
		default: kr = 0.299;
			kb = 0.114;
			full = false;
			break;
		}

		const double kg = 1.0 - kr - kb;
		const double vr = 2.0 * (1.0 - kr);
		const double ug = -2.0 * kb * (1.0 - kb) / kg;
		const double vg = -2.0 * kr * (1.0 - kr) / kg;
		const double ub = 2.0 * (1.0 - kb);

		const double y_scale = full ? 1.0 : (255.0 / 219.0);
		const double y_off = full ? 0.0 : (16.0 / 255.0);
		const double c_scale = full ? 1.0 : (255.0 / 224.0);
		constexpr double c_off = 0.5;

		const double a = y_scale;
		const double ay = -y_scale * y_off;

		yuv_rgb_matrix r{};
		// R = a*Y + (vr*c_scale)*V + bias
		r.m[0] = static_cast<float>(a);
		r.m[1] = 0.0f;
		r.m[2] = static_cast<float>(vr * c_scale);
		r.m[3] = static_cast<float>(ay - vr * c_scale * c_off);
		// G = a*Y + (ug*c_scale)*U + (vg*c_scale)*V + bias
		r.m[4] = static_cast<float>(a);
		r.m[5] = static_cast<float>(ug * c_scale);
		r.m[6] = static_cast<float>(vg * c_scale);
		r.m[7] = static_cast<float>(ay - (ug + vg) * c_scale * c_off);
		// B = a*Y + (ub*c_scale)*U + bias
		r.m[8] = static_cast<float>(a);
		r.m[9] = static_cast<float>(ub * c_scale);
		r.m[10] = 0.0f;
		r.m[11] = static_cast<float>(ay - ub * c_scale * c_off);

		// P010 stores its 10 significant bits at the top of each 16 bit word, so a R16_UNORM /
		// R16G16_UNORM view returns code * 64 / 65535 where the coefficients above expect
		// code / 1023 - every sample arrives 0.096% low. The transform is affine, so the
		// correction is a scale of the three sample columns; the bias column stays put.
		// Callers that already normalise by 65472 must leave is_p010 false.
		if (is_p010)
		{
			constexpr auto p010_scale = static_cast<float>(65535.0 / 65472.0);

			for (auto row = 0; row < 3; ++row)
			{
				r.m[row * 4 + 0] *= p010_scale;
				r.m[row * 4 + 1] *= p010_scale;
				r.m[row * 4 + 2] *= p010_scale;
			}
		}

		return r;
	}

	enum class orientation : uint8_t
	{
		none = 0,
		top_left = 1,
		top_right,
		bottom_right,
		bottom_left,
		left_top,
		right_top,
		right_bottom,
		left_bottom
	};

	constexpr bool flips_xy(const orientation o) noexcept
	{
		return o == orientation::left_top || o == orientation::right_top || o == orientation::right_bottom || o ==
			orientation::left_bottom;
	}

	constexpr bool is_inverted(const orientation o) noexcept
	{
		return o == orientation::top_right || o == orientation::bottom_right || o == orientation::right_top || o ==
			orientation::right_bottom;
	}

	constexpr simple_transform to_simple_transform(const orientation orientation) noexcept
	{
		switch (orientation)
		{
		case orientation::top_left: return simple_transform::none;
		case orientation::top_right: return simple_transform::flip_h;
		case orientation::bottom_right: return simple_transform::rot_180;
		case orientation::bottom_left: return simple_transform::flip_v;
		case orientation::left_top: return simple_transform::transpose;
		case orientation::right_top: return simple_transform::rot_270;
		case orientation::right_bottom: return simple_transform::transverse;
		case orientation::left_bottom: return simple_transform::rot_90;
		default: break;
		}

		return simple_transform::none;
	};

	constexpr simple_transform to_simple_transform_inv(const orientation orientation) noexcept
	{
		switch (orientation)
		{
		case orientation::top_left: return simple_transform::none;
		case orientation::top_right: return simple_transform::flip_h;
		case orientation::bottom_right: return simple_transform::rot_180;
		case orientation::bottom_left: return simple_transform::flip_v;
		case orientation::left_top: return simple_transform::transpose;
		case orientation::right_top: return simple_transform::rot_90;
		case orientation::right_bottom: return simple_transform::transverse;
		case orientation::left_bottom: return simple_transform::rot_270;
		default: break;
		}

		return simple_transform::none;
	};

	sizei scale_dimensions(sizei dims, int limit, bool dont_scale_up = false) noexcept;
	sizei scale_dimensions(sizei dims, sizei limit, bool dont_scale_up = false) noexcept;
	recti scale_dimensions(sizei dims, recti limit, bool dont_scale_up = false) noexcept;

	// Area-average reduction between packed 32 bit surfaces, allocating dst. Answers false for
	// anything that is not a pure reduction of a packed format, which is the caller's cue to keep
	// using swscale.
	bool area_downscale(const const_surface_ptr& src, surface_ptr& dst, sizei dst_extent);

	// Same, pinned to the SSE2 baseline. Only a test uses this, to prove the wider path agrees on a
	// machine that would otherwise exercise just one of the two.
	bool area_downscale_baseline(const const_surface_ptr& src, surface_ptr& dst, sizei dst_extent);

	////////////////////////////////////////////////////////////////////////////////////
	// Pixels Conversions

	using color32 = uint32_t;

	constexpr color32 rgb(const uint32_t r, const uint32_t g, const uint32_t b) noexcept
	{
		return r | g << 8 | b << 16;
	};

	constexpr color32 rgba(const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a = 255) noexcept
	{
		return r | g << 8 | b << 16 | a << 24;
	};

	constexpr color32 saturate_rgba(const int r, const int g, const int b, const int a) noexcept
	{
		return df::byte_clamp(r) | df::byte_clamp(g) << 8 | df::byte_clamp(b) << 16 | df::byte_clamp(a) << 24;
	};

	constexpr color32 saturate_rgba(const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a) noexcept
	{
		return df::byte_clamp(r) | df::byte_clamp(g) << 8 | df::byte_clamp(b) << 16 | df::byte_clamp(a) << 24;
	};

	constexpr color32 saturate_rgba(const float r, const float g, const float b, const float a) noexcept
	{
		const auto rr = static_cast<int>(r * 255.f);
		const auto gg = static_cast<int>(g * 255.f);
		const auto bb = static_cast<int>(b * 255.f);
		const auto aa = static_cast<int>(a * 255.f);
		return saturate_rgba(rr, gg, bb, aa);
	};

	constexpr color32 saturate_rgba(const double r, const double g, const double b, const double a) noexcept
	{
		const auto rr = static_cast<int>(r * 255.0);
		const auto gg = static_cast<int>(g * 255.0);
		const auto bb = static_cast<int>(b * 255.0);
		const auto aa = static_cast<int>(a * 255.0);
		return saturate_rgba(rr, gg, bb, aa);
	};

	constexpr uint32_t get_a(const color32 c) noexcept
	{
		return 0xffu & c >> 24;
	}

	constexpr uint32_t get_r(const color32 c) noexcept
	{
		return 0xffu & c;
	}

	constexpr uint32_t get_g(const color32 c) noexcept
	{
		return 0xffu & c >> 8;
	}

	constexpr uint32_t get_b(const color32 c) noexcept
	{
		return 0xffu & c >> 16;
	}

	constexpr color32 darken(const color32 c, const float ff) noexcept
	{
		const auto rr = static_cast<float>(get_r(c)) / 255.0f;
		const auto gg = static_cast<float>(get_g(c)) / 255.0f;
		const auto bb = static_cast<float>(get_b(c)) / 255.0f;
		const auto aa = static_cast<float>(get_a(c)) / 255.0f;

		return saturate_rgba(rr - rr * ff, gg - gg * ff, bb - bb * ff, aa);
	}

	constexpr color32 lighten(const color32 c, const float ff) noexcept
	{
		const auto rr = static_cast<float>(get_r(c)) / 255.0f;
		const auto gg = static_cast<float>(get_g(c)) / 255.0f;
		const auto bb = static_cast<float>(get_b(c)) / 255.0f;
		const auto aa = static_cast<float>(get_a(c)) / 255.0f;

		return saturate_rgba(rr + rr * ff, gg + gg * ff, bb + bb * ff, aa);
	}


	constexpr color32 emphasize(const color32 c, const float ff = 0.22f) noexcept
	{
		const bool is_bright = get_b(c) > 0x80 || get_g(c) > 0x80 || get_r(c) > 0x80;
		return is_bright ? darken(c, ff) : lighten(c, ff);
	}

	constexpr color32 abgr(const color32 c, const uint32_t a = 0xFF) noexcept
	{
		return c >> 16 & 0xFF | c << 16 & 0xFF0000 | c & 0x0000FF00 | static_cast<uint32_t>(a) << 24;
	}

	constexpr color32 bgr(const color32 c) noexcept
	{
		return c >> 16 & 0xFF | c << 16 & 0xFF0000 | c & 0x0000FF00;
	}

	constexpr color32 average(const color32 c1, const color32 c2) noexcept
	{
		return rgba(
			(get_r(c1) + get_r(c2)) / 2,
			(get_g(c1) + get_g(c2)) / 2,
			(get_b(c1) + get_b(c2)) / 2,
			(get_a(c1) + get_a(c2)) / 2);
	}

	constexpr uint32_t ilerp(const uint32_t a, const uint32_t b, const int t)
	{
		if (t < 0) return a;
		if (t > 255) return b;
		const auto tu = static_cast<uint32_t>(t);
		const auto c16 = a * (255u - tu) + b * tu;
		return c16 >> 8;
	}

	constexpr color32 lerp(const color32 c1, const color32 c2, const int t) noexcept
	{
		return saturate_rgba(
			ilerp(get_r(c1), get_r(c2), t),
			ilerp(get_g(c1), get_g(c2), t),
			ilerp(get_b(c1), get_b(c2), t),
			ilerp(get_a(c1), get_a(c2), t));
	}

	constexpr color32 lerp(const color32 c1, const color32 c2, const float t) noexcept
	{
		return lerp(c1, c2, df::round(t * 255.0f));
	}

	constexpr color32 lerp(const color32 c1, const color32 c2, const double t) noexcept
	{
		return lerp(c1, c2, df::round(t * 255.0));
	}

	int calc_scale_down_factor(sizei sizeIn, sizei sizeOut) noexcept;

	constexpr int calc_stride(const int cx, const int bytes_per_pixel) noexcept
	{
		return cx * bytes_per_pixel + 15 & ~15;
	}

	inline double calc_mega_pixels(const double x, const double y) noexcept
	{
		return x * y / df::one_mega_pixel;
	}

	struct color_style
	{
		color32 background = 0;
		color32 foreground = 0;
		color32 selected = 0;

		float alpha = 1.0f;
		float overlay_alpha = 1.0f;
		float bg_alpha = 0.22f;
	};

	class histogram final : public df::no_copy
	{
	public:
		histogram() noexcept
		{
			clear();
		}

		static constexpr int max_value = 0x100;
		static constexpr int alloc_size = sizeof(int) * max_value;

		histogram& operator=(const histogram& other) noexcept
		{
			_max = other._max;
			memcpy_s(_r, alloc_size, other._r, alloc_size);
			memcpy_s(_g, alloc_size, other._g, alloc_size);
			memcpy_s(_b, alloc_size, other._b, alloc_size);

			return *this;
		}

		void clear() noexcept
		{
			_max = 0;
			memset(_r, 0, alloc_size);
			memset(_g, 0, alloc_size);
			memset(_b, 0, alloc_size);
		}

		int _r[max_value];
		int _g[max_value];
		int _b[max_value];
		int _max;
	};

	enum class pixel_difference_result
	{
		unknown,
		equal,
		not_equal
	};

	class surface final : public std::enable_shared_from_this<surface>
	{
		sizei _dimensions;
		size_t _stride = 0;
		size_t _size = 0;
		double _time = 0.0;
		std::unique_ptr<uint8_t, df::free_delete> _pixels;
		texture_format _format = texture_format::None;
		orientation _orientation = orientation::top_left;
		color_space _cs = color_space::rec601_limited;

	public:
		surface() noexcept = default;
		~surface() noexcept = default;

		surface(const surface&) noexcept = delete;
		surface& operator=(const surface&) noexcept = delete;
		surface(surface&&) noexcept = default;
		surface& operator=(surface&&) noexcept = default;

		void clear()
		{
			_pixels.reset();
			_dimensions.cx = 0;
			_dimensions.cy = 0;
			_size = 0;
			_cs = color_space::rec601_limited;
			_format = texture_format::None;
			_time = 0.0;
			_orientation = orientation::top_left;
		}

		void make_blank() const
		{
			memset(_pixels.get(), 0, _size);
		}

		size_t size() const
		{
			return _size;
		}

		bool empty() const
		{
			return _pixels == nullptr || _dimensions.cx == 0 || _dimensions.cy == 0;
		}

		const sizei dimensions() const noexcept
		{
			return _dimensions;
		}

		uint32_t width() const noexcept
		{
			return _dimensions.cx;
		}

		uint32_t height() const noexcept
		{
			return _dimensions.cy;
		}

		size_t stride() const noexcept
		{
			return _stride;
		}

		texture_format format() const noexcept
		{
			return _format;
		}

		double time() const noexcept
		{
			return _time;
		}

		const orientation orientation() const noexcept
		{
			return _orientation;
		}

		void orientation(const ui::orientation ori) noexcept
		{
			_orientation = ori;
		}

		color_space color_space() const noexcept
		{
			return _cs;
		}

		void color_space(const ui::color_space cs) noexcept
		{
			_cs = cs;
		}

		uint8_t* pixels() noexcept
		{
			return _pixels.get();
		}

		const uint8_t* pixels() const noexcept
		{
			return _pixels.get();
		}

		const uint8_t* pixels_line(const int y) const noexcept
		{
			df::assert_true(y < _dimensions.cy);
			return _pixels.get() + y * _stride;
		}

		uint8_t* pixels_line(const int y) noexcept
		{
			df::assert_true(y < _dimensions.cy);
			return _pixels.get() + y * _stride;
		}

		uint8_t* alloc(const sizei s, const texture_format fmt, const ui::orientation ori = orientation::top_left,
		               const double time = 0.0)
		{
			return alloc(s.cx, s.cy, fmt, ori, time);
		}

		static int calc_bytes_per_pixel(texture_format fmt)
		{
			return 4;
		}

		uint8_t* alloc(const int cx, const int cy, const texture_format fmt,
		               const ui::orientation ori = orientation::top_left, const double time = 0.0)
		{
			if (cx < 1 || cy < 1)
			{
				df::assert_true(false);
				const auto message = std::format("invalid pixel size {}x{}", cx, cy);
				df::log(__FUNCTION__, message);
				throw app_exception(message);
			}

			// NV12/P010 are planar 4:2:0: a full-resolution luma plane followed by a
			// half-height interleaved chroma plane, so the buffer is 1.5x the luma size.
			// (calc_bytes_per_pixel stays 4 for the RGBA copy/draw helpers.)
			const auto is_yuv = fmt == texture_format::NV12 || fmt == texture_format::P010;
			const auto bytes_per_luma = fmt == texture_format::P010 ? 2 : (fmt == texture_format::NV12 ? 1 : 4);

			_stride = calc_stride(cx, bytes_per_luma);
			_size = is_yuv ? (_stride * cy * 3_z / 2_z) : (_stride * cy);
			_pixels = df::unique_alloc<uint8_t>(_size + 64_z);
			_dimensions.cx = cx;
			_dimensions.cy = cy;
			_format = fmt;
			_time = time;
			_orientation = ori;
			return _pixels.get();
		}

		void copy(const surface& s, const recti r)
		{
			if (s.format() == texture_format::NV12 || s.format() == texture_format::P010 ||
				r.left < 0 || r.top < 0 || r.right > s._dimensions.cx || r.bottom > s._dimensions.cy ||
				r.width() < 1 || r.height() < 1)
			{
				throw app_exception("invalid surface crop"s);
			}

			alloc(r.extent(), s.format(), s.orientation(), s.time());

			const ptrdiff_t stride_out = _stride;
			const ptrdiff_t stride_in = s._stride;
			const auto copy_bytes = r.width() * 4_z;
			const auto* const p = s._pixels.get() + r.left * 4_z + r.top * stride_in;

			for (int y = 0; y < _dimensions.cy; ++y)
			{
				memcpy_s(_pixels.get() + y * stride_out, stride_out, p + y * stride_in, copy_bytes);
			}
		}

		void draw(const surface& s, const pointi location, const recti src) const
		{
			const ptrdiff_t stride_this = _stride;
			const ptrdiff_t stride_src = s._stride;
			const ptrdiff_t copy_bytes_len = src.width() * calc_bytes_per_pixel(_format);

			for (int y = 0; y < src.height(); ++y)
			{
				memcpy_s(
					_pixels.get() + (y + location.y) * stride_this + location.x * 4,
					stride_this,
					s._pixels.get() + (y + src.top) * stride_src + src.left * 4,
					copy_bytes_len);
			}
		}

		void swap_rb();

		void set_pixel(const int x, const int y, const color32 c) const
		{
			if (x < 0 || x >= _dimensions.cx || y < 0 || y >= _dimensions.cy) return;
			auto* const line = std::bit_cast<color32*>(_pixels.get() + y * _stride);
			line[x] = c;
		}

		color32 get_pixel(const int x, const int y) const
		{
			if (x < 0 || x >= _dimensions.cx || y < 0 || y >= _dimensions.cy) return 0;
			const auto* const line = std::bit_cast<const color32*>(_pixels.get() + y * _stride);
			return line[x];
		}

		void clear(color32 clr) const;
		surface_ptr transform(simple_transform t) const;

		void fill_logo() const;

		const_surface_ptr transform(const image_edits& photo_edits) const;
		const_surface_ptr transform(const image_edits& photo_edits, const df::cancel_token& token) const;

		pixel_difference_result pixel_difference(const const_surface_ptr& image) const;
	};


	class image final : public std::enable_shared_from_this<image>
	{
		df::blob _data;
		sizei _dimensions;
		image_format _format = image_format::Unknown;
		mutable orientation _orientation = orientation::top_left;

	public:
		image() noexcept = default;
		~image() noexcept = default;

		// Images are only ever owned through const_image_ptr; a copy would deep-copy the encoded blob.
		image(const image&) = delete;
		image& operator=(const image&) = delete;
		image(image&&) noexcept = default;
		image& operator=(image&&) noexcept = default;

		image(df::blob&& data, const sizei d, const image_format f, const orientation orientation) noexcept :
			_data(std::move(data)), _dimensions(d), _format(f), _orientation(orientation)
		{
		}

		image(const df::cspan data, const sizei d, const image_format f, const orientation orientation) noexcept :
			_data(data.begin(), data.end()), _dimensions(d), _format(f), _orientation(orientation)
		{
		}

		friend bool operator==(const image& lhs, const image& rhs)
		{
			return lhs._data == rhs._data
				&& lhs._dimensions == rhs._dimensions;
		}

		friend bool operator!=(const image& lhs, const image& rhs)
		{
			return !(lhs == rhs);
		}

		bool empty() const
		{
			return _format == image_format::Unknown || _data.empty() || _dimensions.cx == 0 || _dimensions.cy == 0;
		}

		const sizei dimensions() const noexcept
		{
			return _dimensions;
		}

		image_format format() const
		{
			return _format;
		}

		uint32_t width() const noexcept
		{
			return _dimensions.cx;
		}

		uint32_t height() const noexcept
		{
			return _dimensions.cy;
		}

		orientation orientation() const noexcept
		{
			return _orientation;
		}

		void orientation(const ui::orientation ori) const noexcept
		{
			_orientation = ori;
		}

		const df::blob& data() const noexcept
		{
			return _data;
		}

		void clear() noexcept
		{
			_data.clear();
			_dimensions.cx = 0;
			_dimensions.cy = 0;
		}

		std::string_view extension() const
		{
			switch (_format)
			{
			case image_format::JPEG: return "jpg";
			case image_format::PNG: return "png";
			case image_format::WEBP: return "webp";
			case image_format::Unknown: break;
			default: break;
			}

			df::assert_true(false); // Unknown extension
			return "x";
		}
	};

	inline bool is_empty(const const_surface_ptr& s)
	{
		return s == nullptr || s->empty();
	}

	inline bool is_valid(const const_surface_ptr& s)
	{
		return !is_empty(s);
	}

	inline bool is_empty(const const_image_ptr& i)
	{
		return i == nullptr || i->empty();
	}

	inline bool is_valid(const const_image_ptr& s)
	{
		return !is_empty(s);
	}

	inline bool is_jpeg(const const_image_ptr& i)
	{
		return i != nullptr && i->format() == image_format::JPEG;
	}


	///////////////////////////
	/////////////////////////// WIN32
	///////////////////////////

	namespace style
	{
		enum class cursor
		{
			none,
			normal,
			link,
			zoom,
			select,
			text_select,
			move,
			size_all,
			left_right,
			up_down,
			hand_up,
			hand_down
		};

		enum class icon
		{
			diffractor_16,
			diffractor_32,
			diffractor_64,
		};

		namespace color
		{
			extern color32 menu_background;
			extern color32 menu_text;
			extern color32 menu_shortcut_text;

			extern color32 toolbar_background;
			extern color32 sidebar_background;
			extern color32 bubble_background;
			extern color32 group_background;

			extern color32 view_selected_background;
			extern color32 view_background;
			extern color32 view_text;

			extern color32 dialog_background;
			extern color32 dialog_text;
			extern color32 dialog_selected_text;
			extern color32 dialog_selected_background;

			extern color32 button_background;
			extern color32 edit_background;
			extern color32 edit_text;

			extern color32 desktop_background;
			extern color32 important_background;
			extern color32 warning_background;
			extern color32 success_background;
			extern color32 info_background;

			extern color32 rank_background;
			extern color32 sidecar_background;
			extern color32 duplicate_background;
		};

		enum class font_face
		{
			code = 1,
			dialog,
			title,
			mega,
			icons,
			small_icons
		};

		enum class text_style
		{
			none,
			single_line,
			single_line_center,
			single_line_far,
			multiline,
			multiline_center
		};
	}

	class color
	{
	public:
		constexpr static float color_epsilon = 1.0f / 255.0f;

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		color() = default;

		explicit color(const color32 rgb) :
			r(get_r(rgb) / 255.0f),
			g(get_g(rgb) / 255.0f),
			b(get_b(rgb) / 255.0f),
			a(get_a(rgb) / 255.0f)
		{
		}

		color(const color32 rgb, const float a) :
			r(get_r(rgb) / 255.0f),
			g(get_g(rgb) / 255.0f),
			b(get_b(rgb) / 255.0f),
			a(a)
		{
			df::assert_true(a < 1.1f);
		}

		color(const float xx, const float yy, const float zz, const float ww) :
			r(xx),
			g(yy),
			b(zz),
			a(ww)
		{
		}

		color inverse() const
		{
			return {1.0f - r, 1.0f - g, 1.0f - b, a};
		}

		color a_min(const float aa) const
		{
			return {r, g, b, std::min(a, aa)};
		}

		color32 rgb() const
		{
			return saturate_rgba(r, g, b, 0.0f);
		}

		color32 rgba() const
		{
			return saturate_rgba(r, g, b, a);
		}

		color operator-(const color other) const
		{
			return {r - other.r, g - other.g, b - other.b, a - other.a};
		}

		float abs_sum() const
		{
			return std::abs(r) +
				std::abs(g) +
				std::abs(b) +
				std::abs(a);
		}

		color& operator+=(const color other)
		{
			r += other.r;
			g += other.g;
			b += other.b;
			a += other.a;
			return *this;
		}

		color operator*(const float x) const
		{
			return color(r * x, g * x, b * x, a * x);
		}

		friend bool operator==(const color lhs, const color rhs)
		{
			return df::equiv(lhs.r, rhs.r)
				&& df::equiv(lhs.g, rhs.g)
				&& df::equiv(lhs.b, rhs.b)
				&& df::equiv(lhs.a, rhs.a);
		}

		friend bool operator!=(const color lhs, const color rhs)
		{
			return !(lhs == rhs);
		}

		void merge(const color other)
		{
			const auto inv_other_a = (1.0f - a) * other.a;
			r = r * a + other.r * inv_other_a;
			g = g * a + other.g * inv_other_a;
			b = b * a + other.b * inv_other_a;
			a = other.a + inv_other_a;
		}

		static constexpr float sat_f(const float x)
		{
			return std::clamp(x, 0.0f, 1.0f);
		}

		color scale(const float x) const
		{
			return {sat_f(r * x), sat_f(g * x), sat_f(b * x), a};
		}

		color average(const color other) const
		{
			return {
				(r + other.r) / 2.0f,
				(g + other.g) / 2.0f,
				(b + other.b) / 2.0f,
				(a + other.a) / 2.0f
			};
		}

		static float femphasize(const float f)
		{
			return (f - 0.5f) * 0.9f + 0.5f;
		}

		color emphasize() const
		{
			return color(femphasize(r), femphasize(g), femphasize(b), a);
		}

		color emphasize(const bool can_emphasize) const
		{
			return can_emphasize ? color(femphasize(r), femphasize(g), femphasize(b), a) : *this;
		}

		static color from_a(const float a)
		{
			df::assert_true(a < 1.1f);
			return {1.0f, 1.0f, 1.0f, a};
		}

		color aa(const float a) const
		{
			df::assert_true(a < 1.1f);
			return {r, g, b, a};
		}
	};

	inline bool is_alpha_zero(const float x)
	{
		return fabs(x) <= color::color_epsilon;
	}

	enum class texture_sampler
	{
		point,
		bilinear,
		bicubic,
	};


	constexpr std::string_view to_string(const texture_format format)
	{
		switch (format)
		{
		case texture_format::ARGB: return "ARGB";
		case texture_format::P010: return "p010";
		case texture_format::NV12: return "NV12";
		case texture_format::RGB: return "RGB";
		default: break;
		}

		return "?";
	}

	constexpr std::string_view to_string(const texture_sampler sampler)
	{
		switch (sampler)
		{
		case texture_sampler::point: return "point";
		case texture_sampler::bicubic: return "bicubic";
		case texture_sampler::bilinear: return "bilinear";
		default: break;
		}

		return "?";
	}

	class color_adjust
	{
		static constexpr int curve_len = 0x1000;

		double _curve[curve_len];
		double _saturation = 0;
		double _vibrance = 0;
		double _temperature = 0;
		double _tint = 0;

	public:
		void color_params(double vibrance, double saturation, double darks, double midtones, double lights,
		                  double contrast, double brightness, double temperature = 0, double tint = 0);
		void apply(const const_surface_ptr& src, uint8_t* dst, size_t dst_stride, const df::cancel_token& token) const;
		void populate_texture_transform(struct texture_transform& transform) const;

	private:
		color32 adjust_color(double y, double u, double v, double a) const;
	};

	struct texture_transform
	{
		static constexpr int curve_len = 0x100;

		std::array<float, curve_len> curve{};
		float perspective_horizontal = 0;
		float perspective_vertical = 0;
		float saturation = 1;
		float vibrance = 0;
		float red_gain = 1;
		float green_gain = 1;
		float blue_gain = 1;
		bool has_perspective = false;
		bool has_color_changes = false;

		texture_transform()
		{
			for (auto index = 0; index < curve_len; ++index)
			{
				curve[index] = index / static_cast<float>(curve_len - 1);
			}
		}

		// Lets a backend reuse the transform it already uploaded instead of re-staging the curve.
		bool operator==(const texture_transform&) const = default;

		bool has_changes() const
		{
			return has_perspective || has_color_changes;
		}
	};

	enum class texture_update_result
	{
		tex_created,
		tex_updated,
		failed
	};

	class texture
	{
	public:
		sizei _dimensions;
		sizei _src_extent;
		texture_format _format = texture_format::None;
		orientation _orientation = orientation::top_left;
		color_space _cs = color_space::rec601_limited;

		virtual ~texture() = default;

		const sizei dimensions() const
		{
			return _dimensions;
		}

		texture_format format() const
		{
			return _format;
		}

		bool has_alpha() const
		{
			return _format == texture_format::ARGB;
		}

		sizei source_extent() const
		{
			return _src_extent.is_empty() ? _dimensions : _src_extent;
		}

		virtual texture_update_result update(const av_frame_ptr& frame) = 0;
		virtual texture_update_result update(const const_surface_ptr& surface) = 0;
		virtual texture_update_result update(sizei dims, texture_format format, orientation orientation,
		                                     const uint8_t* pixels, size_t stride, size_t buffer_size) = 0;

		virtual bool is_valid() const = 0;
	};

	class text_layout
	{
	public:
		virtual ~text_layout() = default;
		virtual void update(std::string_view text, style::text_style style) = 0;
		virtual sizei measure_text(int cx, int cy = 1000) = 0;
	};

	class vertices
	{
	public:
		virtual ~vertices() = default;
		virtual void update(recti rects[], color colors[], int num_bars) = 0;
	};

	using texture_ptr = std::shared_ptr<texture>;
	using vertices_ptr = std::shared_ptr<vertices>;
	using text_layout_ptr = std::shared_ptr<text_layout>;

	struct text_highlight_t
	{
		uint32_t offset = 0;
		uint32_t length = 0;
		color clr = {};
	};

	class measure_context : public df::no_copy
	{
	public:
		virtual sizei measure_text(std::string_view text, style::font_face font, style::text_style style, int cx,
		                           int cy = 0) = 0;
		virtual int text_line_height(style::font_face font) = 0;
		virtual text_layout_ptr create_text_layout(style::font_face font) = 0;

		double scale_factor = 1.0;

		int icon_cxy = 18;
		int padding2 = 8;
		int padding1 = 4;
		int handle_cxy = 12;
		int scroll_width = 20;
		coll_widths col_widths;
	};

	class draw_context : public measure_context
	{
	public:
		double time_now = 0.0;
		color_style colors;
		bool frame_has_focus = false;

		~draw_context() override = default;

		virtual void clear(color c) = 0;

		virtual void draw_rounded_rect(recti bounds, color c, int radius) = 0;
		virtual void draw_rect(recti bounds, color c) = 0;
		// Centre-to-corner gradient. Opt in explicitly; draw_rect and clear are flat.
		virtual void draw_rect_gradient(recti bounds, color c_centre, color c_corner) = 0;
		virtual void draw_text(std::string_view text, recti bounds, style::font_face font, style::text_style style,
		                       color c, color bg) = 0;
		virtual void draw_text(std::string_view text, const std::vector<text_highlight_t>& highlights, recti bounds,
		                       style::font_face font, style::text_style style, color clr, color bg) = 0;
		virtual void draw_text(const text_layout_ptr& tl, recti bounds, color clr, color bg) = 0;
		virtual void draw_shadow(recti bounds, int width, float alpha, bool inverse = false) = 0;
		virtual void draw_border(recti inside, recti outside, color c_inside, color c_outside) = 0;
		virtual void draw_texture(const texture_ptr& t, recti dst, float alpha = 1.0f,
		                          texture_sampler sampler = texture_sampler::point) = 0;
		virtual void draw_texture(const texture_ptr& t, recti dst, recti src, float alpha = 1.0f,
		                          texture_sampler sampler = texture_sampler::point, float radius = 0.0) = 0;
		virtual void draw_texture(const texture_ptr& t, const quadd& dst, recti src, float alpha,
		                          texture_sampler sampler) = 0;
		virtual void draw_texture(const texture_ptr& t, const quadd& dst, recti src, float alpha,
		                          texture_sampler sampler, const texture_transform& transform) = 0;
		virtual void draw_vertices(const vertices_ptr& v) = 0;
		virtual void draw_edge_shadows(float alpha) = 0;

		sizei measure_text(std::string_view text, style::font_face font, style::text_style style, int width,
		                   int height = 0) override = 0;
		int text_line_height(style::font_face type) override = 0;

		virtual texture_ptr create_texture() = 0;
		virtual vertices_ptr create_vertices() = 0;
		text_layout_ptr create_text_layout(style::font_face font) override = 0;

		virtual recti clip_bounds() const = 0;
		virtual void clip_bounds(recti) = 0;
		virtual void restore_clip() = 0;
	};

	// The outcome of flushing a scene. `backend_code` is the graphics API's own result, carried so
	// the window layer can log it and decide whether the device was lost; portable code reads only
	// `failed`. A CPU backend has no device to lose and always succeeds.
	struct present_result
	{
		bool failed = false;
		int32_t backend_code = 0;
	};

	// A draw context backed by a graphics device and a window, rather than the drawing interface
	// alone. Every backend implements this - the Direct3D one, the CPU rasterizer, and whatever a
	// port adds - so it names no graphics API and no window type.
	class draw_context_device : public draw_context
	{
	public:
		virtual void destroy() = 0;
		virtual void update_font_size(int base_font_size) = 0;

		// damage is the region the window layer knows needs repainting; empty means the whole
		// client. It is an optimisation hint only - a backend may redraw more, but the resulting
		// pixels inside damage must not depend on how much was redrawn.
		virtual void begin_draw(sizei client_extent, int base_font_size, recti damage = {}) = 0;

		virtual present_result render() = 0;

		// Discards any damage limit, so the next render covers the whole client. Needed by callers
		// that re-present an existing scene whose textures changed underneath it.
		virtual void reset_damage()
		{
		}

		virtual void resize(sizei size) = 0;
		virtual bool is_valid() const = 0;

		// Releases every reference this context holds on the swap-chain back buffer. Must be
		// called before the buffers are resized - a still-bound render target view makes that fail.
		// No-op on a CPU backend.
		virtual void release_back_buffer_references()
		{
		}

		// Software-rendering extensions (only implemented by the CPU backend used for layered
		// bubble popups; no-ops on a hardware backend).
		virtual void set_layer_alpha(int alpha)
		{
		}

		virtual void draw_bubble_background(recti bounds, pointi focus_location, int padding, float radius)
		{
		}
	};

	using draw_context_device_ptr = std::shared_ptr<draw_context_device>;

	class scoped_clip final : public df::no_copy
	{
		draw_context& _dc;

	public:
		scoped_clip(draw_context& dc, const recti bounds) : _dc(dc)
		{
			_dc.clip_bounds(bounds);
		}

		~scoped_clip() override
		{
			_dc.restore_clip();
		}
	};


	class control_base
	{
	public:
		virtual ~control_base() = default;
		virtual std::any handle() const = 0;
		virtual void destroy() = 0;
		virtual void enable(bool enable) = 0;
		virtual std::string window_text() const = 0;
		virtual void window_text(std::string_view val) = 0;
		virtual void focus() = 0;
		virtual sizei measure(int cx) const = 0;
		virtual bool is_visible() const = 0;
		virtual bool has_focus() const = 0;
		virtual recti window_bounds() const = 0;
		virtual void window_bounds(recti bounds, bool visible) = 0;
		virtual void show(bool show) = 0;
		virtual void options_changed() = 0;
	};

	struct key_state
	{
		bool control = false;
		bool shift = false;
		bool alt = false;
	};

	class toolbar : public control_base
	{
	public:
		virtual sizei measure_toolbar(int cx) = 0;
		virtual void update_button_state(bool resize, bool text_changed) = 0;
		virtual recti button_bounds(const command_ptr& command) const = 0;
	};

	class edit : public control_base
	{
	public:
		virtual void limit_text_len(int i) = 0;
		virtual void replace_sel(std::string_view new_text, bool add_space_if_append) = 0;
		virtual void select(int start, int end) = 0;
		virtual void select_all() = 0;
		virtual void set_icon(icon_index icon) = 0;
		virtual void set_background(color32 bg) = 0;
		virtual void auto_completes(const std::vector<std::string>& texts) = 0;
	};

	class trackbar : public control_base
	{
	public:
		virtual int get_pos() const = 0;
		virtual void SetPos(int val) = 0;
		virtual void buddy(const edit_ptr& edit) = 0;
	};

	class button : public control_base
	{
	public:
		virtual void set_checked(bool checked) = 0;
	};

	class date_time_control : public control_base
	{
	};

	class web_events : public df::no_copy
	{
	public:
		virtual void navigation_complete(std::string_view url) = 0;
		virtual bool before_navigate(std::string_view url) = 0;
	};

	class web_window : public control_base
	{
	public:
		virtual void eval_in_browser(std::string_view script) const = 0;
	};

	struct edit_styles
	{
		bool horizontal_scroll = false;
		bool align_center = false;
		bool number = false;
		bool password = false;
		bool vertical_scroll = false;
		bool multi_line = false;
		bool spelling = false;
		bool want_return = false;
		bool file_system_auto_complete = false;
		bool rounded_corners = false;
		bool select_all_on_focus = false;
		style::font_face font = style::font_face::dialog;
		color32 bg_clr = style::color::dialog_background;
		std::function<bool(int c, key_state keys)> capture_key_down;
		std::vector<std::string> auto_complete_list;
		std::string cue;
	};

	struct toolbar_styles
	{
		bool xTBSTYLE_WRAPABLE = false;
		bool xTBSTYLE_LIST = false;
		sizei button_extent = {0, 0};
	};

	class command
	{
	public:
		command() = default;

		command(const icon_index icon, std::any opaque, std::function<void()> invoke)
			: icon(icon), opaque(std::move(opaque)), invoke(std::move(invoke))
		{
		}

		command(const std::string_view t, std::any opaque, std::function<void()> invoke, const bool ch)
			: text(t), checked(ch), opaque(std::move(opaque)), invoke(std::move(invoke))
		{
		}

		std::string text;
		std::string toolbar_text;
		std::string tooltip_text;
		// Why the command is dimmed right now; empty while it is enabled.
		std::string disabled_reason;
		std::string keyboard_accelerator_text;
		std::vector<keyboard_accelerator_t> kba;

		bool visible = true;
		bool enable = true;
		bool checked = false;
		bool checkable = false;
		// Paints the toolbar button with the accent fill so it reads as the one thing asking to be pressed.
		bool highlight = false;

		bool text_can_change = false;
		bool icon_can_change = false;

		command_group group = command_group::none;
		icon_index icon = icon_index::none;
		color32 clr = 0;

		std::any opaque;
		std::function<void()> invoke;
		std::function<std::vector<command_ptr>()> menu;
	};


	enum class close_result
	{
		ok,
		cancel
	};

	class frame : public control_base
	{
	public:
		~frame() override = default;

		virtual void invalidate(recti bounds = {}, bool erase = false) = 0;
		virtual void layout() = 0;
		virtual void redraw() = 0;
		virtual void redraw_now() = 0;
		virtual void scroll(int dx, int dy, recti bounds, bool scroll_child_controls) = 0;
		virtual void track_menu(recti button_bounds, const std::vector<command_ptr>& buttons) = 0;
		virtual void close(bool is_cancel = false) = 0;

		virtual bool is_enabled() const = 0;
		virtual bool is_maximized() const = 0;
		void enable(bool e) override = 0;
		void focus() override = 0;
		virtual bool is_occluded() const = 0;
		virtual void set_cursor(style::cursor cursor) = 0;
		virtual void reset_graphics() = 0;
		virtual pointi cursor_location() = 0;
	};

	// A host's window does not exist before it is attached or after it is destroyed, yet the host is
	// still populated, laid out, counted and invalidated across both windows. Standing in for the
	// absent window here keeps that contract in one place instead of asking every caller - including
	// view_host and view_scroller, which cannot know - to test for null first.
	class null_frame final : public frame
	{
	public:
		std::any handle() const override { return {}; }
		void destroy() override {}
		void enable(bool) override {}
		std::string window_text() const override { return {}; }
		void window_text(std::string_view) override {}
		void focus() override {}
		sizei measure(int) const override { return {}; }
		bool is_visible() const override { return false; }
		bool has_focus() const override { return false; }
		recti window_bounds() const override { return {}; }
		void window_bounds(recti, bool) override {}
		void show(bool) override {}
		void options_changed() override {}

		void invalidate(recti = {}, bool = false) override {}
		void layout() override {}
		void redraw() override {}
		void redraw_now() override {}
		void scroll(int, int, recti, bool) override {}
		void track_menu(recti, const std::vector<command_ptr>&) override {}
		void close(bool = false) override {}

		bool is_enabled() const override { return false; }
		bool is_maximized() const override { return false; }
		// Callers use this to skip drawing, and nothing drawn into an absent window can be seen.
		bool is_occluded() const override { return true; }
		void set_cursor(style::cursor) override {}
		void reset_graphics() override {}
		// Outside every client rect, so hit testing against it resolves to no controller.
		pointi cursor_location() override { return {-1, -1}; }
	};

	// Stateless, so one instance serves every host that has no window.
	inline const frame_ptr& no_frame()
	{
		static const frame_ptr instance = std::make_shared<null_frame>();
		return instance;
	}

	enum class other_mouse_button
	{
		xb1,
		xb2
	};

	class frame_host
	{
	public:
		virtual ~frame_host() = default;

		virtual void on_window_layout(measure_context& mc, sizei extent, bool is_minimized) = 0;
		virtual void on_window_paint(draw_context& dc) = 0;

		virtual void on_window_destroy()
		{
		}

		virtual void on_mouse_move(const pointi loc, bool is_tracking)
		{
		}

		virtual void on_mouse_left_button_down(const pointi loc, const key_state keys)
		{
		}

		virtual void on_mouse_left_button_up(const pointi loc, const key_state keys)
		{
		}

		virtual void on_mouse_middle_button_down(const pointi loc, const key_state keys)
		{
		}

		virtual void on_mouse_middle_button_up(const pointi loc, const key_state keys)
		{
		}

		virtual void on_mouse_leave(const pointi loc)
		{
		}

		virtual void on_mouse_wheel(const pointi loc, const int delta, const key_state keys, bool& was_handled)
		{
		}

		virtual void on_mouse_hwheel(const pointi loc, const int delta, const key_state keys, bool& was_handled)
		{
		}

		virtual void on_mouse_left_button_double_click(const pointi loc, const key_state keys)
		{
		}

		virtual void on_mouse_other_button_up(const other_mouse_button& button, const pointi loc, const key_state keys)
		{
		}

		virtual void pan_start(const pointi start_loc)
		{
		}

		virtual void pan(const pointi start_loc, const pointi current_loc)
		{
		}

		virtual void pan_end(const pointi start_loc, const pointi final_loc)
		{
		}

		// Returns false when the tap has no touch-specific meaning, so the caller runs the normal double-click.
		virtual bool touch_double_tap(const pointi location)
		{
			return false;
		}

		virtual void focus_changed(const bool has_focus, const control_base_ptr& child)
		{
		}

		virtual void tick()
		{
		}

		virtual void activate(bool is_active)
		{
		}

		virtual bool key_down(const int c, const key_state keys)
		{
			return false;
		}

		virtual void command_hover(const command_ptr& c, const recti window_bounds)
		{
		}

		virtual std::vector<command_ptr> menu(const pointi loc)
		{
			return {};
		}

		virtual platform::drop_effect drag_over(const platform::clipboard_data& data, const key_state keys,
		                                        const pointi loc)
		{
			return platform::drop_effect::none;
		}

		virtual platform::drop_effect drag_drop(platform::clipboard_data& data, const key_state keys, const pointi loc)
		{
			return platform::drop_effect::none;
		}

		virtual void drag_leave()
		{
		}

		virtual bool is_control_area(const pointi loc)
		{
			return true;
		}

		virtual bool is_caption_area(const pointi loc) const
		{
			return false;
		}

		virtual void dpi_changed()
		{
		}
	};

	struct control_layout
	{
		control_base_ptr control;
		recti bounds;
		bool visible = true;
		bool offset = true;
	};

	using control_layouts = std::vector<control_layout>;

	enum class sys_command_type
	{
		MINIMIZE,
		MAXIMIZE,
		RESTORE
	};

	struct frame_style
	{
		bool hardware_accelerated = false;
		bool child = true;
		bool no_focus = false;
		bool can_focus = false;
		bool can_drop = false;

		int timer_milliseconds = 0;

		color_style colors = {
			style::color::dialog_background, style::color::dialog_text, style::color::dialog_selected_background
		};
	};


	// Independent sets of radio buttons hosted by one control frame must declare distinct groups.
	// Platforms scope radio exclusivity by sibling order, so without a group id a collision-policy
	// choice would clear the scope or format choice in the same host (and the reverse).
	constexpr int radio_group_default = 0;
	constexpr int radio_group_scope = 1;
	constexpr int radio_group_format = 2;
	constexpr int radio_group_collision = 3;

	class control_frame : public frame
	{
	public:
		virtual edit_ptr create_edit(const edit_styles& styles, std::string_view text,
		                             std::function<void(const std::string&)> changed) = 0;
		virtual trackbar_ptr create_slider(int min, int max, std::function<void(int, bool)> changed) = 0;
		virtual toolbar_ptr create_toolbar(const toolbar_styles& styles, const std::vector<command_ptr>& buttons) = 0;
		virtual button_ptr create_button(std::string_view text, std::function<void()> invoke,
		                                 bool default_button = false) = 0;
		virtual button_ptr create_button(icon_index icon, std::string_view title, std::string_view details,
		                                 std::function<void()> invoke, bool default_button = false) = 0;
		virtual button_ptr create_check_button(bool val, std::string_view text, bool is_radio,
		                                       std::function<void(bool)> changed,
		                                       int radio_group = radio_group_default) = 0;
		virtual date_time_control_ptr create_date_time_control(df::date_t text, std::function<void(df::date_t)> changed,
		                                                       bool include_time) = 0;
		virtual control_frame_ptr create_dlg(frame_host_weak_ptr host, bool is_popup) = 0;
		virtual frame_ptr create_frame(frame_host_weak_ptr host, const frame_style& ft) = 0;
		virtual bubble_window_ptr create_bubble() = 0;

		virtual void apply_layout(const control_layouts& controls, pointi scroll_offset) = 0;
		virtual close_result wait_for_close(uint32_t timeout_ms) = 0;
		virtual void focus_first() = 0;
		virtual void position(recti bounds) = 0;
		virtual void save_window_position(platform::setting_file_ptr& store) = 0;
		virtual bool is_canceled() const = 0;
		virtual double scale_factor() const = 0;
	};

	class bubble_frame
	{
	public:
		virtual ~bubble_frame() = default;
		virtual void show(const view_elements_ptr& elements, recti bounds, int x_center, int preferred_size,
		                  bool horizontal) = 0;
		virtual void hide() = 0;
		virtual bool is_visible() const = 0;
	};

	enum class os_event_type
	{
		options_changed,
		dpi_changed,
		screen_locked,
		system_device_change,

		// The machine is about to suspend. A suspend is not guaranteed to be followed by a
		// resume, so this is the last chance to persist state, and audio must not survive it.
		system_suspending,

		// Woken from suspend. Volumes, displays and the index may all have moved on.
		system_resumed,

		// The Windows session is ending (shutdown, restart, log off). Persist state without
		// prompting: UI shown here is ignored or force-closed by the shutdown sequence.
		session_ending,

		// The Direct3D device was lost. Release every GPU-backed resource; rendering
		// continues on the CPU software backend for the rest of the session.
		graphics_device_lost
	};

	enum class focus_mode
	{
		none,
		view,
		text_edit
	};

	class app
	{
	public:
		virtual ~app() = default;

		virtual bool pre_init() = 0;
		virtual bool load_settings(const platform::setting_file_ptr& store) = 0;
		virtual bool init(std::string_view command_line) = 0;
		virtual void final_exit() = 0;
		virtual void app_fail(std::string_view message, std::string_view more_text) = 0;
		virtual void idle() = 0;
		virtual void exit() = 0;
		virtual void crash(df::file_path dump_file_path) = 0;
		virtual void system_event(os_event_type ost) = 0;
		virtual bool can_exit() = 0;
		virtual void prepare_frame() = 0;
		virtual void folder_changed(df::folder_path folder) = 0;
		virtual bool key_down(char32_t c, key_state keys) = 0;
		virtual bool text_input(std::string_view text) = 0;
		virtual focus_mode focus_mode() const = 0;
		virtual void track_menu(const frame_ptr& parent, recti button_bounds,
		                        const std::vector<command_ptr>& buttons) = 0;
		virtual std::string restart_cmd_line() = 0;
		virtual void save_recovery_state() = 0;
	};

	class platform_app
	{
	public:
		virtual ~platform_app() = default;

		virtual void sys_command(sys_command_type cmd) = 0;
		virtual void full_screen(bool full) = 0;
		virtual void frame_delay(int animate_delay) = 0;
		virtual void queue_idle() = 0;
		virtual control_frame_ptr create_app_frame(const platform::setting_file_ptr& store,
		                                           const frame_host_weak_ptr& host) = 0;
		virtual void monitor_folders(const std::vector<df::folder_path>& vector) = 0;
		virtual void enable_screen_saver(bool cond) = 0;
		virtual void set_font_base_size(int i) = 0;
		virtual int get_font_base_size() const = 0;
	};

	// False when the CPU software renderer is active, or the system asks for no client-area
	// animation. animate_alpha then jumps straight to its target instead of fading.
	extern bool animations_enabled;

	// Exponential decay rate for alpha fades, per second. Chosen as -ln(1 - 0.333) * 60 so that a
	// 60Hz frame reproduces exactly the fixed 0.333 per frame this replaced.
	constexpr float alpha_fade_rate = 24.33f;

	// Fraction of the remaining distance a fade closes this frame, recomputed once per frame from
	// real elapsed time. Without it a fade advances per frame, so its duration is set by how fast
	// the machine renders - the same fade runs twice as fast on a 120Hz display as on a 60Hz one.
	// Written by prepare_frame and read by every step() it drives, so it is UI-thread-owned like
	// animations_enabled and the animations map beside it.
	extern float animation_step_factor;

	class animate_alpha
	{
		float _val = 0.0f;
		float _target = 0.0f;

	public:
		animate_alpha()
		{
		}

		animate_alpha(const float v) : _val(v), _target(v)
		{
		}

		float val() const
		{
			return _val;
		};

		float target() const
		{
			return _target;
		}

		void reset(const float v)
		{
			_target = _val = v;
		}

		void reset(const float v, const float t)
		{
			_target = t;
			_val = animations_enabled ? v : t;
		}

		void val(const float v)
		{
			_val = v;
		}

		void target(const float v)
		{
			_target = v;
			if (!animations_enabled) _val = v;
		}

		bool step()
		{
			const auto dd = _target - _val;

			if (!animations_enabled || std::abs(dd) < 0.001f)
			{
				const auto changed = _val != _target;
				_val = _target;
				return changed;
			}

			_val += dd * animation_step_factor;
			return true;
		}
	};

	extern std::unordered_map<void*, std::function<bool()>> animations;

	recti desktop_bounds(bool work_area);
	bool is_ui_thread();
	key_state current_key_state();
	std::any focus();
	void focus(const std::any&);
}

ui::app_ptr create_app(const ui::plat_app_ptr& pa);

using command_info_ptr = std::shared_ptr<ui::command>;

enum class commands;
using commands_map = df::hash_map<commands, command_info_ptr>;

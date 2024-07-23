// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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
	extern int APPS;
	extern int BACK;
	extern int BROWSER_BACK;
	extern int BROWSER_FAVORITES;
	extern int BROWSER_FORWARD;
	extern int BROWSER_HOME;
	extern int BROWSER_REFRESH;
	extern int BROWSER_SEARCH;
	extern int BROWSER_STOP;
	extern int DEL;
	extern int DOWN;
	extern int ESCAPE;
	extern int F1;
	extern int F10;
	extern int F11;
	extern int F2;
	extern int F3;
	extern int F4;
	extern int F5;
	extern int F6;
	extern int F7;
	extern int F8;
	extern int F9;
	extern int INSERT;
	extern int LEFT;
	extern int MEDIA_NEXT_TRACK;
	extern int MEDIA_PLAY_PAUSE;
	extern int MEDIA_PREV_TRACK;
	extern int MEDIA_STOP;
	extern int NEXT;
	extern int OEM_4;
	extern int OEM_6;
	extern int OEM_PLUS;
	extern int PRIOR;
	extern int RETURN;
	extern int RIGHT;
	extern int SPACE;
	extern int TAB;
	extern int UP;
	extern int VOLUME_DOWN;
	extern int VOLUME_MUTE;
	extern int VOLUME_UP;
	extern int HOME;
	extern int END;

	std::u8string_view format(int key);
};

struct keyboard_accelerator_t
{
	int key = 0;
	int key_state = 0;

	static constexpr auto shift = 0x04;
	static constexpr auto control = 0x08;
	static constexpr auto alt = 0x10;
};

std::u8string format_keyboard_accelerator(const std::vector<keyboard_accelerator_t>& keyboard_accelerators);

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

	/*struct BITMAPINFOANDPALETTE
	{
		BITMAPINFOHEADER bmiHeader;
		DWORD bmiColors[256];
	};*/

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

	recti scale_dimensions_up(sizei dims, recti limit) noexcept;

	////////////////////////////////////////////////////////////////////////////////////
	// Pixels Conversions

	using color32 = uint32_t;

	constexpr color32 rgb(const uint32_t r, const uint32_t g, const uint32_t b) noexcept
	{
		return r | (g << 8) | (b << 16);
	};

	constexpr color32 rgba(const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a = 255) noexcept
	{
		return r | (g << 8) | (b << 16) | (a << 24);
	};

	constexpr color32 saturate_rgba(const int r, const int g, const int b, const int a) noexcept
	{
		return df::byte_clamp(r) | (df::byte_clamp(g) << 8) | (df::byte_clamp(b) << 16) | (df::byte_clamp(a) << 24);
	};

	constexpr color32 saturate_rgba(const uint32_t r, const uint32_t g, const uint32_t b, const uint32_t a) noexcept
	{
		return df::byte_clamp(r) | (df::byte_clamp(g) << 8) | (df::byte_clamp(b) << 16) | (df::byte_clamp(a) << 24);
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
		return 0xffu & (c >> 24);
	}

	constexpr uint32_t get_r(const color32 c) noexcept
	{
		return 0xffu & c;
	}

	constexpr uint32_t get_g(const color32 c) noexcept
	{
		return 0xffu & (c >> 8);
	}

	constexpr uint32_t get_b(const color32 c) noexcept
	{
		return 0xffu & (c >> 16);
	}

	constexpr uint32_t mul_div_u32(const uint32_t n, const uint32_t num, const uint32_t den)
	{
		return den ? ((n * num) + (den / 2)) / den : -1;
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
		return ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000) | (c & 0x0000FF00) | (static_cast<uint32_t>(a) << 24);
		//return rgba(get_b(c), get_g(c), get_r(c), a);
	}

	constexpr color32 bgr(const color32 c) noexcept
	{
		return ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000) | (c & 0x0000FF00);
		// return rgb(get_b(c), get_g(c), get_r(c));
	}

	constexpr color32 adjust_a(const color32 c, const uint32_t a = 0xFF) noexcept
	{
		return rgba(get_r(c), get_g(c), get_b(c), a);
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
		const auto c16 = (a * (255u - tu)) + (b * tu);
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
		return ((cx * bytes_per_pixel) + 15) & ~15;
	}

	inline sizei round_div2(const sizei extent)
	{
		auto x = extent.cx & ~1;
		auto y = extent.cy & ~1;

		return {x, y};
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

	class histogram : public df::no_copy
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

		void add_color(const color32 c) noexcept
		{
			_r[get_r(c)]++;
			_g[get_g(c)]++;
			_b[get_b(c)]++;
		}

		void calc_max() noexcept
		{
			int m = 0;

			for (int i = 0; i < max_value; ++i)
			{
				m = std::max(std::max(m, _r[i]), std::max(_g[i], _b[i]));
			}

			_max = m;
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
	private:
		sizei _dimensions;
		size_t _stride = 0;
		size_t _size = 0;
		double _time = 0.0;
		std::unique_ptr<uint8_t, df::free_delete> _pixels;
		texture_format _format = texture_format::None;
		orientation _orientation = orientation::top_left;		

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
			_format = texture_format::None;
			_time = 0.0;
			_orientation = orientation::top_left;
		}

		void make_blank()
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
			return _pixels.get() + (y * _stride);
		}

		uint8_t* pixels_line(const int y) noexcept
		{
			df::assert_true(y < _dimensions.cy);
			return _pixels.get() + (y * _stride);
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
				const auto message = str::format(u8"invalid pixel size {}x{}"sv, cx, cy);
				df::log(__FUNCTION__, message);
				throw app_exception(message);
			}

			_stride = calc_stride(cx, calc_bytes_per_pixel(fmt));
			_size = _stride * cy;
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
			alloc(r.extent(), s.format(), s.orientation(), s.time());

			const ssize_t stride_out = _stride;
			const ssize_t stride_in = s._stride;
			const auto* const p = s._pixels.get() + (r.left * 4_z) + (r.top * stride_in);

			for (int y = 0; y < _dimensions.cy; ++y)
			{
				memcpy_s(_pixels.get() + (y * stride_out), stride_out, p + (y * stride_in), stride_in);
			}
		}

		void draw(const surface& s, const pointi location, const recti src)
		{
			const ssize_t stride_this = _stride;
			const ssize_t stride_src = s._stride;
			const ssize_t copy_bytes_len = src.width() * calc_bytes_per_pixel(_format);

			for (int y = 0; y < src.height(); ++y)
			{
				memcpy_s(
					_pixels.get() + ((y + location.y) * stride_this) + (location.x * 4),
					stride_this,
					s._pixels.get() + ((y + src.top) * stride_src) + (src.left * 4),
					copy_bytes_len);
			}
		}

		void swap_rb();

		void set_pixel(const int x, const int y, const color32 c)
		{
			if (x < 0 || x >= _dimensions.cx || y < 0 || y >= _dimensions.cy) return;
			auto* const line = std::bit_cast<color32*>(_pixels.get() + (y * _stride));
			line[x] = c;
		}

		color32 get_pixel(const int x, const int y) const
		{
			if (x < 0 || x >= _dimensions.cx || y < 0 || y >= _dimensions.cy) return 0;
			const auto* const line = std::bit_cast<const color32*>(_pixels.get() + (y * _stride));
			return line[x];
		}

		void clear(color32 clr);
		surface_ptr transform(simple_transform t) const;

		void fill_pie(pointi center, int radius, const color32 color[64], color32 color_center, color32 color_bg);

		const_surface_ptr transform(const image_edits& photo_edits) const;

		pixel_difference_result pixel_difference(const const_surface_ptr& image) const;
	};


	class image final : public std::enable_shared_from_this<image>
	{
	private:
		df::blob _data;
		sizei _dimensions;
		image_format _format = image_format::Unknown;
		mutable orientation _orientation = orientation::top_left;

	public:
		image() noexcept = default;
		~image() noexcept = default;

		image(const image&) noexcept = default;
		image& operator=(const image&) noexcept = default;
		image(image&&) noexcept = default;
		image& operator=(image&&) noexcept = default;

		image(df::blob&& data, const sizei d, const image_format f, const orientation orientation) noexcept :
			_data(std::move(data)), _dimensions(d), _format(f), _orientation(orientation)
		{
		}

		image(df::cspan data, const sizei d, const image_format f, const orientation orientation) noexcept :
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

		std::u8string_view extension() const
		{
			switch (_format)
			{
			case image_format::JPEG: return u8"jpg"sv;
			case image_format::PNG: return u8"png"sv;
			case image_format::WEBP: return u8"webp"sv;
			case image_format::Unknown: break;
			default: break;
			}

			df::assert_true(false); // Unknown extension
			return u8"x"sv;
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
			select,
			move,
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
			small_icons,
			petscii
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

		color(const color32 rgb) :
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

		color a_min(const float a) const
		{
			return {r, g, b, std::min(a, a)};
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
			return abs(r) +
				abs(g) +
				abs(b) +
				abs(a);
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
			return { sat_f(r * x), sat_f(g * x), sat_f(b * x), a };
		}

		color average(const color other) const
		{
			return {
				(r + other.r) / 2.0f,
				(g + other.g) / 2.0f,
				(b + other.b) / 2.0f,
				(a + other.a) / 2.0f };
		}

		static float femphasize(const float f)
		{
			return ((f - 0.5f) * 0.9f) + 0.5f;
		}

		color emphasize() const
		{
			return color(femphasize(r), femphasize(g), femphasize(b), a);
		}

		color emphasize(const bool can_emphasize) const
		{
			return can_emphasize ? color(femphasize(r), femphasize(g), femphasize(b), a) : *this;
		}

		uint16_t get_r16() const { return static_cast<uint16_t>(r * 0xffff); }
		uint16_t get_g16() const { return static_cast<uint16_t>(g * 0xffff); }
		uint16_t get_b16() const { return static_cast<uint16_t>(b * 0xffff); }
		uint16_t get_a16() const { return static_cast<uint16_t>(a * 0xffff); }

		static color from_a(const float a)
		{
			df::assert_true(a < 1.1f);
			return { 1.0f, 1.0f, 1.0f, a };
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


	constexpr std::u8string_view to_string(const texture_format format)
	{
		switch (format)
		{
		case texture_format::ARGB: return u8"ARGB"sv;
		case texture_format::P010: return u8"p010"sv;
		case texture_format::NV12: return u8"NV12"sv;
		case texture_format::RGB: return u8"RGB"sv;
		default: break;
		}

		return u8"?"sv;
	}

	constexpr std::u8string_view to_string(const texture_sampler sampler)
	{
		switch (sampler)
		{
		case texture_sampler::point: return u8"point"sv;
		case texture_sampler::bicubic: return u8"bicubic"sv;
		case texture_sampler::bilinear: return u8"bilinear"sv;
		default: break;
		}

		return u8"?"sv;
	}

	class color_adjust
	{
	private:
		static constexpr int curve_len = 0x1000;

		double _curve[curve_len];
		double _saturation = 0;
		double _vibrance = 0;

	public:
		void color_params(double vibrance, double saturation, double darks, double midtones, double lights,
		                  double contrast, double brightness);
		void apply(const const_surface_ptr& src, uint8_t* dst, const size_t dst_stride, df::cancel_token token) const;

	private:
		color32 adjust_color(double y, double u, double v, double a) const;
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
		virtual void update(std::u8string_view text, style::text_style style) = 0;
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
		virtual sizei measure_text(std::u8string_view text, style::font_face font, style::text_style style, int cx, int cy = 0) = 0;
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

		virtual ~draw_context() = default;

		virtual void clear(color c) = 0;

		virtual void draw_rounded_rect(recti bounds, color c, int radius) = 0;
		virtual void draw_rect(recti bounds, color c) = 0;
		virtual void draw_text(std::u8string_view text, recti bounds, style::font_face font, style::text_style style,
		                       color c, color bg) = 0;
		virtual void draw_text(std::u8string_view text, const std::vector<text_highlight_t>& highlights, recti bounds,
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
		virtual void draw_vertices(const vertices_ptr& v) = 0;
		virtual void draw_edge_shadows(float alpha) = 0;

		sizei measure_text(std::u8string_view text, style::font_face font, style::text_style style, int width,
		                   int height = 0) override = 0;
		int text_line_height(style::font_face type) override = 0;

		virtual texture_ptr create_texture() = 0;
		virtual vertices_ptr create_vertices() = 0;
		text_layout_ptr create_text_layout(style::font_face font) override = 0;

		virtual recti clip_bounds() const = 0;
		virtual void clip_bounds(recti) = 0;
		virtual void restore_clip() = 0;
	};


	class control_base
	{
	public:
		virtual ~control_base() = default;
		virtual std::any handle() const = 0;
		virtual void destroy() = 0;
		virtual void enable(bool enable) = 0;
		virtual std::u8string window_text() const = 0;
		virtual void window_text(std::u8string_view val) = 0;
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
	};

	class edit : public control_base
	{
	public:
		virtual void limit_text_len(int i) = 0;
		virtual void replace_sel(std::u8string_view new_text, bool add_space_if_append) = 0;
		virtual void select(int start, int end) = 0;
		virtual void select_all() = 0;
		virtual void set_icon(icon_index icon) = 0;
		virtual void set_background(color32 bg) = 0;
		virtual void auto_completes(const std::vector<std::u8string>& texts) = 0;
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
	};

	class date_time_control : public control_base
	{
	public:
	};

	class web_events : public df::no_copy
	{
	public:
		virtual void navigation_complete(std::u8string_view url) = 0;
		virtual bool before_navigate(std::u8string_view url) = 0;

		virtual void select_place(const double lat, const double lng)
		{
		}
	};

	class web_window : public control_base
	{
	public:
		virtual void eval_in_browser(std::u8string_view script) const = 0;
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
		std::vector<std::u8string> auto_complete_list;
		std::u8string cue;
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

		command(icon_index icon, std::any opaque, std::function<void()> invoke)
			: icon(icon), opaque(std::move(opaque)), invoke(std::move(invoke))
		{
		}

		command(const std::u8string_view t, std::any opaque, std::function<void()> invoke, const bool ch)
			: text(t), checked(ch), opaque(std::move(opaque)), invoke(std::move(invoke))
		{
		}
				
		std::u8string text;
		std::u8string toolbar_text;
		std::u8string tooltip_text;
		std::u8string keyboard_accelerator_text;
		std::vector<keyboard_accelerator_t> kba;

		bool visible = true;
		bool enable = true;
		bool checked = false;

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

		virtual void on_mouse_leave(const pointi loc)
		{
		}

		virtual void on_mouse_wheel(const pointi loc, const int delta, const key_state keys, bool& was_handled)
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


	class control_frame : public frame
	{
	public:
		virtual edit_ptr create_edit(const edit_styles& styles, std::u8string_view text,
		                             std::function<void(const std::u8string&)> changed) = 0;
		virtual trackbar_ptr create_slider(int min, int max, std::function<void(int, bool)> changed) = 0;
		virtual toolbar_ptr create_toolbar(const toolbar_styles& styles, const std::vector<command_ptr>& buttons) = 0;
		virtual button_ptr create_button(std::u8string_view text, std::function<void()> invoke,
		                                 bool default_button = false) = 0;
		virtual button_ptr create_button(icon_index icon, std::u8string_view title, std::u8string_view details,
		                                 std::function<void()> invoke, bool default_button = false) = 0;
		virtual button_ptr create_check_button(bool val, std::u8string_view text, bool is_radio,
		                                       std::function<void(bool)> changed) = 0;
		virtual date_time_control_ptr create_date_time_control(df::date_t text, std::function<void(df::date_t)> changed,
		                                                       bool include_time) = 0;
		virtual web_window_ptr create_web_window(std::u8string_view start_url, web_events* events) = 0;
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
		system_device_change
	};

	class app
	{
	public:
		virtual ~app() = default;

		virtual bool pre_init() = 0;
		virtual bool init(std::u8string_view command_line) = 0;
		virtual void final_exit() = 0;
		virtual void app_fail(std::u8string_view message, std::u8string_view more_text) = 0;
		virtual void idle() = 0;
		virtual void exit() = 0;
		virtual void crash(df::file_path dump_file_path) = 0;
		virtual void system_event(os_event_type ost) = 0;
		virtual bool can_exit() = 0;
		virtual void prepare_frame() = 0;
		virtual void folder_changed() = 0;
		virtual bool key_down(char32_t c, key_state keys) = 0;
		virtual void track_menu(const frame_ptr& parent, recti button_bounds,
		                        const std::vector<command_ptr>& buttons) = 0;
		virtual std::u8string restart_cmd_line() = 0;
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

	class animate_alpha
	{
	private:
		float _val = 0.0f;
		float _target = 0.0f;

	public:
		animate_alpha()
		{
		}

		animate_alpha(float v) : _val(v), _target(v)
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

		void reset(float v)
		{
			_target = _val = v;
		}

		void reset(float v, float t)
		{
			_target = t;
			_val = t;
		}

		void val(float v)
		{
			_val = v;
		}

		void target(float v)
		{
			_target = v;
			_val = v;
		}

		bool step()
		{
			const auto dd = _target - _val;

			if (abs(dd) < 0.001f)
			{
				_val = _target;
				return false;
			}

			_val += dd * 0.12345f;
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

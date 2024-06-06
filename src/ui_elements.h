// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "app_commands.h"
#include "ui.h"
#include "ui_view.h"

class view_state;
struct view_hover_element;
class ui_element_render;
class ui_element_state;
class view_elements;
class view_element;
class view_host_base;

using view_element_ptr = std::shared_ptr<view_element>;
using view_host_base_ptr = std::shared_ptr<view_host_base>;
using view_elements_ptr = std::shared_ptr<view_elements>;

enum class view_element_style : uint32_t
{
	none = 0,
	line_break = 1 << 0,
	no_break = 1 << 1,
	center = 1 << 2,
	right_justified = 1 << 3,

	grow = 1 << 4,
	// can expand to fill width
	shrink = 1 << 5,
	// can shrink vertically if needed
	prime = 1 << 6,
	// try to show this item in default view - user should not have to scroll down
	no_vert_center = 1 << 7,

	row_title = 1 << 8,
	new_line = 1 << 9,

	checked = 1 << 16,

	visible = 1 << 17,
	can_invoke = 1 << 18,
	has_tooltip = 1 << 19,

	hover = 1 << 20,
	tracking = 1 << 21,
	selected = 1 << 22,
	error = 1 << 23,
	highlight = 1 << 24,

	important = 1 << 25,
	info = 1 << 26,
	background = 1 << 27,
	dark_background = 1 << 28,
};

constexpr view_element_style operator|(const view_element_style a, const view_element_style b)
{
	return static_cast<view_element_style>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr view_element_style operator&(const view_element_style a, const view_element_style b)
{
	return static_cast<view_element_style>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr view_element_style operator^(const view_element_style a, const view_element_style b)
{
	return static_cast<view_element_style>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}

constexpr view_element_style operator~(const view_element_style a)
{
	return static_cast<view_element_style>(~static_cast<uint32_t>(a));
}

constexpr bool operator&&(const view_element_style a, const view_element_style b)
{
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

constexpr view_element_style& operator|=(view_element_style& a, const view_element_style b)
{
	a = a | b;
	return a;
}

constexpr view_element_style& operator&=(view_element_style& a, const view_element_style b)
{
	a = a & b;
	return a;
}

constexpr view_element_style& operator^=(view_element_style& a, const view_element_style b)
{
	a = a ^ b;
	return a;
}

struct view_element_padding
{
	int16_t cx = 0;
	int16_t cy = 0;

	void operator()(int16_t xy)
	{
		cx = cy = xy;
	}

	void operator()(int16_t x, int16_t y)
	{
		cx = x;
		cy = y;
	}

	operator sizei() const
	{
		return {cx, cy};
	}

	constexpr sizei operator *(const double d) const noexcept
	{
		return { df::round(cx * d), df::round(cy * d) };
	}
};


class view_element
{
public:
	recti bounds;
	view_element_padding padding{3, 3};
	view_element_padding margin{0, 0};
	view_element_style style = view_element_style::visible;

	ui::color _bg_color;
	ui::color _bg_target;

	view_element() noexcept = default;

	constexpr explicit view_element(view_element_style style_in) noexcept : style(
		style_in | view_element_style::visible)
	{
	}

	view_element(const view_element&) noexcept = default;
	view_element& operator=(const view_element&) noexcept = default;
	view_element(view_element&&) noexcept = default;
	view_element& operator=(view_element&&) noexcept = default;
	virtual ~view_element() noexcept = default;

	ui::color calc_background_color(ui::draw_context& dc) const
	{
		auto c = _bg_color;
		c.a = _bg_color.a * dc.colors.alpha;
		return c;
	}

	void render_background(ui::draw_context& dc, const pointi element_offset) const
	{
		const auto bg = calc_background_color(dc);

		if (bg.a > 0.0f)
		{
			const auto pad = padding * dc.scale_factor;
			dc.draw_rounded_rect(bounds.offset(element_offset).inflate(pad.cx, pad.cy), bg, dc.baseline_snap);
		}
	}

	sizei porch() const
	{
		return {padding.cx + margin.cx, padding.cy + margin.cy};
	}

	virtual bool is_control_area(const pointi loc, const pointi element_offset) const
	{
		return bounds.contains(loc - element_offset) && (can_invoke() || has_tooltip());
	}

	virtual void render(ui::draw_context& dc, const pointi element_offset) const
	{
	}

	virtual sizei measure(ui::measure_context& mc, const int width_limit) const
	{
		return {};
	}

	virtual void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions)
	{
		bounds = bounds_in;
	}

	virtual void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
	{
	}

	virtual void hover(interaction_context& ic)
	{
	}

	virtual void dispatch_event(const view_element_event& event)
	{
	}

	virtual void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler)
	{
	}

	virtual view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                                     const std::vector<recti>& excluded_bounds);

	bool is_style_bit_set(const view_element_style mask) const
	{
		return style && mask;
	}

	void set_style_bit(view_element_style mask, bool state, const view_host_base_ptr& view, const view_element_ptr& e);
	void update_background_color();
	void set_style_bit(view_element_style mask, bool state);

	bool can_invoke() const
	{
		return is_style_bit_set(view_element_style::can_invoke);
	}

	bool has_tooltip() const
	{
		return is_style_bit_set(view_element_style::has_tooltip);
	}

	bool is_centered() const
	{
		return is_style_bit_set(view_element_style::center);
	}

	bool is_right_justified() const
	{
		return is_style_bit_set(view_element_style::right_justified);
	}

	bool is_line_break() const
	{
		return is_style_bit_set(view_element_style::line_break);
	}

	bool is_no_break() const
	{
		return is_style_bit_set(view_element_style::no_break);
	}

	bool is_row_title() const
	{
		return is_style_bit_set(view_element_style::row_title);
	}

	bool is_new_line_style() const
	{
		return is_style_bit_set(view_element_style::new_line);
	}

	bool is_visible() const
	{
		return is_style_bit_set(view_element_style::visible);
	}

	void is_visible(bool visible)
	{
		set_style_bit(view_element_style::visible, visible);
	}
};

using view_element_ptr = std::shared_ptr<view_element>;
using const_view_element_ptr = std::shared_ptr<const view_element>;

class view_flow_layout
{
	struct element_layout
	{
		sizei extent;
		sizei offset;
		recti bounds;
		bool line_start = false;
		bool line_end = false;
	};

	mutable std::vector<element_layout> _extents;

public:
	view_flow_layout() = default;

	sizei calc_layout(const std::vector<view_element_ptr>& elements, ui::measure_context& mc, const int width_limit) const
	{
		_extents.resize(elements.size());

		auto y = 0;
		auto x = 0;
		auto cy = 0;
		auto max_x = 0;
		auto max_y = 0;
		const auto left_x = 0;

		if (!elements.empty())
		{
			auto items_on_line = 0;
			auto previous_line_break = false;
			auto previous_no_break = false;
			auto max_row_title_cx = 0;

			element_layout* previous_item = nullptr;

			for (const auto& ev : elements)
			{
				if (ev->is_row_title())
				{
					auto extent = ev->measure(mc, width_limit);
					max_row_title_cx = std::max(max_row_title_cx, extent.cx);
				}
			}

			if (max_row_title_cx > width_limit / 2)
			{
				max_row_title_cx = width_limit / 2;
			}

			for (auto i = 0u; i < elements.size(); i++)
			{
				auto&& ev = elements[i];
				auto&& el = _extents[i];

				const auto current_width_limit = width_limit;

				const auto element_padding = ev->porch() * mc.scale_factor;
				auto max_extent_width = current_width_limit - (element_padding.cx * 2) - (previous_no_break
					? (x - left_x)
					: 0);
				auto extent = ev->measure(mc, max_extent_width);
				el.extent = extent;
				el.line_start = false;
				el.line_end = false;

				const auto x_right = x + extent.cx + (element_padding.cx * 2);
				const auto should_break = items_on_line > 0 && !previous_no_break && 
					(previous_line_break || (x_right - left_x) > current_width_limit || ev->is_row_title() || ev->is_new_line_style());

				if (should_break)
				{
					x = left_x;
					y += cy;

					items_on_line = 0;
					cy = 0;

					if (ev->is_row_title())
					{
						x += max_row_title_cx - extent.cx;
					}

					if (previous_item)
					{
						previous_item->line_end = true;
					}

					max_extent_width = current_width_limit - (element_padding.cx * 2) - (x - left_x);
					extent = ev->measure(mc, max_extent_width);
					el.extent = extent;
				}

				cy = std::max(cy, extent.cy + element_padding.cy * 2);

				if (items_on_line == 0)
				{
					el.line_start = true;
				}

				el.offset.cx = x + element_padding.cx;
				el.offset.cy = y + element_padding.cy;

				x += extent.cx + (element_padding.cx * 2);
				max_x = std::max(max_x, x);
				max_y = std::max(max_y, y);

				items_on_line += 1;
				previous_line_break = ev->is_line_break();
				previous_no_break = ev->is_no_break();
				previous_item = &el;
			}

			if (previous_item != nullptr)
			{
				previous_item->line_end = true;
			}

			max_y = std::max(max_y, y + cy);
		}

		return {max_x, max_y};
	}

	sizei measure(const std::vector<view_element_ptr>& elements, ui::measure_context& mc, int width_limit) const
	{
		return calc_layout(elements, mc, width_limit);
	}

	recti layout(std::vector<view_element_ptr>& elements, ui::measure_context& mc, const recti bounds)
	{
		const auto child_count = elements.size();
		auto line_max_center = 0;
		auto line_height = 0;

		for (auto i = 0u; i < child_count; i++)
		{
			auto&& ev = elements[i];
			auto&& el = _extents[i];

			const auto is_line_end = el.line_end;

			const recti r(pointi(bounds.left + el.offset.cx, bounds.top + el.offset.cy), el.extent);
			_extents[i].bounds = r;

			const auto line_center = (r.top + r.bottom) / 2;
			if (line_center > line_max_center) line_max_center = line_center;
			if (el.extent.cy > line_height) line_height = el.extent.cy;

			if (is_line_end)
			{
				const auto is_centered = ev->is_centered();
				const auto is_far = ev->is_right_justified();
				const auto padding = ev->porch() * mc.scale_factor;

				if (ev->is_style_bit_set(view_element_style::grow))
				{
					_extents[i].bounds.right = bounds.right - padding.cx;
				}
				else if (is_centered)
				{					
					const auto cx_offset = (bounds.right - (_extents[i].bounds.right + padding.cx)) / 2;
					auto line_i = i;

					while (line_i >= 0u && elements[line_i]->is_centered())
					{
						_extents[line_i].bounds.left += cx_offset;
						_extents[line_i].bounds.right += cx_offset;

						if (_extents[line_i].line_start)
							break;

						line_i -= 1;
					}
				}
				else if (is_far)
				{
					const auto cx_offset = (bounds.right - (_extents[i].bounds.right + padding.cx));
					auto line_i = i;

					while (line_i >= 0u && elements[line_i]->is_right_justified())
					{
						_extents[line_i].bounds.left += cx_offset;
						_extents[line_i].bounds.right += cx_offset;

						if (_extents[line_i].line_start)
							break;

						line_i -= 1;
					}
				}

				// vertical line center
				int line_i = i;

				while (line_i >= 0)
				{
					auto&& ev = elements[line_i];

					const auto cy_offset = line_max_center - ((_extents[line_i].bounds.top + _extents[line_i].bounds.
						bottom)) / 2;

					_extents[line_i].bounds.top += cy_offset;
					_extents[line_i].bounds.bottom += cy_offset;

					if (_extents[line_i].line_start)
						break;

					line_i -= 1;
				}

				line_max_center = 0;
				line_height = 0;
			}
		}

		ui::control_layouts positions;

		for (auto i = 0u; i < child_count; i++)
		{
			const auto& element_layout = _extents[i];
			elements[i]->layout(mc, element_layout.bounds, positions);
		}

		return bounds;
	}
};

struct calc_stack_elements_result
{
	std::vector<recti> layout_bounds;
	int cy = 0;
};

static calc_stack_elements_result calc_stack_elements(ui::measure_context& mc, const recti avail_bounds,
                                                      const std::vector<view_element_ptr>& elements,
                                                      const bool vertical_center = false, const sizei padding = {0, 0})
{
	calc_stack_elements_result result;

	const auto min_shrink_height = df::round(64 * mc.scale_factor);
	const auto cx_avail = avail_bounds.width();
	auto y = avail_bounds.top + padding.cy;
	auto prime_oversize_y = 0;
	auto shrink_element_count = 0;

	result.layout_bounds.reserve(elements.size());

	for (const auto& e : elements)
	{
		const auto element_padding = e->porch() * mc.scale_factor;
		const auto cx_element_avail = cx_avail - (element_padding.cx * 2) - (padding.cx * 2);
		const auto extent = e->measure(mc, cx_element_avail);

		recti element_bounds;
		element_bounds.left = avail_bounds.left + element_padding.cx + padding.cx;

		if (e->is_style_bit_set(view_element_style::grow))
		{
			element_bounds.right = avail_bounds.right - element_padding.cx - padding.cx;
		}
		else
		{
			element_bounds.right = avail_bounds.left + extent.cx + element_padding.cx + padding.cx;
		}

		element_bounds.top = y + element_padding.cy;
		element_bounds.bottom = element_bounds.top + extent.cy;

		if (e->is_visible())
		{
			y += extent.cy + (element_padding.cy * 2);
		}

		if (e->is_style_bit_set(view_element_style::shrink))
		{
			shrink_element_count += 1;
		}

		if (e->is_style_bit_set(view_element_style::prime))
		{
			if (element_bounds.bottom > avail_bounds.bottom)
			{
				prime_oversize_y = element_bounds.bottom + element_padding.cy - avail_bounds.bottom;
			}
		}

		if (e->is_style_bit_set(view_element_style::center))
		{
			const auto cx_extra = ((cx_avail - (element_padding.cx + padding.cx) * 2) - element_bounds.width()) / 2;
			element_bounds = element_bounds.offset(cx_extra, 0);
		}

		result.layout_bounds.emplace_back(element_bounds);
	}

	// apply shrinks to show prime
	if (prime_oversize_y > 0 && shrink_element_count > 0)
	{
		const auto cy = prime_oversize_y / shrink_element_count;
		auto yy = 0;

		for (auto i = 0u; i < result.layout_bounds.size(); i++)
		{
			const bool is_shrink = elements[i]->is_style_bit_set(view_element_style::shrink);
			//bool is_prime = elements[i]->is_style_bit_set(view_element_style::prime);

			if (is_shrink)
			{
				if (result.layout_bounds[i].height() > cy + min_shrink_height)
				{
					result.layout_bounds[i].bottom -= cy;
					yy += cy;
				}
			}
			else
			{
				result.layout_bounds[i] = result.layout_bounds[i].offset(0, -yy);
			}
		}
	}

	if (vertical_center && y < avail_bounds.height())
	{
		const auto offset = (avail_bounds.height() - y) / 2;

		for (auto i = 0u; i < result.layout_bounds.size(); i++)
		{
			if (!elements[i]->is_style_bit_set(view_element_style::no_vert_center))
			{
				result.layout_bounds[i] = result.layout_bounds[i].offset(0, offset);
			}
		}
	}

	for (auto i = 0u; i < result.layout_bounds.size(); i++)
	{
		const auto& element = elements[i];
		const auto& element_bounds = result.layout_bounds[i];
		const auto element_padding = element->porch() * mc.scale_factor;
		const auto element_bottom = element_bounds.bottom + element_padding.cy - avail_bounds.top;

		if (element_bottom > result.cy) result.cy = element_bottom;
	}

	return result;
}

static int stack_elements(ui::measure_context& mc, ui::control_layouts& positions, const recti avail_bounds,
                          const std::vector<view_element_ptr>& elements, const bool vertical_center = false,
                          const sizei padding = {0, 0})
{
	df::assert_true(elements.size() == std::unordered_set<view_element_ptr>(elements.begin(), elements.end()).size());
	// items need to be unique

	const auto layout = calc_stack_elements(mc, avail_bounds, elements, vertical_center, padding);
	auto result_height = 0;

	for (auto i = 0u; i < layout.layout_bounds.size(); i++)
	{
		const auto& element = elements[i];
		const auto& element_bounds = layout.layout_bounds[i];
		const auto element_padding = element->porch() * mc.scale_factor;
		const auto element_bottom = element_bounds.bottom + element_padding.cy;

		element->layout(mc, element_bounds, positions);
		if (element_bottom > result_height) result_height = element_bottom;
	}

	return result_height + padding.cy;
}

class view_elements final : public view_element, public std::enable_shared_from_this<view_elements>
{
private:
	std::vector<view_element_ptr> _children;
	view_flow_layout _flow;

public:
	view_elements() noexcept = default;

	explicit view_elements(view_element_style style_in) noexcept : view_element(style_in)
	{
	}

	explicit view_elements(std::vector<view_element_ptr> children) noexcept : _children(std::move(children))
	{
	}

	bool is_empty() const
	{
		return _children.empty();
	}

	void clear()
	{
		_children.clear();
	}

	size_t size() const
	{
		return _children.size();
	}

	bool is_control_area(const pointi loc, const pointi element_offset) const override
	{
		for (const auto& c : _children)
		{
			if (c->is_control_area(loc, element_offset))
			{
				return true;
			}
		}

		return false;
	}

	void render(ui::draw_context& dc, const pointi offset) const override
	{
		if (!_children.empty())
		{
			render_background(dc, offset);

			for (const auto& e : _children)
			{
				e->render(dc, offset);
			}
		}
	}

	void add(view_element_ptr&& v)
	{
		_children.emplace_back(v);
	}

	void add(const std::vector<view_element_ptr>& vals)
	{
		if (!vals.empty())
		{
			_children.insert(_children.end(), vals.begin(), vals.end());
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return _flow.measure(_children, mc, width_limit);
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		update_style_from_children();
		bounds = bounds_in;
		_flow.layout(_children, mc, bounds);
	}

	void update_style_from_children()
	{
		auto has_tooltip = false;
		auto can_invoke = false;

		for (const auto& c : _children)
		{
			has_tooltip |= c->has_tooltip();
			can_invoke |= c->can_invoke();
		}

		set_style_bit(view_element_style::has_tooltip, has_tooltip);
		set_style_bit(view_element_style::can_invoke, can_invoke);
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		for (const auto& c : _children)
		{
			if (c->bounds.offset(element_offset).contains(loc))
			{
				c->tooltip(hover, loc, element_offset);
			}
		}
	}

	void hover(interaction_context& ic) override
	{
		for (const auto& c : _children)
		{
			c->hover(ic);
		}
	}


	void dispatch_event(const view_element_event& event) override
	{
		for (const auto& c : _children)
		{
			c->dispatch_event(event);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override;
};

struct view_hover_element
{
	const static int default_prefered_size = 300;

	view_elements elements;
	recti window_bounds;
	recti active_bounds;
	uint32_t active_delay = 0;
	int x_focus = -1;
	int prefered_size = default_prefered_size;
	bool horizontal = false;
	commands id = commands::none;

	view_hover_element() = default;

	view_hover_element(view_elements elements_in, recti bounds, int x_focus = -1) : elements(std::move(elements_in)),
		window_bounds(bounds), x_focus(x_focus)
	{
	}

	bool is_empty() const
	{
		return elements.is_empty();
	}

	void clear()
	{
		elements.clear();
		window_bounds.clear();
		active_bounds.clear();
		x_focus = -1;
		prefered_size = default_prefered_size;
		horizontal = false;
		id = commands::none;
	}

	friend class ui_popup_controler;
};


class text_element_base : public view_element
{
protected:
	std::u8string _text;
	ui::style::font_size _font = ui::style::font_size::dialog;
	ui::style::text_style _text_style = ui::style::text_style::multiline;
	ui::color32 _foreground_clr = 0;

	mutable ui::text_layout_ptr _tl;

public:
	text_element_base(std::u8string_view text,
	                  const view_element_style s = view_element_style::none) noexcept : view_element(s), _text(text)
	{
	}

	void foreground_color(const ui::color32 clr)
	{
		_foreground_clr = clr;
	}

	void text(const std::u8string_view t)
	{
		_text = t;

		if (_tl)
		{
			_tl->update(_text, _text_style);
		}
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg = calc_background_color(dc);

		if (!_tl)
		{
			_tl = dc.create_text_layout(_font);
			_tl->update(_text, _text_style);
		}

		if (_tl)
		{
			const auto clr = _foreground_clr
				                 ? ui::color(_foreground_clr, dc.colors.alpha)
				                 : ui::color(dc.colors.foreground, dc.colors.alpha);
			dc.draw_text(_tl, logical_bounds, clr, bg);
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (!_tl)
		{
			_tl = mc.create_text_layout(_font);
			_tl->update(_text, _text_style);
		}

		if (_tl)
		{
			return _tl->measure_text(width_limit);
		}

		return {};
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::dpi_changed)
		{
			_tl.reset();
		}
	}
};

class text_element final : public std::enable_shared_from_this<text_element>, public text_element_base
{
public:
	text_element(std::u8string_view text, const ui::style::font_size font, const ui::style::text_style text_style,
	             const view_element_style style_in) noexcept : text_element_base(text, style_in)
	{
		_font = font;
		_text_style = text_style;

		update_background_color();
	}

	text_element(std::u8string_view text, const ui::style::text_style text_style) noexcept : text_element_base(text)
	{
		_text_style = text_style;
	}

	text_element(std::u8string_view text, const view_element_style style_in) noexcept : text_element_base(
		text, style_in)
	{
	}

	text_element(std::u8string_view text) noexcept : text_element_base(text)
	{
	}
};


class action_element final : public std::enable_shared_from_this<action_element>, public text_element_base
{
public:
	explicit action_element(std::u8string_view t) : text_element_base(
		t, view_element_style::center | view_element_style::new_line)
	{
		_font = ui::style::font_size::dialog;
		_text_style = ui::style::text_style::multiline;
		_foreground_clr = ui::style::color::dialog_selected_background;
	}

	explicit action_element(std::u8string&& t) : text_element_base(
		t, view_element_style::center | view_element_style::new_line)
	{
		_font = ui::style::font_size::dialog;
		_text_style = ui::style::text_style::multiline;
		_foreground_clr = ui::style::color::dialog_selected_background;
	}
};

class link_element final : public std::enable_shared_from_this<link_element>, public text_element_base
{
private:
	commands _cmd = commands::none;
	std::function<void()> _invoke;
	std::function<void(view_hover_element&)> _tooltip;

public:
	link_element(std::u8string_view text, commands cmd, const ui::style::font_size font,
	             const ui::style::text_style text_style,
	             const view_element_style style_in = view_element_style::none) noexcept : text_element_base(
		text, style_in), _cmd(cmd)
	{
		_font = font;
		_text_style = text_style;
		update_style();
	}

	link_element(std::u8string_view text, std::function<void()> func, const ui::style::font_size font,
	             const ui::style::text_style text_style,
	             const view_element_style style_in = view_element_style::none) noexcept :
		text_element_base(text, style_in), _invoke(std::move(func))
	{
		_font = font;
		_text_style = text_style;
		update_style();
	}

	link_element(std::u8string_view text, std::function<void()> func, std::function<void(view_hover_element&)> tooltip,
	             const ui::style::font_size font, const ui::style::text_style text_style,
	             const view_element_style style_in = view_element_style::none) noexcept :
		text_element_base(text, style_in), _invoke(std::move(func)), _tooltip(std::move(tooltip))
	{
		_font = font;
		_text_style = text_style;
		update_style();
	}

	link_element(std::u8string_view text, commands cmd) noexcept : text_element_base(text), _cmd(cmd)
	{
		update_style();
	}

	link_element(std::u8string_view text, std::function<void()> func) noexcept : text_element_base(text),
		_invoke(std::move(func))
	{
		update_style();
	}

	link_element(std::u8string_view text) noexcept : text_element_base(text)
	{
		update_style();
	}

	void update_style()
	{
		set_style_bit(view_element_style::has_tooltip, _cmd != commands::none || _tooltip);
		set_style_bit(view_element_style::can_invoke, _cmd != commands::none || _invoke);

		_foreground_clr = ui::average(ui::style::color::dialog_text, ui::style::color::dialog_selected_background);
	}

	void update_check(const view_element_event& event);
	void dispatch_event(const view_element_event& event) override;
	void tooltip(view_hover_element& hover, pointi loc, pointi element_offset) const override;

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override;
};

inline view_element_ptr make_icon_element(const icon_index i, const view_element_style style_in)
{
	const wchar_t text[2] = {static_cast<wchar_t>(i), 0};
	return std::make_shared<text_element>(str::utf16_to_utf8(text), ui::style::font_size::icons,
	                                      ui::style::text_style::single_line_center, style_in);
}

inline view_element_ptr make_icon_element(const icon_index i, const size_t repeat, const view_element_style style_in)
{
	const std::wstring text(repeat, static_cast<wchar_t>(i));
	return std::make_shared<text_element>(str::utf16_to_utf8(text), ui::style::font_size::icons,
	                                      ui::style::text_style::single_line_center, style_in);
}

inline view_element_ptr make_icon_link_element(const icon_index i, commands cmd, const view_element_style style_in)
{
	const wchar_t text[2] = {static_cast<wchar_t>(i), 0};
	return std::make_shared<link_element>(str::utf16_to_utf8(text), cmd, ui::style::font_size::icons,
	                                      ui::style::text_style::single_line_center, style_in);
}

inline view_element_ptr make_icon_link_element(const icon_index i, const size_t repeat,
                                               const std::function<void()>& func,
                                               const std::function<void(view_hover_element&)>& tooltip)
{
	const std::wstring text(repeat, static_cast<wchar_t>(i));
	return std::make_shared<link_element>(str::utf16_to_utf8(text), func, tooltip, ui::style::font_size::icons,
	                                      ui::style::text_style::single_line_center, view_element_style::none);
}

inline void xdraw_icon(ui::draw_context& dc, const icon_index i, const recti bounds, const ui::color c,
                       const ui::color bg)
{
	const wchar_t text[2] = {static_cast<wchar_t>(i), 0};
	dc.draw_text(str::utf16_to_utf8(text), bounds, ui::style::font_size::icons,
	             ui::style::text_style::single_line_center, c, bg);
}

class surface_element : public view_element
{
private:
	const ui::const_surface_ptr _surface;
	mutable ui::texture_ptr _tex;
	int _max_size = 0;

public:
	surface_element(ui::const_surface_ptr s, int max_size, view_element_style style_in) noexcept :
		view_element(style_in), _surface(std::move(s)), _max_size(max_size)
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (is_valid(_surface))
		{
			if (!_tex)
			{
				const auto t = dc.create_texture();

				if (t && t->update(_surface) != ui::texture_update_result::failed)
				{
					_tex = t;
				}
			}

			if (_tex)
			{
				dc.draw_texture(_tex, bounds.offset(element_offset));
			}
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (!is_valid(_surface)) return {};
		return ui::scale_dimensions(_surface->dimensions(),
		                            _max_size == 0 ? width_limit : std::min(_max_size, width_limit), true);
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::free_graphics_resources)
		{
			_tex.reset();
		}
	}
};


class divider_element final : public std::enable_shared_from_this<divider_element>, public view_element
{
public:
	divider_element() noexcept : view_element(view_element_style::grow)
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto line_height = df::round(1.1 * dc.scale_factor);
		auto logical_bounds = bounds.offset(element_offset);
		logical_bounds.top = (logical_bounds.top + logical_bounds.bottom - line_height) / 2;
		logical_bounds.bottom = logical_bounds.top + line_height;
		//logical_bounds = logical_bounds.inflate(-logical_bounds.width() / 11, 0);

		const auto clr = ui::color(0x000000, dc.colors.alpha / 4.44f);
		dc.draw_rect(logical_bounds, clr);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, mc.component_snap};
	}
};

class padding_element final : public std::enable_shared_from_this<padding_element>, public view_element
{
public:
	padding_element(int height = 0) noexcept : view_element(view_element_style::grow), _height(height)
	{
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, _height ? _height : mc.component_snap};
	}

	int _height;
};

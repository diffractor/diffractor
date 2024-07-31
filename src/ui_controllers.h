// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "ui.h"
#include "ui_view.h"

class clickable_controller final : public view_controller
{
private:
	const view_element_ptr _element;
	const pointi _element_offset;
	bool _tracking = false;
	bool _hover = false;
	bool _can_click = false;

public:
	clickable_controller(const view_host_ptr& host, view_element_ptr e, const pointi element_offset) :
		view_controller(host, e->bounds.offset(element_offset)), _element(std::move(e)),
		_element_offset(element_offset), _can_click(_element->can_invoke())
	{
		update_highlight();
	}

	clickable_controller(const view_host_ptr& host, view_element_ptr e, const pointi element_offset,
		const recti bounds) : view_controller(host, bounds), _element(std::move(e)),
		_element_offset(element_offset), _can_click(_element->can_invoke())
	{
		update_highlight();
	}

	~clickable_controller() override
	{
		if (_tracking)
		{
			_tracking = false;
		}

		if (_element)
		{
			interaction_context ic{ {-1, -1}, _element_offset, _tracking };
			_element->hover(ic);
		}

		_hover = false;

		update_highlight();
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_hover = _bounds.contains(loc);
		_tracking = true;
		interaction_context ic{ loc, _element_offset, _tracking };
		update_highlight();
		_element->hover(ic);

		if (ic.invalidate_view)
		{
			_host->frame()->invalidate(_bounds);
		}
	}

	void on_mouse_move(const pointi loc) override
	{
		_last_loc = loc;
		_hover = _bounds.contains(loc);
		interaction_context ic{ loc, _element_offset, _tracking };
		update_highlight();
		_element->hover(ic);

		if (ic.invalidate_view)
		{
			_host->frame()->invalidate(_bounds);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		const auto can_invoke = _tracking && _hover && _can_click;

		_last_loc = loc;
		_hover = _bounds.contains(loc);
		_tracking = false;
		interaction_context ic{ loc, _element_offset, _tracking };
		update_highlight();
		_element->hover(ic);

		if (ic.invalidate_view)
		{
			_host->frame()->invalidate(_bounds);
		}

		if (can_invoke)
		{
			const view_element_event e1{ view_element_event_type::click, _host };
			const view_element_event e2{ view_element_event_type::invoke, _host };

			_element->dispatch_event(e1);
			_element->dispatch_event(e2);
		}
	}

	void on_mouse_left_button_double_click(const pointi loc, const ui::key_state keys) override
	{
		if (_can_click)
		{
			const view_element_event e{ view_element_event_type::double_click, _host };
			_element->dispatch_event(e);
		}
	}

	ui::style::cursor cursor() const override
	{
		return _can_click ? ui::style::cursor::link : ui::style::cursor::normal;
	}

	void popup_from_location(view_hover_element& hover) override
	{
		_element->tooltip(hover, _last_loc, _element_offset);
	}

	void update_highlight() const
	{
		if (_element)
		{
			const auto hover = _element->is_style_bit_set(view_element_style::hover);
			//const auto tracking = _element->is_style_bit_set(view_element_style::tracking);

			if (hover != _hover) // || _tracking != tracking)
			{
				_element->set_style_bit(view_element_style::hover, _hover, _host, _element);
				//_element->set_style_bit(view_element_style::tracking, _tracking);
				_host->frame()->invalidate(_bounds);
			}
		}
	}
};

template <class T>
static view_controller_ptr default_controller_from_location(T& this_element, const view_host_ptr& host,
	const pointi loc, const pointi element_offset,
	const std::vector<recti>& excluded_bounds)
{
	if ((this_element.can_invoke() || this_element.has_tooltip()) && this_element.bounds.contains(loc - element_offset))
	{
		auto e = this_element.shared_from_this();
		auto bounds = e->bounds;
		for (const auto& ex : excluded_bounds) bounds.exclude(loc - element_offset, ex);
		return std::make_shared<clickable_controller>(host, e, element_offset, bounds.offset(element_offset));
	}

	return nullptr;
}

template <class TParent>
class selection_move_controller final : public view_controller
{
public:
	TParent& _parent;
	rectd _device_start;
	quadd _existing_selection;
	bool _tracking = false;

	selection_move_controller(const view_host_ptr& host, TParent& parent, const recti bounds_in) :
		view_controller(host, bounds_in), _parent(parent)
	{
		_tracking = false;
	}

	~selection_move_controller() override
	{
		if (_tracking)
		{
			_tracking = false;
			_parent.selection(_existing_selection);
		}
	}

	ui::style::cursor cursor() const override
	{
		return _tracking ? ui::style::cursor::hand_up : ui::style::cursor::hand_down;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		if (!_tracking)
		{
			_tracking = true;
			_first_tic = platform::tick_count();
			_last_loc = _start_loc = loc;
			_device_start = _parent.device_selection();
			_existing_selection = _parent.selection();
		}
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking)
		{
			_last_loc = loc;
			update(true);
		}
	}

	void update(bool tracking) const
	{
		const auto offset = _start_loc - _last_loc;
		auto rr = _device_start.offset(-offset.x, -offset.y);
		_parent.device_selection(rr, false, true);
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		if (_tracking)
		{
			_tracking = false;
			_last_loc = loc;
			_parent._state.invalidate_view(view_invalid::controller);
			update(false);
		}
	}

	void escape() override
	{
		if (_tracking)
		{
			_tracking = false;
			_parent.selection(_existing_selection);
		}
	}
};

template <class TParent>
class handle_move_controller final : public view_controller
{
public:
	TParent& _parent;
	rectd _handle_bounds;
	rectd _device_start;
	quadd _existing_selection;
	bool _tracking;

	bool _left = false;
	bool _top = false;
	bool _right = false;
	bool _bottom = false;

	handle_move_controller(const view_host_ptr& host, TParent& parent, const rectd start, bool l, bool t, bool r,
		bool b) : view_controller(host, _handle_bounds.round()), _parent(parent)
	{
		_handle_bounds = start;
		_left = l;
		_top = t;
		_right = r;
		_bottom = b;

		_tracking = false;
	}

	~handle_move_controller() override
	{
		if (_tracking)
		{
			_tracking = false;
			_parent.selection(_existing_selection);
		}
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::move;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		if (!_tracking)
		{
			_first_tic = platform::tick_count();
			_last_loc = _start_loc = loc;
			_device_start = _parent.device_selection();
			_existing_selection = _parent.selection();
			_tracking = true;
		}
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking)
		{
			_last_loc = loc;
			update(true);
		}
	}

	void update(bool tracking) const
	{
		const auto offset = _start_loc - _last_loc;
		auto rr = _device_start.round();

		if (_left) rr.left = std::min(rr.left - offset.x, rr.right - 1);
		if (_top) rr.top = std::min(rr.top - offset.y, rr.bottom - 1);
		if (_right) rr.right = std::max(rr.right - offset.x, rr.left + 1);
		if (_bottom) rr.bottom = std::max(rr.bottom - offset.y, rr.top + 1);

		/*if (_left) rr.left = rr.left - offset.cx;
		if (_top) rr.top = rr.top - offset.cy;
		if (_right) rr.right = rr.right - offset.cx;
		if (_bottom) rr.bottom = rr.bottom - offset.cy;*/

		// 0 2
		// 3 2
		int active_point = 0;
		if (_top && _right) active_point = 1;
		if (_bottom && _right) active_point = 2;
		if (_bottom && _left) active_point = 3;

		_parent.device_selection2(rr, active_point);
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		if (_tracking)
		{
			_tracking = false;
			_last_loc = loc;
			update(false);
		}
	}

	void escape() override
	{
		if (_tracking)
		{
			_tracking = false;
			_parent.selection(_existing_selection);
		}
	}
};

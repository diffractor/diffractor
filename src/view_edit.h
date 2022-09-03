// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model.h"
#include "ui_view.h"
#include "ui_controllers.h"
#include "ui_dialog.h"

class log_slider_control;
class edit_view;
class media_control;
class task_toolbar_control;
class rating_bar_control;


class edit_view_controls final : public view_host, public std::enable_shared_from_this<edit_view_controls>
{
public:
	view_state& _state;
	edit_view_state& _edit_state;
	std::shared_ptr<edit_view> _view;
	view_scroller _scroller;
	ui::control_frame_ptr _dlg;
	ui::frame_ptr _frame;
	std::vector<view_element_ptr> _controls;
	ui::color _clr = ui::style::color::dialog_text;
	long _layout_height = 0;
	long _layout_width = 0;
	ui::coll_widths _label_width;

	std::shared_ptr<ui::edit_control> _title_edit;
	std::shared_ptr<text_element> _description_label;
	std::shared_ptr<ui::multi_line_edit_control> _description_edit;
	std::shared_ptr<text_element> _comment_label;
	std::shared_ptr<ui::multi_line_edit_control> _comment_edit;
	std::shared_ptr<text_element> _staring_label;
	std::shared_ptr<ui::multi_line_edit_control> _staring_edit;
	std::shared_ptr<text_element> _staring_help;
	std::shared_ptr<text_element> _tags_label;
	std::shared_ptr<ui::multi_line_edit_control> _tags_edit;
	std::shared_ptr<text_element> _tags_help1;
	std::shared_ptr<text_element> _tags_help2;
	std::shared_ptr<rating_bar_control> _raiting_control;
	std::shared_ptr<ui::edit_control> _show_edit;
	std::shared_ptr<ui::edit_control> _artist_edit;
	std::shared_ptr<ui::edit_control> _album_artist_edit;
	std::shared_ptr<ui::edit_control> _album_edit;
	std::shared_ptr<ui::edit_control> _genre_edit;
	std::shared_ptr<ui::num_control> _year_edit;
	std::shared_ptr<ui::num_pair_control> _episode_edit;
	std::shared_ptr<ui::num_control> _season_edit;
	std::shared_ptr<ui::num_pair_control> _track_edit;
	std::shared_ptr<ui::num_pair_control> _disk_edit;
	std::shared_ptr<ui::date_control> _created_control;
	std::shared_ptr<ui::title_control> _straighten_title;
	std::shared_ptr<ui::title_control> _metadata_title;
	std::shared_ptr<log_slider_control> _straighten_slider;
	std::shared_ptr<task_toolbar_control> _rotate_toolbar;
	std::shared_ptr<divider_element> _color_divider;
	std::shared_ptr<divider_element> _metadata_divider;
	std::shared_ptr<divider_element> _artist_divider;
	std::shared_ptr<ui::title_control> _color_title;
	std::shared_ptr<log_slider_control> _vibrance_slider;
	std::shared_ptr<log_slider_control> _darks_slider;
	std::shared_ptr<log_slider_control> _midtones_slider;
	std::shared_ptr<log_slider_control> _lights_slider;
	std::shared_ptr<log_slider_control> _contrast_slider;
	std::shared_ptr<log_slider_control> _brightness_slider;
	std::shared_ptr<log_slider_control> _saturation_slider;
	std::shared_ptr<task_toolbar_control> _color_toolbar;

	edit_view_controls(view_state& s, edit_view_state& es) : _state(s), _edit_state(es)
	{
		_scroller._scroll_child_controls = true;
	}

	void scroll_controls() override;
	void layout_controls(ui::measure_context& mc);
	void create_controls();

	void populate()
	{
		const view_element_event e{view_element_event_type::populate, shared_from_this()};

		for (const auto& c : _controls)
		{
			c->dispatch_event(e);
		}
	}

	void on_window_layout(ui::measure_context& mc, sizei extent, bool is_minimized) override;
	void on_window_paint(ui::draw_context& dc) override;

	void tick() override
	{
	}

	void init(const ui::control_frame_ptr& frame);

	void activate(bool is_active) override
	{
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	}

	const ui::frame_ptr frame() override
	{
		return _frame;
	}

	const ui::control_frame_ptr owner() override
	{
		return _dlg;
	}

	void invoke(const commands cmd) override
	{
		_state.invoke(cmd);
	}

	bool is_command_checked(const commands cmd) override
	{
		return _state.is_command_checked(cmd);
	}

	void controller_changed() override
	{
		_state.invalidate_view(view_invalid::tooltip);
	}

	void invalidate_element(const view_element_ptr& e) override
	{
		if (_frame)
		{
			_frame->invalidate();
		}
	}

	view_controller_ptr controller_from_location(const pointi loc) override
	{
		if (_scroller.can_scroll() && _scroller.scroll_bounds().contains(loc))
		{
			return std::make_shared<scroll_controller>(shared_from_this(), _scroller, _scroller.scroll_bounds());
		}

		const auto offset = -_scroller.scroll_offset();

		for (const auto& e : _controls)
		{
			auto controller = e->controller_from_location(shared_from_this(), loc, offset, {});
			if (controller) return controller;
		}

		return nullptr;
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
		_scroller.offset(shared_from_this(), 0, -(delta / 2));
		update_controller(loc);
	}

	void options_changed();
	void focus_changed(bool has_focus, const ui::control_base_ptr& child) override;
	void command_hover(const ui::command_ptr& c, recti window_bounds) override;

	void track_menu(const recti bounds, const std::vector<ui::command_ptr>& commands) override
	{
		_state.track_menu(_dlg, bounds, commands);
	}

	void invalidate_view(const view_invalid invalid) override
	{
		_state.invalidate_view(invalid);
	}
};

class edit_view final : public view_base
{
	using this_type = edit_view;

	view_state& _state;
	view_host_ptr _host;

	edit_view_state& _edit_state;
	std::shared_ptr<edit_view_controls> _edit_controls;

	sizei _extent;
	affined _image_transform;

	rectd _crop_bounds;
	rectd _crop_handle_tl;
	rectd _crop_handle_tr;
	rectd _crop_handle_bl;
	rectd _crop_handle_br;

	df::file_path _path;
	std::u8string_view _xmp_name;
	file_type_ref _mt = nullptr;
	file_load_result _loaded;
	ui::const_surface_ptr _preview_surface;
	ui::texture_ptr _texture;
	bool _invalid = true;

	friend class selection_move_controller<this_type>;
	friend class handle_move_controller<this_type>;

public:
	edit_view(view_state& s, view_host_ptr host, edit_view_state& evs, std::shared_ptr<edit_view_controls> evc);

	recti calc_media_bounds() const
	{
		return {0, 0, _extent.cx, _extent.cy};
	}

	ui::const_surface_ptr preview_surface() const;

	void activate(sizei extent) override;
	void deactivate() override;
	void layout(ui::measure_context& mc, sizei extent) override;
	bool is_photo() const;
	void changed();
	void cancel() const;
	void exit();
	void save_and_close();
	bool save(df::file_path src_path, df::file_path dst_path, std::u8string_view xmp_name,
	          const ui::control_frame_ptr& owner) const;
	bool has_changes() const;
	void preview(ui::const_surface_ptr surface);
	void render(ui::draw_context& dc, view_controller_ptr controller) override;
	bool can_exit() override;
	void display_changed() override;
	void draw_handle(ui::draw_context& dc, recti handle_bounds2, float alpha);
	pointi clamp_offset(sizei render_extent, pointi offset) const;

	bool check_path(df::file_path& path, const ui::control_frame_ptr& owner);
	void save_and_next(bool forward);
	void save_options() const;
	void save_as();

	void rotate_anticlockwise();
	void rotate_clockwise();
	void rotate_reset();
	void color_reset();

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	void device_selection2(const rectd sel_bounds_in, const int active_point)
	{
		const auto dims = _loaded.dimensions();
		auto sel = quadd(sel_bounds_in).transform(_image_transform.invert());
		sel = sel.crop(rectd(0, 0, dims.cx, dims.cy), active_point);
		selection(sel);
	}

	void device_selection(const rectd sel_bounds_in, bool crop, bool limit)
	{
		const auto dims = _loaded.dimensions();
		const auto limit_bounds = rectd(0, 0, dims.cx, dims.cy);
		auto sel = quadd(sel_bounds_in).transform(_image_transform.invert());
		if (crop) sel = sel.crop(limit_bounds);
		if (limit) sel = sel.limit(limit_bounds);
		selection(sel);
	}

	quadd selection() const
	{
		return _edit_state.selection();
	}

	void selection(const quadd& s)
	{
		_edit_state.selection(s);
		_state.invalidate_view(view_invalid::view_redraw);
	}

	rectd device_selection() const
	{
		const auto dims = _loaded.dimensions();
		const auto crop = _edit_state.selection();
		const auto draw_crop = crop.crop(rectd(0, 0, dims.cx, dims.cy));
		return draw_crop.transform(_image_transform).bounding_rect();
	}
};

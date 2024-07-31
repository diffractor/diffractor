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
#include "view_list.h"

class log_slider_control;
class edit_view;
class media_control;
class task_toolbar_control;
class rating_bar_control;


class edit_view_controls final : public view_controls_host
{
public:
	edit_view_state& _edit_state;
	std::shared_ptr<edit_view> _view;

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

	edit_view_controls(view_state& s, edit_view_state& es) : view_controls_host(s), _edit_state(es)
	{
		_scroller._scroll_child_controls = true;
	}

	void layout_controls(ui::measure_context& mc) override;
	void create_controls();

	void options_changed() override;
};

class edit_view final : public view_base, public std::enable_shared_from_this<edit_view>
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

	std::u8string _title;
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
	edit_view(view_state& s, view_host_ptr host, edit_view_state& evs);

	recti calc_media_bounds() const
	{
		return {0, 0, _extent.cx, _extent.cy};
	}

	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);
	ui::const_surface_ptr preview_surface() const;

	void activate(sizei extent) override;
	void deactivate() override;
	void refresh() override;

	void layout(ui::measure_context& mc, sizei extent) override;
	bool is_photo() const;
	void changed();
	void cancel() const;
	void exit() override;
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

	std::u8string_view title() override
	{
		const auto i = _state._edit_item;

		if (i)
		{
			_title = str::format(u8"{}: {} {}"sv, s_app_name, tt.editing_title, i->name());
		}
		else
		{
			_title = s_app_name;
		}

		return _title;
	}

	void options_changed()
	{
		if (_edit_controls)
		{
			_edit_controls->options_changed();
		}
	}
};

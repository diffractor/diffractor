// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Photo editing view. Implements pixel adjustments and comparison.

#pragma once

#include "model.h"
#include "ui_view.h"
#include "ui_controllers.h"
#include "ui_dialog.h"
#include "view_list.h"

class log_slider_control;
class edit_view;
class task_toolbar_control;
class rating_bar_control;

struct document_detection_result
{
	std::array<pointd, 4> corners{};
	sizei extent;
	double confidence = 0;

	explicit operator bool() const { return extent.cx > 0 && extent.cy > 0; }
};

// The straighten, perspective and crop that present a detected page upright. Expressed in the same
// controls the user already has, so the image morphs and the crop stays a rectangle they can adjust.
struct document_correction
{
	double straighten = 0;
	double perspective_horizontal = 0;
	double perspective_vertical = 0;
	quadd crop;

	explicit operator bool() const { return !crop.is_empty(); }
};

document_detection_result detect_document(const ui::const_surface_ptr& surface, sizei source_extent);
document_correction fit_document_correction(const std::array<pointd, 4>& corners, sizei extent,
                                            ui::orientation orientation);

class edit_view_controls final : public view_controls_host
{
public:
	edit_view_state& _edit_state;
	std::shared_ptr<edit_view> _view;

	std::shared_ptr<text_element> _info;
	std::shared_ptr<ui::title_control> _straighten_title;
	std::shared_ptr<log_slider_control> _straighten_slider;
	std::shared_ptr<log_slider_control> _perspective_horizontal_slider;
	std::shared_ptr<log_slider_control> _perspective_vertical_slider;
	std::shared_ptr<task_toolbar_control> _rotate_toolbar;
	std::shared_ptr<divider_element> _color_divider;
	std::shared_ptr<ui::title_control> _color_title;
	std::shared_ptr<log_slider_control> _vibrance_slider;
	std::shared_ptr<log_slider_control> _darks_slider;
	std::shared_ptr<log_slider_control> _midtones_slider;
	std::shared_ptr<log_slider_control> _lights_slider;
	std::shared_ptr<log_slider_control> _contrast_slider;
	std::shared_ptr<log_slider_control> _brightness_slider;
	std::shared_ptr<log_slider_control> _saturation_slider;
	std::shared_ptr<log_slider_control> _temperature_slider;
	std::shared_ptr<log_slider_control> _tint_slider;
	std::shared_ptr<task_toolbar_control> _color_toolbar;
	std::shared_ptr<divider_element> _save_divider;
	std::shared_ptr<ui::title_control> _save_title;
	std::shared_ptr<ui::check_control> _backup_check;
	std::shared_ptr<ui::slider_control> _jpeg_quality_slider;
	std::shared_ptr<ui::slider_control> _webp_quality_slider;
	std::shared_ptr<ui::check_control> _webp_lossless_check;

	edit_view_controls(view_state& s, edit_view_state& es) : view_controls_host(s), _edit_state(es)
	{
		_scroller._scroll_child_controls = true;
	}

	void layout_controls(ui::measure_context& mc) override;
	void create_controls();
	bool is_tracking() const;

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

	std::string _title;
	df::file_path _path;
	std::string_view _xmp_name;
	file_type_ref _mt = nullptr;
	file_load_result _loaded;
	ui::const_surface_ptr _preview_source;
	ui::const_surface_ptr _dialog_preview_source;
	ui::texture_ptr _texture;
	display_state_ptr _media_display;
	view_element_ptr _media_element;
	view_element_ptr _play_element;
	view_element_ptr _scrubber_element;
	size_t _display_generation = 0;
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
	static ui::const_surface_ptr build_preview_surface(const ui::const_surface_ptr& source, sizei loaded_dimensions,
	                                                   const image_edits& edits);
	ui::const_surface_ptr preview_surface() const;

	void activate(sizei extent) override;
	void deactivate() override;
	void refresh() override;
	void update_media_elements() override;

	void layout(ui::measure_context& mc, sizei extent) override;
	bool is_photo() const;
	void changed();
	void cancel() const;
	void exit() override;
	bool escape() override;
	void save_current();
	void save(df::file_path src_path, df::file_path dst_path, std::string_view xmp_name,
	          const ui::control_frame_ptr& owner, std::function<void(bool)> complete) const;
	bool has_changes() const;
	void select_item(const df::item_element_ptr& item);
	void render(ui::draw_context& dc, view_controller_ptr controller) override;
	bool can_exit() override;
	void display_changed() override;
	static void draw_handle(ui::draw_context& dc, recti handle_bounds2, float alpha);

	bool check_path(df::file_path& path, const ui::control_frame_ptr& owner) const;
	df::item_element_ptr next_editable_item(bool forward) const;
	void save_and_next(bool forward);
	void save_as();

	void rotate_anticlockwise();
	void rotate_clockwise();
	void rotate_reset();
	void color_reset();
	void report_no_result(std::string_view title) const;
	void queue_auto_adjust(int max_dimension, std::string title,
	                       std::function<std::function<void(edit_view_state&)>(const ui::const_surface_ptr&)> analyze);
	void auto_color();
	void auto_straighten();
	void auto_document();
	void toggle_preview();

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	void device_selection2(const rectd& sel_bounds_in, const int active_point)
	{
		const auto dims = _loaded.dimensions();
		auto sel = quadd(sel_bounds_in).transform(_image_transform.invert());
		sel = sel.crop(rectd(0, 0, dims.cx, dims.cy), active_point);
		selection(sel);
	}

	void device_selection(const rectd& sel_bounds_in, const bool crop, const bool limit)
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
		return _edit_state._edits.effective_crop_bounds(_loaded.dimensions());
	}

	void selection(const quadd& s) const
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

	std::string_view title() override
	{
		const auto i = _state._edit_item;

		if (i)
		{
			_title = std::format("{}: {}", s_app_name, tt.editing_title);
		}
		else
		{
			_title = s_app_name;
		}

		return _title;
	}

	void options_changed() const
	{
		if (_edit_controls)
		{
			_edit_controls->options_changed();
		}
	}
};

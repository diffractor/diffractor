// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "model_index.h"
#include "view_edit.h"
#include "app_text.h"
#include "files.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class log_slider_control final : public view_element, public std::enable_shared_from_this<log_slider_control>
{
private:
	ui::trackbar_ptr _slider;
	ui::edit_ptr _edit;
	std::u8string _label;

	double& _val;
	double _min;
	double _max;

	bool _tracking;
	std::function<void()> _changed;

	constexpr static int Range = 100;

public:
	log_slider_control(ui::control_frame_ptr& h, const std::u8string_view text, double& v, double min, double max,
	                   std::function<void()> changed) noexcept :
		_label(text),
		_val(v),
		_min(min),
		_max(max),
		_tracking(false),
		_changed(std::move(changed))
	{
		ui::edit_styles style;
		style.number = true;
		style.align_center = true;
		_edit = h->create_edit(style, {}, [this](const std::u8string_view text) { edit_change(text); });
		_slider = h->create_slider(-Range, Range,
		                           [this](int pos, bool is_tracking) { slider_change(pos, is_tracking); });
	}

	void label(const std::u8string_view label)
	{
		_label = label;
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_slider);
		handler(_edit);
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::populate)
		{
			update_slider();
			update_edit();
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (!str::is_empty(_label))
		{
			const auto label_extent = mc.measure_text(_label, ui::style::font_face::dialog,
			                                          ui::style::text_style::single_line, width_limit);
			mc.col_widths.c1 = std::max(mc.col_widths.c1, label_extent.cx);
		}

		return {width_limit, default_control_height(mc)};
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;

		const auto num_extent = mc.measure_text(u8"00.00"sv, ui::style::font_face::dialog, ui::style::text_style::single_line, bounds.width());
		auto slider_bounds = bounds;
		auto edit_bounds = bounds;
		const auto split = bounds.right - (num_extent.cx + (mc.padding2 * 2));

		edit_bounds.left = split + mc.padding1;
		slider_bounds.right = split - mc.padding1;

		if (!str::is_empty(_label)) slider_bounds.left += mc.col_widths.c1;

		positions.emplace_back(_slider, slider_bounds, is_visible());
		positions.emplace_back(_edit, edit_bounds, is_visible());
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (!str::is_empty(_label))
		{
			const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha);
			auto r = bounds.offset(element_offset);
			r.right = r.left + dc.col_widths.c1;
			dc.draw_text(_label, r, ui::style::font_face::dialog, ui::style::text_style::single_line, text_color, {});
		}
	}

	int value_to_slider(double a) const
	{
		const auto d = a / _max;
		return df::round(sqrt(fabs(d)) * (a < 0 ? -Range : Range));
	}

	double slider_to_value(const double s) const
	{
		const auto d = s / Range;
		return d * d * (s < 0 ? _min : _max);
	}

	void update_edit()
	{
		const auto actual = _edit->window_text();
		const auto expected = str::format(u8"{:.2}"sv, _val);

		if (actual.empty() || actual != expected)
		{
			_edit->window_text(expected);
		}
	}

	void update_slider()
	{
		if (!df::equiv(_val, slider_to_value(_slider->get_pos())))
		{
			_slider->SetPos(value_to_slider(_val));
		}
	}

	void edit_change(const std::u8string_view text)
	{
		_val = str::to_double(text);
		update_slider();
		_changed();
	}

	void slider_change(int pos, bool is_tracking)
	{
		_tracking = is_tracking;
		_val = slider_to_value(pos);
		update_edit();
		_changed();
	}

	bool is_tracking() const
	{
		return _tracking;
	}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class task_toolbar_control final : public view_element, public std::enable_shared_from_this<task_toolbar_control>
{
	ui::toolbar_ptr _tb;

public:
	task_toolbar_control(const ui::control_frame_ptr& h, const std::vector<ui::command_ptr>& buttons)
	{
		_tb = h->create_toolbar({}, buttons);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, default_control_height(mc)  };
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_tb);
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		const auto extent = _tb->measure_toolbar(bounds.width());
		const auto r = center_rect(extent, bounds);
		positions.emplace_back(_tb, r, is_visible());
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class edit_rating_control final : public std::enable_shared_from_this<edit_rating_control>, public ui::frame_host
{
public:
	bool _hover = false;
	int _rating = -1;
	int _hover_rating = -1;
	ui::edit_ptr _edit;
	ui::frame_ptr _frame;
	sizei _extent;

	const int cell_width = 18;

public:
	edit_rating_control(ui::edit_ptr edit) : _edit(std::move(edit))
	{
	}

	void init(const ui::control_frame_ptr& owner)
	{
		_frame = owner->create_frame(weak_from_this(), {});
	}

	void on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized) override
	{
		_extent = extent;
	}

	void on_mouse_move(const pointi loc, const bool is_tracking) override
	{
		if (!_hover)
		{
			_hover = true;
			_frame->invalidate();
		}

		const auto hr = point_to_stars(loc);

		if (hr != _hover_rating)
		{
			_hover_rating = hr;
			_frame->invalidate();
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		const auto star = point_to_stars(loc);

		_rating = star;

		if (_edit)
		{
			_edit->window_text(_rating < 1 ? std::u8string_view{} : str::to_string(_rating));
		}

		_frame->invalidate();
	}

	void on_mouse_leave(const pointi loc) override
	{
		if (_hover)
		{
			_hover = false;
			_hover_rating = -1;
			_frame->invalidate();
		}
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
	}

	void tick() override
	{
	}

	void activate(bool is_active) override
	{
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	}

	void on_window_paint(ui::draw_context& dc) override
	{
		dc.clear(dc.colors.background);

		const recti logical_bounds = _extent;
		const auto bg = _hover
			                ? view_handle_color(false, _hover, false, dc.frame_has_focus, true).aa(dc.colors.alpha)
			                : ui::color{};
		const auto rating = _hover ? _hover_rating : _rating;
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		wchar_t text[6] = {};

		for (auto i = 0; i < 5; i++)
		{
			text[i] = static_cast<wchar_t>(i < rating ? icon_index::star_solid : icon_index::star);
		}

		dc.draw_text(str::utf16_to_utf8(text), logical_bounds, ui::style::font_face::icons,
		             ui::style::text_style::single_line, clr, bg);
	}

	void rating(int stars)
	{
		if (_rating != stars)
		{
			_rating = stars;
			if (_frame) _frame->invalidate();
		}
	}

	int point_to_stars(const pointi loc) const
	{
		return 1 + (loc.x / cell_width);
	}
};

using edit_rating_control_ptr = std::shared_ptr<edit_rating_control>;

class rating_bar_control final : public view_element, public std::enable_shared_from_this<rating_bar_control>
{
private:
	edit_rating_control_ptr _stars;
	ui::edit_ptr _edit;
	std::u8string _label;
	int& _val;

public:
	rating_bar_control(const ui::control_frame_ptr& owner, std::u8string_view label, int& v) : _label(label), _val(v)
	{
		ui::edit_styles style;
		style.number = true;
		style.align_center = true;
		_edit = owner->create_edit(style, {}, [this](const std::u8string_view text) { edit_change(text); });
		_stars = std::make_shared<edit_rating_control>(_edit);
		_stars->init(owner);
	}

	void label(const std::u8string_view label)
	{
		_label = label;
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_stars->_frame);
		handler(_edit);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (!str::is_empty(_label))
		{
			const auto label_extent = mc.measure_text(_label, ui::style::font_face::dialog,
			                                          ui::style::text_style::single_line, width_limit);
			mc.col_widths.c1 = std::max(mc.col_widths.c1, label_extent.cx + mc.padding2);
		}

		return {width_limit, default_control_height(mc)};
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		auto rStars = bounds;
		auto rEdit = bounds;

		if (!str::is_empty(_label)) rEdit.left += mc.col_widths.c1;
		rEdit.right = rEdit.left + 50;
		rStars.left = rEdit.right + 8;

		positions.emplace_back(_edit, rEdit, is_visible());
		positions.emplace_back(_stars->_frame, rStars);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (!str::is_empty(_label))
		{
			auto r = bounds.offset(element_offset);
			r.right = r.left + dc.col_widths.c1 - dc.padding2;
			dc.draw_text(_label, r, ui::style::font_face::dialog, ui::style::text_style::single_line,
			             {dc.colors.foreground, dc.colors.alpha}, {});
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::populate)
		{
			_edit->window_text(str::to_string(_val));
			_stars->rating(_val);
		}
	}

	void edit_change(const std::u8string_view text)
	{
		_val = std::clamp(str::to_int(text), 0, 5);
		_stars->rating(_val);
	}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class save_buttons_control final : public view_element, public std::enable_shared_from_this<save_buttons_control>
{
	ui::button_ptr _ok;
	ui::button_ptr _abort;
	ui::button_ptr _cancel;

public:
	save_buttons_control(const ui::control_frame_ptr& h,
	                     std::function<void()> fn_save,
	                     std::function<void()> fn_dont,
	                     std::function<void()> fn_cancel)
	{
		_ok = h->create_button(tt.button_save, std::move(fn_save), true);
		_abort = h->create_button(tt.button_dont_save, std::move(fn_dont));
		_cancel = h->create_button(tt.button_cancel, std::move(fn_cancel));
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_ok);
		handler(_abort);
		handler(_cancel);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, mc.text_line_height(ui::style::font_face::dialog) + mc.padding2 * 4 };
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		const auto width = bounds.width() / 3;

		auto rok = bounds;
		auto rab = bounds;
		auto rcan = bounds;

		rab.left = rok.right = rok.left + width;
		rab.right = rcan.left = rcan.right - width;

		positions.emplace_back(_ok, rok.inflate(-mc.padding1), is_visible());
		positions.emplace_back(_abort, rab.inflate(-mc.padding1), is_visible());
		positions.emplace_back(_cancel, rcan.inflate(-mc.padding1), is_visible());
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class preview_builder : df::no_copy
{
private:
	// Source
	ui::const_surface_ptr _surface;
	const image_edits _edits;
	const sizei _extent;
	const sizei _image_dimensions;

	// Results
	ui::const_surface_ptr _preview_surface;

public:
	preview_builder(ui::const_surface_ptr surface, const image_edits& edits, const sizei client_extent) :
		_surface(std::move(surface)),
		_edits(edits),
		_extent(client_extent),
		_image_dimensions(_surface->dimensions())
	{
	}

	pointi clamp_offset(const sizei render_extent, const pointi offset) const
	{
		const recti avail_bounds(0, 0, _extent.cx, _extent.cy);

		const auto xx = std::max(render_extent.cx - avail_bounds.width(), 0) / 2;
		const auto yy = std::max(render_extent.cy - avail_bounds.height(), 0) / 2;

		const recti limit(-xx, -yy, xx, yy);
		return offset.clamp(limit);
	}

	void build(df::cancel_token token)
	{
		auto dst = std::make_shared<ui::surface>();
		ui::color_adjust adjust;
		adjust.color_params(_edits.vibrance(),
		                    _edits.saturation(),
		                    _edits.darks(),
		                    _edits.midtones(),
		                    _edits.lights(),
		                    _edits.contrast(),
		                    _edits.brightness());

		const auto dims_out = _surface->dimensions();
		dst->alloc(dims_out.cx, dims_out.cy, _surface->format());
		adjust.apply(_surface, dst->pixels(), dst->stride(), token);

		if (!token.is_cancelled())
		{
			_preview_surface = std::move(dst);
		}
	}

	void complete(edit_view& parent) const
	{
		parent.preview(_preview_surface);
	}
};

edit_view::edit_view(view_state& s, view_host_ptr host, edit_view_state& evs, std::shared_ptr<edit_view_controls> evc) :
	_state(s),
	_host(std::move(host)),
	_edit_state(evs),
	_edit_controls(std::move(evc))
{
}

ui::const_surface_ptr edit_view::preview_surface() const
{
	return _preview_surface;
}

void edit_view::activate(const sizei extent)
{
	_extent = extent;

	if (_edit_controls->_controls.empty())
	{
		_edit_controls->create_controls();
	}

	display_changed();

	_state.stop();
	changed();
	_host->frame()->invalidate();
	_host->frame()->layout();
}

void edit_view::deactivate()
{
	_preview_surface.reset();
	_loaded.clear();
}

void edit_view::layout(ui::measure_context& mc, const sizei extent)
{
	_extent = extent;
}

bool edit_view::is_photo() const
{
	return !_loaded.is_empty() && _mt->has_trait(file_traits::bitmap);
}

view_controller_ptr edit_view::controller_from_location(const view_host_ptr& host, const pointi loc)
{
	if (is_photo())
	{
		if (_crop_handle_tl.round().inflate(8).contains(loc))
		{
			return std::make_shared<handle_move_controller<edit_view>>(host, *this, _crop_handle_tl, true, true, false,
			                                                           false);
		}

		if (_crop_handle_tr.round().inflate(8).contains(loc))
		{
			return std::make_shared<handle_move_controller<edit_view>>(host, *this, _crop_handle_tr, false, true, true,
			                                                           false);
		}

		if (_crop_handle_bl.round().inflate(8).contains(loc))
		{
			return std::make_shared<handle_move_controller<edit_view>>(host, *this, _crop_handle_bl, true, false, false,
			                                                           true);
		}

		if (_crop_handle_br.round().inflate(8).contains(loc))
		{
			return std::make_shared<handle_move_controller<edit_view>>(host, *this, _crop_handle_br, false, false, true,
			                                                           true);
		}

		if (_crop_bounds.contains(loc))
		{
			auto crop_bounds = _crop_bounds.round();
			const auto crop_center = _crop_bounds.center().round();

			crop_bounds.exclude(crop_center, _crop_handle_tl.round());
			crop_bounds.exclude(crop_center, _crop_handle_tr.round());
			crop_bounds.exclude(crop_center, _crop_handle_bl.round());
			crop_bounds.exclude(crop_center, _crop_handle_br.round());

			return std::make_shared<selection_move_controller<edit_view>>(host, *this, crop_bounds);
		}
	}

	return nullptr;
}

bool select_path(df::file_path& path)
{
	if (!files::can_save(path))
	{
		path = path.extension(u8".jpg"sv);
	}

	return platform::prompt_for_save_path(path);
}

std::u8string format_invalid_name_message(const std::u8string_view name)
{
	const auto name_error = str::format(tt.error_invalid_path_fmt, name);
	return str::format(u8"{}\n{} \\ / : * ? \" < > |"sv, name_error, tt.error_invalid_path);
}

bool edit_view::check_path(df::file_path& path, const ui::control_frame_ptr& owner)
{
	if (!path.is_save_valid())
	{
		const auto dlg = make_dlg(owner);
		const auto message = format_invalid_name_message(path.name());
		dlg->show_message(icon_index::error, s_app_name, message);
		return false;
	}

	if (is_photo())
	{
		const auto dlg = make_dlg(owner);
		const auto dimensions = _loaded.dimensions();

		if (_edit_state._edits.has_changes(dimensions) && !files::can_save(path))
		{
			const std::vector<view_element_ptr> controls{
				set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::save, tt.save_changes,
				                                                str::format(tt.save_as_jpeg_fmt, path.extension()),
				                                                std::vector<ui::const_surface_ptr>{preview_surface()})),
				std::make_shared<divider_element>(),
				std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_save_as_jpeg)
			};

			const auto result = dlg->show_modal(controls);

			if (result == ui::close_result::ok)
			{
				path = path.extension(u8".jpg"sv);
			}
			else
			{
				return false;
			}
		}
	}


	return true;
}

bool edit_view::can_exit()
{
	if (has_changes())
	{
		auto dlg = make_dlg(_host->owner());

		auto save_fn = [dlg, this]()
		{
			auto path = _path;
			const auto xmp_name = _xmp_name;
			if (check_path(path, dlg->_frame)) save(_path, path, xmp_name, dlg->_frame);
			dlg->close(false);
			_state.view_mode(view_type::items);
		};
		auto cancel_fn = [dlg, this]()
		{
			dlg->close(false);
			cancel();
		};

		const std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::save, tt.changes,
			                                                format(tt.has_changes, _path.name()),
			                                                std::vector<ui::const_surface_ptr>{preview_surface()})),
			std::make_shared<divider_element>(),
			std::make_shared<save_buttons_control>(dlg->_frame,
			                                       save_fn,
			                                       cancel_fn,
			                                       [dlg]() { dlg->close(true); })
		};

		dlg->show_modal(controls);
	}

	return !has_changes();
}

void edit_view::save_and_close()
{
	auto path = _path;
	const auto xmp_name = _xmp_name;
	const auto owner = _host->owner();

	if (check_path(path, owner) && save(_path, path, xmp_name, owner))
	{
		_state.view_mode(view_type::items);
	}
}

static prop::item_metadata_ptr safe_metadata(const df::file_item_ptr& i)
{
	if (i)
	{
		const auto md = i->metadata();
		if (md) return md;
	}

	return std::make_shared<prop::item_metadata>();
}

bool edit_view::has_changes() const
{
	const auto metadata_edits = _edit_state.build_metadata_edits(safe_metadata(_state._edit_item));

	if (is_photo())
	{
		const auto dimensions = _loaded.dimensions();
		return _edit_state._edits.has_changes(dimensions) || metadata_edits.has_changes();
	}

	return metadata_edits.has_changes();
}

bool edit_view::save(const df::file_path src_path, const df::file_path dst_path, const std::u8string_view xmp_name,
                     const ui::control_frame_ptr& owner) const
{
	platform::file_op_result update_result;

	auto dlg = make_dlg(owner);
	dlg->show_status(icon_index::save, format(tt.saving_file_name, dst_path.name()));

	detach_file_handles detach(_state);
	platform::thread_event event_wait(true, false);

	const auto me = _edit_state.build_metadata_edits(safe_metadata(_state._edit_item));
	const auto pe = _edit_state._edits;
	const auto dimensions = _loaded.dimensions();

	_state.queue_async(async_queue::work,
	                   [&update_result, &event_wait, dst_path, me, pe, &s = _state, src_path, i = _state._edit_item,
		                   dimensions, xmp_name]()
	                   {
		                   files ff;

		                   file_encode_params encode_params;
		                   encode_params.jpeg_save_quality = setting.jpeg_save_quality;
		                   encode_params.webp_lossless = setting.webp_quality;
		                   encode_params.webp_quality = setting.webp_lossless;

		                   const auto create_original = src_path == dst_path && setting.create_originals && pe.
			                   has_changes(dimensions);
		                   update_result = ff.update(src_path, dst_path, me, pe, encode_params, create_original,
		                                             xmp_name);

		                   df::item_set set;
		                   set.add(i);
		                   s.item_index.queue_scan_modified_items(set);
		                   event_wait.set();
	                   });

	platform::wait_for({event_wait}, 10000, false);

	if (update_result.success())
	{
		if (_mt->has_trait(file_traits::photo_metadata))
		{
			if (me.has_changes())
			{
				record_feature_use(features::edit_photo_metadata);
			}

			if (pe.has_changes(_loaded.dimensions()))
			{
				record_feature_use(features::edit_photo_bitmap);
			}
		}
		else if (_mt->has_trait(file_traits::video_metadata))
		{
			record_feature_use(features::edit_video_metadata);
		}
		else if (_mt->has_trait(file_traits::music_metadata))
		{
			record_feature_use(features::edit_audio_metadata);
		}
	}
	else
	{
		dlg->show_message(icon_index::error, s_app_name,
		                  update_result.format_error(str::format(tt.error_create_file_failed_fmt, dst_path)));
	}

	return update_result.success();
}


void edit_view::save_and_next(bool forward)
{
	auto path = _path;
	const auto xmp_name = _xmp_name;

	if (has_changes())
	{
		const auto owner = _host->owner();

		if (!check_path(path, owner) || !save(_path, path, xmp_name, owner))
			return;
	}

	const auto i = _state.next_item(forward, false);

	if (i)
	{
		_state.select(_host, i, false, false, false);
		_state.item_index.queue_scan_modified_items(_state.selected_items());
	}
}

void edit_view::save_options() const
{
	const auto dlg = make_dlg(_host->owner());
	const std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::settings, tt.options_save_options,
		                                                std::u8string{})),
		std::make_shared<divider_element>(),
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_backup_copy, setting.create_originals),
		set_margin(std::make_shared<text_element>(tt.options_jpeg_quality)),
		set_margin(std::make_shared<ui::slider_control>(dlg->_frame, setting.jpeg_save_quality, 0, 100)),
		std::make_shared<divider_element>(),
		set_margin(std::make_shared<text_element>(tt.options_webp_quality)),
		set_margin(std::make_shared<ui::slider_control>(dlg->_frame, setting.webp_quality, 1, 100)),
		set_margin(std::make_shared<ui::check_control>(dlg->_frame, tt.lossless_compression, setting.webp_lossless)),
		std::make_shared<divider_element>(),
		std::make_shared<ui::close_control>(dlg->_frame)
	};

	dlg->show_modal(controls, { 44 });
}

void edit_view::save_as()
{
	auto path = _path;
	const auto xmp_name = _xmp_name;

	while (path.exists())
	{
		auto name = std::u8string(path.file_name_without_extension()) + u8"-edit"s;
		path = df::file_path(path.folder(), name, path.extension());
	}

	const auto owner = _host->owner();

	if (select_path(path) &&
		check_path(path, owner) &&
		save(_path, path, xmp_name, owner))
	{
		_state.view_mode(view_type::items);
		_state.open(_host, path);
		_state.item_index.queue_scan_modified_items(_state.selected_items());
	}
}


void edit_view::cancel() const
{
	_edit_state._edits.clear();
	_state.view_mode(view_type::items);
}

void edit_view::exit()
{
	if (_state.view_mode() == view_type::edit)
	{
		if (can_exit())
		{
			if (_state.view_mode() == view_type::edit)
			{
				cancel();
			}
		}
	}
}

void edit_view::rotate_anticlockwise()
{
	const auto selection = _edit_state._edits.crop_bounds();
	_edit_state._edits.crop_bounds(selection.transform(simple_transform::rot_270));
	changed();
}

void edit_view::rotate_clockwise()
{
	const auto selection = _edit_state._edits.crop_bounds();
	_edit_state._edits.crop_bounds(selection.transform(simple_transform::rot_90));
	changed();
}

void edit_view::rotate_reset()
{
	_edit_state._edits.crop_bounds(recti(pointi(0, 0), _loaded.dimensions()));
	_edit_state._straighten = 0;
	_edit_controls->populate();
	changed();
}

void edit_view::color_reset()
{
	_edit_state.color_reset();
	_edit_controls->populate();
	changed();
}

void edit_view::changed()
{
	if (_mt && _edit_controls->_straighten_slider)
	{
		_edit_state.changed(_extent, _edit_controls->_straighten_slider->is_tracking());

		if (_mt->has_trait(file_traits::bitmap))
		{
			if (_edit_state._edits.has_color_changes())
			{
				const auto surface = _loaded.to_surface();

				if (is_valid(surface))
				{
					auto builder = std::make_shared<preview_builder>(surface, _edit_state._edits, _extent);

					static std::atomic_int version;
					df::cancel_token token(version);

					_state.queue_async(async_queue::render, [this, token, builder, &s = _state]()
					{
						builder->build(token);

						if (!token.is_cancelled()) // reduce memory
						{
							s.queue_ui([this, token, builder]()
							{
								builder->complete(*this);
							});
						}
					});
				}
			}
			else
			{
				_preview_surface.reset();
				_invalid = true;
				_host->frame()->invalidate();
			}
		}
		else
		{
			_preview_surface.reset();
			_invalid = true;
			_host->frame()->invalidate();
		}
	}
}


static std::u8string fix_crlf(const std::u8string_view s)
{
	std::u8string result;
	result = str::replace(s, u8"\r\n"sv, u8"\n"sv);
	result = str::replace(result, u8"\n"sv, u8"\r\n"sv);
	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void edit_view_controls::scroll_controls()
{
	if (_frame)
	{
		_frame->layout();
	}
}

void edit_view_controls::layout_controls(ui::measure_context& mc)
{
	if (!_controls.empty())
	{
		const auto item = _state._edit_item;
		const auto* const ft = item ? item->file_type() : file_type::other;
		const auto is_bitmap = ft->has_trait(file_traits::bitmap);
		const auto has_music_metadata = ft->has_trait(file_traits::music_metadata);
		const auto has_video_metadata = ft->has_trait(file_traits::video_metadata);
		const auto has_photo_metadata = ft->has_trait(file_traits::photo_metadata);

		_title_edit->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_description_label->is_visible(has_photo_metadata || has_video_metadata);
		_description_edit->is_visible(has_photo_metadata || has_video_metadata);
		_comment_label->is_visible(has_video_metadata || has_music_metadata);
		_comment_edit->is_visible(has_video_metadata || has_music_metadata);
		_staring_label->is_visible(has_video_metadata || has_music_metadata);
		_staring_edit->is_visible(has_video_metadata || has_music_metadata);
		_staring_help->is_visible(has_video_metadata || has_music_metadata);
		_show_edit->is_visible(has_video_metadata || has_music_metadata);
		_artist_divider->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_artist_edit->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_album_artist_edit->is_visible(has_music_metadata);
		_album_edit->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_genre_edit->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_genre_edit->auto_completes(tt.add_translate_text(_state.item_index.distinct_genres()));
		_year_edit->is_visible(has_music_metadata);
		_episode_edit->is_visible(has_video_metadata);
		_season_edit->is_visible(has_video_metadata);
		_track_edit->is_visible(has_music_metadata);
		_disk_edit->is_visible(has_music_metadata);
		_tags_label->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_tags_edit->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_tags_help1->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_tags_help2->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_raiting_control->is_visible(has_photo_metadata || has_video_metadata || has_music_metadata);
		_created_control->is_visible(has_photo_metadata);
		_straighten_title->is_visible(is_bitmap);
		_straighten_slider->is_visible(is_bitmap);
		_rotate_toolbar->is_visible(is_bitmap);
		_color_divider->is_visible(is_bitmap);
		_metadata_divider->is_visible(is_bitmap);
		_color_title->is_visible(is_bitmap);
		_vibrance_slider->is_visible(is_bitmap);
		_darks_slider->is_visible(is_bitmap);
		_midtones_slider->is_visible(is_bitmap);
		_lights_slider->is_visible(is_bitmap);
		_contrast_slider->is_visible(is_bitmap);
		_brightness_slider->is_visible(is_bitmap);
		_saturation_slider->is_visible(is_bitmap);
		_color_toolbar->is_visible(is_bitmap);

		const auto layout_padding = mc.padding1;
		auto avail_bounds = recti(_extent);
		avail_bounds.right -= mc.scroll_width + layout_padding;

		ui::control_layouts positions;
		const auto height = stack_elements(mc, positions, avail_bounds, _controls, false,
		                                   {layout_padding, layout_padding});

		_layout_height = height;
		_layout_width = avail_bounds.width();
		_label_width = mc.col_widths;

		const recti scroll_bounds{_extent.cx - mc.scroll_width, 0, _extent.cx, _extent.cy};
		const recti client_bounds{0, 0, _extent.cx - mc.scroll_width, _extent.cy};
		_scroller.layout({_layout_width, _layout_height}, client_bounds, scroll_bounds);

		_dlg->apply_layout(positions, -_scroller.scroll_offset());
		_dlg->invalidate();
	}
}

void edit_view_controls::init(const ui::control_frame_ptr& owner)
{
	_frame = _dlg = owner->create_dlg(shared_from_this(), false);
}

void edit_view_controls::options_changed()
{
	if (!_controls.empty())
	{
		for (const auto& c : _controls)
		{
			c->visit_controls([](const ui::control_base_ptr& cc) { cc->options_changed(); });
		}

		_straighten_title->text(tt.straighten);
		_color_title->text(tt.color);
		_vibrance_slider->label(tt.vibrance);
		_darks_slider->label(tt.darks);
		_midtones_slider->label(tt.midtones);
		_lights_slider->label(tt.lights);
		_contrast_slider->label(tt.contrast);
		_brightness_slider->label(tt.brightness);
		_saturation_slider->label(tt.saturation);
		_metadata_title->text(tt.metadata);
		_title_edit->label(tt.prop_name_title);
		_description_label->text(tt.prop_name_description);
		_comment_label->text(tt.prop_name_comment);
		_staring_label->text(tt.staring);
		_staring_help->text(tt.help_artist);
		_tags_label->text(tt.tags_title);
		_tags_help1->text(tt.help_tag1);
		_tags_help2->text(tt.help_tag2);
		_show_edit->label(tt.prop_name_show);
		_artist_edit->label(tt.prop_name_artist);
		_album_artist_edit->label(tt.album_artist);
		_album_edit->label(tt.prop_name_album);
		_genre_edit->label(tt.prop_name_genre);
		_year_edit->label(tt.prop_name_year);
		_episode_edit->label(tt.prop_name_episode);
		_season_edit->label(tt.prop_name_season);
		_track_edit->label(tt.prop_name_track);
		_disk_edit->label(tt.prop_name_disk);
		_raiting_control->label(tt.prop_name_rating);
		_created_control->label(tt.prop_name_created);
	}
}

void edit_view_controls::focus_changed(const bool has_focus, const ui::control_base_ptr& child)
{
	if (child && child->has_focus())
	{
		auto point_offset = _scroller.scroll_offset();
		const auto rc = child->window_bounds().offset(point_offset - _dlg->window_bounds().top_left());

		if (rc.top < point_offset.y)
		{
			point_offset.y = rc.top;
		}
		else if (rc.bottom > (point_offset.y + _extent.cy))
		{
			point_offset.y = rc.bottom - _extent.cy;
		}

		_scroller.scroll_offset(shared_from_this(), 0, point_offset.y);
	}
}

void edit_view_controls::command_hover(const ui::command_ptr& c, const recti window_bounds)
{
	_state.command_hover(c, window_bounds);
}

void edit_view_controls::create_controls()
{
	const std::vector<ui::command_ptr> rotate_butons =
	{
		std::make_shared<ui::command>(icon_index::rotate_anticlockwise, commands::tool_rotate_anticlockwise,
		                              [this]() { _view->rotate_anticlockwise(); }),
		std::make_shared<ui::command>(icon_index::rotate_clockwise, commands::tool_rotate_clockwise,
		                              [this]() { _view->rotate_clockwise(); }),
		std::make_shared<ui::command>(icon_index::undo, commands::tool_rotate_reset,
		                              [this]() { _view->rotate_reset(); }),
	};

	const std::vector<ui::command_ptr> color_buttons =
	{
		std::make_shared<ui::command>(icon_index::undo, commands::edit_item_color_reset,
		                              [this]() { _view->color_reset(); })
	};


	auto changed_func = [this] { _view->changed(); };

	_straighten_title = std::make_shared<ui::title_control>(icon_index::rotate_clockwise, tt.straighten);
	_straighten_slider = std::make_shared<log_slider_control>(_dlg, std::u8string_view{}, _edit_state._straighten,
	                                                          -44.9, 44.9, changed_func);
	_rotate_toolbar = std::make_shared<task_toolbar_control>(_dlg, rotate_butons);
	_color_divider = std::make_shared<divider_element>();
	_artist_divider = std::make_shared<divider_element>();
	_color_title = std::make_shared<ui::title_control>(icon_index::color, tt.color);
	_vibrance_slider = std::make_shared<log_slider_control>(_dlg, tt.vibrance, _edit_state._vibrance, -1, 1,
	                                                        changed_func);
	_darks_slider = std::make_shared<log_slider_control>(_dlg, tt.darks, _edit_state._darks, -1, 1, changed_func);
	_midtones_slider = std::make_shared<log_slider_control>(_dlg, tt.midtones, _edit_state._midtones, -1, 1,
	                                                        changed_func);
	_lights_slider = std::make_shared<log_slider_control>(_dlg, tt.lights, _edit_state._lights, -1, 1, changed_func);
	_contrast_slider = std::make_shared<log_slider_control>(_dlg, tt.contrast, _edit_state._contrast, -1, 1,
	                                                        changed_func);
	_brightness_slider = std::make_shared<log_slider_control>(_dlg, tt.brightness, _edit_state._brightness, -1, 1,
	                                                          changed_func);
	_saturation_slider = std::make_shared<log_slider_control>(_dlg, tt.saturation, _edit_state._saturation, -1, 1,
	                                                          changed_func);
	_color_toolbar = std::make_shared<task_toolbar_control>(_dlg, color_buttons);
	_metadata_divider = std::make_shared<divider_element>();
	_metadata_title = std::make_shared<ui::title_control>(icon_index::document, tt.metadata);
	_title_edit = std::make_shared<ui::edit_control>(_dlg, tt.prop_name_title, _edit_state._item_title);
	_description_label = std::make_shared<text_element>(tt.prop_name_description);
	_description_edit = std::make_shared<ui::multi_line_edit_control>(_dlg, _edit_state._item_description, 8, true);
	_comment_label = std::make_shared<text_element>(tt.prop_name_comment);
	_comment_edit = std::make_shared<ui::multi_line_edit_control>(_dlg, _edit_state._item_comment, 6, true);
	_staring_label = std::make_shared<text_element>(tt.staring);
	_staring_edit = std::make_shared<ui::multi_line_edit_control>(_dlg, _edit_state._item_artist, 6, true);
	_staring_help = std::make_shared<text_element>(tt.help_artist);
	_tags_label = std::make_shared<text_element>(tt.tags_title);
	_tags_edit = std::make_shared<ui::multi_line_edit_control>(_dlg, _edit_state._item_tags);
	_tags_help1 = std::make_shared<text_element>(tt.help_tag1);
	_tags_help2 = std::make_shared<text_element>(tt.help_tag2);
	_show_edit = std::make_shared<ui::edit_control>(_dlg, tt.prop_name_show, _edit_state._item_show);
	_artist_edit = std::make_shared<ui::edit_control>(_dlg, tt.prop_name_artist, _edit_state._item_artist);
	_album_artist_edit = std::make_shared<ui::edit_control>(_dlg, tt.album_artist, _edit_state._item_album_artist);
	_album_edit = std::make_shared<ui::edit_control>(_dlg, tt.prop_name_album, _edit_state._item_album);
	_genre_edit = std::make_shared<ui::edit_control>(_dlg, tt.prop_name_genre, _edit_state._item_genre,
	                                                 std::vector<std::u8string>{u8"genre"});
	_year_edit = std::make_shared<ui::num_control>(_dlg, tt.prop_name_year, _edit_state._item_year);
	_episode_edit = std::make_shared<ui::num_pair_control>(_dlg, tt.prop_name_episode, _edit_state._item_episode);
	_season_edit = std::make_shared<ui::num_control>(_dlg, tt.prop_name_season, _edit_state._item_season);
	_track_edit = std::make_shared<ui::num_pair_control>(_dlg, tt.prop_name_track, _edit_state._item_track_number);
	_disk_edit = std::make_shared<ui::num_pair_control>(_dlg, tt.prop_name_disk, _edit_state._item_disk_number);
	_raiting_control = std::make_shared<rating_bar_control>(_dlg, tt.prop_name_rating, _edit_state._item_rating);
	_created_control = std::make_shared<ui::date_control>(_dlg, tt.prop_name_created, _edit_state._item_created);

	_staring_help->margin(16, 4);
	_tags_help1->margin(16, 4);
	_tags_help2->margin(16, 4);

	_controls = {
		_straighten_title,
		_straighten_slider,
		_rotate_toolbar,
		_color_divider,
		_color_title,
		_vibrance_slider,
		_darks_slider,
		_midtones_slider,
		_lights_slider,
		_contrast_slider,
		_brightness_slider,
		_saturation_slider,
		_color_toolbar,
		_metadata_divider,
		_metadata_title,
		_title_edit,
		_description_label,
		_description_edit,
		_comment_label,
		_comment_edit,
		_staring_label,
		_staring_edit,
		_staring_help,
		_tags_label,
		_tags_edit,
		_tags_help1,
		_tags_help2,
		_show_edit,
		_artist_divider,
		_artist_edit,
		_album_artist_edit,
		_album_edit,
		_genre_edit,
		_year_edit,
		_episode_edit,
		_season_edit,
		_track_edit,
		_disk_edit,
		_raiting_control,
		_created_control,
	};

	_clr = ui::style::color::dialog_text;

	/*_host->initialise();
	_host->_max_width = _host->_extent.cx;
	_host->_max_height = _host->_extent.cy;
	_host->layout();
	_host->focus_first();
	_host->on_message(WM_HSCROLL, [this](WPARAM wParam, LPARAM lParam) { _view->changed(); return false; });*/
}

void edit_view_controls::on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized)
{
	_extent = extent;
	layout_controls(mc);
}

void edit_view_controls::on_window_paint(ui::draw_context& dc)
{
	dc.col_widths = _label_width;

	const auto offset = _scroller.scroll_offset();

	for (const auto& c : _controls)
	{
		if (c->is_visible())
		{
			c->render(dc, -offset);
		}
	}

	if (_active_controller)
	{
		_active_controller->draw(dc);
	}

	if (!_scroller._active)
	{
		_scroller.draw_scroll(dc);
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

metadata_edits edit_view_state::build_metadata_edits(const prop::item_metadata_ptr& md) const
{
	metadata_edits results;
	if (_item_title.compare(md->title) != 0) results.title = _item_title;

	if (_item_tags.compare(md->tags) != 0)
	{
		results.add_tags = tag_set(_item_tags);
		results.remove_tags = tag_set(md->tags);
	}

	if (_item_description.compare(fix_crlf(md->description)) != 0) results.description = _item_description;
	if (_item_comment.compare(fix_crlf(md->comment)) != 0) results.comment = _item_comment;
	if (_item_artist.compare(md->artist) != 0) results.artist = _item_artist;
	if (_item_album.compare(md->album) != 0) results.album = _item_album;
	if (_item_album_artist.compare(md->album_artist) != 0) results.album_artist = _item_album_artist;
	if (_item_genre.compare(md->genre) != 0) results.genre = _item_genre;
	if (_item_show.compare(md->show) != 0) results.show = _item_show;
	if (_item_rating != md->rating) results.rating = _item_rating;
	if (_item_year != md->year) results.year = _item_year;
	if (_item_created != md->created()) results.created = _item_created;
	if (_item_episode != md->episode) results.episode = _item_episode;
	if (_item_season != md->season) results.season = _item_season;
	if (_item_track_number != md->track) results.track_num = _item_track_number;
	if (_item_disk_number != md->disk) results.disk_num = _item_disk_number;

	return results;
}


void edit_view_state::reset(const prop::item_metadata_ptr& md, const sizei dimensions)
{
	_edits.clear();

	_item_title = md->title;
	_item_tags = md->tags;
	_item_description = fix_crlf(md->description);
	_item_comment = fix_crlf(md->comment);
	_item_artist = md->artist;
	_item_album = md->album;
	_item_album_artist = md->album_artist;
	_item_genre = md->genre;
	_item_show = md->show;
	_item_rating = md->rating;
	_item_year = md->year;
	_item_created = md->created();
	_item_episode = md->episode;
	_item_season = md->season;
	_item_track_number = md->track;
	_item_disk_number = md->disk;

	_straighten = 0;
	_vibrance = 0;
	_darks = 0;
	_midtones = 0;
	_lights = 0;
	_contrast = 0;
	_brightness = 0;
	_saturation = 0;

	_edits.crop_bounds(recti(pointi(0, 0), dimensions));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void edit_view::display_changed()
{
	const auto display = _state.display_state();
	const auto item = display ? display->_item1 : df::file_item_ptr{};

	_state._edit_item = item;
	_loaded.clear();
	_preview_surface.reset();
	_texture.reset();
	_invalid = true;

	if (item)
	{
		_path = item->path();
		_xmp_name = item->xmp();
		_mt = item->file_type();

		if (_mt->has_trait(file_traits::bitmap))
		{
			files loader;
			prop::item_metadata ps;
			_loaded = loader.load(_path, false);
		}
		else
		{
			_loaded = display ? display->_selected_texture1->loaded() : file_load_result{};
		}
	}
	else
	{
		_path.clear();
		_mt = file_type::other;
	}

	if (_loaded.is_empty())
	{
		_loaded.s = _mt->default_thumbnail();
		_loaded.success = true;
	}

	_edit_state.reset(safe_metadata(item), _loaded.dimensions());

	if (_edit_controls->_dlg)
	{
		_edit_controls->populate();
		_edit_controls->_dlg->layout();
	}

	changed();
}

void edit_view::draw_handle(ui::draw_context& dc, const recti handle_bounds2, const float alpha)
{
	const auto handle_clr = ui::color(ui::style::color::dialog_selected_background, alpha * dc.colors.bg_alpha);
	const auto handle_bg_clr = ui::color(ui::style::color::dialog_selected_background, alpha * dc.colors.bg_alpha);

	dc.draw_rect(handle_bounds2, handle_clr);
	dc.draw_border(handle_bounds2.inflate(df::round(-2 * dc.scale_factor)), handle_bounds2, handle_bg_clr, handle_bg_clr);
}

void edit_view::render(ui::draw_context& dc, view_controller_ptr controller)
{
	auto clip_rect = dc.clip_bounds();

	if (is_photo())
	{
		const auto image_extent = _loaded.dimensions();
		const auto alpha = dc.colors.alpha;

		const quadd image_bounds(image_extent);

		const auto pad30 = dc.padding2 * 2;
		const auto selection_bounds = _edit_state.selection();
		const auto selection_angle = -selection_bounds.angle();
		const auto selection_rotated_bounds = image_bounds.transform(affined().rotate(selection_angle)).bounding_rect();
		const auto view_bounds = rectd(0, 0, _extent.cx, _extent.cy);
		const auto view_scale = std::min((view_bounds.Width - pad30) / selection_rotated_bounds.Width,
		                                 (view_bounds.Height - pad30) / selection_rotated_bounds.Height);

		_image_transform = affined().translate(-image_bounds.center_point()).rotate(selection_angle).scale(view_scale).
		                             translate(view_bounds.center());

		const auto image_view_bounds = image_bounds.transform(_image_transform);

		if (_invalid || !_texture)
		{
			const auto preview_dimensions = ui::scale_dimensions(image_extent, _extent);

			if (is_valid(_preview_surface))
			{
				auto t = dc.create_texture();

				if (t && t->update(_preview_surface) != ui::texture_update_result::failed)
				{
					_texture = t;
				}
			}
			else
			{
				auto t = dc.create_texture();

				if (t && t->update(_loaded.to_surface(preview_dimensions)) != ui::texture_update_result::failed)
				{
					_texture = t;
				}
			}

			_invalid = false;
		}

		//auto surface = !_edits.has_color_changes() || _texture.is_empty() ? _state.texture(rc, dst.ActualSize().Round()) : _texture;
		//const auto clr = ui::color(0x333333u, alpha);
		const auto sampler = calc_sampler(image_view_bounds.extent().round(), _texture->dimensions(),
		                                  ui::orientation::top_left);

		//dc.draw_rect(image_view_bounds, clr, clr);
		dc.draw_texture(_texture, image_view_bounds, _texture->dimensions(), alpha, sampler);

		const auto draw_crop = selection_bounds.crop(rectd(0, 0, image_extent.cx, image_extent.cy));
		const auto actual_size = draw_crop.actual_extent();
		const auto crop_bounding = draw_crop.transform(_image_transform).bounding_rect();
		const auto border = recti(0, 0, _extent.cx, _extent.cy);

		_crop_bounds = crop_bounding;

		const auto pad1 = df::round(1 * dc.scale_factor);
		const auto pad8 = dc.padding2;
		const auto pad16 = pad8 * 2;

		_crop_handle_tl.set(crop_bounding.left() - pad16, crop_bounding.top() - pad16, crop_bounding.left() + pad8,
		                    crop_bounding.top() + pad8);
		_crop_handle_tr.set(crop_bounding.right() - pad8, crop_bounding.top() - pad16, crop_bounding.right() + pad16,
		                    crop_bounding.top() + pad8);
		_crop_handle_bl.set(crop_bounding.left() - pad16, crop_bounding.bottom() - pad8, crop_bounding.left() + pad8,
		                    crop_bounding.bottom() + pad16);
		_crop_handle_br.set(crop_bounding.right() - pad8, crop_bounding.bottom() - pad8, crop_bounding.right() + pad16,
		                    crop_bounding.bottom() + pad16);

		const auto bounding = crop_bounding.round();		
		auto c1 = ui::color(0, alpha / 2.0f);
		dc.draw_border(bounding, border, c1, c1);
		auto c2 = ui::color(ui::style::color::dialog_selected_background, alpha);
		dc.draw_border(bounding, bounding.inflate(pad1), c2, c2);

		const auto grid_alpha = _edit_state.grid_alpha_animation.val();

		if (grid_alpha > 0)
		{
			const auto c = ui::color(0, grid_alpha);

			// draw grid
			const auto center = bounding.center();
			dc.draw_rect(recti(center.x - pad1, bounding.top, center.x + pad1, bounding.bottom), c);
			dc.draw_rect(recti(bounding.left, center.y - pad1, bounding.right, center.y + pad1), c);

			for (int div = 6; div >= 3; div /= 2)
			{
				const auto l = center.x - bounding.width() / div;
				const auto r = center.x + bounding.width() / div;
				const auto t = center.y - bounding.height() / div;
				const auto b = center.y + bounding.height() / div;

				dc.draw_rect(recti(bounding.left, t - pad1, bounding.right, t + pad1), c);
				dc.draw_rect(recti(bounding.left, b - pad1, bounding.right, b + pad1), c);
				dc.draw_rect(recti(l - pad1, bounding.top, l + pad1, bounding.bottom), c);
				dc.draw_rect(recti(r - pad1, bounding.top, r + pad1, bounding.bottom), c);
			}
		}

		draw_handle(dc, _crop_handle_tl.round(), alpha);
		draw_handle(dc, _crop_handle_tr.round(), alpha);
		draw_handle(dc, _crop_handle_bl.round(), alpha);
		draw_handle(dc, _crop_handle_br.round(), alpha);

		const auto text_dims = str::format(u8"{}x{}"sv, df::round(actual_size.Width), df::round(actual_size.Height));
		const sizei text_size = dc.measure_text(text_dims, ui::style::font_face::dialog, ui::style::text_style::single_line_center, bounding.width()).inflate(dc.padding2);
		const auto text_x = (bounding.left + bounding.right - text_size.cx) / 2;
		const recti draw_text_rect(text_x, bounding.top - text_size.cy, text_x + text_size.cx, bounding.top);
		
		dc.draw_text(text_dims, draw_text_rect, ui::style::font_face::dialog, ui::style::text_style::single_line_center,
		             ui::color(dc.colors.foreground, alpha), {});

		if (setting.show_debug_info)
		{
			const auto text_degs = str::print(u8"%3.3f degrees"sv, selection_angle);
			dc.draw_text(text_degs, crop_bounding.round().inflate(df::round(100 * dc.scale_factor)), ui::style::font_face::title,
			             ui::style::text_style::single_line_center,
			             ui::color(ui::style::color::warning_background, alpha), {});
		}
	}
	else if (!_loaded.is_empty())
	{
		const auto alpha = dc.colors.alpha;
		const auto dims = ui::scale_dimensions(_loaded.dimensions(), _extent, true);
		const auto bounds = center_rect(dims, _extent);

		if (_invalid || !_texture)
		{
			auto t = dc.create_texture();

			if (t && t->update(_loaded.to_surface(bounds.extent())) != ui::texture_update_result::failed)
			{
				_texture = t;
				_invalid = false;
			}
		}

		const auto sampler = calc_sampler(bounds.extent(), _texture->dimensions(), ui::orientation::top_left);
		dc.draw_texture(_texture, bounds, alpha, sampler);

		auto draw_text_rect = recti(_extent);
		draw_text_rect.top = bounds.bottom + dc.padding2;
		draw_text_rect.bottom = draw_text_rect.top + dc.text_line_height(ui::style::font_face::title) + (dc.padding2 * 2);

		dc.draw_text(_path.name(), draw_text_rect, ui::style::font_face::title,
		             ui::style::text_style::single_line_center, ui::color(dc.colors.foreground, alpha), {});
	}
}

pointi edit_view::clamp_offset(const sizei render_extent, const pointi offset) const
{
	const recti avail_bounds(0, 0, _extent.cx, _extent.cy);

	const auto xx = std::max(render_extent.cx - avail_bounds.width(), 0) / 2;
	const auto yy = std::max(render_extent.cy - avail_bounds.height(), 0) / 2;

	const recti limit(-xx, -yy, xx, yy);
	return offset.clamp(limit);
}

void edit_view::preview(ui::const_surface_ptr surface)
{
	_preview_surface = std::move(surface);
	_invalid = true;
	_host->frame()->invalidate();
}

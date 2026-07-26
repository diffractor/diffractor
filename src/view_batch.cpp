// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Shared preview and processing view for batch conversion, metadata, and date tools.

#include "pch.h"
#include "model.h"
#include "model_index.h"
#include "view_batch.h"
#include "ui_controls.h"

static std::string convert_extension()
{
	if (setting.convert.to_png) return ".png";
	if (setting.convert.to_webp) return ".webp";
	return ".jpg";
}

// Offer the built-in genres that suit the selected media, merged with the genres already present
// in the collection so an established vocabulary is never hidden behind the built-in list.
static std::vector<std::string> genre_suggestions(const view_state& s, const df::item_set& items)
{
	auto kinds = genre_kind::none;

	for (const auto& i : items.items())
	{
		const auto ft = i->file_type();
		if (ft->has_trait(file_traits::music_metadata)) kinds |= genre_kind::audio;
		if (ft->has_trait(file_traits::video_metadata)) kinds |= genre_kind::video;
		if (ft->has_trait(file_traits::photo_metadata)) kinds |= genre_kind::photo;
	}

	if (kinds == genre_kind::none) kinds = genre_kind::any;

	auto result = tt.known_genres(kinds);

	for (auto&& genre : tt.add_translate_text(s.item_index.distinct_genres(), "genre"))
	{
		result.emplace_back(std::move(genre));
	}

	std::ranges::sort(result, str::iless());
	result.erase(std::ranges::unique(result, df::ieq()).begin(), result.end());
	return result;
}

std::array<text_t, list_view::max_col_count> batch_tool_view::col_titles()
{
	if (_mode == batch_tool_mode::convert) return {tt.source, tt.destination, tt.changes, {}};
	if (_mode == batch_tool_mode::metadata) return {tt.file, tt.changes, tt.status, {}};
	return {tt.file, tt.edit_original, tt.after, {}};
}

std::string_view batch_tool_view::title()
{
	const auto text = _mode == batch_tool_mode::convert ? tt.command_convert_or_resize.sv() :
		_mode == batch_tool_mode::metadata ? tt.command_edit_metadata.sv() : tt.command_adjust_date.sv();
	_title = std::format("{}: {}", s_app_name, text);
	return _title;
}

// Toolbar text, so it names the operation without the dialog-button accelerator marker.
text_t batch_tool_view::run_text() const
{
	if (_mode == batch_tool_mode::convert) return tt.command_convert;
	if (_mode == batch_tool_mode::metadata) return tt.command_update_metadata;
	return tt.command_adjust_date;
}

std::string_view batch_tool_view::operation_name() const
{
	// View into the persistent text table, not into a text_t copy.
	if (_mode == batch_tool_mode::convert) return tt.command_convert.sv();
	if (_mode == batch_tool_mode::metadata) return tt.command_update_metadata.sv();
	return tt.command_adjust_date.sv();
}

void batch_tool_view::activate(const sizei extent)
{
	list_view::activate(extent);
	refresh();
}

void batch_tool_view::deactivate()
{
	_rows.clear();
	_convert_plan.clear();
	_status.clear();
	// The next use starts from its own selection, so a date chosen for the previous selection must
	// not survive as the starting date for a different set of items.
	_original_start = {};
	_new_start = {};
	_new_start_seeded = false;
}

metadata_edits batch_tool_view::metadata_changes() const
{
	metadata_edits edits;
	if (_set_title) edits.title = _metadata_title;
	if (_set_comment) edits.comment = _metadata_comment;
	if (_set_rating) edits.rating = _metadata_rating;
	if (_set_year) edits.year = _metadata_year;
	if (_set_created) edits.created = _metadata_created;
	if (_set_episode) edits.episode = _metadata_episode;
	if (_set_season) edits.season = _metadata_season;
	if (_set_track) edits.track_num = _metadata_track;
	if (_set_disk) edits.disk_num = _metadata_disk;
	if (setting.set_artist) edits.artist = setting.artist;
	if (setting.set_caption) edits.description = setting.caption;
	if (setting.set_album) edits.album = setting.album;
	if (setting.set_album_artist) edits.album_artist = setting.album_artist;
	if (setting.set_genre) edits.genre = setting.genre;
	if (setting.set_tv_show) edits.show = setting.tv_show;
	if (setting.set_copyright_notice) edits.copyright_notice = setting.copyright_notice;
	if (setting.set_copyright_creator) edits.copyright_creator = setting.copyright_creator;
	if (setting.set_copyright_source) edits.copyright_source = setting.copyright_source;
	if (setting.set_copyright_credit) edits.copyright_credit = setting.copyright_credit;
	if (setting.set_copyright_url) edits.copyright_url = setting.copyright_url;
	return edits;
}

void batch_tool_view::refresh_convert()
{
	const auto& items = _state.selected_items();
	_convert_plan = plan_convert_outputs(df::folder_path(setting.write_folder), items, convert_extension(),
	                                     setting.convert.collision);
	std::vector<row_element_ptr> rows;
	rows.reserve(_convert_plan.size());
	const auto error_color = ui::lighten(ui::style::color::warning_background, 0.55f);
	const auto resolved_color = ui::lighten(ui::style::color::important_background, 0.55f);
	int order = 0;
	int collisions = 0;
	for (const auto& entry : _convert_plan)
	{
		auto row = std::make_shared<row_element>(*this);
		row->_text[0] = entry.item->path().pack();
		row->_text[1] = entry.destination.pack();
		const auto md = entry.item->metadata();
		const auto dimensions = md ? md->dimensions() : sizei{};
		const auto max_side = setting.convert.limit_dimension ? setting.convert.max_side : 0;
		const auto scale = max_side > 0 && std::max(dimensions.cx, dimensions.cy) > max_side
			? ui::scale_dimensions(dimensions, max_side, true) : dimensions;

		if (entry.collides) ++collisions;

		if (entry.skipped)
		{
			row->_text[2] = std::string(tt.collision_skip.sv());
			row->_text_color[2] = resolved_color;
		}
		else if (entry.renamed_to_avoid_collision)
		{
			row->_text[2] = std::string(tt.collision_rename.sv());
			row->_text_color[2] = resolved_color;
		}
		else if (entry.collides)
		{
			// Replace states the overwrite; Block Run states the reason the run is refused.
			row->_text[2] = format_plural_text(tt.would_overwrite_fmt,
			                                   std::vector<std::string>{std::string(entry.destination.name())});
			row->_text_color[2] = setting.convert.collision == collision_policy::replace ? resolved_color : error_color;
		}
		else
		{
			row->_text[2] = std::format("{}x{} {}", scale.cx, scale.cy, convert_extension());
		}

		row->_order = order++;
		rows.emplace_back(row);
	}
	_rows = std::move(rows);
	_status = collisions > 0
		          ? std::format("{}   {}", format_plural_text(tt.convert_info_fmt, items),
		                        format_collision_summary(setting.convert.collision, collisions))
		          : format_plural_text(tt.convert_info_fmt, items);
}

void batch_tool_view::refresh_metadata()
{
	const auto& items = _state.selected_items();

	// The date picker cannot show an empty date, so an untouched control displays today while an
	// unset value would be written. Seed the value that the control already shows so the preview,
	// the control and the written date agree.
	if (!_metadata_created.is_valid()) _metadata_created = platform::now();

	std::vector<std::string_view> fields;
	if (_set_title) fields.emplace_back(tt.prop_name_title.sv());
	if (_set_comment) fields.emplace_back(tt.prop_name_comment.sv());
	if (_set_rating) fields.emplace_back(tt.prop_name_rating.sv());
	if (_set_year) fields.emplace_back(tt.prop_name_year.sv());
	if (_set_created) fields.emplace_back(tt.prop_name_created.sv());
	if (_set_episode) fields.emplace_back(tt.prop_name_episode.sv());
	if (_set_season) fields.emplace_back(tt.prop_name_season.sv());
	if (_set_track) fields.emplace_back(tt.prop_name_track.sv());
	if (_set_disk) fields.emplace_back(tt.prop_name_disk.sv());
	if (setting.set_artist) fields.emplace_back(tt.prop_name_artist.sv());
	if (setting.set_caption) fields.emplace_back(tt.prop_name_description.sv());
	if (setting.set_album) fields.emplace_back(tt.prop_name_album.sv());
	if (setting.set_album_artist) fields.emplace_back(tt.album_artist.sv());
	if (setting.set_genre) fields.emplace_back(tt.prop_name_genre.sv());
	if (setting.set_tv_show) fields.emplace_back(tt.prop_name_show.sv());
	if (setting.set_copyright_notice) fields.emplace_back(tt.copyright_notice.sv());
	if (setting.set_copyright_creator) fields.emplace_back(tt.copyright_creator.sv());
	if (setting.set_copyright_source) fields.emplace_back(tt.copyright_source.sv());
	if (setting.set_copyright_credit) fields.emplace_back(tt.copyright_credit.sv());
	if (setting.set_copyright_url) fields.emplace_back(tt.copyright_url.sv());
	const auto field_text = str::combine(fields, ", ", false);
	std::vector<row_element_ptr> rows;
	const auto warning_color = ui::lighten(ui::style::color::important_background, 0.55f);
	int order = 0;
	for (const auto& item : items.items())
	{
		auto row = std::make_shared<row_element>(*this);
		row->_text[0] = item->path().pack();
		row->_text[1] = field_text;
		const auto md = item->metadata();
		const auto overwrites = md && ((_set_title && !prop::is_null(md->title)) ||
			(_set_comment && !prop::is_null(md->comment)) || (_set_rating && md->rating != 0) ||
			(_set_year && md->year != 0) || (_set_created && md->created().is_valid()) ||
			(_set_episode && (md->episode.x != 0 || md->episode.y != 0)) || (_set_season && md->season != 0) ||
			(_set_track && (md->track.x != 0 || md->track.y != 0)) ||
			(_set_disk && (md->disk.x != 0 || md->disk.y != 0)) ||
			(setting.set_artist && !prop::is_null(md->artist)) ||
			(setting.set_caption && !prop::is_null(md->description)) || (setting.set_album && !prop::is_null(md->album)) ||
			(setting.set_album_artist && !prop::is_null(md->album_artist)) ||
			(setting.set_genre && !prop::is_null(md->genre)) || (setting.set_tv_show && !prop::is_null(md->show)) ||
			(setting.set_copyright_notice && !prop::is_null(md->copyright_notice)) ||
			(setting.set_copyright_creator && !prop::is_null(md->copyright_creator)) ||
			(setting.set_copyright_source && !prop::is_null(md->copyright_source)) ||
			(setting.set_copyright_credit && !prop::is_null(md->copyright_credit)) ||
			(setting.set_copyright_url && !prop::is_null(md->copyright_url)));
		row->_text[2] = overwrites
			? std::string(tt.collision_replace.sv())
			: std::string(tt.update.sv());
		if (overwrites) row->_text_color[2] = warning_color;
		row->_order = order++;
		rows.emplace_back(row);
	}
	_rows = std::move(rows);
	_status = format_plural_text(tt.edit_metadata_fmt, items);
}

void batch_tool_view::update_date_start()
{
	const auto& items = _state.selected_items();
	_original_start = {};
	for (const auto& item : items.items())
	{
		const auto date = item->media_created();
		if (date.is_valid() && (!_original_start.is_valid() || date < _original_start)) _original_start = date;
	}

	// Seed once. The picker cannot show an empty date, so an unseeded control would display today
	// while a different date was written. Re-seeding on every refresh would instead discard a date
	// the user chose but that is not a valid capture date, silently running something else.
	if (!_new_start_seeded)
	{
		_new_start = _original_start.is_valid() ? _original_start : platform::now();
		_new_start_seeded = true;
	}
}

void batch_tool_view::refresh_dates()
{
	const auto& items = _state.selected_items();
	update_date_start();
	std::vector<row_element_ptr> rows;
	int order = 0;
	for (const auto& item : items.items())
	{
		auto row = std::make_shared<row_element>(*this);
		const auto original = item->media_created();
		row->_text[0] = item->path().pack();
		row->_text[1] = platform::format_date_time(original);
		row->_text[2] = platform::format_date_time(adjusted_item_date(original, _new_start, _original_start));
		row->_order = order++;
		rows.emplace_back(row);
	}
	_rows = std::move(rows);
	// Run is disabled for a date that cannot be a capture date, so state the reason rather than
	// leaving the button dead and the preview blank.
	_status = _new_start.is_valid()
		          ? format_plural_text(tt.adjust_date_info_fmt, items)
		          : std::string(tt.adjust_date_required.sv());
}

void batch_tool_view::refresh()
{
	if (_mode == batch_tool_mode::convert) refresh_convert();
	else if (_mode == batch_tool_mode::metadata) refresh_metadata();
	else refresh_dates();
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller | view_invalid::status |
		view_invalid::command_state);
}

bool batch_tool_view::can_run() const
{
	if (progress().active || _rows.empty()) return false;
	if (_mode == batch_tool_mode::convert)
		return !df::folder_path(setting.write_folder).is_empty() &&
			(!setting.convert.limit_dimension || setting.convert.max_side > 0) &&
			// Only Block Run refuses the run; the other policies resolve the collision.
			(setting.convert.collision != collision_policy::block_run ||
				std::ranges::none_of(_convert_plan, [](const auto& item) { return item.collides; })) &&
			std::ranges::any_of(_convert_plan, [](const auto& item) { return !item.skipped; });
	if (_mode == batch_tool_mode::metadata) return metadata_changes().has_changes();
	return _new_start.is_valid();
}

void batch_tool_view::run()
{
	if (!can_run()) return;
	if (_mode == batch_tool_mode::convert) run_convert();
	else if (_mode == batch_tool_mode::metadata) run_metadata();
	else run_dates();
}

void batch_tool_view::queue_run_result(view_state& s, const std::shared_ptr<batch_tool_view>& view,
                                       platform::file_op_result result, const bool canceled, std::string title,
                                       std::shared_ptr<detach_file_handles> detach, std::string error)
{
	s.queue_ui([view, result = std::move(result), canceled, title = std::move(title), detach = std::move(detach),
		           error = std::move(error)]
	{
		view->end_processing();
		view->refresh();
		if (!error.empty() || result.failed())
		{
			auto dlg = make_dlg(view->_host->owner());
			dlg->show_message(icon_index::error, title, error.empty() ? result.format_error() : error);
		}
		else if (canceled)
		{
			view->_status = std::string(tt.error_op_cancelled.sv());
			view->_state.invalidate_view(view_invalid::status | view_invalid::command_state);
		}
	});
}

void batch_tool_view::run_convert()
{
	auto plan = _convert_plan;
	struct convert_request
	{
		df::file_path source;
		df::file_path destination;
		str::cached xmp;
	};
	std::vector<convert_request> requests;
	requests.reserve(plan.size());
	for (const auto& entry : plan)
	{
		// Skip resolved the collision by leaving the existing file alone; its destination still
		// names that file, so converting the row would overwrite what the review said to keep.
		if (entry.skipped) continue;
		requests.emplace_back(entry.item->path(), entry.destination, entry.item->xmp());
	}
	const auto title = std::string(tt.command_convert_or_resize.sv());
	const auto max_side = setting.convert.limit_dimension ? setting.convert.max_side : 0;
	record_feature_use(max_side > 0 ? features::convert | features::resize : features::convert);
	const auto jpeg_quality = setting.convert.jpeg_quality;
	const auto webp_quality = setting.convert.webp_quality;
	const auto webp_lossless = setting.convert.webp_lossless;
	const auto write_folder = df::folder_path(setting.write_folder);
	const auto detach = std::make_shared<detach_file_handles>(_state);
	begin_processing(requests.size());
	const auto cancel_source = processing_cancel_source();
	const auto cancel_version = cancel_source->load();
	_status = std::string(tt.processing.sv());
	_state.queue_async(async_queue::work, [&s = _state, view = shared_from_this(), requests = std::move(requests), cancel_source, cancel_version, max_side,
		jpeg_quality, webp_quality, webp_lossless, write_folder, detach, title]() mutable
	{
		platform::file_op_result result;
		std::string error;
		try
		{
			files ff;
			result = platform::create_folder(write_folder);
			for (size_t index = 0; result.success() && index < requests.size() && cancel_source->load() == cancel_version; ++index)
			{
				s.queue_ui([view, index] { view->processing_item(index); });
				file_encode_params params;
				params.jpeg_save_quality = jpeg_quality;
				params.webp_quality = webp_quality;
				params.webp_lossless = webp_lossless;
				const auto& request = requests[index];
				result = ff.update(request.source, request.destination, {},
				                   max_side > 0 ? image_edits(max_side) : image_edits(), params, false, request.xmp);
			}
		}
		catch (const std::exception& e)
		{
			error = str::utf8_cast(e.what());
		}
		const auto canceled = cancel_source->load() != cancel_version;
		queue_run_result(s, view, std::move(result), canceled, title, std::move(detach), std::move(error));
	});
}

void batch_tool_view::run_metadata()
{
	struct metadata_request
	{
		df::file_path path;
		str::cached xmp;
	};
	const auto items = _state.selected_items().items();
	std::vector<metadata_request> requests;
	requests.reserve(items.size());
	for (const auto& item : items)
	{
		requests.emplace_back(item->path(), item->xmp());
	}
	const auto title = std::string(tt.command_edit_metadata.sv());
	const auto edits = metadata_changes();
	record_feature_use(features::batch_edit);
	const auto detach = std::make_shared<detach_file_handles>(_state);
	begin_processing(items.size());
	const auto cancel_source = processing_cancel_source();
	const auto cancel_version = cancel_source->load();
	_status = std::string(tt.processing.sv());
	_state.queue_async(async_queue::work, [&s = _state, view = shared_from_this(), requests = std::move(requests), edits, cancel_source, cancel_version, detach, title]() mutable
	{
		platform::file_op_result result;
		std::string error;
		try
		{
			files ff;
			for (size_t index = 0; index < requests.size() && cancel_source->load() == cancel_version; ++index)
			{
				s.queue_ui([view, index] { view->processing_item(index); });
				const auto& request = requests[index];
				result = ff.update(request.path, edits, {}, file_encode_params{}, false, request.xmp);
				if (result.failed()) break;
			}
		}
		catch (const std::exception& e)
		{
			error = str::utf8_cast(e.what());
		}
		const auto canceled = cancel_source->load() != cancel_version;
		queue_run_result(s, view, std::move(result), canceled, title, std::move(detach), std::move(error));
	});
}

void batch_tool_view::run_dates()
{
	struct date_update_request
	{
		df::file_path path;
		df::date_t media_created;
		str::cached xmp;
	};

	std::vector<date_update_request> requests;
	for (const auto& item : _state.selected_items().items())
	{
		requests.emplace_back(item->path(), item->media_created(), item->xmp());
	}

	record_feature_use(features::adjust_date);

	const auto title = std::string(tt.command_adjust_date.sv());
	const auto new_start = _new_start;
	const auto original_start = _original_start;
	const auto detach = std::make_shared<detach_file_handles>(_state);
	begin_processing(requests.size());
	const auto cancel_source = processing_cancel_source();
	const auto cancel_version = cancel_source->load();
	_status = std::string(tt.processing.sv());
	_state.queue_async(async_queue::work, [&s = _state, view = shared_from_this(), requests = std::move(requests), new_start, original_start,
		cancel_source, cancel_version, detach, title]() mutable
	{
		platform::file_op_result result;
		std::string error;
		try
		{
			files ff;
			for (size_t index = 0; index < requests.size() && cancel_source->load() == cancel_version; ++index)
			{
				s.queue_ui([view, index] { view->processing_item(index); });
				const auto& request = requests[index];
				const auto date = adjusted_item_date(request.media_created, new_start, original_start);
				metadata_edits edits;
				edits.created = date;
				result = ff.update(request.path, edits, {}, file_encode_params{}, false, request.xmp);
				if (result.success()) platform::created_date(request.path, date.local_to_system());
				else break;
			}
		}
		catch (const std::exception& e)
		{
			error = str::utf8_cast(e.what());
		}
		const auto canceled = cancel_source->load() != cancel_version;
		queue_run_result(s, view, std::move(result), canceled, title, std::move(detach), std::move(error));
	});
}

view_controls_host_ptr batch_tool_view::controls(const ui::control_frame_ptr& owner)
{
	auto result = std::make_shared<view_controls_host>(_state);
	auto frame = owner->create_dlg(result, false);
	std::vector<view_element_ptr> controls;
	const auto& items = _state.selected_items();
	auto selection_thumbnails = std::make_shared<ui::selection_thumbnails_control>(frame);
	selection_thumbnails->selection(items.thumbs(), items.size());

	if (_mode == batch_tool_mode::convert)
	{
		controls.emplace_back(create_view_info_element(tt.convert_info));
		controls.emplace_back(selection_thumbnails);
		controls.emplace_back(std::make_shared<text_element>(format_plural_text(tt.convert_info_fmt, items)));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<text_element>(tt.destination_folder));
		controls.emplace_back(std::make_shared<ui::folder_picker_control>(frame, setting.write_folder, false,
			[this](std::string_view) { refresh(); }));
		auto jpeg = std::make_shared<ui::check_control>(frame, tt.jpeg_best, setting.convert.to_jpeg, true, false,
			[this](bool checked) { if (checked) setting.convert.to_png = setting.convert.to_webp = false; refresh(); },
			ui::radio_group_format);
		auto jpeg_options = std::make_shared<ui::group_control>();
		jpeg_options->add(std::make_shared<text_element>(tt.options_jpeg_quality));
		jpeg_options->add(std::make_shared<ui::slider_control>(frame, std::string_view{}, setting.convert.jpeg_quality, 1,
			100, [this] { refresh(); }));
		jpeg->child(jpeg_options);
		controls.emplace_back(jpeg);
		controls.emplace_back(std::make_shared<ui::check_control>(frame, tt.png_best, setting.convert.to_png, true, false,
			[this](bool checked) { if (checked) setting.convert.to_jpeg = setting.convert.to_webp = false; refresh(); },
			ui::radio_group_format));
		auto webp = std::make_shared<ui::check_control>(frame, tt.webp_best, setting.convert.to_webp, true, false,
			[this](bool checked) { if (checked) setting.convert.to_jpeg = setting.convert.to_png = false; refresh(); },
			ui::radio_group_format);
		auto webp_options = std::make_shared<ui::group_control>();
		webp_options->add(std::make_shared<ui::slider_control>(frame, std::string_view{}, setting.convert.webp_quality, 1,
			100, [this] { refresh(); }));
		webp_options->add(std::make_shared<ui::check_control>(frame, tt.lossless_compression,
			setting.convert.webp_lossless, false, false, [this](bool) { refresh(); }));
		webp->child(webp_options);
		controls.emplace_back(webp);
		auto dimension = std::make_shared<ui::check_control>(frame, tt.limit_output_dimensions,
			setting.convert.limit_dimension, false, false, [this](bool) { refresh(); });
		dimension->child(std::make_shared<ui::num_control>(frame, std::string_view{}, setting.convert.max_side, false,
			[this](int) { refresh(); }));
		controls.emplace_back(dimension);
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(
			create_collision_policy_control(frame, setting.convert.collision, [this] { refresh(); }));
	}
	else if (_mode == batch_tool_mode::metadata)
	{
		controls.emplace_back(create_view_info_element(tt.edit_metadata_info));
		controls.emplace_back(selection_thumbnails);
		controls.emplace_back(std::make_shared<text_element>(format_plural_text(tt.edit_metadata_fmt, items)));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<text_element>(tt.metadata_select_field));
		auto add_field = [&](const text_t label, bool& selected, std::string& value, const bool multiline = false)
		{
			auto check = std::make_shared<ui::check_control>(frame, label, selected, false, false,
				[this](bool) { refresh(); });
			if (multiline) check->child(std::make_shared<ui::multi_line_edit_control>(frame, value, 6, true,
				[this](std::string_view) { refresh(); }));
			else check->child(std::make_shared<ui::edit_control>(frame, std::string_view{}, value,
				[this](std::string_view) { refresh(); }));
			controls.emplace_back(check);
		};
		auto add_control_field = [&](const text_t label, bool& selected, const view_element_ptr& child)
		{
			auto check = std::make_shared<ui::check_control>(frame, label, selected, false, false,
				[this](bool) { refresh(); });
			check->child(child);
			controls.emplace_back(check);
		};
		add_field(tt.prop_name_title, _set_title, _metadata_title);
		add_field(tt.prop_name_comment, _set_comment, _metadata_comment, true);
		add_field(tt.prop_name_artist, setting.set_artist, setting.artist);
		add_field(tt.prop_name_description, setting.set_caption, setting.caption, true);
		add_field(tt.prop_name_album, setting.set_album, setting.album);
		add_field(tt.album_artist, setting.set_album_artist, setting.album_artist);
		add_control_field(tt.prop_name_genre, setting.set_genre,
			std::make_shared<ui::edit_picker_control>(frame, setting.genre,
				genre_suggestions(_state, items),
				[this](std::string_view) { refresh(); }));
		controls.emplace_back(std::make_shared<text_element>(tt.genre_separator_help));
		add_field(tt.prop_name_show, setting.set_tv_show, setting.tv_show);
		add_control_field(tt.prop_name_rating, _set_rating,
			std::make_shared<rating_edit_control>(_metadata_rating, [this](int) { refresh(); }));
		add_control_field(tt.prop_name_year, _set_year,
			std::make_shared<ui::num_control>(frame, std::string_view{}, _metadata_year, true,
				[this](int) { refresh(); }));
		add_control_field(tt.prop_name_created, _set_created,
			std::make_shared<ui::date_control>(frame, _metadata_created, true, [this] { refresh(); }));
		add_control_field(tt.prop_name_episode, _set_episode,
			std::make_shared<ui::num_pair_control>(frame, std::string_view{}, _metadata_episode));
		add_control_field(tt.prop_name_season, _set_season,
			std::make_shared<ui::num_control>(frame, std::string_view{}, _metadata_season, true,
				[this](int) { refresh(); }));
		add_control_field(tt.prop_name_track, _set_track,
			std::make_shared<ui::num_pair_control>(frame, std::string_view{}, _metadata_track));
		add_control_field(tt.prop_name_disk, _set_disk,
			std::make_shared<ui::num_pair_control>(frame, std::string_view{}, _metadata_disk));
		add_field(tt.copyright_notice, setting.set_copyright_notice, setting.copyright_notice, true);
		add_field(tt.copyright_creator, setting.set_copyright_creator, setting.copyright_creator);
		add_field(tt.copyright_source, setting.set_copyright_source, setting.copyright_source);
		add_field(tt.copyright_credit, setting.set_copyright_credit, setting.copyright_credit);
		add_field(tt.copyright_url, setting.set_copyright_url, setting.copyright_url);
	}
	else
	{
		// The controls are built before the view is activated, so the starting date must be known here
		// or the picker would show a date that neither the preview nor the run would use.
		update_date_start();
		controls.emplace_back(create_view_info_element(tt.adjust_date_info));
		controls.emplace_back(selection_thumbnails);
		controls.emplace_back(std::make_shared<text_element>(format_plural_text(tt.adjust_date_info_fmt, items)));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<text_element>(tt.adjust_date_help1));
		controls.emplace_back(std::make_shared<text_element>(tt.adjust_date_help2));
		controls.emplace_back(std::make_shared<text_element>(tt.adjust_date_help3));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<text_element>(tt.starting_date_label));
		controls.emplace_back(std::make_shared<ui::date_control>(frame, _new_start, true, [this] { refresh(); }));
	}

	for (const auto& control : controls)
	{
		control->margin.cx = 8;
		control->margin.cy = 4;
	}
	result->_controls = std::move(controls);
	result->_frame = result->_dlg = frame;
	return result;
}
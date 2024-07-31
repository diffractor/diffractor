// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "util_zip.h"
#include "util_top.h"
#include "model.h"
#include "model_index.h"
#include "model_db.h"
#include "model_tokenizer.h"
#include "ui_controls.h"
#include "ui_dialog.h"
#include "ui_controllers.h"
#include "view_edit.h"
#include "app_match.h"
#include "app_text.h"
#include "app_command_status.h"
#include "app_util.h"
#include "app.h"
#include "crypto.h"
#include "util_base64.h"
#include "view_import.h"
#include "view_rename.h"
#include "view_sync.h"
#include "view_test.h"

static std::u8string decode_secret(const std::u8string_view input, std::u8string_view password)
{
	const auto data = base64_decode(input);
	auto decoded = crypto::decrypt(data, password);
	return std::u8string(decoded.begin(), decoded.end());
}

#if __has_include("secrets.h") 
# include "secrets.h"
#else
static const std::u8string google_maps_api_key = u8"";
static const std::u8string azure_maps_api_key = u8"";
#endif

extern bool toggle_details_state;


static constexpr std::u8string_view docs_url = u8"https://www.diffractor.com/docs"sv;
static constexpr std::u8string_view support_url = u8"https://diffractor.com/help"sv;
static constexpr std::u8string_view donate_url = u8"https://www.paypal.com/donate/?hosted_button_id=HX5NRS9JGKLRL"sv;

static void zoom_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto display = s.display_state();

	if (display && display->can_zoom())
	{
		display->toggle_zoom();
	}
};

static void pin_invoke(view_state& s)
{
	const auto focus = s.focus_item();

	if (s._pin_item == focus)
	{
		s._pin_item.reset();
	}
	else
	{
		s._pin_item = focus;
	}

	s.invalidate_view(view_invalid::view_layout);
}


static void edit_invoke(view_state& s)
{
	if (s.can_edit_media())
	{
		s.view_mode(view_type::edit);
	}
}

static void containing_folder_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto i = s.command_item();

	if (i)
	{
		df::unique_paths selection;
		selection.emplace(i->path());
		s.open(view, i->containing(), selection);
	}
}

static void open_in_file_browser_invoke(view_state& s, const ui::control_frame_ptr& parent,
	const view_host_base_ptr& view)
{
	const auto title = tt.open_in_browser_title;
	const auto dlg = make_dlg(parent);
	const auto can_process = s.
		can_process_selection_and_mark_errors(view, df::process_items_type::local_file_or_folder);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto f = s.command_item();

		if (f)
		{
			const auto file = std::dynamic_pointer_cast<df::item_element>(f);

			if (file)
			{
				platform::show_in_file_browser(file->path());
			}
		}
	}
}

static void new_folder_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	int i = 2;
	const auto folder = s.save_path();
	auto new_name = std::u8string(tt.new_folder_name);
	auto new_path = folder.combine(new_name);

	while (platform::exists(new_path) && (i < 100))
	{
		new_name = str::format(u8"{} {}"sv, tt.new_folder_name, i++);
		new_path = folder.combine(new_name);
	}

	const auto create_folder_result = platform::create_folder(new_path);

	if (create_folder_result.failed())
	{
		const auto dlg = make_dlg(parent);
		dlg->show_message(icon_index::folder, tt.new_folder_title, create_folder_result.format_error());
	}
	else
	{
		df::unique_paths selection;
		selection.emplace(new_path);
		s.open(view, s.search(), selection);
	}
}

static void burn_command_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto dlg = make_dlg(parent);
	const auto title = tt.burn_title;
	const auto can_process = s.
		can_process_selection_and_mark_errors(view, df::process_items_type::local_file_or_folder);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto items = s.selected_items();
		platform::burn_to_cd(items.file_paths(true), items.folder_paths());
	}
}

static void print_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto dlg = make_dlg(parent);
	const auto title = tt.print_title;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::local_file);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto items = s.selected_items();
		record_feature_use(features::print);
		platform::print(items.file_paths(false), items.folder_paths());
	}
}

static void remove_metadata_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto dlg = make_dlg(parent);
	const auto title = tt.remove_metadata_title;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto items = s.selected_items();
		record_feature_use(features::remove_metadata);
		platform::remove_metadata(items.file_paths(false), items.folder_paths());
	}
}

static void rename_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto title = tt.command_rename;
	const auto icon = icon_index::rename;
	auto dlg = make_dlg(parent);
	const auto can_process = s.
		can_process_selection_and_mark_errors(view, df::process_items_type::local_file_or_folder);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto& items = s.selected_items();

		if (items.size() > 1)
		{
			s.view_mode(view_type::rename);
		}
		else if (items.size() == 1)
		{
			const auto& file_system_items = items.items();
			const auto i = file_system_items[0];
			auto name = i->base_name();

			std::vector<view_element_ptr> controls{
				set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title,
																format_plural_text(tt.rename_fmt, items),
																items.thumbs())),
				std::make_shared<divider_element>(),
				set_margin(std::make_shared<text_element>(tt.rename_label)),
				set_margin(std::make_shared<ui::edit_control>(dlg->_frame, name)),
				std::make_shared<divider_element>(),
				std::make_shared<ui::ok_cancel_control>(dlg->_frame)
			};

			if (dlg->show_modal(controls) == ui::close_result::ok)
			{
				detach_file_handles detach(s);

				const auto result = i->rename(s.item_index, name);

				if (result.failed())
				{
					dlg->show_message(icon_index::error, title, result.format_error(tt.error_rename_failed, {}));
				}
			}
		}

		s.invalidate_view(
			view_invalid::view_layout | view_invalid::group_layout | view_invalid::media_elements |
			view_invalid::app_layout);
	}
}

static void file_properties_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto title = tt.open_properties_title;
	const auto dlg = make_dlg(parent);
	const auto can_process = s.
		can_process_selection_and_mark_errors(view, df::process_items_type::local_file_or_folder);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto items = s.selected_items();
		platform::show_file_properties(items.file_paths(false), items.folder_paths());
	}
}

static void edit_paste_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto dlg = make_dlg(parent);
	const auto data = platform::clipboard();
	platform::file_op_result result;

	if (data->has_drop_files())
	{
		detach_file_handles detach(s);
		result = data->drop_files(s.save_path(), platform::drop_effect::none);
	}
	else if (data->has_bitmap())
	{
		detach_file_handles detach(s);
		result = data->save_bitmap(s.save_path(), tt.pasted_file_name, true);
	}

	if (result.failed())
	{
		dlg->show_message(icon_index::error, tt.command_edit_paste,
			str::is_empty(result.error_message) ? tt.error_unknown : result.error_message);
	}
	else
	{
		s.open(view, s.search(), make_unique_paths(result.created_files));
	}
}

static void show_flatten_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto current_search = s.search();

	if (current_search.has_selector())
	{
		const auto first_selector = current_search.selectors().front();

		if (first_selector.is_recursive())
		{
			auto a = current_search;
			a.remove_recursive();
			s.open(view, a, {});
		}
		else
		{
			df::search_t a;

			for (const auto& sel : current_search.selectors())
			{
				a.add_selector(df::item_selector(sel.folder(), true));
			}

			s.open(view, a, {});
		}
	}
}

static void setting_invoke(view_state& s, bool& val, const bool new_val)
{
	val = new_val;
	s.invalidate_view(view_invalid::view_layout | view_invalid::group_layout | view_invalid::app_layout);
}

static file_encode_params make_file_encode_params()
{
	file_encode_params result;
	result.jpeg_save_quality = setting.jpeg_save_quality;
	return result;
}

static void rotate_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
	const simple_transform t)
{
	auto dlg = make_dlg(parent);
	const auto title = t == simple_transform::rot_90 ? tt.command_rotate_clockwise : tt.command_rotate_anticlockwise;
	const auto icon = t == simple_transform::rot_90 ? icon_index::rotate_clockwise : icon_index::rotate_anticlockwise;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::can_save_pixels);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto& items = s.selected_items();
		bool do_rotate = true;

		if (items.size() > 1)
		{
			files ff;
			const auto thumb = s.first_selected_thumb();

			std::vector<view_element_ptr> controls;
			controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
				dlg->_frame, icon, title, format_plural_text(tt.rotate_info_fmt, items), items.thumbs())));
			controls.emplace_back(std::make_shared<divider_element>());

			if (is_valid(thumb))
			{
				const auto surface = ff.image_to_surface(thumb);
				controls.emplace_back(std::make_shared<ui::before_after_control>(surface, surface->transform(t)));
			}

			controls.emplace_back(std::make_shared<divider_element>());
			controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_rotate));

			do_rotate = dlg->show_modal(controls) == ui::close_result::ok;
		}

		if (do_rotate)
		{
			record_feature_use(features::rotate);

			detach_file_handles detach(s);
			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());

			s.queue_async(async_queue::work, [items, results, t]()
				{
					result_scope rr(results);
					std::u8string message;

					for (const auto& i : items.items())
					{
						results->start_item(i->name());
						platform::file_op_result update_result;
						const auto path = i->path();

						files ff;
						const auto load_result = ff.load(path, false);

						if (load_result.success)
						{
							image_edits pt_edit;
							metadata_edits md_edits;

							const auto current_orientation = setting.show_rotated ? load_result.orientation() : ui::orientation::top_left;
							const auto crop = quadd(load_result.dimensions()).transform(to_simple_transform(current_orientation)).transform(t);

							if (current_orientation != ui::orientation::top_left)
							{
								md_edits.orientation = ui::orientation::top_left;
							}

							pt_edit.crop_bounds(crop);
							update_result = ff.update(path, path, md_edits, pt_edit, make_file_encode_params(), false, i->xmp());
							if (!update_result.success()) message = update_result.format_error();
						}

						results->end_item(i->name(), to_status(update_result.code));

						if (results->is_canceled())
							break;
					}

					rr.complete(message);
				});

			results->wait_for_complete();

			s.item_index.queue_scan_modified_items(items);
		}
	}
}

static void desktop_background_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	s.capture_display([&s, parent](const file_load_result& loaded)
		{
			const auto title = tt.command_desktop_background;
			auto dlg = make_dlg(parent);
			std::vector<view_element_ptr> controls = {
				set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::wallpaper, title,
																tt.desktop_background_info)),
				std::make_shared<divider_element>(),
				std::make_shared<wallpaper_control>(dlg->_frame, loaded, setting.desktop_background.maximize),
				set_margin(std::make_shared<ui::check_control>(dlg->_frame, tt.maximize_image,
															   setting.desktop_background.maximize, false, false,
															   [d = dlg->_frame](bool checked) { d->invalidate(); })),
				std::make_shared<divider_element>(),
				std::make_shared<ui::ok_cancel_control>(dlg->_frame),
			};

			if (dlg->show_modal(controls) == ui::close_result::ok)
			{
				const auto write_extension = u8".png"sv;
				const auto write_path = df::file_path(known_path(platform::known_folder::app_data), u8"wallpaper"sv,
					write_extension);
				const auto path_temp = platform::temp_file(write_extension);
				const auto bounds = ui::desktop_bounds(true);
				const auto screen_extent = bounds.extent();
				const auto max_dim = std::max(screen_extent.cx, screen_extent.cy);
				const auto dimensions = setting.desktop_background.maximize ? sizei(max_dim, max_dim) : screen_extent;

				image_edits edits(dimensions);
				files ff;

				if (!ff.save(path_temp, loaded))
				{
					dlg->show_message(icon_index::error, title, tt.update_failed);
				}
				else
				{
					const auto update_result = ff.update(path_temp, write_path, {}, edits, make_file_encode_params(), false,
						{});

					if (update_result.failed())
					{
						dlg->show_message(icon_index::error, title,
							update_result.format_error(tt.error_create_file_failed_fmt));
					}
					else
					{
						platform::set_desktop_wallpaper(write_path);
					}
				}

				if (path_temp.exists())
				{
					platform::delete_file(path_temp);
				}
			}
		});
}

static void capture_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	s.capture_display([&s, parent](file_load_result lr)
		{
			const auto item = s.command_item();

			if (item && !lr.is_empty())
			{
				auto i = 1;
				const auto source_path = item->path();
				const auto save_ext = lr.is_jpeg() ? u8"jpg"sv : u8"png"sv;
				const auto save_folder = s.save_path();
				const auto save_name = source_path.file_name_without_extension();
				auto save_path = df::file_path(save_folder, save_name, save_ext);

				while (save_path.exists())
				{
					save_path = df::file_path(save_folder, str::format(u8"{}-{}"sv, save_name, i++), save_ext);
				}

				if (platform::prompt_for_save_path(save_path))
				{
					files ff;

					if (!ff.save(save_path, lr))
					{
						const auto dlg = make_dlg(parent);
						dlg->show_message(icon_index::error, tt.command_capture, tt.error_save_image);
					}
				}
			}
		});
}

static void related_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto i = s.command_item();

	if (i)
	{
		df::related_info r;
		r.load(i);
		s.open(view, df::search_t().related(r), {});
	}
}

static void edit_metadata_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	const auto title = tt.command_edit_metadata;
	const auto icon = icon_index::album_artist;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		// https://paulmaguirephoto.com/2019/how-to-customise-the-metadata-panel-in-lightroom/

		const auto& items = s.selected_items();
		std::vector<view_element_ptr> controls;

		controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
			dlg->_frame, icon, title, format_plural_text(tt.edit_metadata_fmt, items), items.thumbs())));
		controls.emplace_back(std::make_shared<divider_element>());

		const auto col0 = std::make_shared<ui::group_control>();
		const auto col4 = std::make_shared<ui::group_control>();
		const auto col1 = std::make_shared<ui::group_control>();

		const auto artist = std::make_shared<ui::check_control>(dlg->_frame, tt.prop_name_artist, setting.set_artist);
		artist->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.artist));
		col0->add(artist);

		const auto description = std::make_shared<ui::check_control>(dlg->_frame, tt.prop_name_description,
			setting.set_caption);
		description->child(std::make_shared<ui::multi_line_edit_control>(dlg->_frame, setting.caption, 10, true));
		col0->add(description);

		col4->add(std::make_shared<ui::title_control>(tt.media_metadata_title));

		const auto album = std::make_shared<ui::check_control>(dlg->_frame, tt.prop_name_album, setting.set_album);
		album->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.album));
		col4->add(album);

		const auto albumArtist = std::make_shared<ui::check_control>(dlg->_frame, tt.album_artist,
			setting.set_album_artist);
		albumArtist->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.album_artist));
		col4->add(albumArtist);

		auto genres = tt.add_translate_text(s.item_index.distinct_genres(), u8"genre"sv);
		std::ranges::sort(genres, str::iless());

		const auto genre = std::make_shared<ui::check_control>(dlg->_frame, tt.prop_name_genre, setting.set_genre);
		genre->child(std::make_shared<ui::edit_picker_control>(dlg->_frame, setting.genre, genres));
		col4->add(genre);

		const auto show = std::make_shared<ui::check_control>(dlg->_frame, tt.prop_name_show, setting.set_tv_show);
		show->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.tv_show));
		col4->add(show);

		col1->add(std::make_shared<ui::title_control>(tt.copyright_title));

		const auto copyright_notice = std::make_shared<ui::check_control>(dlg->_frame, tt.copyright_notice,
			setting.set_copyright_notice);
		copyright_notice->child(
			std::make_shared<ui::multi_line_edit_control>(dlg->_frame, setting.copyright_notice, 8, true));
		col1->add(copyright_notice);

		const auto copyright_creator = std::make_shared<ui::check_control>(dlg->_frame, tt.copyright_creator,
			setting.set_copyright_creator);
		copyright_creator->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.copyright_creator));
		col1->add(copyright_creator);

		const auto copyright_source = std::make_shared<ui::check_control>(dlg->_frame, tt.copyright_source,
			setting.set_copyright_source);
		copyright_source->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.copyright_source));
		col1->add(copyright_source);

		const auto copyright_credit = std::make_shared<ui::check_control>(dlg->_frame, tt.copyright_credit,
			setting.set_copyright_credit);
		copyright_credit->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.copyright_credit));
		col1->add(copyright_credit);

		const auto copyright_url = std::make_shared<ui::check_control>(dlg->_frame, tt.copyright_url,
			setting.set_copyright_url);
		copyright_url->child(std::make_shared<ui::edit_control>(dlg->_frame, setting.copyright_url));
		col1->add(copyright_url);

		auto cols = std::make_shared<ui::col_control>();
		cols->add(col0);
		cols->add(col1);
		cols->add(col4);

		controls.emplace_back(cols);
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_update));

		if (ui::close_result::ok == dlg->show_modal(controls, { 111 }))
		{
			record_feature_use(features::batch_edit);

			metadata_edits edits;
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

			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());
			s.modify_items(results, icon, title, items.items(), edits, view);
		}
	}
}


class location_auto_complete_strategy;

class location_auto_complete final : public ui::auto_complete_match,
	public std::enable_shared_from_this<location_auto_complete>
{
public:
	location_match match;
	ui::complete_strategy_t& _parent;

	std::u8string _id;
	std::u8string _text;
	std::vector<str::part_t> _highlights;

	explicit location_auto_complete(ui::complete_strategy_t& parent, location_match m, const int w) :
		auto_complete_match(view_element_style::can_invoke), match(std::move(m)), _parent(parent)
	{
		weight = w;
	}

	explicit location_auto_complete(ui::complete_strategy_t& parent, const location_t& loc, const int w) :
		auto_complete_match(view_element_style::can_invoke), _parent(parent)
	{
		weight = w;
		match.city.text = loc.place;
		match.state.text = loc.state;
		match.country.text = loc.country;
		match.location = loc;
	}

	explicit location_auto_complete(ui::complete_strategy_t& parent, std::u8string id, std::u8string text,
		std::vector<str::part_t> highlights, const int w) :
		auto_complete_match(view_element_style::can_invoke), _parent(parent), _id(std::move(id)),
		_text(std::move(text)), _highlights(std::move(highlights))
	{
		weight = w;
	}

	std::u8string edit_text() const override
	{
		return match.location.str();
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg_color = calc_background_color(dc);

		if (bg_color.a > 0.0f)
		{
			const auto pad = padding * dc.scale_factor;
			dc.draw_rounded_rect(logical_bounds.inflate(pad.cx, pad.cy), bg_color, dc.padding1);
		}

		const auto highlight_clr = ui::color(ui::style::color::dialog_selected_text, dc.colors.alpha);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
		const auto city_extent = dc.measure_text(match.city.text, ui::style::font_face::dialog,
			ui::style::text_style::single_line, bounds.width());
		const auto state_extent = dc.measure_text(match.state.text, ui::style::font_face::dialog,
			ui::style::text_style::single_line, bounds.width());
		//const auto country_extent = dc.measure_text(_match.country.text, render::style::font_size::dialog, render::style::text_style::single_line, bounds.width());

		if (str::is_empty(_text))
		{
			auto rr = logical_bounds;
			dc.draw_text(match.city.text, make_highlights(match.city.highlights, highlight_clr), rr,
				ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += city_extent.cx + dc.padding2;
			dc.draw_text(match.state.text, make_highlights(match.state.highlights, highlight_clr), rr,
				ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += state_extent.cx + dc.padding2;
			dc.draw_text(match.country.text, make_highlights(match.country.highlights, highlight_clr), rr,
				ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += state_extent.cx + dc.padding2;
		}
		else
		{
			dc.draw_text(_text, make_highlights(_highlights, highlight_clr), logical_bounds,
				ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
		const pointi element_offset,
		const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::click)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::click);
		}
		else if (event.type == view_element_event_type::double_click)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::double_click);
		}
	}
};


static void fetch(async_strategy& async, std::u8string key, std::u8string host, std::u8string path, std::function<void(std::u8string)> f)
{
	async.web_service_cache(
		key, [&async, key,
		host = std::move(host),
		path = std::move(path),
		f = std::move(f)](const std::u8string& cache_response)
		{
			if (cache_response.empty())
			{
				platform::web_request req;
				req.host = host;
				req.path = path;

				async.queue_async(async_queue::web, [&async, req, f, key]()
					{
						auto response = send_request(req);

						// only cache if success
						if (response.status_code == 200)
						{
							async.web_service_cache(key, response.body);
						}

						return f(std::move(response.body));
					});
			}
			else
			{
				f(cache_response);
			}
		});
}

static void fetch_place(async_strategy& async, const std::u8string_view place_id, std::function<void(std::u8string)> f)
{
	const auto url = str::format(u8"/maps/api/place/details/json?placeid={}&key={}"sv, df::url_encode(place_id), google_maps_api_key);
	const auto key = str::format(u8"place_details:{}"sv, place_id);
	fetch(async, key, u8"maps.googleapis.com"s, url, std::move(f));
}

static str::cached find_component(const rapidjson::GenericValue<rapidjson::UTF8<char8_t>>& json, const std::u8string& component_field, const std::u8string_view component_name)
{
	if (json.HasMember(component_field))
	{
		for (const auto& component : json[component_field].GetArray())
		{
			if (component.HasMember(u8"types"))
			{
				for (const auto& type : component[u8"types"].GetArray())
				{
					if (str::icmp(component_name, type.GetString()) == 0)
					{
						return str::cache(df::util::json::safe_string(component, u8"long_name"));
					}
				}
			}
		}
	}

	return {};
}

class location_auto_complete_strategy final : public std::enable_shared_from_this<location_auto_complete_strategy>,
	public ui::complete_strategy_t
{
public:
	view_state& _state;
	ui::control_frame_ptr _parent;
	ui::auto_complete_results _results;
	ui::auto_complete_match_ptr _selected;

	df::hash_set<std::u8string, df::ihash, df::ieq> _recent_location_set;
	std::vector<std::u8string> _recent_locations;

	bool _empty_query = true;
	std::function<void(std::shared_ptr<location_auto_complete>)> _changed;

	location_auto_complete_strategy(view_state& s, ui::control_frame_ptr parent,
		std::function<void(std::shared_ptr<location_auto_complete>)> changed,
		const std::vector<std::u8string>& recent_locations) : _state(s),
		_parent(std::move(parent)),
		_recent_location_set(recent_locations.begin(), recent_locations.end()), _recent_locations(recent_locations),
		_changed(std::move(changed))
	{
		resize_to_show_results = false;
		max_predictions = 15u;

		std::ranges::reverse(_recent_locations);

		if (_recent_locations.size() > max_predictions)
		{
			_recent_locations.resize(max_predictions);
		}
	}

	~location_auto_complete_strategy() override
	{
	}

	std::u8string no_results_message() override
	{
		return std::u8string(tt.type_to_search);
	}

	void initialise(std::function<void(const ui::auto_complete_results&)> complete) override
	{
		search({}, std::move(complete));
	}

	void show_results(const ui::auto_complete_results& found,
		std::function<void(const ui::auto_complete_results&)> complete)
	{
		_state.queue_ui([t = shared_from_this(), complete = std::move(complete), found]()
			{
				t->_results = found;
				if (t->_results.size() > t->max_predictions) t->_results.resize(t->max_predictions);
				complete(t->_results);
			});
	}

	int calc_weight(const location_match& lm) const
	{
		const auto is_recent = _recent_location_set.contains(str::to_string(lm.location.id));
		return is_recent ? 1 : 1 + df::round(lm.distance_away);
	}

	static bool compare_weight(const ui::auto_complete_match_ptr& l, const ui::auto_complete_match_ptr& r)
	{
		const auto diff = l->weight - r->weight;
		return diff == 0 ? str::icmp(l->edit_text(), r->edit_text()) < 0 : diff < 0;
	}

	void search(const std::u8string& query, std::function<void(const ui::auto_complete_results&)> complete) override
	{
		_empty_query = str::is_empty(query);

		_state.queue_location(
			[t = shared_from_this(), query, complete = std::move(complete), &s = _state](location_cache& locations)
			{
				const auto query_len = query.size();

				if (query_len > 0)
				{
					auto locally_found = locations.auto_complete(query, t->max_predictions, setting.default_location);

					if (query_len <= 3 || locally_found.size() > 1)
					{
						ui::auto_complete_results found;

						for (const auto& local : locally_found)
						{
							found.emplace_back(
								std::make_shared<location_auto_complete>(*t, local, t->calc_weight(local)));
						}

						std::ranges::stable_sort(found, compare_weight);
						t->show_results(found, complete);
					}
					else
					{
						// Only use google for queries over length 3
						const auto key = str::format(u8"auto_complete_location:{}"sv, query);
						const auto path = str::format(
							u8"/maps/api/place/autocomplete/json?input={}&key={}"sv,
							df::url_encode(query), google_maps_api_key);

						fetch(t->_state._async, key, u8"maps.googleapis.com"s, path,
							[t, query, complete, locally_found](std::u8string response)
							{
								ui::auto_complete_results found;

								for (const auto& local : locally_found)
								{
									found.emplace_back(
										std::make_shared<location_auto_complete>(
											*t, local, t->calc_weight(local)));
								}

								df::util::json::json_doc json_response;
								json_response.Parse(response.data(), response.size());

								if (json_response.HasMember(u8"predictions"))
								{
									for (const auto& loc : json_response[u8"predictions"].GetArray())
									{
										auto text = df::util::json::safe_string(loc, u8"description");
										auto place_id = df::util::json::safe_string(loc, u8"place_id");

										std::vector<str::part_t> selections;

										if (loc.HasMember(u8"matched_substrings"s))
										{
											for (const auto& match : loc[u8"matched_substrings"].GetArray())
											{
												const auto length = static_cast<size_t>(df::util::json::safe_int(
													match, u8"length"));
												const auto offset = static_cast<size_t>(df::util::json::safe_int(
													match, u8"offset"));

												selections.emplace_back(offset, length);
											}
										}

										const auto is_recent = t->_recent_location_set.contains(place_id);
										found.emplace_back(
											std::make_shared<location_auto_complete>(
												*t, place_id, text, selections, is_recent ? 2 : 1));
									}

									std::ranges::stable_sort(found, compare_weight);
									t->show_results(found, complete);
								}
							});
					}
				}
				else
				{
					ui::auto_complete_results results;

					for (const auto& recent_place_id : t->_recent_locations)
					{
						if (str::is_num(recent_place_id))
						{
							const auto id = str::to_int(recent_place_id);
							auto loc = locations.find_by_id(id);

							if (!loc.is_empty())
							{
								results.emplace_back(
									std::make_shared<location_auto_complete>(*t, loc, 100 - static_cast<int>(results.size())));
							}
						}
						else
						{
							platform::thread_event event_wait(true, false);

							fetch_place(t->_state._async, recent_place_id,
								[t, &event_wait, recent_place_id, &results](std::u8string response)
								{
									df::util::json::json_doc json_response;
									json_response.Parse(response);

									if (json_response.HasMember(u8"result"))
									{
										std::vector<str::part_t> highlights;
										const auto formatted_search = df::util::json::safe_string(
											json_response[u8"result"], u8"formatted_address");
										results.emplace_back(
											std::make_shared<location_auto_complete>(
												*t, recent_place_id, formatted_search, highlights,
												100 - static_cast<int>(results.size())));
									}

									event_wait.set();
								});

							platform::wait_for({ event_wait }, 10000, false);
						}
					}

					if (results.empty())
					{
						const auto loc = locations.find_closest(setting.default_location.latitude(),
							setting.default_location.longitude());

						if (!loc.is_empty())
						{
							results.emplace_back(
								std::make_shared<location_auto_complete>(*t, loc, 100 - static_cast<int>(results.size())));
						}
					}

					t->show_results(results, complete);
				}
			});
	}

	void selected(const ui::auto_complete_match_ptr& i, const select_type st) override
	{
		if (i)
		{
			const auto ii = std::dynamic_pointer_cast<location_auto_complete>(i);

			if (ii && _changed)
			{
				_changed(ii);
			}

			if (_selected)
			{
				_selected->set_style_bit(view_element_style::selected, false);
			}

			_selected = i;

			if (_selected)
			{
				_selected->set_style_bit(view_element_style::selected, true);
			}

			if (st == select_type::double_click)
			{
				_parent->close(false);
			}
		}
		else
		{
			_selected = {};
		}
	}

	ui::auto_complete_match_ptr selected() const override
	{
		return _selected;
	}
};


static void id_to_location(async_strategy& async, const std::u8string place_id, std::function<void(location_t)> cb)
{
	if (str::is_num(place_id))
	{
		async.queue_location([&async, place_id, cb](location_cache& locations)
			{
				const auto id = str::to_int(place_id);
				auto loc = locations.find_by_id(id);

				if (!loc.is_empty())
				{
					async.queue_ui([cb, loc]() { cb(loc);  });
				}
			});
	}
	else
	{
		fetch_place(async, place_id,
			[&async, place_id, cb](std::u8string response)
			{
				df::util::json::json_doc json_response;
				json_response.Parse(response);

				if (json_response.HasMember(u8"result"))
				{
					const std::u8string component_field = u8"address_components";
					const auto& address = json_response[u8"result"];
					auto place = find_component(address, component_field, u8"locality");
					if (place.is_empty())  place = find_component(address, component_field, u8"sublocality");
					if (place.is_empty())  place = find_component(address, component_field, u8"route");
					if (place.is_empty())  place = find_component(address, component_field, u8"postal_town");
					const auto state = find_component(address, component_field, u8"administrative_area_level_2");
					const auto country = find_component(address, component_field, u8"country");
					const gps_coordinate position(address[u8"geometry"][u8"location"][u8"lat"].GetDouble(),
						address[u8"geometry"][u8"location"][u8"lng"].GetDouble());

					location_t loc(0, place, state, country, position, 0.0);
					async.queue_ui([cb, loc]() { cb(loc);  });

				}
			});
	}
}

bool ui::browse_for_location(view_state& vs, const control_frame_ptr& parent, gps_coordinate& position)
{
	auto dlg = make_dlg(parent);
	const auto title = tt.select_location;
	const auto icon = icon_index::location;

	const auto ls = std::make_shared<selected_location_t>();

	const auto place_edit = std::make_shared<text_element>(ls->place_text);
	const auto state_edit = std::make_shared<text_element>(ls->state_text);
	const auto country_edit = std::make_shared<text_element>(ls->country_text);
	const auto latitude_edit = std::make_shared<text_element>(str::to_string(ls->latitude, 5));
	const auto longitude_edit = std::make_shared<text_element>(str::to_string(ls->longitude, 5));

	auto populate_place = [ls, place_edit, state_edit, country_edit, latitude_edit, longitude_edit, dlg
	](const location_t& loc)
		{
			df::assert_true(is_ui_thread());

			ls->id = loc.id;
			ls->place_text = loc.place;
			ls->state_text = loc.state;
			ls->country_text = loc.country;
			ls->latitude = loc.position.latitude();
			ls->longitude = loc.position.longitude();

			view_element_event e{ view_element_event_type::populate, dlg };
			place_edit->text(ls->place_text);
			state_edit->text(ls->state_text);
			country_edit->text(ls->country_text);
			latitude_edit->text(str::to_string(ls->latitude, 5));
			longitude_edit->text(str::to_string(ls->longitude, 5));

			dlg->_frame->invalidate();
			dlg->layout();
		};

	auto coord_changed = [&vs, populate_place](const gps_coordinate coord)
		{
			vs._async.queue_location([&vs, coord, populate_place](location_cache& locations)
				{
					auto place = locations.find_closest(coord.latitude(), coord.longitude());
					place.position = coord;
					vs._async.queue_ui([place, populate_place] { populate_place(place); });
				});
		};

	auto map = std::make_shared<map_control>(dlg->_frame, coord_changed);

	auto sel_changed = [&vs, map, populate_place](const std::shared_ptr<location_auto_complete>& sel)
		{
			if (str::is_empty(sel->_id))
			{
				map->set_location_marker(sel->match.location.position);
				populate_place(sel->match.location);
			}
			else
			{
				id_to_location(vs._async, sel->_id, [map, populate_place](location_t loc)
					{
						map->set_location_marker(loc.position);
						populate_place(loc);
					});
			}
		};

	map->init_map(azure_maps_api_key);

	auto strategy = std::make_shared<location_auto_complete_strategy>(vs, dlg->_frame, sel_changed,
		vs.recent_locations.items());
	const auto search_control = std::make_shared<ui::search_control>(dlg->_frame, ls->search_text, strategy);

	const auto map_col = std::make_shared<group_control>();
	map_col->add(map);
	map_col->add(std::make_shared<text_element>(tt.click_map_to_select, view_element_style::center));

	const auto search_col = std::make_shared<group_control>();
	search_col->add(std::make_shared<text_element>(tt.type_to_search, view_element_style::center));
	search_col->add(search_control);

	const auto props_table = std::make_shared<table_element>();
	props_table->add(tt.prop_name_place, place_edit);
	props_table->add(tt.prop_name_state, state_edit);
	props_table->add(tt_prep(tt.prop_name_country), country_edit);
	props_table->add(tt.prop_name_latitude, latitude_edit);
	props_table->add(tt.prop_name_longitude, longitude_edit);

	const auto props_col = std::make_shared<group_control>();
	props_col->add(std::make_shared<title_control>(tt.metadata));
	props_col->add(props_table);

	const auto cols = std::make_shared<col_control>();
	cols->add(search_col);
	cols->add(map_col, { 66 });
	cols->add(props_col);

	const std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<title_control2>(dlg->_frame, icon, title, std::u8string{})),
		std::make_shared<divider_element>(),
		cols,
		std::make_shared<divider_element>(),
		set_margin(std::make_shared<ok_cancel_control>(dlg->_frame)),
	};

	if (close_result::ok == dlg->show_modal(controls, { 122 }))
	{
		position = { ls->latitude, ls->longitude };
		return true;
	}

	return false;
}

static void locate_invoke(view_state& vs, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	const auto title = tt.command_locate;
	const auto icon = icon_index::location;
	const auto can_process = vs.can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	pause_media pause(vs);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto ls = std::make_shared<selected_location_t>();

		const auto place_edit = std::make_shared<ui::edit_control>(dlg->_frame, ls->place_text);
		const auto state_edit = std::make_shared<ui::edit_control>(dlg->_frame, ls->state_text);
		const auto country_edit = std::make_shared<ui::edit_control>(dlg->_frame, ls->country_text);
		const auto latitude_edit = std::make_shared<ui::float_control>(dlg->_frame, ls->latitude);
		const auto longitude_edit = std::make_shared<ui::float_control>(dlg->_frame, ls->longitude);

		auto populate_place = [ls, place_edit, state_edit, country_edit, latitude_edit, longitude_edit, dlg
		](const location_t& loc)
			{
				df::assert_true(ui::is_ui_thread());

				ls->id = loc.id;
				ls->place_text = loc.place;
				ls->state_text = loc.state;
				ls->country_text = loc.country;
				ls->latitude = loc.position.latitude();
				ls->longitude = loc.position.longitude();

				const view_element_event e{ view_element_event_type::populate, dlg };
				place_edit->dispatch_event(e);
				state_edit->dispatch_event(e);
				country_edit->dispatch_event(e);
				latitude_edit->dispatch_event(e);
				longitude_edit->dispatch_event(e);
			};

		auto coord_changed = [&vs, populate_place](const gps_coordinate coord)
			{
				vs._async.queue_location([&vs, coord, populate_place](location_cache& locations)
					{
						auto place = locations.find_closest(coord.latitude(), coord.longitude());
						place.position = coord;
						vs._async.queue_ui([place, populate_place] { populate_place(place); });
					});
			};

		auto map = std::make_shared<map_control>(dlg->_frame, coord_changed);

		auto sel_changed = [&vs, map, populate_place](const std::shared_ptr<location_auto_complete>& sel)
			{
				if (str::is_empty(sel->_id))
				{
					map->set_location_marker(sel->match.location.position);
					populate_place(sel->match.location);
				}
				else
				{
					id_to_location(vs._async, sel->_id, [map, populate_place](location_t loc)
						{
							map->set_location_marker(loc.position);
							populate_place(loc);
						});
				}
			};

		map->init_map(azure_maps_api_key);

		auto strategy = std::make_shared<location_auto_complete_strategy>(
			vs, dlg->_frame, sel_changed, vs.recent_locations.items());
		const auto search_control = std::make_shared<ui::search_control>(dlg->_frame, ls->search_text, strategy);

		const auto map_col = std::make_shared<ui::group_control>();
		map_col->add(map);
		map_col->add(std::make_shared<text_element>(tt.click_map_to_select, view_element_style::center));

		const auto search_col = std::make_shared<ui::group_control>();
		search_col->add(std::make_shared<text_element>(tt.type_to_search, view_element_style::center));
		search_col->add(search_control);

		const auto props_col = std::make_shared<ui::group_control>();
		props_col->add(std::make_shared<ui::title_control>(tt.metadata));
		props_col->add(std::make_shared<text_element>(tt.prop_name_place));
		props_col->add(place_edit);
		props_col->add(std::make_shared<text_element>(tt.prop_name_state));
		props_col->add(state_edit);
		props_col->add(std::make_shared<text_element>(tt_prep(tt.prop_name_country)));
		props_col->add(country_edit);
		props_col->add(std::make_shared<text_element>(tt.prop_name_latitude));
		props_col->add(latitude_edit);
		props_col->add(std::make_shared<text_element>(tt.prop_name_longitude));
		props_col->add(longitude_edit);

		auto gps_overwrite_count = 0;

		for (const auto& i : vs.selected_items().items())
		{
			if (i->has_gps())
			{
				++gps_overwrite_count;
			}
		}

		if (gps_overwrite_count > 0)
		{
			const auto element = std::make_shared<text_element>(
				format_plural_text(tt.gps_overwrite_count_fmt, vs.selected_items()),
				view_element_style::grow | view_element_style::important);
			element->padding = { 8, 8 };
			element->margin = { 8, 8 };
			props_col->add(element);
		}

		const auto cols = std::make_shared<ui::col_control>();
		cols->add(search_col);
		cols->add(map_col, { 66 });
		cols->add(props_col);

		const std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title,
															format_plural_text(
																tt.be_updated_fmt, vs.selected_items().items()),
															vs.selected_items().thumbs())),
			std::make_shared<divider_element>(),
			cols,
			std::make_shared<divider_element>(),
			set_margin(std::make_shared<ui::ok_cancel_control>(dlg->_frame)),
		};

		if (ui::close_result::ok == dlg->show_modal(controls, { 122 }))
		{
			record_feature_use(features::locate);

			metadata_edits edits;
			edits.location_place = ls->place_text;
			edits.location_state = ls->state_text;
			edits.location_country = ls->country_text;
			edits.location_coordinate = { ls->latitude, ls->longitude };

			vs.recent_locations.add(str::to_string(ls->id));

			const auto results = std::make_shared<command_status>(vs._async, dlg, icon, title, vs.selected_count());
			vs.modify_items(results, icon, title, vs.selected_items().items(), edits, view);
		}
	}
}

static void convert_resize_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	const auto title = tt.command_convert_or_resize;
	const auto icon = icon_index::photo;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::photos_only);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto& items = s.selected_items();
		std::vector<view_element_ptr> controls;

		controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
			dlg->_frame, icon, title, format_plural_text(tt.convert_info_fmt, items), items.thumbs())));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(set_margin(std::make_shared<text_element>(tt.destination_folder)));
		controls.emplace_back(
			set_margin(std::make_shared<ui::folder_picker_control>(dlg->_frame, setting.write_folder)));

		auto jpeg = std::make_shared<ui::check_control>(dlg->_frame, tt.jpeg_best, setting.convert.to_jpeg, true);
		auto jpeg_group = std::make_shared<ui::group_control>();
		jpeg_group->add(std::make_shared<text_element>(tt.options_jpeg_quality));
		jpeg_group->add(std::make_shared<ui::slider_control>(dlg->_frame, setting.convert.jpeg_quality, 1, 100));
		jpeg->child(jpeg_group);
		controls.emplace_back(jpeg);

		auto png = std::make_shared<ui::check_control>(dlg->_frame, tt.png_best, setting.convert.to_png, true);
		auto png_group = std::make_shared<ui::group_control>();
		png->child(png_group);
		controls.emplace_back(png);

		auto webp = std::make_shared<ui::check_control>(dlg->_frame, tt.webp_best, setting.convert.to_webp, true);
		auto webp_group = std::make_shared<ui::group_control>();
		//webp_group->add(std::make_shared<text_element>(tt.options_jpeg_quality));
		webp_group->add(std::make_shared<ui::slider_control>(dlg->_frame, setting.convert.webp_quality, 1, 100));
		webp_group->add(
			std::make_shared<ui::check_control>(dlg->_frame, tt.lossless_compression, setting.convert.webp_lossless));
		webp->child(webp_group);
		controls.emplace_back(webp);

		auto dimension = std::make_shared<ui::check_control>(dlg->_frame, tt.limit_output_dimensions,
			setting.convert.limit_dimension);
		dimension->child(
			std::make_shared<ui::num_control>(dlg->_frame, std::u8string_view{}, setting.convert.max_side));
		controls.emplace_back(dimension);

		controls.emplace_back(std::make_shared<ui::check_control>(dlg->_frame, tt.open_dest, setting.show_results));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_convert));


		if (dlg->show_modal(controls) == ui::close_result::ok)
		{
			record_feature_use(features::convert);

			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());
			df::file_path first_successful_path;
			df::folder_path write_folder(setting.write_folder);

			bool can_process = true;
			const auto folder = df::folder_path(setting.write_folder);
			const auto overwrite_result = check_overwrite(folder, items, u8"jpg"sv);

			if (!overwrite_result.empty())
			{
				std::vector<view_element_ptr> controls = {
					std::make_shared<ui::title_control2>(dlg->_frame, icon, title,
														 format_plural_text(tt.would_overwrite_fmt, overwrite_result)),
					std::make_shared<divider_element>(),
					std::make_shared<ui::ok_cancel_control>(dlg->_frame),
				};

				can_process = dlg->show_modal(controls) == ui::close_result::ok;
			}

			if (can_process)
			{
				const auto create_folder_result = platform::create_folder(write_folder);

				if (create_folder_result.failed())
				{
					results->complete(
						create_folder_result.format_error(str::format(tt.failed_to_create_folder_fmt, write_folder)));
					results->wait_for_complete();
				}
				else
				{
					detach_file_handles detach(s);

					s.queue_async(async_queue::work, [items, results, write_folder, &first_successful_path]()
						{
							result_scope rr(results);
							files ff;
							std::u8string message;

							for (const auto& i : items.items())
							{
								const auto mt = i->file_type();
								platform::file_op_result update_result;

								results->start_item(i->name());

								try
								{
									if (mt->has_trait(file_traits::bitmap))
									{
										file_encode_params encode_params;
										encode_params.jpeg_save_quality = setting.convert.jpeg_quality;
										encode_params.webp_quality = setting.convert.webp_quality;
										encode_params.webp_lossless = setting.convert.webp_lossless;

										auto ext = u8".jpg"sv;
										if (setting.convert.to_png) ext = u8".png"sv;
										if (setting.convert.to_webp) ext = u8".webp"sv;

										const auto write_path = df::file_path(
											write_folder, i->path().file_name_without_extension(), ext);
										const auto edits = setting.convert.limit_dimension
											? image_edits(setting.convert.max_side)
											: image_edits();
										update_result = ff.update(i->path(), write_path, {}, edits, encode_params, false,
											i->xmp());
										if (update_result.success() && first_successful_path.is_empty())
											first_successful_path = write_path;
										if (!update_result.success()) message = update_result.format_error();
									}
								}
								catch (std::exception& e)
								{
									df::log(__FUNCTION__, e.what());
								}

								results->end_item(i->name(), to_status(update_result.code));

								if (results->is_canceled())
									break;
							}


							rr.complete(message);
						});

					results->wait_for_complete();

					if (setting.show_results)
					{
						s.open(view, first_successful_path);
					}
				}
			}
		}
	}
}

static void tag_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	const auto icon = icon_index::tag;
	const auto title = tt.tag_add_remove;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto& items = s.selected_items();
		std::vector<view_element_ptr> controls;
		std::shared_ptr<ui::multi_line_edit_control> edit;

		auto cols = std::make_shared<ui::col_control>();
		auto edit_col = std::make_shared<ui::group_control>();
		auto list_control = std::make_shared<ui::group_control>();

		auto tags = setting.last_tags;
		edit_col->add(set_margin(std::make_shared<text_element>(tt.tag_add_or_remove_label)));
		edit_col->add(set_margin(edit = std::make_shared<ui::multi_line_edit_control>(dlg->_frame, tags, 10, false)));
		edit_col->add(set_margin(std::make_shared<text_element>(tt.help_tag1)));
		edit_col->add(set_margin(std::make_shared<text_element>(tt.help_tag2)));
		edit_col->add(set_margin(std::make_shared<text_element>(tt.help_tag_add_remove)));

		auto favorites = str::split(setting.favorite_tags, true);

		if (!favorites.empty())
		{
			list_control->add(set_margin(std::make_shared<text_element>(
				tt.tags_favorite_label, ui::style::font_face::title, ui::style::text_style::multiline,
				view_element_style::line_break)));
			list_control->add(set_margin(std::make_shared<ui::recommended_words_control>(
				dlg->_frame, favorites, [edit](const std::u8string_view tag)
				{
					edit->add_word(str::quote_if_white_space(tag));
				})));
		}

		auto distinct_tags = s.item_index.distinct_tags();

		df::string_counts common_counts;

		for (const auto& t : distinct_tags)
		{
			++common_counts[t.first];
		}

		s.recent_tags.count_strings(common_counts, 1000000);

		for (const auto& f : favorites)
		{
			common_counts.erase(f);
		}

		const auto common = str::combine(top_map(common_counts, 20));
		const auto common_tags = str::split(common, true);
		const auto existing = top_map(s.selected_tags(), 20);

		if (!common_tags.empty())
		{
			list_control->add(set_margin(std::make_shared<text_element>(
				tt.tags_common_label, ui::style::font_face::title, ui::style::text_style::multiline,
				view_element_style::line_break)));
			list_control->add(set_margin(std::make_shared<ui::recommended_words_control>(
				dlg->_frame, common_tags, [edit](const std::u8string_view tag)
				{
					edit->add_word(str::quote_if_white_space(tag));
				})));
		}

		if (!existing.empty())
		{
			list_control->add(set_margin(std::make_shared<text_element>(
				tt.tags_remove_label, ui::style::font_face::title, ui::style::text_style::multiline,
				view_element_style::line_break)));
			list_control->add(set_margin(std::make_shared<ui::recommended_words_control>(
				dlg->_frame, existing, [edit](const std::u8string_view tag)
				{
					edit->add_word(str::format(u8"-{}"sv, str::quote_if_white_space(tag)));
				})));
		}

		cols->add(edit_col);
		cols->add(list_control);

		controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
			dlg->_frame, icon, title, format_plural_text(tt.tag_info_fmt, items), items.thumbs())));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(cols);
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_tag));

		if (dlg->show_modal(controls, { 77 }) == ui::close_result::ok)
		{
			record_feature_use(features::tag);

			setting.last_tags = tags;
			std::vector<std::u8string> adds;
			std::vector<std::u8string> removes;

			search_tokenizer t;

			for (const auto& part : t.parse(tags))
			{
				if (part.modifier.positive)
				{
					adds.emplace_back(part.term);
				}
				else
				{
					removes.emplace_back(part.term);
				}
			}

			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, s.selected_count());

			metadata_edits edits;
			edits.add_tags = tag_set(adds);
			edits.remove_tags = tag_set(removes);
			s.modify_items(results, icon, title, items.items(), edits, view);
			s.recent_tags.add_items(adds);
		}
	}
}


static void adjust_date_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	const auto title = tt.command_adjust_date;
	const auto icon = icon_index::time;
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto& items = s.selected_items();

		df::date_t start_date;
		df::date_t end_date;

		for (const auto& i : items.items())
		{
			auto d = i->media_created();

			if (d.is_valid())
			{
				if (!end_date.is_valid() || end_date < d)
				{
					end_date = d;
				}

				if (!start_date.is_valid() || start_date > d)
				{
					start_date = d;
				}
			}
		}

		auto new_date = start_date;

		const std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title,
															format_plural_text(tt.adjust_date_info_fmt, items),
															items.thumbs())),
			std::make_shared<divider_element>(),
			set_margin(std::make_shared<text_element>(tt.adjust_date_help)),
			std::make_shared<divider_element>(),
			set_margin(std::make_shared<text_element>(tt.selected_date_range_label)),
			set_margin(std::make_shared<bullet_element>(icon_index::bullet, str::format(tt.starting_fmt, start_date))),
			set_margin(std::make_shared<bullet_element>(icon_index::bullet, str::format(tt.ending_fmt, end_date))),
			set_margin(std::make_shared<text_element>(tt.starting_date_label)),
			set_margin(std::make_shared<ui::date_control>(dlg->_frame, std::u8string_view{}, new_date)),
			std::make_shared<divider_element>(),
			std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_update),
		};

		if (dlg->show_modal(controls) == ui::close_result::ok)
		{
			record_feature_use(features::adjust_date);
			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());

			s.queue_async(async_queue::work, [items, results, new_date, start_date]()
				{
					result_scope rr(results);
					files ff;

					for (const auto& i : items.items())
					{
						results->start_item(i->name());

						auto created = i->media_created();
						df::date_t dt = created + (new_date - start_date);
						metadata_edits edits;
						edits.created = dt;
						const auto update_result = ff.update(i->path(), edits, {}, make_file_encode_params(), false,
							i->xmp());
						platform::created_date(i->path(), dt.local_to_system());

						results->end_item(i->name(), to_status(update_result.code));

						if (results->is_canceled())
							break;
					}

					rr.complete();
				});

			results->wait_for_complete();
		}
	}
};

static void rate_items_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view, int r)
{
	auto dlg = make_dlg(parent);
	metadata_edits edits;
	edits.rating = r;

	const auto icon = icon_index::star;
	const auto title = tt.title_updating;
	const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, s.selected_count());

	s.modify_items(results, icon, title, s.selected_items().items(), edits, view);
}

static void label_items_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
	const std::u8string_view label)
{
	const auto selected = s.selected_items();
	auto dlg = make_dlg(parent);
	metadata_edits edits;
	edits.label = label;

	const auto icon = icon_index::star;
	const auto title = tt.title_updating;
	const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, selected.items().size());

	s.modify_items(results, icon, title, selected.items(), edits, view);
}

static void cut_copy_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
	const bool is_move)
{
	const auto dlg = make_dlg(parent);
	const auto title = is_move ? tt.command_edit_cut : tt.command_edit_copy;
	const auto can_process = s.can_process_selection_and_mark_errors(
		view, is_move ? df::process_items_type::local_file_or_folder : df::process_items_type::local_file_or_folder);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto items = s.selected_items();
		const auto item_paths = items.file_paths(true);
		const auto folder_paths = items.folder_paths();

		s.capture_display([item_paths, folder_paths, is_move = is_move](const file_load_result& loaded)
			{
				platform::set_clipboard(item_paths, folder_paths, loaded, is_move);
			});
	}
}

class folder_auto_complete final : public std::enable_shared_from_this<folder_auto_complete>,
	public ui::complete_strategy_t
{
private:
	view_state& _state;
	df::folder_counts _folders;
	std::vector<df::folder_path> _recents;
	ui::control_frame_ptr _parent;
	ui::auto_complete_match_ptr _selected;

public:
	std::u8string no_results_message() override
	{
		return std::u8string(tt.type_to_search);
	}

	std::weak_ptr<ui::search_control> _search_control;


	folder_auto_complete(view_state& s, ui::control_frame_ptr parent) : _state(s), _parent(std::move(parent))
	{
		resize_to_show_results = false;
		auto_select_first = false;
		folder_select_button = true;
		max_predictions = 12u;

		_folders = _state.known_folders();

		auto recents = _state.recent_folders.items();
		std::ranges::reverse(recents);

		if (recents.size() > max_predictions)
		{
			recents.resize(max_predictions);
		}

		for (const auto& i : recents)
		{
			_recents.emplace_back(df::folder_path(i));
		}
	}

	void initialise(std::function<void(const ui::auto_complete_results&)> complete) override
	{
		search({}, std::move(complete));
	}

	static bool compare_weight(const ui::auto_complete_match_ptr& l, const ui::auto_complete_match_ptr& r)
	{
		const auto diff = l->weight - r->weight;
		return diff == 0 ? str::icmp(l->edit_text(), r->edit_text()) < 0 : diff > 0;
	}

	using folder_match_ptr = std::shared_ptr<folder_match>;
	using results_by_folder = df::hash_map<df::folder_path, folder_match_ptr, df::ihash, df::ieq>;

	void add_result(results_by_folder& results, const df::folder_path folder, const ui::match_highlights& m,
		const int weight)
	{
		const auto found = results.find(folder);

		if (found == results.end())
		{
			results.emplace(folder, std::make_shared<folder_match>(*this, folder, m, weight));
		}
		else
		{
			found->second->weight += weight;
		}
	}

	void search(const std::u8string& query, std::function<void(const ui::auto_complete_results&)> complete) override
	{
		df::assert_true(ui::is_ui_thread());

		if (query.empty())
		{
			ui::auto_complete_results results;

			for (const auto& folder : _recents)
			{
				results.emplace_back(std::make_shared<folder_match>(*this, folder));
			}

			complete(results);
		}
		else
		{
			results_by_folder results;
			const auto query_parts = str::split(query, true);

			for (const auto& folder : _folders)
			{
				ui::match_highlights m;

				if (find_auto_complete(query_parts, folder.first.text(), true, m))
				{
					add_result(results, folder.first, m, folder.second);
				}
			}

			if (df::is_path(query))
			{
				const df::folder_path folder(query);
				const df::item_selector selector(folder);

				for (const auto& fi : platform::select_folders(selector, setting.show_hidden))
				{
					ui::match_highlights m;
					auto path = folder.combine(fi.name); // Perf

					if (find_auto_complete(query_parts, path.text(), true, m))
					{
						add_result(results, path, m, 10);
					}
				}
			}

			std::vector<folder_match_ptr> found;
			for (auto&& r : results) found.emplace_back(std::move(r.second));
			std::ranges::sort(found, compare_weight);
			if (found.size() > max_predictions) found.resize(max_predictions);
			complete(ui::auto_complete_results{ found.begin(), found.end() });
		}
	}

	void selected(const ui::auto_complete_match_ptr& i, const select_type st) override
	{
		_selected = i;

		const auto c = _search_control.lock();

		if (c && i)
		{
			c->update_edit_text(i->edit_text());
		}
	}

	ui::auto_complete_match_ptr selected() const override
	{
		return _selected;
	}
};

static void copy_move_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
	const bool is_move)
{
	const auto title = is_move ? tt.command_move : tt.command_copy;
	const auto icon = is_move ? icon_index::move_to_folder : icon_index::copy_to_folder;
	auto dlg = make_dlg(parent);

	pause_media pause(s);

	const auto can_process = s.can_process_selection_and_mark_errors(
		view, is_move ? df::process_items_type::local_file_or_folder : df::process_items_type::local_file_or_folder);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		std::u8string text;

		const auto auto_complete = std::make_shared<folder_auto_complete>(s, parent);
		const auto search_control = std::make_shared<ui::search_control>(dlg->_frame, text, auto_complete);
		auto_complete->_search_control = search_control;

		const auto& items = s.selected_items();
		const auto items_message = format_plural_text(is_move ? tt.move_fmt : tt.copy_fmt, items);

		std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title, items_message, items.thumbs())),
			std::make_shared<divider_element>(),
			search_control,
			std::make_shared<ui::check_control>(dlg->_frame, tt.open_dest, setting.show_results),
			std::make_shared<divider_element>(),
			std::make_shared<ui::ok_cancel_control>(dlg->_frame)
		};

		if (ui::close_result::ok == dlg->show_modal(controls, { 66 }))
		{
			df::folder_path write_folder(text);

			if (!write_folder.is_qualified())
			{
				dlg->show_message(icon_index::error, title, str::format(tt.is_not_valid_folder_fmt, write_folder));
			}
			else if (const auto create_folder_result = platform::create_folder(write_folder); create_folder_result.
				failed())
			{
				dlg->show_message(icon_index::error, title,
					create_folder_result.format_error(
						str::format(tt.failed_to_create_folder_fmt, write_folder)));
			}
			else
			{
				detach_file_handles detach(s);
				s.recent_folders.add(write_folder.text());
				platform::move_or_copy(items.file_paths(true), items.folder_paths(), write_folder, is_move);
				s.item_index.queue_scan_folder(write_folder);

				if (setting.show_results)
				{
					s.open(view, df::file_path(write_folder, items.first_name()));
				}
			}
		}
	}
}

static void repeat_mode_toggle(view_state& s, const ui::control_frame_ptr& parent)
{
	auto m = setting.repeat;

	if (m == repeat_mode::repeat_all)
	{
		m = repeat_mode::repeat_one;
	}
	else if (m == repeat_mode::repeat_one)
	{
		m = repeat_mode::repeat_none;
	}
	else
	{
		m = repeat_mode::repeat_all;
	}

	setting.repeat = m;
	s.invalidate_view(view_invalid::tooltip | view_invalid::view_redraw | view_invalid::command_state);
}

static void font_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	setting.large_font = !setting.large_font;
	s.invalidate_view(view_invalid::options);
}

static void toggle_layout_scale_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	auto scale = setting.item_scale + 1;
	if (scale >= settings_t::item_scale_count) scale = 0;
	setting.item_scale = scale;
}

static void toggle_details_invoke(view_state& s, const bool only_toggle_selected)
{
	const auto sel = s.selected_item_group();

	if (only_toggle_selected)
	{
		if (sel.group)
		{
			sel.group->toggle_display();
		}
	}
	else
	{
		toggle_details_state = !toggle_details_state;
		const auto new_display = toggle_details_state
			? df::item_group_display::detail
			: df::item_group_display::icons;

		for (const auto& g : s.groups())
		{
			g->display(new_display);
		}

		s.invalidate_view(view_invalid::command_state);
	}
}

class open_with_auto_complete final : public ui::complete_strategy_t,
	public std::enable_shared_from_this<open_with_auto_complete>
{
private:
	struct entry
	{
		std::u8string name;
		std::function<void(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)>
			invoke;
		int weight = 0;
	};

	class open_with_match final : public ui::auto_complete_match, public std::enable_shared_from_this<open_with_match>
	{
	public:
		entry _handler;
		ui::match_highlights _match;
		open_with_auto_complete& _parent;

		open_with_match(open_with_auto_complete& parent, entry h) : auto_complete_match(view_element_style::can_invoke),
			_handler(std::move(h)), _parent(parent)
		{
		}

		open_with_match(open_with_auto_complete& parent, entry h, ui::match_highlights match) :
			auto_complete_match(view_element_style::can_invoke), _handler(std::move(h)), _match(std::move(match)),
			_parent(parent)
		{
		}

		std::u8string edit_text() const override
		{
			return _handler.name;
		}

		void render(ui::draw_context& dc, const pointi element_offset) const override
		{
			const auto logical_bounds = bounds.offset(element_offset);
			const auto bg_color = calc_background_color(dc);

			if (bg_color.a > 0.0f)
			{
				const auto pad = padding * dc.scale_factor;
				dc.draw_rounded_rect(logical_bounds.inflate(pad.cx, pad.cy), bg_color, dc.padding1);
			}

			const auto highlight_clr = ui::color(ui::style::color::dialog_selected_text, dc.colors.alpha);
			const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
			const auto highlights = make_highlights(_match, highlight_clr);

			dc.draw_text(_handler.name, highlights, logical_bounds, ui::style::font_face::dialog,
				ui::style::text_style::single_line, clr, {});
		}

		void dispatch_event(const view_element_event& event) override
		{
			if (event.type == view_element_event_type::click)
			{
				_parent.selected(shared_from_this(), select_type::click);
			}
			else if (event.type == view_element_event_type::double_click)
			{
				_parent.selected(shared_from_this(), select_type::double_click);
			}
			else if (event.type == view_element_event_type::invoke)
			{
				const auto items = _parent._items;

				if (_handler.invoke)
				{
					_handler.invoke(items->file_paths(false), items->folder_paths());
				}

				_parent._state.recent_apps.add(_handler.name);
			}
		}

		view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
			const pointi element_offset,
			const std::vector<recti>& excluded_bounds) override
		{
			return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
		}
	};

	using open_with_match_ptr = std::shared_ptr<open_with_match>;

	df::hash_map<std::u8string, entry, df::ihash, df::ieq> _handlers;

	//ui::auto_complete_results _results;
	view_state& _state;
	ui::auto_complete_match_ptr _result;
	std::vector<command_info_ptr> _cmds;
	ui::frame_ptr _parent;
	std::shared_ptr<df::item_set> _items;

public:
	std::u8string no_results_message() override
	{
		return std::u8string(tt.type_to_search);
	}

	open_with_auto_complete(view_state& s, std::shared_ptr<df::item_set> items, ui::frame_ptr parent,
		std::vector<command_info_ptr> cmds) : _state(s), _cmds(std::move(cmds)),
		_parent(std::move(parent)), _items(std::move(items))
	{
		resize_to_show_results = false;
		max_predictions = 20u;
	}

	void initialise(std::function<void(const ui::auto_complete_results&)> complete) override
	{
		for (const auto& c : _cmds)
		{
			const auto name = c->text;
			df::assert_true(!str::is_empty(name));

			entry h;
			h.name = str::format(u8"{} ({})"sv, name, tt.open_with_tool);
			h.invoke = [c](const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
				{
					c->invoke();
				};
			h.weight = 1;
			_handlers[c->text] = h;
		}

		if (_items->single_file_extension())
		{
			const auto first_item = _items->items().front();
			const auto ext = first_item->extension();

			const auto platform_handlers = platform::assoc_handlers(ext);

			for (const auto& h : platform_handlers)
			{
				entry e;
				e.name = str::format(u8"{} ({})"sv, h.name, tt.open_with_app);
				e.invoke = h.invoke;
				e.weight = h.weight;
				_handlers[h.name] = e;
			}
		}

		df::string_counts counts;
		_state.recent_apps.count_strings(counts, 1);

		for (const auto& c : counts)
		{
			const auto found = _handlers.find(std::u8string(c.first));

			if (found != _handlers.cend())
			{
				found->second.weight += c.second;
			}
		}

		search({}, complete);
	}

	static bool compare_weight(const open_with_match_ptr& l, const open_with_match_ptr& r)
	{
		const auto diff = l->_handler.weight - r->_handler.weight;
		return diff == 0 ? str::icmp(l->_handler.name, r->_handler.name) < 0 : diff < 0;
	}

	void search(const std::u8string& query, std::function<void(const ui::auto_complete_results&)> complete) override
	{
		std::vector<open_with_match_ptr> results;

		if (str::is_empty(query))
		{
			for (const auto& i : _handlers)
			{
				results.emplace_back(std::make_shared<open_with_match>(*this, i.second));
			}
		}
		else
		{
			const auto query_parts = str::split(query, true);

			for (const auto& i : _handlers)
			{
				ui::match_highlights m;

				if (find_auto_complete(query_parts, i.first, false, m))
				{
					results.emplace_back(std::make_shared<open_with_match>(*this, i.second, m));
				}
			}
		}

		std::ranges::sort(results, compare_weight);
		df::assert_true(ui::is_ui_thread());
		complete(ui::auto_complete_results{ results.begin(), results.end() });
	}

	void selected(const ui::auto_complete_match_ptr& i, const select_type st) override
	{
		_result = i;

		if (st == select_type::click || st == select_type::double_click)
		{
			_parent->close(false);
		}
	}

	ui::auto_complete_match_ptr selected() const override
	{
		return _result;
	}

	void invoke()
	{
		if (_result)
		{
			const view_element_event e{ view_element_event_type::invoke, nullptr };
			_result->dispatch_event(e);
		}
	}
};

static void open_with_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
	commands_map& commands)
{
	const auto title = tt.open_with_app_tool;
	const auto dlg = make_dlg(parent);
	const auto can_process = s.
		can_process_selection_and_mark_errors(view, df::process_items_type::local_file_or_folder);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		std::vector<command_info_ptr> cmds =
		{
			commands[commands::tool_locate],
			commands[commands::tool_adjust_date],
			commands[commands::tool_burn],
			commands[commands::tool_convert],
			commands[commands::tool_desktop_background],
			commands[commands::tool_email],
			commands[commands::print],
			commands[commands::tool_rotate_anticlockwise],
			commands[commands::tool_rotate_clockwise],
			commands[commands::tool_edit_metadata],
		};

		const auto selected_items = std::make_shared<df::item_set>(s.selected_items());
		const auto complete = std::make_shared<open_with_auto_complete>(s, selected_items, dlg->_frame, cmds);

		std::u8string text;
		const std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control>(icon_index::next, title)),
			std::make_shared<ui::search_control>(dlg->_frame, text, complete),
			std::make_shared<divider_element>(),
			std::make_shared<ui::ok_cancel_control>(dlg->_frame)
		};


		if (ui::close_result::ok == dlg->show_modal(controls))
		{
			dlg->_frame->show(false);
			complete->invoke();
		}
	}
}

static void eject_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto title = tt.command_eject;
	const auto icon = icon_index::eject;
	auto dlg = make_dlg(parent);

	std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title, tt.eject_help)),
		std::make_shared<divider_element>()
	};

	const auto drives = platform::scan_drives(false);

	for (const auto& d : drives)
	{
		if (d.type == platform::drive_type::removable)
		{
			auto name = str::format(tt.eject_title_fmt, d.name);
			auto details = str::format(u8"{} {} {} ({} {})"sv, d.vol_name, d.file_system, d.capacity, d.used,
				tt.space_used);
			controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, drive_icon(d.type), d.name, details,
				[dlg, d, title]
				{
					if (platform::eject(df::folder_path(d.name)))
					{
						dlg->close(false);
					}
					else
					{
						dlg->show_message(
							icon_index::error, title,
							str::format(
								tt.eject_failed_fmt, d.name));
					}
				}));
		}
	}

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::close, tt.close,
		tt.eject_close_info, [f = dlg->_frame]
		{
			f->close(false);
		}));

	pause_media pause(s);
	dlg->show_modal(controls);
}


static void scan_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto scan_result = platform::scan(s.save_path());

	if (scan_result.success)
	{
		record_feature_use(features::scan);

		df::unique_paths selection;
		selection.emplace(scan_result.saved_file_path);
		s.open(view, s.search(), selection);
	}
	else
	{
		const auto dlg = make_dlg(parent);
		dlg->show_message(icon_index::error, tt.command_scan,
			str::format(u8"{}\n{}"sv, tt.scan_failed, scan_result.error_message));
	}
};


static void browse_parent_invoke(view_state& s, const view_host_base_ptr& view)
{
	if (s.view_mode() != view_type::items)
	{
		s.view_mode(view_type::items);
	}
	else
	{
		const auto parent = s.parent_search();

		if (!parent.parent.is_empty())
		{
			s.open(view, parent.parent, make_unique_paths(parent.selection));
		}
	}
}

static void favorite_invoke(view_state& state, const ui::control_frame_ptr& parent)
{
	const auto search = state.search();
	const bool is_favorite = state.search_is_favorite();

	bool updated = false;

	for (auto i = 0; i < setting.search.count && !updated; i++)
	{
		if (str::is_empty(setting.search.path[i]))
		{
			auto dlg = make_dlg(parent);
			auto dlg_parent = dlg->_frame;

			const auto text = str::format(tt.favorite_add_fmt, search.text());
			std::u8string title_text;

			if (search.selectors().size() == 1)
			{
				title_text = search.selectors().front().folder().name();
			}

			std::vector<view_element_ptr> controls = {
				set_margin(std::make_shared<ui::title_control>(icon_index::star_solid, tt.command_favorite)),
				std::make_shared<divider_element>(),
				set_margin(std::make_shared<text_element>(text)),
				set_margin(std::make_shared<text_element>(tt.favorite_title)),
				set_margin(std::make_shared<ui::edit_control>(dlg_parent, title_text)),
				std::make_shared<divider_element>(),
				std::make_shared<ui::ok_cancel_control>(dlg->_frame)
			};

			pause_media pause(state);

			if (dlg->show_modal(controls) == ui::close_result::ok)
			{
				setting.search.path[i] = search.text();
				setting.search.title[i] = title_text;
				updated = true;
			}
			else
			{
				// exit on cancel
				return;
			}
		}
		else
		{
			if (is_favorite)
			{
				const auto fav = df::search_t::parse(setting.search.path[i]);

				if (search == fav)
				{
					setting.search.path[i].clear();
					setting.search.title[i].clear();
					updated = true;
				}
			}
		}
	}

	if (updated)
	{
		state.invalidate_view(view_invalid::sidebar | view_invalid::command_state);
		state.update_search_is_favorite_or_collection_root();
	}
	else
	{
		auto dlg = make_dlg(parent);
		dlg->show_message(icon_index::folder, tt.command_favorite, tt.favorite_failed_to_add);
	}
}

bool ui::browse_for_term(view_state& vs, const control_frame_ptr& parent, std::u8string& result)
{
	const auto dlg = make_dlg(parent);
	auto dlg_parent = dlg->_frame;

	std::u8string scope;
	std::u8string text;

	auto default_texts = vs.item_index.auto_complete_text(prop::tag);
	auto edit_control = std::make_shared<ui::edit_control>(dlg_parent, tt.value, text, default_texts);

	auto create_commands = [&vs, edit_control](const std::shared_ptr<select_control>& sel)
		{
			std::vector<command_ptr> commands;

			for (const auto& s : prop::search_scopes())
			{
				auto c = std::make_shared<command>();
				c->text = s.scope;
				c->invoke = [&vs, sel, s, edit_control]()
					{
						sel->update_text(s.scope);
						edit_control->auto_completes(vs.item_index.auto_complete_text(s.type));
					};

				commands.emplace_back(c);
			}

			return commands;
		};

	const std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<title_control>(icon_index::search, tt.search_select_term)),
		std::make_shared<divider_element>(),
		set_margin(std::make_shared<select_control>(dlg_parent, tt.scope, scope, create_commands)),
		set_margin(edit_control),
		std::make_shared<divider_element>(),
		std::make_shared<ok_cancel_control>(dlg->_frame)
	};

	if (dlg->show_modal(controls) == close_result::ok)
	{
		if (str::is_empty(str::trim(scope)) || str::icmp(str::trim(scope), u8"any"sv) == 0)
		{
			result = text;
		}
		else
		{
			result = str::format(u8"{}:{}"sv, scope, str::quote_if_white_space(text));
		}

		return true;
	}

	return false;
}


static void advanced_search_invoke(view_state& state, const ui::control_frame_ptr& parent,
	const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	auto dlg_parent = dlg->_frame;

	const auto search = state.search();

	static std::u8string selected_folder;
	static std::u8string all_terms;
	static std::u8string none_terms;

	static bool search_collection = true;
	static bool search_folder = false;
	static bool search_sub_folders = true;
	//static bool search_not_in_collection = false;
	static bool search_location = false;

	static bool search_photos = false;
	static bool search_videos = false;
	static bool search_audio = false;

	static bool search_date_from = false;
	static bool search_date_until = false;
	static bool search_date_created = false;
	static bool search_date_modified = false;
	static df::date_t from_val;
	static df::date_t until_val;

	static location_and_distance_t location;

	if (!search.selectors().empty())
	{
		selected_folder = search.selectors().front().folder().text();
		//search_sub_folders = search.selectors().front().is_recursive();
	}

	auto search_collection_radio = std::make_shared<ui::check_control>(dlg->_frame, tt.search_collection,
		search_collection, true);
	auto jpeg_group = std::make_shared<ui::group_control>();
	search_collection_radio->child(jpeg_group);

	auto search_folder_ratio = std::make_shared<ui::check_control>(dlg->_frame, tt.search_folder, search_folder, true);
	auto webp_group = std::make_shared<ui::group_control>();
	webp_group->add(std::make_shared<ui::folder_picker_control>(dlg_parent, selected_folder));
	webp_group->add(std::make_shared<ui::check_control>(dlg_parent, tt.search_sub_folders, search_sub_folders));
	//webp_group->add(std::make_shared<ui::check_control>(dlg_parent, tt.search_not_in_collection, search_not_in_collection));
	search_folder_ratio->child(webp_group);

	const auto location_check = std::make_shared<ui::check_control>(dlg_parent, tt.search_located_within,
		search_location, false, true);
	location_check->child(std::make_shared<ui::location_picker_control>(state, dlg_parent, location));

	auto file_type_control = std::make_shared<ui::col_control>(std::vector<view_element_ptr>{
		std::make_shared<ui::check_control>(dlg_parent, tt.search_photos, search_photos),
			std::make_shared<ui::check_control>(dlg_parent, tt.search_videos, search_videos),
			std::make_shared<ui::check_control>(dlg_parent, tt.search_audio, search_audio)
	});

	auto date_type_control = std::make_shared<ui::col_control>(std::vector<view_element_ptr>{
		std::make_shared<ui::check_control>(dlg_parent, tt.prop_name_created, search_date_created),
			std::make_shared<ui::check_control>(dlg_parent, tt.prop_name_modified, search_date_modified)
	});

	auto from_check = std::make_shared<ui::check_control>(dlg->_frame, tt.search_date_from, search_date_from, false,
		true);
	from_check->child(std::make_shared<ui::date_control>(dlg_parent, from_val, false));
	auto until_check = std::make_shared<ui::check_control>(dlg->_frame, tt.search_date_until, search_date_until, false,
		true);
	until_check->child(std::make_shared<ui::date_control>(dlg_parent, until_val, false));

	file_type_control->compact = true;
	date_type_control->compact = true;

	std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<ui::title_control>(icon_index::search, tt.command_advanced_search)),
		std::make_shared<divider_element>(),
		set_margin(search_collection_radio),
		set_margin(search_folder_ratio),
		std::make_shared<divider_element>(),
		set_margin(std::make_shared<ui::term_picker_control>(state, dlg_parent, tt.search_all_terms, all_terms)),
		set_margin(std::make_shared<ui::term_picker_control>(state, dlg_parent, tt.search_none_terms, none_terms)),
		std::make_shared<divider_element>(),
		set_margin(date_type_control),
		set_margin(from_check),
		set_margin(until_check),
		std::make_shared<divider_element>(),
		set_margin(location_check),
		std::make_shared<divider_element>(),
		set_margin(file_type_control),
		std::make_shared<divider_element>(),
		std::make_shared<ui::ok_cancel_control>(dlg->_frame)
	};

	pause_media pause(state);

	if (dlg->show_modal(controls, { 66 }) == ui::close_result::ok)
	{
		df::search_t new_search;

		if (search_folder && !str::is_empty(selected_folder))
		{
			new_search.add_selector(df::item_selector(df::folder_path(selected_folder), search_sub_folders));
		}

		if (search_location && location.position.is_valid())
		{
			new_search.with(df::search_term(df::search_term_type::location, location.position, location.km,
				df::search_term_modifier{}));
		}

		if (search_photos)
		{
			new_search.with(df::search_term(file_group::photo, df::search_term_modifier{}));
		}

		if (search_videos)
		{
			new_search.with(df::search_term(file_group::video, df::search_term_modifier{}));
		}

		if (search_audio)
		{
			new_search.with(df::search_term(file_group::audio, df::search_term_modifier{}));
		}

		auto date_target = df::date_parts_prop::any;
		if (search_date_created && !search_date_modified) date_target = df::date_parts_prop::created;
		if (!search_date_created && search_date_modified) date_target = df::date_parts_prop::modified;

		if (search_date_from)
		{
			df::date_parts parts(from_val.date(), date_target);
			df::search_term_modifier mod;
			mod.greater_than = true;
			mod.equals = true;
			new_search.with(df::search_term(df::search_term_type::date, parts, mod));
		}

		if (search_date_until)
		{
			df::date_parts parts(until_val.date(), date_target);
			df::search_term_modifier mod;
			mod.less_than = true;
			mod.equals = true;
			new_search.with(df::search_term(df::search_term_type::date, parts, mod));
		}

		search_tokenizer t;

		for (const auto& part : t.parse(all_terms))
		{
			new_search.parse_part(part);
		}

		for (auto part : t.parse(none_terms))
		{
			part.modifier.positive = false;
			new_search.parse_part(part);
		}

		if (!new_search.is_empty())
		{
			state.open(view, new_search, {});
		}
	}
}

static void upgrade_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto title = tt.update_title;
	auto text = str::format(tt.update_help_fmt, setting.available_version, s_app_version);
	df::file_path download_path_result;

	auto dlg = make_dlg(parent);
	std::vector<view_element_ptr> controls;

	controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::lightbulb, title)));
	controls.emplace_back(set_margin(std::make_shared<text_element>(text)));
	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::import, tt.update,
		tt.update_help, [f = dlg->_frame]() { f->close(); }));

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::time, tt.update_not_now,
		tt.update_not_now_help, [&s, f = dlg->_frame]()
		{
			setting.min_show_update_day = platform::now().
				to_days() + 7;
			s.invalidate_view(
				view_invalid::view_layout |
				view_invalid::app_layout);
			f->close(true);
		}));

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::question, tt.update_more_info,
		tt.update_more_info_help, [f = dlg->_frame]()
		{
			platform::open(u8"https://www.diffractor.com/blog"sv);
			f->close(true);
		}));

	pause_media pause(s);

	if (dlg->show_modal(controls) == ui::close_result::ok)
	{
		controls.clear();
		controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::import, title)));
		controls.emplace_back(
			std::make_shared<ui::busy_control>(dlg->_frame, icon_index::lightbulb, tt.update_please_wait));
		controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame, true, tt.button_cancel));

		auto download_complete = [&s, dlg, &download_path_result](const df::file_path download_path)
			{
				s.queue_ui([dlg, &download_path_result, download_path]()
					{
						download_path_result = download_path;
						dlg->close(download_path_result.is_empty());
					});
			};

		s.queue_async(async_queue::web, [download_complete]()
			{
				platform::download_and_verify(setting.is_tester, download_complete);
			});

		if (dlg->show_modal(controls) == ui::close_result::ok)
		{
			const auto module_folder = known_path(platform::known_folder::running_app_folder);
			const auto install_result = platform::install(download_path_result, module_folder, false, false);

			if (install_result.failed())
			{
				dlg->show_message(icon_index::error, s_app_name, install_result.format_error(tt.update_failed));
			}
		}
	}
}

static void test_new_version_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	setting.force_available_version = true;
	setting.min_show_update_day = 0;
	s.invalidate_view(view_invalid::view_layout | view_invalid::app_layout);
};

static void add_keyboard_commands(const dialog_ptr& dlg, std::shared_ptr<ui::group_control> controls,
	const commands_map& commands, const command_group group,
	const std::u8string_view title)
{
	controls->add(std::make_shared<ui::title_control>(icon_index::none, title));

	std::vector<command_info_ptr> items;

	for (auto c : commands)
	{
		if (c.second->group == group && !c.second->kba.empty())
		{
			items.emplace_back(c.second);
		}
	}

	std::ranges::sort(items, [](auto&& left, auto&& right)
		{
			return str::icmp(left->text, right->text) < 0;
		});

	const auto table = std::make_shared<ui::table_element>();

	for (const auto c : items)
	{
		table->add(c->icon, c->keyboard_accelerator_text, c->text);
	}

	controls->add(table);
}

static bool is_not_virt_key(int key)
{
	return (key >= '0' && key <= '9') ||
		(key >= 'A' && key <= 'Z');
}

std::u8string format_keyboard_accelerator(const std::vector<keyboard_accelerator_t>& keyboard_accelerators)
{
	const auto control = keyboard_accelerator_t::control;
	const auto shift = keyboard_accelerator_t::shift;
	const auto alt = keyboard_accelerator_t::alt;

	std::u8string result;

	for (const auto& ac : keyboard_accelerators)
	{
		// Add Accelerator
		if (!result.empty())
		{
			result += str::format(u8" {} "sv, tt.keyboard_or);
		}

		if (ac.key_state & alt)
		{
			result += str::format(u8"{}-"sv, tt.keyboard_alt);
		}
		if (ac.key_state & control)
		{
			result += str::format(u8"{}-"sv, tt.keyboard_control);
		}
		if (ac.key_state & shift)
		{
			result += str::format(u8"{}-"sv, tt.keyboard_shift);
		}

		if (ac.key == keys::RETURN)
		{
			result += tt.keyboard_enter;
		}
		else if (ac.key == keys::BACK)
		{
			result += tt.keyboard_backspace;
		}
		else if (is_not_virt_key(ac.key))
		{
			const char8_t szTemp[2] = { static_cast<char8_t>(ac.key), 0 };
			result += szTemp;
		}
		else
		{
			result += keys::format(ac.key);
		}
	}

	return result;
}

static void show_keyboard_reference(view_state& s, const ui::control_frame_ptr& parent, const commands_map& commands)
{
	const auto dlg = make_dlg(parent);


	const auto col1 = std::make_shared<ui::group_control>();
	const auto col2 = std::make_shared<ui::group_control>();
	const auto col3 = std::make_shared<ui::group_control>();
	const auto col4 = std::make_shared<ui::group_control>();
	const auto col5 = std::make_shared<ui::group_control>();

	col1->add(std::make_shared<ui::title_control>(icon_index::none, tt.keyboard_basics_title));

	const auto table = std::make_shared<ui::table_element>();
	table->add(icon_index::bullet, format_keyboard_accelerator({ keyboard_accelerator_t{keys::RETURN} }),
		tt.keyboard_enter_desc);
	table->add(icon_index::bullet, format_keyboard_accelerator({ keyboard_accelerator_t{keys::SPACE} }),
		tt.keyboard_space_desc);
	table->add(icon_index::bullet, format_keyboard_accelerator({ keyboard_accelerator_t{keys::ESCAPE} }),
		tt.keyboard_escape_desc);
	table->add(icon_index::bullet,
		format_keyboard_accelerator({ keyboard_accelerator_t{keys::LEFT}, keyboard_accelerator_t{keys::RIGHT} }),
		tt.keyboard_left_right_desc);
	table->add(icon_index::bullet, format_keyboard_accelerator({
				   keyboard_accelerator_t{keys::LEFT, keyboard_accelerator_t::control},
				   keyboard_accelerator_t{keys::RIGHT, keyboard_accelerator_t::control}
		}), tt.keyboard_ctrl_left_right_desc);
	col1->add(table);

	const auto& c = commands;

	add_keyboard_commands(dlg, col1, c, command_group::file_management, tt.keyboard_file_management_title);
	add_keyboard_commands(dlg, col2, c, command_group::media_playback, tt.keyboard_playback_title);
	add_keyboard_commands(dlg, col2, c, command_group::tools, tt.keyboard_tools_title);
	add_keyboard_commands(dlg, col3, c, command_group::open, tt.keyboard_open_title);
	add_keyboard_commands(dlg, col3, c, command_group::navigation, tt.keyboard_navigation_title);
	//add_commands(dlg, col3, c, command_group::sorting, u8"Sorting"sv);
	add_keyboard_commands(dlg, col4, c, command_group::selection, tt.keyboard_selection_title);
	add_keyboard_commands(dlg, col4, c, command_group::rate_flag, tt.keyboard_rate_label_title);
	add_keyboard_commands(dlg, col5, c, command_group::options, tt.keyboard_options_title);
	add_keyboard_commands(dlg, col5, c, command_group::help, tt.keyboard_help_title);


	std::vector<view_element_ptr> controls;
	controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::keyboard, tt.keyboard_ref_title)));
	controls.emplace_back(set_margin(std::make_shared<divider_element>()));

	auto cols = std::make_shared<ui::col_control>();
	cols->add(col1);
	cols->add(col2);
	cols->add(col3);
	cols->add(col4);
	cols->add(col5);

	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame));

	pause_media pause(s);
	dlg->show_modal(controls, { 155 }, { 99 });
	dlg->_frame->destroy();
}

void send_info(view_state& s)
{
	const auto log_file_path = df::log_path;
	const auto previous_log_path = df::previous_log_path;
	const auto crash_zip_path = platform::temp_file();
	const auto log_file_copy = platform::temp_file();

	if (log_file_path.exists())
	{
		platform::copy_file(log_file_path, log_file_copy, true, true);
	}

	df::zip_file zip;

	if (zip.create(crash_zip_path))
	{
		if (log_file_copy.exists()) zip.add(log_file_copy, u8"diffractor.log"sv);
		if (previous_log_path.exists()) zip.add(previous_log_path);
		zip.close();
	}

	u8ostringstream message;

	for (const auto& i : calc_app_info(s.item_index, true))
	{
		message << i.first << u8" "sv << i.second << '\n';
	}

	platform::web_request req;
	req.verb = platform::web_request_verb::POST;
	req.host = u8"diffractor.com"sv;
	req.path = u8"/crash"sv;
	req.form_data.emplace_back(u8"message"sv, message.str());
	req.form_data.emplace_back(u8"version"sv, platform::OS());
	req.form_data.emplace_back(u8"diffractor"sv, s_app_version);
	req.form_data.emplace_back(u8"build"sv, g_app_build);
	req.form_data.emplace_back(u8"subject"sv, u8"Diffractor LOG"sv);
	req.form_data.emplace_back(u8"submit"sv, u8"Send Report"sv);
	req.file_form_data_name = u8"ff"sv;
	req.file_name = u8"logs.zip"sv;
	req.file_path = crash_zip_path;

	send_request(req);
}

static void about_invoke(view_state& s, const ui::control_frame_ptr& parent, commands_map& commands)
{
	const auto dlg = make_dlg(parent);
	auto dlg_parent = dlg->_frame;

	files ff;
	const auto title = ff.image_to_surface(load_resource(platform::resource_item::title));

	std::vector<view_element_ptr> controls;
	controls.emplace_back(std::make_shared<surface_element>(title, title->width(), view_element_style::center));
	controls.emplace_back(std::make_shared<text_element>(df::format_version(false), ui::style::font_face::dialog,
		ui::style::text_style::single_line_center,
		view_element_style::center));
	controls.emplace_back(std::make_shared<divider_element>());

	auto cols = std::make_shared<ui::col_control>();

	const auto learn = std::make_shared<ui::group_control>();
	learn->add(std::make_shared<ui::title_control>(icon_index::question, tt.documentation));
	learn->add(std::make_shared<text_element>(tt.about_info));
	learn->add(std::make_shared<link_element>(tt.learn_more_diffractor_com, []() { platform::open(docs_url); }));
	learn->add(std::make_shared<ui::title_control>(icon_index::keyboard, tt.keyboard));
	learn->add(std::make_shared<link_element>(tt.list_of_accelerators, [&s, dlg_parent, &c = commands]()
		{
			show_keyboard_reference(s, dlg_parent, c);
		}));

	const auto support = std::make_shared<ui::group_control>();
	support->add(std::make_shared<ui::title_control>(icon_index::buy, tt.donate));
	support->add(std::make_shared<text_element>(tt.donate_help));
	support->add(std::make_shared<link_element>(tt.donate_link, []() { platform::open(donate_url); }));
	support->add(std::make_shared<ui::title_control>(icon_index::support, tt.support));
	support->add(std::make_shared<link_element>(tt.help_more_info, []() { platform::open(support_url); }));
	support->add(std::make_shared<link_element>(tt.help_send_info, [&s]() { send_info(s); }));

	cols->add(set_margin(learn, 8, 8));
	cols->add(set_margin(support, 8, 8));
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame));

	pause_media pause(s);
	dlg->show_modal(controls, { 55 });
	dlg_parent->destroy();
};

static void settings_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto dlg = make_dlg(parent);

	std::shared_ptr<text_element> custom_index_locations_label;

	//std::shared_ptr<ui::folder_picker_control> custom_index_path;
	//std::shared_ptr<ui::folder_picker_control> more_custom_index_paths;

	std::vector<view_element_ptr> controls;

	const auto settings = std::make_shared<ui::group_control>();
	const auto settings2 = std::make_shared<ui::group_control>();
	const auto advanced = std::make_shared<ui::group_control>();

	settings->add(std::make_shared<ui::title_control>(tt.options_app_options));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_rotated, setting.show_rotated));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_hidden, setting.show_hidden));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_confirm_del, setting.confirm_deletions));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_shadow, setting.show_shadow));
	settings->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_update_modified, setting.update_modified));
	settings->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_last_played_pos, setting.last_played_pos));

	settings->add(std::make_shared<ui::title_control>(tt.option_slideshow_title));
	settings->add(std::make_shared<text_element>(tt.option_slideshow_delay));
	settings->add(std::make_shared<ui::slider_control>(dlg->_frame, setting.slideshow_delay, 0, 30));

	settings2->add(std::make_shared<ui::title_control>(tt.options_save_options));
	settings2->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_backup_copy, setting.create_originals));
	settings2->add(std::make_shared<text_element>(tt.options_jpeg_quality));
	settings2->add(std::make_shared<ui::slider_control>(dlg->_frame, setting.jpeg_save_quality, 0, 100));
	settings2->add(std::make_shared<text_element>(tt.options_webp_quality));
	settings2->add(std::make_shared<ui::slider_control>(dlg->_frame, setting.webp_quality, 1, 100));
	settings2->add(std::make_shared<ui::check_control>(dlg->_frame, tt.lossless_compression, setting.webp_lossless));

	settings2->add(std::make_shared<ui::title_control>(tt.options_updates));
	settings2->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_check_for_update, setting.check_for_updates));
	settings2->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_auto_update, setting.install_updates));
	settings2->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_beta_tester, setting.is_tester));

	advanced->add(std::make_shared<ui::title_control>(tt.options_advanced));
	advanced->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_help_tooltips, setting.show_help_tooltips));
	advanced->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_use_gpu, setting.use_gpu));
	advanced->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_use_gpu_video, setting.use_d3d11va));
	advanced->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_use_yuv_tex, setting.use_yuv));
	advanced->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_send_crash_reports, setting.send_crash_dumps));
	advanced->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_debug_info, setting.show_debug_info));

	auto cols = std::make_shared<ui::col_control>();
	cols->add(set_margin(settings));
	cols->add(set_margin(settings2));
	cols->add(set_margin(advanced));

	controls.emplace_back(set_margin(
		std::make_shared<ui::title_control2>(dlg->_frame, icon_index::settings, tt.command_options,
			std::u8string_view{})));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame));

	pause_media pause(s);
	dlg->show_modal(controls, { 111 });
	s.invalidate_view(view_invalid::options);
}


static std::u8string format_index_text(view_state& s)
{
	const auto file_types = s.item_index.file_types();
	const auto total = file_types.total_items();
	const auto num = platform::format_number(str::to_string(total.count));
	//const auto num_folder = platform::format_number(str::to_string(df::stats.index_folder_count));
	//const auto total_size = prop::format_size(total.size);
	const auto database_size = prop::format_size(s.item_index.stats.database_size);
	const auto text = str::format(tt.index_size_fmt, database_size, num);
	return text;
}

class path_text_element final : public std::enable_shared_from_this<path_text_element>, public view_element
{
private:
	std::u8string _text;
	df::file_path _path;
	ui::style::font_face _font = ui::style::font_face::dialog;
	ui::style::text_style _text_style = ui::style::text_style::multiline;

public:
	path_text_element(const df::file_path path) noexcept : view_element(
		view_element_style::has_tooltip |
		view_element_style::can_invoke), _text(path.str()),
		_path(path)
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg = calc_background_color(dc);
		dc.draw_text(_text, logical_bounds, _font, _text_style, ui::color(dc.colors.foreground, dc.colors.alpha), bg);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		sizei result;

		if (!_text.empty())
		{
			result = mc.measure_text(_text, _font, _text_style, width_limit);
		}

		return result;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			platform::show_in_file_browser(_path);
		}
	}

	void tooltip(view_hover_element& result, const pointi loc, const pointi element_offset) const override
	{
		result.elements->add(make_icon_element(icon_index::data, view_element_style::no_break));
		result.elements->add(std::make_shared<text_element>(_text, ui::style::font_face::title,
			ui::style::text_style::multiline,
			view_element_style::line_break));
		result.active_bounds = result.window_bounds = bounds.offset(element_offset);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
		const pointi element_offset,
		const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

static void index_maintenance(const ui::control_frame_ptr& parent, view_state& s)
{
	const auto dlg = make_dlg(parent);
	const auto title = tt.index_maintenance_title;
	auto dlg_parent = dlg->_frame;
	bool is_reset = false;

	const std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::settings, title,
														tt.defragment_and_compact)),
		std::make_shared<divider_element>(),
		set_margin(std::make_shared<text_element>(format_index_text(s))),
		set_margin(std::make_shared<path_text_element>(s.item_index.stats.database_path)),
		set_margin(std::make_shared<text_element>(tt.index_maintenance_help)),
		set_margin(std::make_shared<ui::check_control>(dlg->_frame, tt.reset_database, is_reset)),
		std::make_shared<divider_element>(),
		std::make_shared<ui::ok_cancel_control>(dlg->_frame)
	};

	// TODO
	/*if (database::has_errors())
	{
		controls.emplace_back(set_margin(set_padding(std::make_shared<text_element>(tt.index_maintenance_reset_recommended, true), 8, 8)));
	}*/

	if (dlg->show_modal(controls) == ui::close_result::ok)
	{
		platform::thread_event event_wait(true, false);
		dlg->show_status(icon_index::star, is_reset ? tt.resetting : tt.defragmenting);

		s._async.queue_database([&event_wait, is_reset](database& db)
			{
				db.maintenance(is_reset);
				event_wait.set();
			});

		platform::wait_for({ event_wait }, 10000, false);
		s.invalidate_view(view_invalid::index);
	}
};


static void index_settings_invoke(view_state& s, const ui::control_frame_ptr& parent, settings_t::index_t collection_settings)
{
	const auto dlg = make_dlg(parent);

	std::vector<view_element_ptr> controls;
	std::shared_ptr<ui::folder_picker_control> more_custom_index_paths;

	auto dlg_parent = dlg->_frame;
	const auto local_index = std::make_shared<ui::group_control>();
	const auto custom_index = std::make_shared<ui::group_control>();
	//auto cloud_index = std::make_shared<ui::group_control>();

	auto index_text = format_index_text(s);

	local_index->add(std::make_shared<text_element>(tt.collection_info));
	local_index->add(std::make_shared<link_element>(tt.more_collection_options_information, []() { platform::open(docs_url); }));

	const auto local_folders = platform::local_folders();

	local_index->add(std::make_shared<ui::title_control>(tt.collection_options_local_folders_title));
	local_index->add(std::make_shared<ui::check_control>(dlg_parent, local_folders.pictures.text(), collection_settings.pictures));
	local_index->add(std::make_shared<ui::check_control>(dlg_parent, local_folders.video.text(), collection_settings.video));
	local_index->add(std::make_shared<ui::check_control>(dlg_parent, local_folders.music.text(), collection_settings.music));

	if (local_folders.onedrive_pictures.exists())
		local_index->add(
			std::make_shared<ui::check_control>(dlg_parent, local_folders.onedrive_pictures.text(),
				collection_settings.onedrive_pictures));
	if (local_folders.onedrive_video.exists())
		local_index->add(
			std::make_shared<ui::check_control>(dlg_parent, local_folders.onedrive_video.text(),
				collection_settings.onedrive_video));
	if (local_folders.onedrive_music.exists())
		local_index->add(
			std::make_shared<ui::check_control>(dlg_parent, local_folders.onedrive_music.text(),
				collection_settings.onedrive_music));
	if (local_folders.dropbox_photos.exists())
		local_index->add(
			std::make_shared<ui::check_control>(dlg_parent, local_folders.dropbox_photos.text(),
				collection_settings.drop_box));

	local_index->add(std::make_shared<ui::title_control>(tt.index_maintenance_title));
	local_index->add(std::make_shared<text_element>(tt.indexing_message));
	local_index->add(std::make_shared<text_element>(index_text));
	local_index->add(std::make_shared<link_element>(tt.defragment_and_compact, [&s, dlg_parent]
		{
			index_maintenance(dlg_parent, s);
		}));

	custom_index->add(std::make_shared<ui::title_control>(tt.collection_options_custom_folders_title));
	custom_index->add(std::make_shared<text_element>(tt.collection_options_more_folders));

	auto more_folders_parts = str::split(collection_settings.more_folders, true,
		[](wchar_t c) { return c == '\n' || c == '\r'; });
	auto more_folders_text = str::combine(more_folders_parts, u8"\r\n"sv, false);

	custom_index->add(
		more_custom_index_paths = std::make_shared<ui::folder_picker_control>(dlg_parent, more_folders_text, true));
	custom_index->add(std::make_shared<text_element>(tt.collection_options_custom_locations_help));
	custom_index->add(std::make_shared<text_element>(tt.collection_options_custom_folders_help));


	auto cols = std::make_shared<ui::col_control>();
	cols->add(set_margin(local_index));
	cols->add(set_margin(custom_index));

	controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::set, tt.command_collection_options, tt.collection_options_info)));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame));

	pause_media pause(s);

	if (ui::close_result::ok == dlg->show_modal(controls, { 99 }))
	{
		// apply changes
		more_folders_parts = str::split(more_folders_text, false, [](wchar_t c) { return c == '\n' || c == '\r'; });
		collection_settings.more_folders = str::combine(more_folders_parts, u8"\n"sv, true);
		setting.collection = collection_settings;
	}

	dlg->_frame->destroy();
	s.invalidate_view(view_invalid::index | view_invalid::options);
}

static void customise_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto dlg = make_dlg(parent);

	auto dlg_parent = dlg->_frame;
	const auto searches = std::make_shared<ui::group_control>();
	const auto tags = std::make_shared<ui::group_control>();
	const auto sidebar = std::make_shared<ui::group_control>();

	searches->add(std::make_shared<ui::title_control>(tt.customise_searches_title));
	searches->add(
		std::make_shared<ui::two_col_table_control>(dlg_parent, setting.search.title, setting.search.path,
			setting.search.count));

	tags->add(std::make_shared<ui::title_control>(tt.customise_tags_title));
	tags->add(set_margin(std::make_shared<text_element>(tt.customise_tags_help)));
	tags->add(set_margin(std::make_shared<text_element>(tt.help_tag1)));
	tags->add(set_margin(std::make_shared<text_element>(tt.help_tag2)));
	tags->add(std::make_shared<ui::multi_line_edit_control>(dlg_parent, setting.favorite_tags));
	tags->add(std::make_shared<ui::check_control>(dlg->_frame, tt.option_favorite_tags, setting.sidebar.show_favorite_tags_only));

	sidebar->add(std::make_shared<ui::title_control>(tt.customise_sidebar_title));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_total, setting.sidebar.show_total_items));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_history, setting.sidebar.show_history));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_world_map,
		setting.sidebar.show_world_map));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_indexed_folders,
		setting.sidebar.show_indexed_folders));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_drives, setting.sidebar.show_drives));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_searches,
		setting.sidebar.show_favorite_searches));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_tags, setting.sidebar.show_tags));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_ratings, setting.sidebar.show_ratings));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_labels, setting.sidebar.show_labels));

	auto cols = std::make_shared<ui::col_control>();
	cols->add(set_margin(searches));
	cols->add(set_margin(tags));
	cols->add(set_margin(sidebar));

	std::vector<view_element_ptr> controls;
	controls.emplace_back(set_margin(
		std::make_shared<ui::title_control2>(dlg->_frame, icon_index::settings, tt.command_customise,
			tt.customise_sidebar_desc)));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame));

	pause_media pause(s);
	dlg->show_modal(controls, { 111 });

	s.invalidate_view(view_invalid::options);
};

static void email_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto title = tt.command_share_email;
	const auto icon = icon_index::mail;
	auto dlg = make_dlg(parent);
	const auto can_process = s.can_process_selection_and_mark_errors(view, df::process_items_type::local_file);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto& items = s.selected_items();

		std::vector<view_element_ptr> controls;
		controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
			dlg->_frame, icon_index::mail, title, format_plural_text(tt.email_info_fmt, items), items.thumbs())));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(set_margin(std::make_shared<text_element>(tt.email_small_help)));
		controls.emplace_back(std::make_shared<ui::check_control>(dlg->_frame, tt.email_zip, setting.email.zip));
		controls.emplace_back(
			std::make_shared<ui::check_control>(dlg->_frame, tt.email_convert_to_jpeg, setting.email.convert));

		auto limit = std::make_shared<ui::check_control>(dlg->_frame, tt.email_limit_dimensions, setting.email.limit);
		limit->child(std::make_shared<ui::num_control>(dlg->_frame, std::u8string_view{}, setting.email.max_side));
		controls.emplace_back(limit);
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_send));

		if (ui::close_result::ok == dlg->show_modal(controls))
		{
			record_feature_use(features::email);

			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());

			s.queue_async(async_queue::work, [&s, results, items]()
				{
					//auto email = std::make_shared<df::email_sender>();
					//email->add_files(*results, items, setting.email.zip, setting.email.convert, setting.email.limit ? setting.email.max_side : 0);
					const auto zip = setting.email.zip;
					const auto scale = setting.email.limit ? setting.email.max_side : 0;
					const auto convert_to_jpeg = setting.email.convert;

					files _codecs;
					platform::attachments_t attachments;
					df::file_paths temp_file_paths;
					df::zip_file zip_file;
					df::file_path zip_path;
					bool is_valid = true;

					if (zip)
					{
						zip_path = platform::temp_file(u8"zip"sv);
						zip_file.create(zip_path);
					}

					auto file_paths = items.file_paths();
					auto pos = 0;

					for (const auto& path : file_paths)
					{
						auto format = str::format(tt.email_processing_fmt, path.name());
						results->message(format, pos++, file_paths.size());

						auto file_name = path.name();
						const auto is_jpeg = files::is_jpeg(path.name());
						auto attachment_path = path;

						if (scale || (convert_to_jpeg && !is_jpeg))
						{
							const auto ft = files::file_type_from_name(path);

							if (ft->has_trait(file_traits::bitmap))
							{
								image_edits edits;
								const auto ext = !is_jpeg && convert_to_jpeg ? u8".jpg"sv : path.extension();
								const auto edited_path = platform::temp_file(ext);

								if (scale)
								{
									edits.scale(scale);
								}

								const auto update_result = _codecs.update(path, edited_path, {}, edits,
									make_file_encode_params(), false, {});

								if (update_result.success())
								{
									attachment_path = edited_path;
									file_name = path.extension(ext).name();
								}

								temp_file_paths.emplace_back(edited_path);
							}
						}

						if (zip)
						{
							zip_file.add(attachment_path, file_name);
						}
						else
						{
							attachments.emplace_back(file_name, attachment_path);
						}
					}

					if (zip)
					{
						if (zip_file.close())
						{
							attachments.emplace_back(u8"items.zip"sv, zip_path);
							temp_file_paths.emplace_back(zip_path);
							is_valid = true;
						}
					}
					else
					{
						is_valid = true;
					}

					if (is_valid)
					{
						results->message(tt.email_connecting_to_mapi);

						s.queue_ui([attachments, results]
							{
								if (results->has_failures())
								{
									results->show_errors();
								}
								else
								{
									results->complete(tt.email_sending);

									if (!platform::mapi_send({}, {}, {}, attachments))
									{
										results->complete(tt.email_failed);
									}
									else
									{
										results->complete();
									}
								}
							});
					}
				});

			results->wait_for_complete();
		}

		controls.clear();
	}
}

void app_frame::initialise_commands()
{
	update_command_text();

	_commands[commands::menu_open]->toolbar_text = _commands[commands::menu_open]->text;
	_commands[commands::menu_tag_with]->toolbar_text = _commands[commands::menu_tag_with]->text;
	_commands[commands::menu_tools_toolbar]->toolbar_text = _commands[commands::menu_tools_toolbar]->text;
	_commands[commands::menu_playback]->toolbar_text = _commands[commands::menu_playback]->text;
	_commands[commands::view_exit]->toolbar_text = _commands[commands::view_exit]->text;
	_commands[commands::sync_analyze]->toolbar_text = _commands[commands::sync_analyze]->text;
	_commands[commands::sync_run]->toolbar_text = _commands[commands::sync_run]->text;
	_commands[commands::rename_run]->toolbar_text = _commands[commands::rename_run]->text;
	_commands[commands::import_analyze]->toolbar_text = _commands[commands::import_analyze]->text;
	_commands[commands::import_run]->toolbar_text = _commands[commands::import_run]->text;
	_commands[commands::test_crash]->toolbar_text = _commands[commands::test_crash]->text;
	_commands[commands::test_boom]->toolbar_text = _commands[commands::test_boom]->text;
	_commands[commands::test_new_version]->toolbar_text = _commands[commands::test_new_version]->text;
	_commands[commands::test_run_all]->toolbar_text = _commands[commands::test_run_all]->text;
	_commands[commands::edit_item_save]->toolbar_text = _commands[commands::edit_item_save]->text;
	_commands[commands::edit_item_save_and_prev]->toolbar_text = _commands[commands::edit_item_save_and_prev]->text;
	_commands[commands::edit_item_save_and_next]->toolbar_text = _commands[commands::edit_item_save_and_next]->text;
	_commands[commands::edit_item_save_as]->toolbar_text = _commands[commands::edit_item_save_as]->text;
	_commands[commands::edit_item_options]->toolbar_text = _commands[commands::edit_item_options]->text;
	_commands[commands::menu_group_toolbar]->toolbar_text = _commands[commands::menu_group_toolbar]->text;
	_commands[commands::filter_photos]->toolbar_text = _commands[commands::filter_photos]->text;
	_commands[commands::filter_videos]->toolbar_text = _commands[commands::filter_videos]->text;
	_commands[commands::filter_audio]->toolbar_text = _commands[commands::filter_audio]->text;

	_commands[commands::filter_photos]->text_can_change = true;
	_commands[commands::filter_videos]->text_can_change = true;
	_commands[commands::filter_audio]->text_can_change = true;
	_commands[commands::menu_group_toolbar]->text_can_change = true;

	_commands[commands::play]->icon_can_change = true;
	_commands[commands::view_fullscreen]->icon_can_change = true;
	_commands[commands::repeat_toggle]->icon_can_change = true;
	_commands[commands::playback_volume_toggle]->icon_can_change = true;
	_commands[commands::favorite]->icon_can_change = true;
	_commands[commands::options_collection]->icon_can_change = true;

	_commands[commands::option_highlight_large_items]->clr = ui::style::color::rank_background;
	_commands[commands::rate_rejected]->clr = color_rate_rejected;
	_commands[commands::label_approved]->clr = color_label_approved;
	_commands[commands::label_to_do]->clr = color_label_to_do;
	_commands[commands::label_select]->clr = color_label_select;
	_commands[commands::label_review]->clr = color_label_review;
	_commands[commands::label_second]->clr = color_label_second;

	const auto t = shared_from_this();

	//add_command(ID_SHARE_FACEBOOK, [this] { facebook_share_invoke(_state, _frame); });
	//add_command(ID_SHARE_FLICKR, [this] { flickr_share_invoke(_state, _frame); });
	//add_command(ID_SHARE_TWITTER, [this] { twitter_share_invoke(_state, _frame); });

	add_command_invoke(commands::tool_adjust_date, [this] { adjust_date_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_edit_metadata, [this] { edit_metadata_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::exit, [this] { _app_frame->close(); });
	add_command_invoke(commands::playback_auto_play, [this] { setting.auto_play = !setting.auto_play; });
	add_command_invoke(commands::playback_last_played_pos,
		[this] { setting.last_played_pos = !setting.last_played_pos; });
	add_command_invoke(commands::playback_repeat_one, [this] { setting.repeat = repeat_mode::repeat_one; });
	add_command_invoke(commands::playback_repeat_none, [this] { setting.repeat = repeat_mode::repeat_none; });
	add_command_invoke(commands::playback_repeat_all, [this] { setting.repeat = repeat_mode::repeat_all; });
	add_command_invoke(commands::browse_back, [this] { _state.browse_back(_view_frame); });
	add_command_invoke(commands::browse_forward, [this] { _state.browse_forward(_view_frame); });

	add_command_invoke(commands::browse_next_folder, [this] { _state.open_next_path(_view_frame, true); });
	add_command_invoke(commands::browse_next_group, [this]
		{
			_state.select(_view_frame, _state.next_group_item(true), false, false, false);
		});
	add_command_invoke(commands::browse_next_item, [this] { _state.select_next(_view_frame, true, false, false); });
	add_command_invoke(commands::browse_next_item_extend,
		[this] { _state.select_next(_view_frame, true, true, false); });
	add_command_invoke(commands::browse_parent, [this] { browse_parent_invoke(_state, _view_frame); });
	add_command_invoke(commands::browse_previous_folder, [this] { _state.open_next_path(_view_frame, false); });
	add_command_invoke(commands::browse_previous_group, [this]
		{
			_state.select(_view_frame, _state.next_group_item(false), false, false, false);
		});
	add_command_invoke(commands::browse_previous_item,
		[this] { _state.select_next(_view_frame, false, false, false); });
	add_command_invoke(commands::browse_previous_item_extend,
		[this] { _state.select_next(_view_frame, false, true, false); });

	add_command_invoke(commands::tool_burn, [this] { burn_command_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_save_current_video_frame, [this] { capture_invoke(_state, _app_frame); });
	add_command_invoke(commands::favorite, [this] { favorite_invoke(_state, _app_frame); });
	add_command_invoke(commands::advanced_search, [this] { advanced_search_invoke(_state, _app_frame, _view_frame); });

	add_command_invoke(commands::view_exit, [this] { _view->exit(); });
	add_command_invoke(commands::edit_item_save, [this] { _view_edit->save_and_close(); });
	add_command_invoke(commands::edit_item_save_and_prev, [this] { _view_edit->save_and_next(false); });
	add_command_invoke(commands::edit_item_save_and_next, [this] { _view_edit->save_and_next(true); });
	add_command_invoke(commands::edit_item_save_as, [this] { _view_edit->save_as(); });
	add_command_invoke(commands::edit_item_options, [this] { _view_edit->save_options(); });
	add_command_invoke(commands::tool_remove_metadata,
		[this] { remove_metadata_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_convert, [this] { convert_resize_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_copy_to_folder,
		[this] { copy_move_invoke(_state, _app_frame, _view_frame, false); });
	add_command_invoke(commands::test_crash, [this]
		{
			crash(known_path(platform::known_folder::test_files_folder).combine_file(u8"Test.jpg"sv));
			_app_frame->show(true);
		});
	add_command_invoke(commands::test_boom, [this]
		{
			int* i = nullptr;
			*i = 19; // Crash**			
		});
	add_command_invoke(commands::test_run_all, [this] { _view_test->run_tests(); });
	add_command_invoke(commands::tool_delete, [this] { delete_items(_state.selected_items()); });
	add_command_invoke(commands::tool_desktop_background, [this] { desktop_background_invoke(_state, _app_frame); });
	add_command_invoke(commands::tool_edit, [this] { edit_invoke(_state); });
	add_command_invoke(commands::edit_copy, [this] { cut_copy_invoke(_state, _app_frame, _view_frame, false); });
	add_command_invoke(commands::edit_cut, [this] { cut_copy_invoke(_state, _app_frame, _view_frame, true); });
	add_command_invoke(commands::edit_paste, [this] { edit_paste_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_eject, [this]
		{
			eject_invoke(_state, _app_frame);
			invalidate_view(view_invalid::sidebar);
		});
	add_command_invoke(commands::tool_file_properties,
		[this] { file_properties_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::browse_search, [this] { _search_edit->focus(); });
	add_command_invoke(commands::browse_recursive, [this] { show_flatten_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::view_fullscreen, [this] { toggle_full_screen(); });
	add_command_invoke(commands::option_highlight_large_items, [this]
		{
			setting_invoke(_state, setting.highlight_large_items, !setting.highlight_large_items);
		});
	add_command_invoke(commands::sort_dates_descending, [this]
		{
			setting_invoke(_state, setting.sort_dates_descending, true);
		});
	add_command_invoke(commands::sort_dates_ascending, [this]
		{
			setting_invoke(_state, setting.sort_dates_descending, false);
		});
	add_command_invoke(commands::options_collection, [this] { index_settings_invoke(_state, _app_frame, setting.collection); });
	add_command_invoke(commands::keyboard, [this] { show_keyboard_reference(_state, _app_frame, _commands); });
	add_command_invoke(commands::tool_locate, [this] { locate_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::view_maximize, [this] { _pa->sys_command(ui::sys_command_type::MAXIMIZE); });
	add_command_invoke(commands::view_minimize, [this] { _pa->sys_command(ui::sys_command_type::MINIMIZE); });
	add_command_invoke(commands::tool_move_to_folder,
		[this] { copy_move_invoke(_state, _app_frame, _view_frame, true); });
	add_command_invoke(commands::view_show_sidebar, [this]
		{
			setting_invoke(_state, setting.show_sidebar, !setting.show_sidebar);
		});
	add_command_invoke(commands::tool_new_folder, [this] { new_folder_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::info_new_version, [this] { upgrade_invoke(_state, _app_frame); });
	add_command_invoke(commands::browse_open_containingfolder, [this]
		{
			containing_folder_invoke(_state, _app_frame, _view_frame);
		});
	add_command_invoke(commands::browse_open_googlemap, [this] { _state.open_gps_on_google_maps(); });
	add_command_invoke(commands::browse_open_in_file_browser, [this]
		{
			open_in_file_browser_invoke(_state, _app_frame, _view_frame);
		});
	add_command_invoke(commands::tool_open_with, [this]
		{
			open_with_invoke(_state, _app_frame, _view_frame, _commands);
		});
	add_command_invoke(commands::options_general, [this] { settings_invoke(_state, _app_frame); });
	add_command_invoke(commands::pin_item, [this] { pin_invoke(_state); });
	add_command_invoke(commands::play, [this] { _state.play(_view_frame); });
	add_command_invoke(commands::print, [this] { print_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::rate_none, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 0); });
	add_command_invoke(commands::rate_1, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 1); });
	add_command_invoke(commands::rate_2, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 2); });
	add_command_invoke(commands::rate_3, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 3); });
	add_command_invoke(commands::rate_4, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 4); });
	add_command_invoke(commands::rate_5, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 5); });
	add_command_invoke(commands::rate_rejected, [this] { rate_items_invoke(_state, _app_frame, _view_frame, -1); });
	add_command_invoke(commands::label_select, [this]
		{
			label_items_invoke(_state, _app_frame, _view_frame, label_select_text);
		});
	add_command_invoke(commands::label_second, [this]
		{
			label_items_invoke(_state, _app_frame, _view_frame, label_second_text);
		});
	add_command_invoke(commands::label_approved, [this]
		{
			label_items_invoke(_state, _app_frame, _view_frame, label_approved_text);
		});
	add_command_invoke(commands::label_review, [this]
		{
			label_items_invoke(_state, _app_frame, _view_frame, label_review_text);
		});
	add_command_invoke(commands::label_to_do, [this]
		{
			label_items_invoke(_state, _app_frame, _view_frame, label_to_do_text);
		});
	add_command_invoke(commands::label_none, [this] { label_items_invoke(_state, _app_frame, _view_frame, {}); });
	add_command_invoke(commands::refresh, [this] { reload(); });
	add_command_invoke(commands::search_related, [this] { related_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_rename, [this] { rename_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::repeat_toggle, [this] { repeat_mode_toggle(_state, _app_frame); });
	add_command_invoke(commands::view_restore, [this] { _pa->sys_command(ui::sys_command_type::RESTORE); });
	add_command_invoke(commands::tool_rotate_anticlockwise, [this]
		{
			rotate_invoke(_state, _app_frame, _view_frame, simple_transform::rot_270);
		});
	add_command_invoke(commands::tool_rotate_clockwise, [this]
		{
			rotate_invoke(_state, _app_frame, _view_frame, simple_transform::rot_90);
		});
	add_command_invoke(commands::option_scale_up, [this]
		{
			setting_invoke(_state, setting.scale_up, !setting.scale_up);
		});
	add_command_invoke(commands::view_favorite_tags, [this]
		{
			setting.sidebar.show_favorite_tags_only = !setting.sidebar.show_favorite_tags_only;
			_state.invalidate_view(view_invalid::sidebar);
		});
	add_command_invoke(commands::tool_scan, [this] { scan_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::options_sidebar, [this] { customise_invoke(_state, _app_frame); });
	add_command_invoke(commands::select_all, [this] { _state.select_all(_view_frame); });
	add_command_invoke(commands::select_invert, [this] { _state.select_inverse(_view_frame); });
	add_command_invoke(commands::select_nothing, [this] { _state.select_nothing(_view_frame); });
	add_command_invoke(commands::tool_email, [this] { email_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::option_show_thumbnails, [this]
		{
			_state.view_mode(
				_state.view_mode() == view_type::items ? view_type::media : view_type::items);
		});
	add_command_invoke(commands::option_show_rotated, [this]
		{
			setting_invoke(_state, setting.show_rotated, !setting.show_rotated);
		});
	add_command_invoke(commands::verbose_metadata, [this]
		{
			setting_invoke(_state, setting.verbose_metadata, !setting.verbose_metadata);
			invalidate_view(view_invalid::media_elements);
		});
	add_command_invoke(commands::show_raw_preview, [this]
		{
			setting_invoke(_state, setting.raw_preview, !setting.raw_preview);
			invalidate_view(view_invalid::media_elements);
		});
	add_command_invoke(commands::tool_tag, [this] { tag_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::test_new_version, [this] { test_new_version_invoke(_state, _app_frame); });
	add_command_invoke(commands::option_toggle_details, [this]
		{
			toggle_details_invoke(_state, ui::current_key_state().shift);
		});
	add_command_invoke(commands::option_toggle_item_size, [this]
		{
			toggle_layout_scale_invoke(_state, _app_frame);
			invalidate_view(view_invalid::view_layout);
		});
	add_command_invoke(commands::view_help, [this] { about_invoke(_state, _app_frame, _commands); });
	add_command_invoke(commands::view_items, [this] { _state.view_mode(view_type::items); });
	add_command_invoke(commands::large_font, [this] { font_invoke(_state, _app_frame); });
	add_command_invoke(commands::playback_volume_toggle, [this] { toggle_volume(); });
	add_command_invoke(commands::playback_volume100, [this] { setting.media_volume = media_volumes[0]; });
	add_command_invoke(commands::playback_volume75, [this] { setting.media_volume = media_volumes[1]; });
	add_command_invoke(commands::playback_volume50, [this] { setting.media_volume = media_volumes[2]; });
	add_command_invoke(commands::playback_volume25, [this] { setting.media_volume = media_volumes[3]; });
	add_command_invoke(commands::playback_volume0, [this] { setting.media_volume = media_volumes[4]; });
	add_command_invoke(commands::view_zoom, [this] { zoom_invoke(_state, _app_frame); });

	add_command_invoke(commands::filter_photos, [this]
		{
			_state.filter().toggle(file_group::photo);
			invalidate_view(view_invalid::command_state | view_invalid::group_layout);
		});
	add_command_invoke(commands::filter_videos, [this]
		{
			_state.filter().toggle(file_group::video);
			invalidate_view(view_invalid::command_state | view_invalid::group_layout);
		});
	add_command_invoke(commands::filter_audio, [this]
		{
			_state.filter().toggle(file_group::audio);
			invalidate_view(view_invalid::command_state | view_invalid::group_layout);
		});

	add_command_invoke(commands::group_album, [this] { _state.group_order(group_by::album_show, {}); });
	add_command_invoke(commands::group_camera, [this] { _state.group_order(group_by::camera, {}); });
	add_command_invoke(commands::group_created, [this] { _state.group_order(group_by::date_created, {}); });
	add_command_invoke(commands::group_presence, [this] { _state.group_order(group_by::presence, {}); });
	add_command_invoke(commands::group_file_type, [this] { _state.group_order(group_by::file_type, {}); });
	add_command_invoke(commands::group_location, [this] { _state.group_order(group_by::location, {}); });
	add_command_invoke(commands::group_modified, [this] { _state.group_order(group_by::date_modified, {}); });
	add_command_invoke(commands::group_pixels, [this] { _state.group_order(group_by::resolution, {}); });
	add_command_invoke(commands::group_rating, [this] { _state.group_order(group_by::rating_label, {}); });
	add_command_invoke(commands::group_shuffle, [this] { _state.group_order(group_by::shuffle, {}); });
	add_command_invoke(commands::group_size, [this] { _state.group_order(group_by::size, {}); });
	add_command_invoke(commands::group_extension, [this] { _state.group_order(group_by::extension, {}); });
	add_command_invoke(commands::group_folder, [this] { _state.group_order(group_by::folder, {}); });
	add_command_invoke(commands::group_toggle, [this] { _state.toggle_group_order(); });

	add_command_invoke(commands::sort_def, [this] { _state.group_order({}, sort_by::def); });
	add_command_invoke(commands::sort_name, [this] { _state.group_order({}, sort_by::name); });
	add_command_invoke(commands::sort_size, [this] { _state.group_order({}, sort_by::size); });
	add_command_invoke(commands::sort_date_modified, [this] { _state.group_order({}, sort_by::date_modified); });

	add_command_invoke(commands::tool_test, [this] { _state.view_mode(view_type::test); });
	add_command_invoke(commands::tool_import, [this] { _state.view_mode(view_type::import); });
	add_command_invoke(commands::tool_sync, [this] { _state.view_mode(view_type::sync); });

	add_command_invoke(commands::rename_run, [this] { _view_rename->run(); });

	add_command_invoke(commands::import_analyze, [this] { _view_import->analyze(); });
	add_command_invoke(commands::import_run, [this] { _view_import->run(); });

	add_command_invoke(commands::sync_analyze, [this] { _view_sync->analyze(); });
	add_command_invoke(commands::sync_run, [this] { _view_sync->run(); });

	add_command_invoke(commands::english, [this]
		{
			setting.language = u8"en"sv;
			tt.load_lang({});
			invalidate_view(view_invalid::options);
		});

	_commands[commands::menu_main]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::view_fullscreen),
				find_command(commands::play),
				find_command(commands::browse_search),
				nullptr,
				find_command(commands::menu_navigate),
				find_command(commands::menu_open),
				find_command(commands::menu_tools),
				find_command(commands::menu_rate_or_label),
				find_command(commands::menu_select),
				find_command(commands::menu_group),
				nullptr,
				find_command(commands::tool_import),
				find_command(commands::tool_sync),
				find_command(commands::tool_scan),
				find_command(commands::refresh),
				find_command(commands::tool_new_folder),
				find_command(commands::favorite),
				nullptr,
				find_command(commands::edit_cut),
				find_command(commands::edit_copy),
				find_command(commands::edit_paste),
				nullptr,
				find_command(commands::options_general),
				find_command(commands::options_collection),
				find_command(commands::options_sidebar),
				find_command(commands::menu_display_options),
				find_command(commands::playback_menu),
				find_command(commands::menu_language),
				nullptr,
				find_command(commands::keyboard),
				find_command(commands::view_help),
				find_command(commands::exit)
			};
			return result;
		};
	_commands[commands::menu_open]->menu = [this]
		{
			std::vector<ui::command_ptr> result;
			auto selected_items = std::make_shared<df::item_set>(_state.selected_items());

			if (selected_items->single_file_extension())
			{
				const auto first_item = selected_items->items().front();
				const auto ext = first_item->extension();
				const auto handlers = platform::assoc_handlers(ext);

				for (const auto& h : handlers)
				{
					auto command = std::make_shared<ui::command>();

					command->text = str::format(tt.open_with_fmt, h.name);
					command->invoke = [h, selected_items, f = _app_frame]
						{
							const auto success = h.invoke(selected_items->file_paths(), selected_items->folder_paths());

							if (!success)
							{
								const auto dlg = make_dlg(f);
								dlg->show_message(icon_index::error, tt.open_with_title, tt.open_with_failed);
							}
						};

					result.emplace_back(command);
				}

				const auto file_tools = first_item->file_type()->all_tools();

				if (!file_tools.empty())
				{
					std::vector<ui::command_ptr> file_tool_commands;

					for (const auto& t : file_tools)
					{
						if (t->exists())
						{
							auto command = std::make_shared<ui::command>();

							command->text = format(tt.open_with_fmt, t->text);
							command->invoke = [t, first_item, f = _app_frame]
								{
									const auto success = t->invoke(first_item->path());

									if (!success)
									{
										const auto dlg = make_dlg(f);
										dlg->show_message(icon_index::error, tt.open_with_title, tt.open_with_failed);
									}
								};

							file_tool_commands.emplace_back(command);
						}
					}

					if (!result.empty() && !file_tool_commands.empty())
					{
						result.emplace_back(nullptr);
					}

					result.insert(result.end(), file_tool_commands.begin(), file_tool_commands.end());
				}

				if (!result.empty())
				{
					result.emplace_back(nullptr);
				}
			}

			result.emplace_back(find_command(commands::tool_open_with));
			result.emplace_back(find_command(commands::browse_open_containingfolder));
			result.emplace_back(find_command(commands::browse_open_googlemap));
			result.emplace_back(find_command(commands::browse_open_in_file_browser));
			result.emplace_back(find_command(commands::tool_file_properties));
			return result;
		};
	_commands[commands::menu_tag_with]->menu = [this]
		{
			std::vector<ui::command_ptr> result;
			const bool is_enabled = _state.can_process_selection(_view_frame, df::process_items_type::can_save_metadata);
			const auto favorite_tags = str::split(setting.favorite_tags, true);

			for (const auto& t : favorite_tags)
			{
				auto tag = str::cache(t);
				auto command = std::make_shared<ui::command>();

				command->text = tag;
				command->enable = is_enabled;
				command->invoke = [this, tag]
					{
						auto dlg = make_dlg(_app_frame);
						const auto results = std::make_shared<command_status>(*this, dlg, icon_index::star, tt.tag_selected,
							_state.selected_count());
						_state.toggle_selected_item_tags(_view_frame, results, tag);
					};

				result.emplace_back(command);
			}

			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::tool_tag));
			return result;
		};
	_commands[commands::menu_language]->menu = [this]
		{
			const auto lang_folder = known_path(platform::known_folder::running_app_folder).combine(u8"languages"sv);
			const auto folder_contents = platform::iterate_file_items(lang_folder, false);

			std::vector<ui::command_ptr> result;
			result.emplace_back(find_command(commands::english));
			result.emplace_back(nullptr);

			for (const auto& f : folder_contents.files)
			{
				const auto lang_path = lang_folder.combine_file(f.name);
				const auto extension = lang_path.extension();

				if (str::icmp(extension, u8".po"sv) == 0)
				{
					const auto lang_code = lang_path.file_name_without_extension();

					auto command = std::make_shared<ui::command>();
					command->text = language_name(lang_code);
					command->enable = true;
					command->checked = setting.language == lang_code;
					command->invoke = [this, lang_path, lang_code]
						{
							setting.language = lang_code;
							tt.load_lang(lang_path);
							invalidate_view(view_invalid::options);
						};

					result.emplace_back(command);
				}
			}
			return result;
		};
	_commands[commands::menu_tools_toolbar]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::tool_locate),
				find_command(commands::tool_adjust_date),
				find_command(commands::tool_burn),
				find_command(commands::tool_convert),
				find_command(commands::tool_desktop_background),
				find_command(commands::tool_edit_metadata),
				find_command(commands::tool_email),
				find_command(commands::print),
				nullptr,
				find_command(commands::tool_save_current_video_frame),
				nullptr,
				find_command(commands::tool_delete),
				find_command(commands::tool_rename),
				find_command(commands::tool_copy_to_folder),
				find_command(commands::tool_move_to_folder)
			};
			return result;
		};
	_commands[commands::menu_tools]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::tool_locate),
				find_command(commands::tool_adjust_date),
				find_command(commands::tool_burn),
				find_command(commands::tool_convert),
				find_command(commands::tool_desktop_background),
				find_command(commands::tool_edit_metadata),
				find_command(commands::tool_email),
				find_command(commands::print),
				find_command(commands::tool_rotate_anticlockwise),
				find_command(commands::tool_rotate_clockwise),
			};
			return result;
		};
	_commands[commands::menu_group_toolbar]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::group_album),
				find_command(commands::group_camera),
				find_command(commands::group_created),
				find_command(commands::group_modified),
				find_command(commands::group_extension),
				find_command(commands::group_file_type),
				find_command(commands::group_location),
				find_command(commands::group_pixels),
				find_command(commands::group_presence),
				find_command(commands::group_rating),
				find_command(commands::group_size),
				find_command(commands::group_folder),
				nullptr,
				find_command(commands::sort_def),
				find_command(commands::sort_name),
				find_command(commands::sort_size),
				find_command(commands::sort_date_modified),
				nullptr,
				find_command(commands::group_shuffle),
				find_command(commands::group_toggle),
			};
			return result;
		};
	_commands[commands::menu_group]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::group_album),
				find_command(commands::group_camera),
				find_command(commands::group_created),
				find_command(commands::group_modified),
				find_command(commands::group_extension),
				find_command(commands::group_file_type),
				find_command(commands::group_location),
				find_command(commands::group_pixels),
				find_command(commands::group_presence),
				find_command(commands::group_rating),
				find_command(commands::group_size),
				find_command(commands::group_folder),
				nullptr,
				find_command(commands::sort_def),
				find_command(commands::sort_name),
				find_command(commands::sort_size),
				find_command(commands::sort_date_modified),
				nullptr,
				find_command(commands::group_shuffle),
				find_command(commands::group_toggle),
				nullptr,
				find_command(commands::sort_dates_descending),
				find_command(commands::sort_dates_ascending),
			};
			return result;
		};
	_commands[commands::menu_select]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::browse_previous_item),
				find_command(commands::browse_previous_item_extend),
				find_command(commands::browse_next_item),
				find_command(commands::browse_next_item_extend),
				nullptr,
				find_command(commands::select_all),
				find_command(commands::select_invert),
				find_command(commands::select_nothing),
				nullptr,
				find_command(commands::pin_item)
			};
			return result;
		};

	_commands[commands::menu_rate_or_label]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::rate_1),
				find_command(commands::rate_2),
				find_command(commands::rate_3),
				find_command(commands::rate_4),
				find_command(commands::rate_5),
				nullptr,
				find_command(commands::rate_none),
				find_command(commands::rate_rejected),
				nullptr,
				find_command(commands::label_select),
				find_command(commands::label_second),
				find_command(commands::label_approved),
				find_command(commands::label_review),
				find_command(commands::label_to_do),
				find_command(commands::label_none)
			};

			return result;
		};
	_commands[commands::menu_navigate]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::browse_parent),
				find_command(commands::browse_back),
				find_command(commands::browse_forward),
				find_command(commands::browse_previous_folder),
				find_command(commands::browse_next_folder),
				find_command(commands::browse_previous_group),
				find_command(commands::browse_next_group),
				nullptr,
				find_command(commands::browse_recursive),
				nullptr,
				find_command(commands::advanced_search),
			};
			return result;
		};
	_commands[commands::menu_display_options]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::option_show_thumbnails),
				find_command(commands::option_scale_up),
				find_command(commands::option_show_rotated),
				find_command(commands::option_highlight_large_items),
				find_command(commands::verbose_metadata),
				find_command(commands::show_raw_preview),
				nullptr,
				find_command(commands::large_font),
				nullptr,
				find_command(commands::view_show_sidebar),
				find_command(commands::option_toggle_item_size),
				find_command(commands::option_toggle_details)
			};

			return result;
		};
	_commands[commands::menu_playback]->menu = _commands[commands::playback_menu]->menu = [this]
		{
			std::vector<ui::command_ptr> result = {
				find_command(commands::playback_volume100),
				find_command(commands::playback_volume75),
				find_command(commands::playback_volume50),
				find_command(commands::playback_volume25),
				find_command(commands::playback_volume0),
				nullptr,
				find_command(commands::playback_auto_play),
				find_command(commands::playback_last_played_pos),
				nullptr,
				find_command(commands::playback_repeat_all),
				find_command(commands::playback_repeat_one),
				find_command(commands::playback_repeat_none),
			};

			const auto devices = list_audio_playback_devices();

			if (!devices.empty())
			{
				result.emplace_back(nullptr);

				const auto play_id = _player->play_audio_device_id();

				for (const auto& d : devices)
				{
					auto command = std::make_shared<ui::command>();
					command->text = d.name;
					command->enable = true;
					command->checked = play_id == d.id;
					command->invoke = [this, d]
						{
							_state.change_audio_device(d.id);
							setting.sound_device = d.id;
							invalidate_view(view_invalid::options);
						};

					result.emplace_back(command);
				}
			}

			const auto& display = _state.display_state();

			if (display && display->is_one() && display->_player_media_info.has_multiple_audio_streams)
			{
				result.emplace_back(nullptr);
				for (const auto& st : display->_player_media_info.streams)
				{
					if (st.type == av_stream_type::audio)
					{
						auto text = st.title;
						if (text.empty()) text = str::format(tt.stream_name_fmt, st.index);

						auto command = std::make_shared<ui::command>();
						command->text = text;
						command->enable = true;
						command->checked = st.is_playing;
						command->invoke = [this, st]
							{
								_state.change_tracks(-1, st.index);
							};

						result.emplace_back(command);
					}
				}
			}

			return result;
		};

	for (const auto& c : _commands)
	{
		c.second->opaque = c.first;
	}
}

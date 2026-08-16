// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Command handlers for all user actions. Implements file operations, editing,
// sharing, metadata updates, and other application commands.

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
#include "ui_map.h"
#include "view_edit.h"
#include "view_items.h"
#include "app_match.h"
#include "app_text.h"
#include "app_command_status.h"
#include "app_util.h"
#include "app.h"
#include "crypto.h"
#include "util_base64.h"
#include "view_import.h"
#include "view_locate.h"
#include "view_rename.h"
#include "view_batch.h"
#include "view_sync.h"
#include "view_tags.h"

static std::string decode_secret(const std::string_view input, const std::string_view password)
{
	const auto data = base64_decode(input);
	auto decoded = crypto::decrypt(data, password);
	return std::string(decoded.begin(), decoded.end());
}

#if __has_include("secrets.h")
# include "secrets.h"
#else
static const std::string google_maps_api_key = "";
static const std::string azure_maps_api_key = "";
#endif

extern bool toggle_details_state;

static constexpr auto docs_url = "https://www.diffractor.com/docs";
static constexpr auto releases_url = "https://www.diffractor.com/releases";
static constexpr auto support_url = "https://github.com/diffractor/diffractor/issues";
static constexpr auto donate_url = "https://www.paypal.com/donate/?hosted_button_id=HX5NRS9JGKLRL";

static void zoom_invoke(const view_state& s, const ui::control_frame_ptr& parent)
{
	const auto display = s.display_state();

	if (display && display->can_zoom())
	{
		display->toggle_zoom();
	}
};

static void zoom_fit_invoke(const view_state& s)
{
	if (const auto display = s.display_state(); display && display->can_zoom()) display->zoom(false);
}

static void zoom_fit_variant_invoke(const view_state& s, const df::zoom_scale_mode mode)
{
	if (const auto display = s.display_state(); display && display->can_zoom()) display->zoom_fit_variant(mode);
}

static void zoom_100_invoke(const view_state& s)
{
	if (const auto display = s.display_state(); display && display->can_zoom()) display->zoom_100();
}

static void zoom_toggle_fit_invoke(const view_state& s)
{
	if (const auto display = s.display_state(); display && display->can_zoom()) display->toggle_zoom_fit();
}

static void zoom_step_invoke(const view_state& s, const int direction)
{
	if (const auto display = s.display_state(); display && display->can_zoom()) display->adjust_zoom_scale(direction);
}

static void pin_invoke(view_state& s)
{
	// Only one item is ever held, so the command that reports a pin is held must also release it.
	// Re-pointing it at whatever has focus instead would leave no way back to plain browsing
	// without walking focus to the pinned item first.
	if (s.has_pin())
	{
		s._pin_item.reset();
	}
	else
	{
		s._pin_item = s.focus_item();
	}

	s.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw | view_invalid::command_state);
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

static void open_in_file_browser_invoke(const view_state& s, const ui::control_frame_ptr& parent,
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
	if (!s.search().is_showing_folder()) return;

	int i = 2;
	const auto folder = s.save_path();
	auto new_name = std::string(tt.new_folder_name);
	auto new_path = folder.combine(new_name);

	while (platform::exists(new_path) && i < 100)
	{
		new_name = std::format("{} {}", tt.new_folder_name, i++);
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

static void burn_command_invoke(view_state& s, const ui::control_frame_ptr& parent,
                                const view_host_base_ptr& view)
{
	const auto dlg = make_dlg(parent);
	const auto title = tt.burn_title;
	const auto can_process = s.
		can_process_selection_and_mark_errors(view, df::process_items_type::local_file_or_folder);

	pause_media pause(s);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		const auto items = s.selected_items();
		const std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon_index::disk, title,
			                                                format_plural_text(tt.burn_info_fmt, items), items.thumbs(),
			                                                items.size())),
			std::make_shared<divider_element>(),
			set_margin(std::make_shared<text_element>(tt.burn_help)),
			std::make_shared<divider_element>(),
			std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.burn_stage),
		};

		if (dlg->show_modal(controls) == ui::close_result::ok &&
			!platform::burn_to_cd(items.file_paths(true), items.folder_paths()))
		{
			dlg->show_message(icon_index::error, title, tt.burn_failed);
		}
	}
}

static void print_invoke(const view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
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

static void rename_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto title = tt.command_rename;
	constexpr auto icon = icon_index::rename;
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

			const std::vector<view_element_ptr> controls{
				set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title,
				                                                format_plural_text(tt.rename_fmt, items),
				                                                items.thumbs(), items.size())),
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

static void file_properties_invoke(const view_state& s, const ui::control_frame_ptr& parent,
                                   const view_host_base_ptr& view)
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

static void edit_paste_invoke(view_state& s, const ui::control_frame_ptr& parent,
                              const std::shared_ptr<view_frame>& view)
{
	const auto dlg = make_dlg(parent);
	const auto data = platform::clipboard();
	platform::file_op_result result;

	if (data->has_drop_files())
	{
		detach_file_handles detach(s);
		shell_file_operation_ui processing(*view, parent);
		result = data->drop_files(s.save_path(), platform::drop_effect::none);
		if (result.success() && (!result.created_files.files.empty() || !result.created_files.folders.empty()))
			detach.keep_display_closed();
	}
	else if (data->has_bitmap())
	{
		detach_file_handles detach(s);
		result = data->save_bitmap(s.save_path(), tt.pasted_file_name, true);
		if (result.success() && (!result.created_files.files.empty() || !result.created_files.folders.empty()))
			detach.keep_display_closed();
	}

	if (result.failed())
	{
		dlg->show_message(icon_index::error, tt.command_edit_paste,
		                  str::is_empty(result.error_message) ? tt.error_unknown.sv() : result.error_message);
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

static void setting_invoke(const view_state& s, bool& val, const bool new_val)
{
	val = new_val;
	s.invalidate_view(view_invalid::view_layout | view_invalid::group_layout | view_invalid::app_layout);
}

static void zoom_navigator_mode_invoke(const view_state& state, const zoom_navigator_mode mode)
{
	setting.zoom_navigator = mode;
	if (const auto display = state.display_state()) display->mark_zoom_activity();
	state.invalidate_view(view_invalid::view_redraw | view_invalid::controller | view_invalid::options_save);
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
	// Rotation rewrites each item in place, so it never needs a save folder. The only
	// precondition is that the selection can be written back, which can_process checks below
	// and which is also what enables the command.
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
		const auto is_single = items.size() == 1;

		// A single-item rotation is reviewed only while the user asks for it; rotating several items
		// overwrites several originals at once and is always reviewed.
		if (!is_single || setting.confirm_rotations)
		{
			files ff;
			const auto thumb = s.first_selected_thumb();
			bool confirm_single = setting.confirm_rotations;

			std::vector<view_element_ptr> controls;
			controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
				dlg->_frame, icon, title, format_plural_text(tt.rotate_info_fmt, items), items.thumbs(),
				items.size())));
			controls.emplace_back(std::make_shared<divider_element>());

			if (is_valid(thumb))
			{
				const auto surface = ff.image_to_surface(thumb);
				controls.emplace_back(std::make_shared<ui::before_after_control>(surface, surface->transform(t)));
			}

			if (is_single)
			{
				controls.emplace_back(set_margin(std::make_shared<ui::check_control>(
					dlg->_frame, tt.rotate_confirm_single, confirm_single)));
			}

			controls.emplace_back(std::make_shared<divider_element>());
			controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_rotate));

			if (dlg->show_modal(controls) != ui::close_result::ok) return;

			if (is_single && confirm_single != setting.confirm_rotations)
			{
				setting.confirm_rotations = confirm_single;
				s.invalidate_view(view_invalid::options_save);
			}
		}

		{
			record_feature_use(features::rotate);

			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());

			batch_edit_spec spec;
			spec.process_type = df::process_items_type::can_save_pixels;
			spec.encode_params = make_file_encode_params();
			// A rotation always changes what is drawn, whether it is stored as an orientation tag or
			// as rotated pixels.
			spec.changes_presentation = true;
			spec.make_edits = [t](const df::file_path path) -> std::optional<item_edits>
			{
				// Only the frame size and stored orientation are needed, and load_image_file derives
				// both from this same scan, so read the header instead of the whole file.
				sizei dimensions{0, 0};
				auto stored_orientation = ui::orientation::top_left;
				file_read_stream stream;

				if (stream.open(path))
				{
					const auto info = scan_photo(stream);
					dimensions = info.dimensions();
					stored_orientation = info.orientation;
				}

				if (dimensions.cx <= 0 || dimensions.cy <= 0)
				{
					// Bytes the header scan cannot describe, such as a container carrying a saveable
					// extension; the full load still decodes them.
					files ff;
					const auto load_result = ff.load(path, false);
					if (!load_result.success) return {};
					dimensions = load_result.dimensions();
					stored_orientation = load_result.orientation();
				}

				item_edits result;
				const auto current_orientation = setting.show_rotated
					                                 ? stored_orientation
					                                 : ui::orientation::top_left;
				const auto crop = quadd(dimensions).transform(
					to_simple_transform(current_orientation)).transform(t);

				if (current_orientation != ui::orientation::top_left)
				{
					result.metadata.orientation = ui::orientation::top_left;
				}

				result.image.crop_bounds(crop);
				return result;
			};

			s.modify_items(results, items.items(), spec, view);
		}
	}
}

static void desktop_background_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	s.capture_display([&s, parent](const file_load_result& loaded)
	{
		const auto title = tt.command_desktop_background;
		auto dlg = make_dlg(parent);
		pause_media pause(s);
		const std::vector<view_element_ptr> controls = {
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
			constexpr auto write_extension = ".png";
			const auto write_path = df::file_path(known_path(platform::known_folder::app_data), "wallpaper",
			                                      write_extension);
			const auto path_temp = platform::temp_file(write_extension);
			const auto bounds = ui::desktop_bounds(true);
			const auto screen_extent = bounds.extent();
			const auto max_dim = std::max(screen_extent.cx, screen_extent.cy);
			const auto dimensions = setting.desktop_background.maximize ? sizei(max_dim, max_dim) : screen_extent;
			const auto encode_params = make_file_encode_params();

			const auto results = std::make_shared<command_status>(s._async, dlg, icon_index::wallpaper, title, 1);

			// Writing a screen-sized PNG twice is far too slow to run on the UI thread. Only the
			// wallpaper switch comes back, and a failure is now reported instead of being
			// described as a failed update.
			s.queue_async(async_queue::work,
			              [&s, results, loaded, write_path, path_temp, dimensions, encode_params]
			              {
				              files ff;
				              const image_edits edits(dimensions);
				              std::string error;

				              if (!ff.save(path_temp, loaded))
				              {
					              error = str_format(tt.error_create_file_failed_fmt.sv(), path_temp);
				              }
				              else
				              {
					              const auto update_result = ff.update(path_temp, write_path, {}, edits, encode_params,
					                                                   false, {});

					              if (update_result.failed())
					              {
						              error = update_result.format_error(
							              str_format(tt.error_create_file_failed_fmt.sv(), write_path));
					              }
				              }

				              if (path_temp.exists())
				              {
					              platform::delete_file(path_temp);
				              }

				              if (error.empty())
				              {
					              s.queue_ui([results, write_path]
					              {
						              platform::set_desktop_wallpaper(write_path);
						              results->complete();
					              });
				              }
				              else
				              {
					              results->abort(error);
				              }
			              });

			results->wait_for_complete();
		}
	});
}

static void capture_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	s.capture_display([&s, parent](const file_load_result& lr)
	{
		const auto item = s.command_item();

		if (item && !lr.is_empty())
		{
			auto i = 1;
			const auto source_path = item->path();
			const auto save_ext = lr.is_jpeg() ? "jpg" : "png";
			const auto save_folder = s.save_path();
			const auto save_name = source_path.file_name_without_extension();
			auto save_path = df::file_path(save_folder, save_name, save_ext);

			while (save_path.exists())
			{
				save_path = df::file_path(save_folder, std::format("{}-{}", save_name, i++), save_ext);
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

static void favorite_tags_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	auto dlg = make_dlg(parent);
	auto favorite_tags = setting.favorite_tags;
	auto edit = std::make_shared<ui::multi_line_edit_control>(dlg->_frame, favorite_tags);
	auto content = std::make_shared<ui::group_control>();
	const auto toolbar_refresh = std::make_shared<ui::recommended_words_control::refresh_group::element_type>();
	const auto unique_favorites = [&favorite_tags]
	{
		std::vector<std::string> result;
		df::hash_set<std::string, df::ihash, df::ieq> seen;
		for (const auto tag : str::split(favorite_tags, true))
		{
			if (seen.emplace(tag).second) result.emplace_back(tag);
		}
		return result;
	};
	const auto contains_favorite = [&unique_favorites](const std::string_view tag)
	{
		const auto favorites = unique_favorites();
		return std::ranges::find_if(favorites, [tag](const std::string_view favorite)
		{
			return str::icmp(favorite, tag) == 0;
		}) != favorites.end();
	};
	const auto toggle_favorite = [&favorite_tags, edit, unique_favorites](const std::string_view tag)
	{
		auto favorites = unique_favorites();
		const auto found = std::ranges::find_if(favorites, [tag](const std::string_view favorite)
		{
			return str::icmp(favorite, tag) == 0;
		});
		if (found == favorites.end())
			favorites.emplace_back(tag);
		else
			favorites.erase(found);
		favorite_tags = str::combine(favorites);
		edit->text(favorite_tags);
	};
	favorite_tags = str::combine(unique_favorites());
	edit->text(favorite_tags);

	content->add(set_margin(std::make_shared<text_element>(tt.customise_tags_help)));
	content->add(set_margin(std::make_shared<text_element>(tt.help_tag1)));
	content->add(set_margin(std::make_shared<text_element>(tt.help_tag2)));
	content->add(edit);

	df::string_counts common_counts;
	for (const auto& tag : s.item_index.distinct_tags())
	{
		++common_counts[tag.first];
	}
	s.recent_tags.count_strings(common_counts, 1000000);

	const auto common_tags = top_map(common_counts, 20);
	if (!common_tags.empty())
	{
		content->add(std::make_shared<ui::title_control>(tt.tags_common_label));
		content->add(set_margin(std::make_shared<ui::recommended_words_control>(
			dlg->_frame, common_tags, toggle_favorite, contains_favorite, toolbar_refresh)));
	}

	static constexpr std::array<std::string_view, 12> workflow_tags = {
		"Approved", "Archive", "Client", "Draft", "Final", "Progress", "Review", "Published",
		"Rejected", "Rights", "Select", "Todo"
	};
	std::vector<std::string_view> workflow_suggestions;
	df::hash_set<std::string_view, df::ihash, df::ieq> workflow_seen;
	const auto append_workflow_tag = [&workflow_suggestions, &workflow_seen](const std::string_view tag)
	{
		if (workflow_seen.emplace(tag).second) workflow_suggestions.emplace_back(tag);
	};
	for (const auto tag : str::split(tt.default_favorite_tags, true)) append_workflow_tag(tag);
	for (const auto tag : workflow_tags) append_workflow_tag(tag);
	content->add(std::make_shared<ui::title_control>(tt.tags_workflow_label));
	content->add(set_margin(std::make_shared<ui::recommended_words_control>(
		dlg->_frame, workflow_suggestions, toggle_favorite, contains_favorite, toolbar_refresh)));

	std::vector<view_element_ptr> controls;
	controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
		dlg->_frame, icon_index::tag, tt.customise_tags_title, tt.customise_tags_help)));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(content);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame));

	pause_media pause(s);

	// The edit box and the suggestion toolbars all write to the local copy, so Cancel and
	// Escape leave the saved favourites alone.
	if (dlg->show_modal(controls, {54}, {66}) != ui::close_result::ok) return;

	favorite_tags = str::combine(unique_favorites());
	setting.favorite_tags = favorite_tags;
	s.invalidate_view(view_invalid::sidebar | view_invalid::options_save | view_invalid::command_state |
		view_invalid::tooltip);
}

static void rate_items_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view, int r)
{
	auto dlg = make_dlg(parent);
	metadata_edits edits;
	edits.rating = r;

	constexpr auto icon = icon_index::star;
	const auto title = tt.prop_name_rating;
	const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, s.selected_count());

	s.modify_items(results, icon, title, s.selected_items().items(), edits, view);
}

static void label_items_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
                               const std::string_view label)
{
	const auto selected = s.selected_items();
	const auto& items = selected.items();

	// Applying a label the whole selection already carries removes it, so a key repeats the toggle
	// the grading control performs with the pointer and clearing never needs a separate command.
	const auto is_set = !label.empty() && !items.empty() &&
		std::ranges::all_of(items, [label](const df::item_element_ptr& i)
		{
			return str::icmp(i->label(), label) == 0;
		});

	auto dlg = make_dlg(parent);
	metadata_edits edits;
	edits.label = is_set ? std::string_view{} : label;

	constexpr auto icon = icon_index::flag;
	const auto title = tt.prop_name_label;
	const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, items.size());

	s.modify_items(results, icon, title, items, edits, view);
}

static void cut_copy_invoke(const view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view,
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

static void copy_item_path_invoke(const view_state& s)
{
	const auto items = s.selected_items().items();
	std::string paths;

	for (const auto& item : items)
	{
		if (!paths.empty()) paths += "\r\n";
		paths += item->path().pack();
	}

	if (!paths.empty()) platform::set_clipboard(paths);
}

class folder_auto_complete final : public std::enable_shared_from_this<folder_auto_complete>,
                                   public ui::complete_strategy_t
{
	view_state& _state;
	df::folder_counts _folders;
	std::vector<df::folder_path> _recents;
	ui::control_frame_ptr _parent;
	ui::auto_complete_match_ptr _selected;

public:
	std::string no_results_message() override
	{
		return std::string(tt.type_to_search);
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

	void search(const std::string& query,
	            const std::function<void(const ui::auto_complete_results&)> complete) override
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
			complete(ui::auto_complete_results{found.begin(), found.end()});
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

static void copy_move_invoke(view_state& s, const ui::control_frame_ptr& parent,
                             const std::shared_ptr<view_frame>& view,
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
		// Opening with an empty box means OK fails on the state the dialog itself started in,
		// so it starts on the destination last used.
		std::string text = s.recent_folders.items().empty() ? std::string{} : s.recent_folders.items().back();

		const auto auto_complete = std::make_shared<folder_auto_complete>(s, parent);
		const auto search_control = std::make_shared<ui::search_control>(dlg->_frame, text, auto_complete);
		auto_complete->_search_control = search_control;

		const auto& items = s.selected_items();
		const auto items_message = format_plural_text(is_move ? tt.move_fmt : tt.copy_fmt, items);

		std::vector<view_element_ptr> controls = {
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title, items_message, items.thumbs(),
			                                                items.size())),
			std::make_shared<divider_element>(),
			search_control,
			std::make_shared<ui::check_control>(dlg->_frame, tt.open_dest, setting.show_results),
			std::make_shared<divider_element>(),
			std::make_shared<ui::ok_cancel_control>(dlg->_frame)
		};

		if (ui::close_result::ok == dlg->show_modal(controls, {66}))
		{
			df::folder_path write_folder(text);

			if (!write_folder.is_qualified())
			{
				dlg->show_message(icon_index::error, title, str_format(tt.is_not_valid_folder_fmt.sv(), write_folder));
			}
			else if (const auto create_folder_result = platform::create_folder(write_folder); create_folder_result.
				failed())
			{
				dlg->show_message(icon_index::error, title,
				                  create_folder_result.format_error(
					                  str_format(tt.failed_to_create_folder_fmt.sv(), write_folder)));
			}
			else
			{
				// The shell would silently auto-rename every collision, so a user who meant to replace
				// got a second copy and no way to say otherwise. Name the collisions and let them
				// choose, defaulting to the option that cannot destroy anything.
				auto replace_existing = false;
				const auto collisions = check_overwrite(write_folder, items, {});

				if (!collisions.empty())
				{
					const auto collision_dlg = make_dlg(parent);
					auto policy = collision_policy::auto_rename;

					std::vector<view_element_ptr> collision_controls;
					collision_controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
						collision_dlg->_frame, icon, title,
						format_plural_text(tt.would_overwrite_fmt, collisions))));
					collision_controls.emplace_back(create_collision_policy_control(
						collision_dlg->_frame, policy, [] {}, true, false));
					collision_controls.emplace_back(std::make_shared<divider_element>());
					collision_controls.emplace_back(
						std::make_shared<ui::ok_cancel_control>(collision_dlg->_frame));

					if (collision_dlg->show_modal(collision_controls) != ui::close_result::ok) return;
					replace_existing = policy == collision_policy::replace;
				}

				detach_file_handles detach(s);
				shell_file_operation_ui processing(*view, parent);
				// A move empties the folders it came from, and only the destination was ever reported.
				// A search that names no folder is not watched, so it would keep listing what moved.
				df::unique_folders sources;

				if (is_move)
				{
					for (const auto& path : items.file_paths(true)) sources.emplace(path.folder());
					for (const auto& path : items.folder_paths()) sources.emplace(path.parent());
				}

				const auto result = platform::move_or_copy(
					items.file_paths(true), items.folder_paths(), write_folder, is_move, replace_existing);

				if (result.success())
				{
					s.recent_folders.add(write_folder.text());
					s.item_index.queue_scan_folder(write_folder);
					if (!sources.empty()) s.item_index.queue_validate_changed_folders(std::move(sources));

					if (setting.show_results)
					{
						detach.keep_display_closed();
						s.open(view, df::search_t().add_selector(write_folder),
						       make_unique_paths(result.created_files));
					}
				}
				else if (result.code != platform::file_op_result_code::CANCELLED)
				{
					dlg->show_message(icon_index::error, title, result.format_error());
				}
			}
		}
	}
}

static void repeat_mode_toggle(const view_state& s, const ui::control_frame_ptr& parent)
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

static void font_invoke(const view_state& s, const ui::control_frame_ptr& parent)
{
	setting.large_font = !setting.large_font;
	s.invalidate_view(view_invalid::visual_options);
}

static void toggle_layout_scale_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	setting.step_item_scale(1);
}

static void toggle_details_invoke(const view_state& s, const bool only_toggle_selected)
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

			// Persist the choice per media type so a subsequent search (e.g. clicking a
			// sidebar tag) recreates the groups with the same display instead of reverting. (#229)
			setting.set_detail_display(g->_key.type, new_display == df::item_group_display::detail);
		}

		s.invalidate_view(view_invalid::command_state);
	}
}

class open_with_auto_complete final : public ui::complete_strategy_t,
                                      public std::enable_shared_from_this<open_with_auto_complete>
{
	struct entry
	{
		std::string name;
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

		std::string edit_text() const override
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

	df::hash_map<std::string, entry, df::ihash, df::ieq> _handlers;

	view_state& _state;
	ui::auto_complete_match_ptr _result;
	std::vector<command_info_ptr> _cmds;
	ui::frame_ptr _parent;
	std::shared_ptr<df::item_set> _items;

public:
	std::string no_results_message() override
	{
		return std::string(tt.type_to_search);
	}

	open_with_auto_complete(view_state& s, std::shared_ptr<df::item_set> items, ui::frame_ptr parent,
	                        std::vector<command_info_ptr> cmds) : _state(s), _cmds(std::move(cmds)),
	                                                              _parent(std::move(parent)), _items(std::move(items))
	{
		resize_to_show_results = false;
		max_predictions = 20u;
	}

	void initialise(const std::function<void(const ui::auto_complete_results&)> complete) override
	{
		for (const auto& c : _cmds)
		{
			if (!c->enable) continue;

			const auto name = c->text;
			df::assert_true(!str::is_empty(name));

			entry h;
			h.name = std::format("{} ({})", name, tt.open_with_tool);
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
				e.name = std::format("{} ({})", h.name, tt.open_with_app);
				e.invoke = h.invoke;
				e.weight = h.weight;
				_handlers[h.name] = e;
			}
		}

		df::string_counts counts;
		_state.recent_apps.count_strings(counts, 1);

		for (const auto& c : counts)
		{
			const auto found = _handlers.find(std::string(c.first));

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

	void search(const std::string& query,
	            const std::function<void(const ui::auto_complete_results&)> complete) override
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
		complete(ui::auto_complete_results{results.begin(), results.end()});
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

	void invoke() const
	{
		if (_result)
		{
			const view_element_event e{view_element_event_type::invoke, nullptr};
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

		std::string text;
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

static std::string format_drive_details(const platform::drive_t& d)
{
	std::vector<std::string> parts;

	if (!d.vol_name.empty())
	{
		parts.emplace_back(std::format("{}: {}", tt_prep(tt.disk_label.sv()), d.vol_name));
	}

	// Capacity is zero for a card reader or optical drive with no media inserted.
	if (d.capacity.is_valid())
	{
		parts.emplace_back(std::format("{}: {}", tt_prep(tt.disk_capacity.sv()), d.capacity.str()));
		parts.emplace_back(std::format("{}: {}", tt_prep(tt.disk_used.sv()), d.used.str()));
	}

	if (!d.file_system.empty())
	{
		parts.emplace_back(std::format("{}: {}", tt_prep(tt.disk_system.sv()), d.file_system));
	}

	return str::combine(parts, ", ", false);
}

static void show_eject_dialog(view_state& s, const ui::control_frame_ptr& parent, const platform::drives& drives)
{
	const auto title = tt.command_eject;
	constexpr auto icon = icon_index::eject;
	auto dlg = make_dlg(parent);

	std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title, tt.eject_help)),
		std::make_shared<divider_element>()
	};

	// Recorded by a drive button, reported once the dialog has closed. Showing a message from this
	// frame while it is inside show_modal would destroy the controls of the running button.
	const auto failed_drive = std::make_shared<std::string>();
	const auto ejecting = std::make_shared<bool>(false);
	auto found_removable = false;

	for (const auto& d : drives)
	{
		if (d.type != platform::drive_type::removable)
		{
			continue;
		}

		found_removable = true;

		controls.emplace_back(std::make_shared<ui::button_control>(
			dlg->_frame, drive_icon(d.type), str_format(tt.eject_title_fmt.sv(), d.name), format_drive_details(d),
			[&s, weak_frame = std::weak_ptr(dlg->_frame), path = df::folder_path(d.name), name = d.name, failed_drive,
				ejecting]
			{
				if (*ejecting) return;
				*ejecting = true;

				s.queue_async(async_queue::work, [&s, weak_frame, path, name, failed_drive]
				{
					const auto success = platform::eject(path);

					s.queue_ui([weak_frame, name, failed_drive, success]
					{
						const auto frame = weak_frame.lock();
						if (!frame) return;

						if (!success) *failed_drive = name;
						frame->close(!success);
					});
				});
			}));
	}

	if (!found_removable)
	{
		controls.emplace_back(set_margin(std::make_shared<text_element>(tt.eject_none_found)));
	}

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::close, tt.close,
	                                                           tt.eject_close_info, [f = dlg->_frame]
	                                                           {
		                                                           f->close(false);
	                                                           }));

	{
		pause_media pause(s);
		dlg->show_modal(controls);
	}

	if (!failed_drive->empty())
	{
		dlg.reset();
		make_dlg(parent)->show_message(icon_index::error, title,
		                               str_format(tt.eject_failed_fmt.sv(), *failed_drive));
	}
}

static void eject_invoke(view_state& s, const ui::control_frame_ptr& parent, std::function<void()> complete)
{
	s.queue_async(async_queue::work, [&s, weak_parent = std::weak_ptr(parent), complete = std::move(complete)]() mutable
	{
		auto drives = platform::scan_drives();

		s.queue_ui([&s, weak_parent, drives = std::move(drives), complete = std::move(complete)]
		{
			const auto parent_frame = weak_parent.lock();

			if (parent_frame)
			{
				show_eject_dialog(s, parent_frame, drives);
			}

			if (complete) complete();
		});
	});
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
		                  std::format("{}\n{}", tt.scan_failed, scan_result.error_message));
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

			const auto text = str_format(tt.favorite_add_fmt.sv(), search.text());
			std::string title_text;

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

bool ui::browse_for_term(view_state& vs, const control_frame_ptr& parent, std::string& result)
{
	const auto dlg = make_dlg(parent);
	auto dlg_parent = dlg->_frame;

	pause_media pause(vs);

	std::string scope;
	std::string text;

	auto default_texts = vs.item_index.auto_complete_text(prop::tag);
	auto edit_control = std::make_shared<ui::edit_control>(dlg_parent, tt.value, text, default_texts);

	auto create_commands = [&vs, edit_control](const std::shared_ptr<select_control>& sel)
	{
		std::vector<command_ptr> commands;

		for (const auto& s : prop::search_scopes())
		{
			auto c = std::make_shared<command>();
			c->text = s.scope;
			c->invoke = [&vs, sel, s, edit_control]
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
		if (str::is_empty(str::trim(scope)) || str::icmp(str::trim(scope), "any") == 0)
		{
			result = text;
		}
		else
		{
			result = std::format("{}:{}", scope, str::quote_if_white_space(text));
		}

		return true;
	}

	return false;
}


// UI-thread state behind the advanced search location column. Marker sets are built off
// the UI thread and published here, so the generation counter drops results that arrive
// after the map has moved on and the map is only reached through a weak pointer that is
// locked back on the UI thread.
struct advanced_search_location_state
{
	enum class marker_place_state : uint8_t
	{
		unknown,
		pending,
		resolved
	};

	std::weak_ptr<map_control> map;
	std::vector<df::file_path> marker_paths;
	std::vector<marker_place_state> marker_place_states;
	std::vector<uint32_t> marker_place_view_generations;
	std::vector<std::string> marker_place_names;
	uint32_t generation = 0;

	// The map is framed once, on the first thing worth looking at. Re-framing later would
	// undo the pan and zoom the user performed to reach the hot spot they were aiming for.
	bool framed = false;

	// Names arrive from the gazetteer after the pick, so a later pick must win.
	uint32_t pick_generation = 0;
	std::function<void(std::string name, gps_coordinate centre, double radius_km)> apply_place;

	df::file_path hover_path;
	df::item_element_ptr hover_item;
	df::unique_paths thumbnail_requests;

	// Bounded number of bubble rebuilds spent waiting for a hover thumbnail, so a marker
	// whose thumbnail never decodes does not rebuild its bubble forever.
	int hover_retries = 0;
};

static void advanced_search_invoke(view_state& state, const ui::control_frame_ptr& parent,
                                   const view_host_base_ptr& view)
{
	auto dlg = make_dlg(parent);
	auto dlg_parent = dlg->_frame;

	const auto search = state.search();

	static std::string selected_folder;
	static std::string all_terms;
	static std::string none_terms;

	static bool search_collection = true;
	static bool search_folder = false;
	static bool search_sub_folders = true;
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

	static location_and_distance_t location{{}, location_default_search_km};
	static std::string location_name;
	static gps_coordinate selected_location;

	if (!search.selectors().empty())
	{
		// The dialog opens on the scope the user is already looking at. Filling the folder in
		// while leaving "the collection" selected would show a path that plays no part in the
		// search.
		selected_folder = search.selectors().front().folder().text();
		search_folder = true;
		search_collection = false;
	}

	// An empty date picker cannot be read or compared, so a range starts on today and the
	// user narrows from there.
	if (!from_val.is_valid()) from_val = platform::now();
	if (!until_val.is_valid()) until_val = platform::now();

	auto search_collection_radio = std::make_shared<ui::check_control>(dlg->_frame, tt.search_collection,
	                                                                   search_collection, true);

	auto search_folder_ratio = std::make_shared<ui::check_control>(dlg->_frame, tt.search_folder, search_folder, true);
	auto webp_group = std::make_shared<ui::group_control>();
	webp_group->add(std::make_shared<ui::folder_picker_control>(dlg_parent, selected_folder));
	webp_group->add(std::make_shared<ui::check_control>(dlg_parent, tt.search_sub_folders, search_sub_folders));
	search_folder_ratio->child(webp_group);

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

	//
	// Location. The map fills the right of the dialog and is always live, showing the
	// collection's photo hot spots. The user pans and zooms until a hot spot they want is
	// visible and clicks it; the line under "Located within" says what that pick means.
	//

	const auto ls = std::make_shared<advanced_search_location_state>();

	const auto location_check = std::make_shared<ui::check_control>(dlg_parent, tt.search_located_within,
	                                                                search_location);

	const auto describe_location = []()
	{
		if (!location.position.is_valid()) return std::string{};

		const auto where = location_name.empty()
			                   ? std::format("{:.5f}, {:.5f}", location.position.latitude(),
			                                 location.position.longitude())
			                   : location_name;

		return str_format(tt.search_within_fmt.sv(), format_distance_km(location.km), where);
	};

	const auto location_summary = std::make_shared<text_element>(describe_location());

	auto map = std::make_shared<map_control>(state._async, std::function<void(gps_coordinate)>{});
	map->init(dlg->_frame);
	map->set_show_crosshair(false);
	ls->map = map;

	// Seed the map so it is never blank, and mark the remembered pick if there is one.
	map->set_location_marker(location.position.is_valid() ? location.position : setting.default_location);

	if (selected_location.is_valid() || location.position.is_valid())
	{
		map->set_selected(selected_location.is_valid() ? selected_location : location.position, 0);
	}

	if (location.position.is_valid())
	{
		// A remembered pick is what the dialog is about, so the map opens showing that area
		// and the distance it covers rather than the whole collection.
		const auto lat_span = location.km / 111.0;
		const auto lon_span = location.km / (111.0 * std::max(
			0.05, std::cos(gps_coordinate::deg2rad(location.position.latitude()))));

		map_box box;
		box.add(gps_coordinate(std::max(-85.0, location.position.latitude() - lat_span),
		                       std::max(-180.0, location.position.longitude() - lon_span)));
		box.add(gps_coordinate(std::min(85.0, location.position.latitude() + lat_span),
		                       std::min(180.0, location.position.longitude() + lon_span)));
		map->frame_on(box);
		ls->framed = true;
	}

	// Applies whatever the pick resolved to, and is the only writer of the location statics.
	ls->apply_place = [location_check, location_summary, describe_location, dlg, ls,
			search_collection_radio, search_folder_ratio](
		const std::string& name, const gps_coordinate centre, const double radius_km)
		{
			df::assert_true(ui::is_ui_thread());

			location_name = name;
			location.position = centre;
			location.km = location_distance_at_detent(location_distance_detent_at_least(radius_km));
			location_summary->text(describe_location());
			location_check->checked(true);

			// The hot spots are the whole collection's, so picking one under a folder scope would
			// promise items the search then refuses to look for.
			search_folder_ratio->checked(false);
			search_collection_radio->checked(true);

			dlg->_frame->invalidate();
			dlg->layout();
		};

	// Clicking a hot spot is the whole gesture: it names the area and turns the option on.
	// The coordinate answers straight away; the gazetteer then upgrades it to a place name so
	// the search reads as `loc:Tokyo, 100km` rather than a pair of numbers.
	map->marker_picked = [&state, ls](const gps_coordinate coord, const double radius_km, int)
	{
		df::assert_true(ui::is_ui_thread());

		selected_location = coord;
		ls->apply_place({}, coord, radius_km);

		const auto generation = ++ls->pick_generation;
		const std::weak_ptr<advanced_search_location_state> weak_ls = ls;

		state.queue_location([&state, weak_ls, generation, coord, radius_km](const location_cache& locations)
		{
			const auto found = locations.find_largest_attributed(coord);
			auto name = found.position.is_valid() ? qualified_name(found) : std::string{};
			auto centre = found.position;

			// The place centre is not the bubble, so the radius has to cover the offset too. A
			// name that would widen the search by more than one detent is not worth the trade.
			auto km = radius_km;

			if (!name.empty())
			{
				km = centre.distance_in_kilometers(coord) + radius_km;

				constexpr auto max_km = location_distance_at_detent(location_distance_detent_count - 1);

				if (location_distance_detent_at_least(km) > location_distance_detent_at_least(radius_km) + 1)
				{
					name.clear();
					centre = coord;
					km = radius_km;
				}
				else
				{
					km = std::min(km, max_km);
				}
			}
			else
			{
				centre = coord;
			}

			state.queue_ui([weak_ls, generation, name = std::move(name), centre, km]
			{
				const auto ls = weak_ls.lock();
				if (!ls || ls->pick_generation != generation || !ls->apply_place) return;
				ls->apply_place(name, centre, km);
			});
		});
	};

	// Photo hot spots. build_location_matrix reads the index, so it runs on the query
	// queue and is published back to the map on the UI thread.
	auto rebuild_markers = [&state, ls](const int zoom)
	{
		const auto generation = ++ls->generation;
		const std::weak_ptr<advanced_search_location_state> weak_ls = ls;

		state.queue_async(async_queue::query, [&state, weak_ls, generation, zoom]
		{
			location_matrix_params params;
			params.zoom = zoom;
			auto matrix = state.item_index.build_location_matrix(params);

			std::vector<map_engine::marker> markers;
			std::vector<df::file_path> paths;
			markers.reserve(matrix.cells.size());
			paths.reserve(matrix.cells.size());

			// Where the collection actually holds photos, so the map can open on something
			// clickable instead of the default coordinate at street level.
			map_box box;

			for (auto& cell : matrix.cells)
			{
				markers.push_back({cell.centroid, cell.count});
				paths.emplace_back(cell.representative_path);
				box.add(gps_coordinate(cell.min_latitude, cell.min_longitude));
				box.add(gps_coordinate(cell.max_latitude, cell.max_longitude));
			}

			state._async.queue_ui(
				[weak_ls, generation, box, markers = std::move(markers), paths = std::move(paths)]() mutable
				{
					const auto s = weak_ls.lock();
					if (!s || s->generation != generation) return;

					const auto m = s->map.lock();
					if (!m) return;

					s->marker_paths = std::move(paths);
					s->marker_place_states.assign(s->marker_paths.size(),
					                              advanced_search_location_state::marker_place_state::unknown);
					s->marker_place_view_generations.assign(s->marker_paths.size(), 0);
					s->marker_place_names.assign(s->marker_paths.size(), {});
					s->hover_path = {};
					s->hover_item.reset();
					m->set_markers(markers);

					if (!s->framed && box.valid)
					{
						// Framing changes the zoom, which asks for a rebuild at that zoom; the
						// generation check drops this now-stale set when that answer arrives.
						s->framed = true;
						m->frame_on(box);
					}
				});
		});
	};

	map->zoom_changed = [rebuild_markers](const int zoom) { rebuild_markers(zoom); };

	map->marker_hover = [&state, ls](view_hover_element& hover, const int marker_index, const int count,
	                                 const pointi anchor, bool& needs_refresh)
	{
		if (marker_index < 0 || marker_index >= static_cast<int>(ls->marker_paths.size())) return;

		const auto map = ls->map.lock();
		if (!map) return;

		const auto view_generation = map->view_generation();

		auto& place_state = ls->marker_place_states[marker_index];
		if (ls->marker_place_view_generations[marker_index] != view_generation)
		{
			ls->marker_place_view_generations[marker_index] = view_generation;
			ls->marker_place_names[marker_index].clear();
			place_state = advanced_search_location_state::marker_place_state::unknown;
		}

		if (place_state == advanced_search_location_state::marker_place_state::unknown)
		{
			place_state = advanced_search_location_state::marker_place_state::pending;
			needs_refresh = true;

			const auto generation = ls->generation;
			const auto marker_coordinate = map->gps_at_screen(anchor);
			const auto visible_coordinates =
				std::make_shared<const std::vector<gps_coordinate>>(map->visible_cluster_coordinates());
			const std::weak_ptr<advanced_search_location_state> weak_ls = ls;

			state.queue_location([&async = state._async, weak_ls, generation, view_generation, marker_index,
					marker_coordinate, visible_coordinates](
				const location_cache& locations)
				{
					std::string unique_name;
					const auto found = locations.find_largest_attributed(marker_coordinate);

					if (found.id != 0)
					{
						const auto radius_km = location_attribution_radius_km(found.population);
						auto unique = true;

						for (const auto coordinate : *visible_coordinates)
						{
							if (!unique) break;
							if (coordinate == marker_coordinate ||
								coordinate.distance_in_kilometers(found.position) > radius_km)
								continue;

							unique = locations.find_largest_attributed(coordinate).id != found.id;
						}

						if (unique) unique_name = qualified_name(found);
					}

					async.queue_ui([weak_ls, generation, view_generation, marker_index,
						unique_name = std::move(unique_name)]
					{
						const auto s = weak_ls.lock();
						if (!s || s->generation != generation ||
							marker_index >= static_cast<int>(s->marker_place_states.size()))
							return;

						const auto map = s->map.lock();
						if (!map || map->view_generation() != view_generation) return;

						s->marker_place_states[marker_index] =
							advanced_search_location_state::marker_place_state::resolved;
						s->marker_place_names[marker_index] = unique_name;
						if (const auto m = s->map.lock()) m->hover_needs_refresh = true;
					});
				});
		}
		else if (place_state == advanced_search_location_state::marker_place_state::pending)
		{
			needs_refresh = true;
		}

		const auto path = ls->marker_paths[marker_index];

		if (ls->hover_path != path)
		{
			ls->hover_path = path;
			ls->hover_item.reset();
			ls->hover_retries = 20;
		}

		if (!ls->hover_item)
		{
			const auto indexed = state.item_index.find_item(path);
			if (indexed.ft) ls->hover_item = std::make_shared<df::item_element>(path, indexed);
		}

		const auto item = ls->hover_item;
		if (!item) return;

		const auto elements = std::make_shared<view_elements>();
		const auto thumb = item->thumbnail();

		if (is_valid(thumb))
		{
			files ff;
			elements->add(std::make_shared<surface_element>(ff.image_to_surface(thumb), 160, flex_item::center,
			                                                item->layout_orientation()));
		}
		else if (ls->hover_retries > 0)
		{
			// The thumbnail decodes asynchronously; ask once and rebuild the bubble on the
			// next tick so the preview appears without the user moving the mouse.
			ls->hover_retries -= 1;
			needs_refresh = true;

			if (ls->thumbnail_requests.emplace(item->path()).second)
			{
				state.item_index.queue_load_thumbnail(item);
			}
		}

		const auto& place_name = ls->marker_place_names[marker_index];
		const auto caption = count <= 1
			                     ? std::string(item->name().sv())
			                     : place_name.empty()
			                     ? format_plural_text(tt.map_items_here_fmt, count)
			                     : str_format(tt.map_items_close_to_fmt.sv(),
			                                  platform::format_number(str::to_string(count)), place_name);

		elements->add(std::make_shared<text_element>(caption, flex_item::center | flex_item::new_line));

		hover.elements = elements;
		hover.window_bounds = recti(anchor.x - 8, anchor.y - 8, anchor.x + 8, anchor.y + 8);
		hover.active_bounds = recti(anchor.x - 12, anchor.y - 12, anchor.x + 12, anchor.y + 12);
		hover.preferred_size = 180;
		hover.horizontal = false;
	};

	const auto criteria_col = std::make_shared<ui::group_control>();
	criteria_col->add(set_margin(search_collection_radio));
	criteria_col->add(set_margin(search_folder_ratio));
	criteria_col->add(std::make_shared<divider_element>());
	criteria_col->add(set_margin(std::make_shared<ui::term_picker_control>(state, dlg_parent, tt.search_all_terms,
	                                                                       all_terms)));
	criteria_col->add(set_margin(std::make_shared<ui::term_picker_control>(state, dlg_parent, tt.search_none_terms,
	                                                                       none_terms)));
	criteria_col->add(std::make_shared<divider_element>());
	// "Created" and "Modified" mean nothing on their own, so the block says what the boxes
	// are about before the user reads them.
	criteria_col->add(set_margin(std::make_shared<text_element>(tt.dates_title, ui::style::font_face::title)));
	criteria_col->add(set_margin(date_type_control));
	criteria_col->add(set_margin(from_check));
	criteria_col->add(set_margin(until_check));
	criteria_col->add(std::make_shared<divider_element>());
	criteria_col->add(set_margin(std::make_shared<text_element>(tt.media_metadata_title, ui::style::font_face::title)));
	criteria_col->add(set_margin(file_type_control));
	criteria_col->add(std::make_shared<divider_element>());
	criteria_col->add(set_margin(location_check));
	criteria_col->add(set_margin(location_summary));

	const auto cols = std::make_shared<ui::col_control>();
	cols->add(criteria_col);
	cols->add(set_margin(map), {66});

	std::vector<view_element_ptr> controls = {
		set_margin(std::make_shared<ui::title_control>(icon_index::search, tt.command_advanced_search)),
		std::make_shared<divider_element>(),
		cols,
		std::make_shared<divider_element>(),
		std::make_shared<ui::ok_cancel_control>(dlg->_frame)
	};

	// The first pass only has to say where the collection is, so it runs at the coarsest zoom
	// the map can show. Framing on the answer then asks for an accurate rebuild at the zoom
	// the user actually ends up looking at.
	rebuild_markers(map_engine::min_zoom);

	pause_media pause(state);

	const auto result = dlg->show_modal(controls, {122});

	// Stop late marker results from reaching a map that is about to be destroyed, and drop
	// the callbacks that reference the dialog.
	ls->generation += 1;
	ls->pick_generation += 1;
	ls->apply_place = {};
	ls->map.reset();
	map->marker_hover = {};
	map->zoom_changed = {};
	map->marker_picked = {};

	if (result == ui::close_result::ok)
	{
		df::search_t new_search;

		if (search_folder && !str::is_empty(selected_folder))
		{
			new_search.add_selector(df::item_selector(df::folder_path(selected_folder), search_sub_folders));
		}

		if (search_location && location.position.is_valid())
		{
			if (location_name.empty())
			{
				new_search.with(df::search_term(df::search_term_type::location, location.position, location.km,
				                                df::search_term_modifier{}));
			}
			else
			{
				// A named centre keeps the search readable and retypable in the address box.
				auto term = df::search_term(df::search_term_type::location, location_name,
				                            df::search_term_modifier{});
				term.float_val = location.km;
				new_search.with(term);
			}
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

		for (const auto& part : df::coalesce_parts(t.parse(all_terms)))
		{
			new_search.parse_part(part);
		}

		for (auto part : df::coalesce_parts(t.parse(none_terms)))
		{
			part.modifier.positive = false;
			new_search.parse_part(part);
		}

		if (new_search.is_empty())
		{
			// Nothing was chosen, so there is no search to run. Closing on OK without changing
			// what is on screen is indistinguishable from Cancel, so say why.
			dlg->show_message(icon_index::search, tt.command_advanced_search, tt.search_no_criteria);
		}
		else
		{
			state.open(view, new_search, {});
		}
	}
}

#ifndef WINSTORE
static void show_update_dialog(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto title = tt.update_title;

	auto dlg = make_dlg(parent);
	std::vector<view_element_ptr> controls;

	pause_media pause(s);

	// Phase 1: always check online for a newer version, showing a busy indicator.
	controls.emplace_back(
		set_margin(std::make_shared<ui::title_control>(icon_index::lightbulb, title)));
	controls.emplace_back(std::make_shared<ui::busy_control>(dlg->_frame, icon_index::lightbulb, tt.update_checking));
	controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame, true, tt.button_close));

	auto found_version = std::make_shared<std::string>();
	auto check_failed = std::make_shared<bool>(false);

	s.queue_async(async_queue::web, [&s, dlg, found_version, check_failed]
	{
		platform::web_request req;
		req.path = "/ver";
		req.query = platform::web_params{
			{"v"s, std::string(s_app_version)},
			{"b"s, std::string(g_app_build)},
			{"os"s, platform::OS()},
		};

		const auto con = platform::connect_to_host("diffractor.com");
		const auto response = platform::send_request(con, req);

		std::string version;

		if (response.status_code == 200)
		{
			df::util::json::json_doc json;
			json.Parse(response.body);
			version = df::util::json::safe_string(json, "current_version");
		}

		const auto failed = version.empty();

		s.queue_ui([dlg, found_version, check_failed, version, failed]
		{
			*found_version = version;
			*check_failed = failed;
			dlg->close(false);
		});
	});

	if (dlg->show_modal(controls) != ui::close_result::ok)
	{
		return;
	}

	if (*check_failed)
	{
		// Without an answer from the server the stored version says nothing about what is
		// available, so reporting "you are using the latest version" would be a guess.
		dlg->show_message(icon_index::error, title, tt.update_check_failed);
		return;
	}

	if (!found_version->empty())
	{
		setting.available_version = *found_version;
		s.invalidate_view(view_invalid::view_layout | view_invalid::app_layout);
	}

	// Phase 2. Being up to date is news, not a decision, so it is reported as a message rather
	// than as a list of choices; only the upgrade path offers buttons.
	if (df::version(s_app_version) >= df::version(setting.available_version))
	{
		dlg->show_message(icon_index::lightbulb, title, str_format(tt.update_up_to_date_fmt.sv(), s_app_version));
		return;
	}

	controls.clear();
	controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::lightbulb, title)));
	controls.emplace_back(set_margin(std::make_shared<text_element>(
		str_format(tt.update_help_fmt.sv(), setting.available_version, s_app_version))));

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::import, tt.update_install_now,
	                                                           tt.update_help, [f = dlg->_frame] { f->close(); }));

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::time, tt.update_not_now,
	                                                           tt.update_not_now_help, [&s, f = dlg->_frame]
	                                                           {
		                                                           setting.min_show_update_day = platform::now().
			                                                           to_days() + 7;
		                                                           s.invalidate_view(
			                                                           view_invalid::view_layout |
			                                                           view_invalid::app_layout);
		                                                           f->close(true);
	                                                           }));

	controls.emplace_back(std::make_shared<ui::button_control>(dlg->_frame, icon_index::question, tt.update_more_info,
	                                                           tt.update_more_info_help, [f = dlg->_frame]
	                                                           {
		                                                           platform::open(
			                                                           "https://www.diffractor.com/blog");
		                                                           f->close(true);
	                                                           }));

	if (dlg->show_modal(controls) == ui::close_result::ok)
	{
		controls.clear();
		controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::import, title)));
		controls.emplace_back(
			std::make_shared<ui::busy_control>(dlg->_frame, icon_index::lightbulb, tt.update_please_wait));
		controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame, true, tt.button_close));

		struct download_state
		{
			std::atomic_bool active = true;
			// Set on the UI thread when the download reported a result, so a dialog that closed itself
			// can be told apart from one the user dismissed.
			bool completed = false;
			df::file_path path;
		};

		auto state = std::make_shared<download_state>();
		auto download_complete = [&s, dlg, state](const df::file_path download_path)
		{
			s.queue_ui([dlg, state, download_path]
			{
				if (state->active)
				{
					state->completed = true;
					state->path = download_path;
					dlg->close(state->path.is_empty());
				}
			});
		};

		s.queue_async(async_queue::web, [download_complete]
		{
			platform::download_and_verify(download_complete);
		});

		if (dlg->show_modal(controls) == ui::close_result::ok)
		{
			state->active = false;

			// Launch the downloaded installer. It is interactive and will close any
			// running instance of Diffractor before installing over the current folder.
			const auto module_folder = known_path(platform::known_folder::running_app_folder);
			const auto install_result = platform::install(state->path, module_folder, false, false);

			if (install_result.failed())
			{
				dlg->show_message(icon_index::error, s_app_name, install_result.format_error(tt.update_failed));
			}
		}
		else
		{
			state->active = false;

			if (state->completed)
			{
				// The download answered but produced nothing. Without this the dialog just vanishes
				// and the user is left believing the update installed.
				dlg->show_message(icon_index::error, title, tt.update_failed);
			}
		}
	}
}

#endif


#ifdef _DEBUG
void app_frame::run_test_action(const std::string_view action)
{
	if (action == "reset-graphics")
	{
		_state._events.free_graphics_resources(false, false);
		_app_frame->reset_graphics();
	}
	else if (action == "crash")
	{
		int* value = nullptr;
		*value = 19;
	}
	else if (action == "send-crash-report")
	{
		crash(known_path(platform::known_folder::test_files_folder).combine_file("Test.jpg"));
		_app_frame->show(true);
	}
	else if (action == "new-version")
	{
		setting.force_available_version = true;
		setting.min_show_update_day = 0;
		_state.invalidate_view(view_invalid::view_layout | view_invalid::app_layout);
	}
}
#endif

struct keyboard_ref_row
{
	std::string keys;
	std::string description;
};

struct keyboard_ref_section
{
	std::string title;
	std::vector<keyboard_ref_row> rows;
};

// The dialog and the clipboard text are both rendered from this one collection so that they
// cannot drift apart.
static void add_keyboard_section(std::vector<keyboard_ref_section>& sections, const commands_map& commands,
                                 const command_group group, const std::string_view title)
{
	std::vector<command_info_ptr> items;

	for (const auto& c : commands)
	{
		if (c.second->group == group && !c.second->kba.empty())
		{
			items.emplace_back(c.second);
		}
	}

	// A group with no accelerators would otherwise render as a heading over nothing.
	if (items.empty()) return;

	std::ranges::sort(items, [](auto&& left, auto&& right)
	{
		return str::icmp(left->text, right->text) < 0;
	});

	keyboard_ref_section section;
	section.title = title;
	section.rows.reserve(items.size());

	for (const auto& c : items)
	{
		section.rows.emplace_back(c->keyboard_accelerator_text, c->text);
	}

	sections.emplace_back(std::move(section));
}

static bool is_not_virt_key(const int key)
{
	return (key >= '0' && key <= '9') ||
		(key >= 'A' && key <= 'Z');
}

std::string format_keyboard_accelerator(const std::vector<keyboard_accelerator_t>& keyboard_accelerators)
{
	constexpr auto control = keyboard_accelerator_t::control;
	constexpr auto shift = keyboard_accelerator_t::shift;
	constexpr auto alt = keyboard_accelerator_t::alt;

	std::string result;

	for (const auto& ac : keyboard_accelerators)
	{
		// Add Accelerator
		if (!result.empty())
		{
			result += std::format(" {} ", tt.keyboard_or);
		}

		if (ac.key_state & alt)
		{
			result += std::format("{}+", tt.keyboard_alt);
		}
		if (ac.key_state & control)
		{
			result += std::format("{}+", tt.keyboard_control);
		}
		if (ac.key_state & shift)
		{
			result += std::format("{}+", tt.keyboard_shift);
		}

		if (ac.key == keys::RETURN)
		{
			result += tt.keyboard_enter;
		}
		else if (ac.key == keys::BACK)
		{
			result += tt.keyboard_backspace;
		}
		else if (ac.key == keys::OEM_PLUS || ac.key == keys::OEM_MINUS)
		{
			// Quoted so the key is not read as the modifier separator.
			result += std::format("'{}'", keys::format(ac.key));
		}
		else if (is_not_virt_key(ac.key))
		{
			const char szTemp[2] = {static_cast<char>(ac.key), 0};
			result += szTemp;
		}
		else
		{
			result += keys::format(ac.key);
		}
	}

	return result;
}

// The keyboard reference is read top to bottom, so the sections follow the order a user meets
// them rather than the column packing the old layout needed.
static std::vector<keyboard_ref_section> build_keyboard_reference(const commands_map& commands)
{
	std::vector<keyboard_ref_section> sections;

	keyboard_ref_section basics;
	basics.title = tt.keyboard_basics_title;
	basics.rows.emplace_back(format_keyboard_accelerator({keyboard_accelerator_t{keys::RETURN}}),
	                         std::string(tt.keyboard_enter_desc.sv()));
	basics.rows.emplace_back(format_keyboard_accelerator({keyboard_accelerator_t{keys::SPACE}}),
	                         std::string(tt.keyboard_space_desc.sv()));
	basics.rows.emplace_back(format_keyboard_accelerator({keyboard_accelerator_t{keys::ESCAPE}}),
	                         std::string(tt.keyboard_escape_desc.sv()));
	basics.rows.emplace_back(
		format_keyboard_accelerator({keyboard_accelerator_t{keys::LEFT}, keyboard_accelerator_t{keys::RIGHT}}),
		std::string(tt.keyboard_left_right_desc.sv()));
	basics.rows.emplace_back(
		format_keyboard_accelerator({keyboard_accelerator_t{keys::UP}, keyboard_accelerator_t{keys::DOWN}}),
		std::string(tt.keyboard_up_down_desc.sv()));
	basics.rows.emplace_back(
		format_keyboard_accelerator({keyboard_accelerator_t{keys::HOME}, keyboard_accelerator_t{keys::END}}),
		std::string(tt.keyboard_home_end_desc.sv()));
	basics.rows.emplace_back(
		format_keyboard_accelerator({keyboard_accelerator_t{keys::OEM_PLUS}, keyboard_accelerator_t{keys::OEM_MINUS}}),
		std::string(tt.keyboard_zoom_keys_desc.sv()));
	basics.rows.emplace_back(format_keyboard_accelerator({
		                         keyboard_accelerator_t{keys::LEFT, keyboard_accelerator_t::control},
		                         keyboard_accelerator_t{keys::RIGHT, keyboard_accelerator_t::control}
	                         }), std::string(tt.keyboard_ctrl_left_right_desc.sv()));
	sections.emplace_back(std::move(basics));

	const auto& c = commands;

	add_keyboard_section(sections, c, command_group::navigation, tt.keyboard_navigation_title);
	add_keyboard_section(sections, c, command_group::selection, tt.keyboard_selection_title);
	add_keyboard_section(sections, c, command_group::open, tt.keyboard_open_title);
	add_keyboard_section(sections, c, command_group::file_management, tt.keyboard_file_management_title);
	add_keyboard_section(sections, c, command_group::edit_item, tt.keyboard_edit_title);
	add_keyboard_section(sections, c, command_group::media_playback, tt.keyboard_playback_title);
	add_keyboard_section(sections, c, command_group::rate_flag, tt.keyboard_rate_label_title);
	add_keyboard_section(sections, c, command_group::group_by, tt.keyboard_group_title);
	add_keyboard_section(sections, c, command_group::sort_by, tt.command_view_sort);
	add_keyboard_section(sections, c, command_group::tools, tt.keyboard_tools_title);
	add_keyboard_section(sections, c, command_group::options, tt.options_title);
	add_keyboard_section(sections, c, command_group::help, tt.keyboard_help_title);

	return sections;
}

// Tabs between the key and its description so the reference pastes into a document or a
// spreadsheet with the same two columns the dialog shows.
static std::string format_keyboard_reference(const std::vector<keyboard_ref_section>& sections)
{
	std::string result;
	result += tt.keyboard_ref_title.sv();
	result += "\r\n";

	for (const auto& section : sections)
	{
		result += "\r\n";
		result += section.title;
		result += "\r\n";

		for (const auto& row : section.rows)
		{
			result += "\t";
			result += row.keys;
			result += "\t";
			result += row.description;
			result += "\r\n";
		}
	}

	return result;
}

// The dialog stacks its controls in a column, so copy and close need one element to share a row.
class keyboard_ref_buttons final : public view_element, public std::enable_shared_from_this<keyboard_ref_buttons>
{
	ui::button_ptr _copy;
	ui::button_ptr _close;

	mutable int _copy_width = 100;
	mutable int _close_width = 100;

public:
	keyboard_ref_buttons(const ui::control_frame_ptr& h, std::function<void()> copy)
	{
		_copy = h->create_button(tt.copy_to_clipboard, std::move(copy));
		_close = h->create_button(tt.button_close, [h] { h->close(false); }, true);
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_copy);
		handler(_close);
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		const auto copy_extent = _copy->measure(cx);
		const auto close_extent = _close->measure(cx);

		_copy_width = std::max(cx / 5, copy_extent.cx + mc.padding2);
		_close_width = std::max(cx / 5, close_extent.cx + mc.padding2);

		return {cx, std::max(copy_extent.cy, close_extent.cy) + mc.padding2};
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;

		const auto total_button_width = _copy_width + _close_width + mc.padding2;
		const auto button_rect = center_rect(sizei{total_button_width, bounds.height()}, bounds);

		auto rcopy = button_rect;
		auto rclose = button_rect;

		rcopy.right = rcopy.left + _copy_width;
		rclose.left = rclose.right - _close_width;

		positions.emplace_back(_copy, rcopy, is_visible());
		positions.emplace_back(_close, rclose, is_visible());
	}
};

static void show_keyboard_reference(view_state& s, const ui::control_frame_ptr& parent, const commands_map& commands)
{
	const auto dlg = make_dlg(parent);
	const auto sections = build_keyboard_reference(commands);

	std::vector<view_element_ptr> controls;
	controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::keyboard, tt.keyboard_ref_title)));
	controls.emplace_back(set_margin(std::make_shared<divider_element>()));

	for (const auto& section : sections)
	{
		// Shaded heading over indented rows, matching the grouping used for verbose metadata.
		auto heading = std::make_shared<group_title_control>(section.title);
		heading->padding(8);
		heading->margin(4, 8);
		heading->set_style_bit(view_element_style::background, true);
		controls.emplace_back(std::move(heading));

		const auto table = std::make_shared<ui::table_element>(flex_item::grow);
		table->no_shrink_col[0] = true;

		for (const auto& row : section.rows)
		{
			table->add(icon_index::bullet, row.keys, row.description);
		}

		controls.emplace_back(set_margin(table, 16, 4));
	}

	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<keyboard_ref_buttons>(dlg->_frame, [&sections]
	{
		platform::set_clipboard(format_keyboard_reference(sections));
	}));

	pause_media pause(s);
	// Title above and actions below stay put; only the key list scrolls.
	dlg->_pinned_header = 2;
	dlg->_pinned_footer = 2;
	dlg->show_modal(controls, {88}, {88});
}

void send_info(const view_state& s, const ui::control_frame_ptr& parent)
{
	const auto title = tt.support;
	const auto dlg = make_dlg(parent);

	// Gathered here because it reads app state, then handed to the worker as a detached
	// value. Everything after this - copying and zipping the logs, and the upload itself -
	// runs off the UI thread.
	std::ostringstream message;

	for (const auto& i : calc_app_info(s.item_index, true))
	{
		message << i.first << " " << i.second << '\n';
	}

	const auto results = std::make_shared<command_status>(s._async, dlg, icon_index::support, title, 1);

	s.queue_async(async_queue::web, [results, info = message.str()]
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
			if (log_file_copy.exists()) zip.add(log_file_copy, "diffractor.log");
			if (previous_log_path.exists()) zip.add(previous_log_path);
			zip.close();
		}

		platform::web_request req;
		req.verb = platform::web_request_verb::POST;
		req.path = "/crash";
		req.form_data.emplace_back("message", info);
		req.form_data.emplace_back("version", platform::OS());
		req.form_data.emplace_back("diffractor", s_app_version);
		req.form_data.emplace_back("build", g_app_build);
		req.form_data.emplace_back("subject", "Diffractor LOG");
		req.form_data.emplace_back("submit", "Send Report");
		req.file_form_data_name = "ff";
		req.file_name = "logs.zip";
		req.file_path = crash_zip_path;

		const auto con = platform::connect_to_host("diffractor.com");
		const auto response = send_request(con, req);
		const auto sent = response.status_code >= 200 && response.status_code < 300;

		if (log_file_copy.exists()) platform::delete_file(log_file_copy);
		if (crash_zip_path.exists()) platform::delete_file(crash_zip_path);

		// A send that failed is reported. Silence used to be indistinguishable from success.
		if (sent)
		{
			results->complete(tt.diagnostics_sent);
		}
		else
		{
			results->abort(tt.diagnostics_send_failed);
		}
	});

	results->wait_for_complete();
}

static void about_invoke(view_state& s, const ui::control_frame_ptr& parent, commands_map& commands)
{
	const auto dlg = make_dlg(parent);
	auto dlg_parent = dlg->_frame;

	std::vector<view_element_ptr> controls;
	controls.emplace_back(create_app_logo_element(s, ui::style::font_face::mega, false, false, 1.6,
	                                              flex_item::center));
	controls.emplace_back(std::make_shared<text_element>(df::format_version(false), ui::style::font_face::dialog,
	                                                     ui::style::text_style::single_line_center,
	                                                     flex_item::center));
	controls.emplace_back(std::make_shared<divider_element>());

	auto cols = std::make_shared<ui::col_control>();

	const auto learn = std::make_shared<ui::group_control>();
	learn->add(std::make_shared<ui::title_control>(icon_index::question, tt.documentation));
	learn->add(std::make_shared<text_element>(tt.about_info));
	learn->add(std::make_shared<link_element>(tt.learn_more_diffractor_com, [] { platform::open(docs_url); }));
	learn->add(std::make_shared<link_element>(tt.releases, [] { platform::open(releases_url); }));
	learn->add(std::make_shared<ui::title_control>(icon_index::keyboard, tt.keyboard));
	learn->add(std::make_shared<link_element>(tt.list_of_accelerators, [&s, dlg_parent, &c = commands]
	{
		show_keyboard_reference(s, dlg_parent, c);
	}));

	const auto support = std::make_shared<ui::group_control>();
	support->add(std::make_shared<ui::title_control>(icon_index::buy, tt.donate));
	support->add(std::make_shared<text_element>(tt.donate_help));
	support->add(std::make_shared<link_element>(tt.donate_link, [] { platform::open(donate_url); }));
	support->add(std::make_shared<ui::title_control>(icon_index::support, tt.support));
	support->add(std::make_shared<link_element>(tt.help_more_info, [] { platform::open(support_url); }));
	support->add(std::make_shared<link_element>(tt.help_send_info, [&s, dlg_parent] { send_info(s, dlg_parent); }));

	cols->add(set_margin(learn, 8, 8));
	cols->add(set_margin(support, 8, 8));
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame));

	pause_media pause(s);
	dlg->show_modal(controls, {55});
};

static void settings_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto dlg = make_dlg(parent);

	// Edit a copy so Cancel discards and OK commits, like every other dialog.
	settings_t edited = setting;

	std::shared_ptr<text_element> custom_index_locations_label;

	std::vector<view_element_ptr> controls;

	const auto settings = std::make_shared<ui::group_control>();
	const auto settings2 = std::make_shared<ui::group_control>();
	const auto advanced = std::make_shared<ui::group_control>();

	settings->add(std::make_shared<ui::title_control>(tt.options_app_options));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_rotated, edited.show_rotated));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_hidden, edited.show_hidden));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_confirm_del, edited.confirm_deletions));
	settings->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_confirm_rotate, edited.confirm_rotations));
	settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_shadow, edited.show_shadow));
	settings->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_last_played_pos, edited.last_played_pos));

	settings->add(std::make_shared<ui::title_control>(tt.option_slideshow_title));
	settings->add(std::make_shared<text_element>(tt.option_slideshow_delay));
	settings->add(std::make_shared<ui::slider_control>(dlg->_frame, std::string_view{}, edited.slideshow_delay,
	                                                   settings_t::min_slideshow_delay,
	                                                   settings_t::max_slideshow_delay));

	settings2->add(std::make_shared<ui::title_control>(tt.options_save_options));
	settings2->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_backup_copy, edited.create_originals));
	settings2->add(std::make_shared<text_element>(tt.options_jpeg_quality));
	settings2->add(std::make_shared<ui::slider_control>(dlg->_frame, std::string_view{}, edited.jpeg_save_quality, 0,
	                                                    100));
	settings2->add(std::make_shared<text_element>(tt.options_webp_quality));
	settings2->add(std::make_shared<ui::slider_control>(dlg->_frame, std::string_view{}, edited.webp_quality, 1, 100));
	settings2->add(std::make_shared<ui::check_control>(dlg->_frame, tt.lossless_compression, edited.webp_lossless));

#ifndef WINSTORE
	settings2->add(std::make_shared<ui::title_control>(tt.options_updates));
	settings2->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_check_for_update, edited.check_for_updates));
#endif

	advanced->add(std::make_shared<ui::title_control>(tt.options_advanced));
	advanced->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_help_tooltips, edited.show_help_tooltips));
	advanced->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_use_gpu, edited.use_gpu));
	advanced->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_use_gpu_video, edited.use_d3d11va));
	advanced->add(std::make_shared<ui::check_control>(dlg->_frame, tt.options_use_yuv_tex, edited.use_yuv));
#ifndef WINSTORE
	advanced->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_send_crash_reports, edited.send_crash_dumps));
#endif
	advanced->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.options_show_debug_info, edited.show_debug_info));

	auto cols = std::make_shared<ui::col_control>();
	cols->add(set_margin(settings));
	cols->add(set_margin(settings2));
	cols->add(set_margin(advanced));

	controls.emplace_back(set_margin(
		std::make_shared<ui::title_control2>(dlg->_frame, icon_index::settings, tt.command_options,
		                                     std::string_view{})));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame));

	pause_media pause(s);

	if (dlg->show_modal(controls, {111}) == ui::close_result::ok)
	{
		setting = edited;
		s.invalidate_view(view_invalid::options);
	}
}


static std::string format_index_text(const view_state& s)
{
	const auto file_types = s.item_index.file_types();
	const auto total = file_types.total_items();
	const auto num = platform::format_number(str::to_string(total.count));
	const auto database_size = prop::format_size(s.item_index.stats.database_size);
	const auto text = str_format(tt.index_size_fmt.sv(), database_size, num);
	return text;
}

class path_text_element final : public std::enable_shared_from_this<path_text_element>, public view_element
{
	std::string _text;
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
		result.elements->add(make_icon_element(icon_index::data, flex_item::no_break));
		result.elements->add(std::make_shared<text_element>(_text, ui::style::font_face::title,
		                                                    ui::style::text_style::multiline,
		                                                    flex_item::line_break));
		result.active_bounds = result.window_bounds = bounds.offset(element_offset);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

static void index_maintenance(const ui::control_frame_ptr& parent, const view_state& s)
{
	const auto dlg = make_dlg(parent);
	const auto title = tt.index_maintenance_title;
	bool is_reset = false;

	struct database_result
	{
		bool has_errors = false;
		std::string error;
	};

	const auto check_result = std::make_shared<database_result>();
	// The busy window says which operation is running; "Processing..." said nothing.
	dlg->show_status(icon_index::star, title);
	s._async.queue_database([&s, dlg, check_result](const database& db)
	{
		try
		{
			check_result->has_errors = db.has_errors();
		}
		catch (const std::exception& e)
		{
			check_result->error = str::utf8_cast(e.what());
		}

		s.queue_ui([dlg] { dlg->close(false); });
	});
	dlg->wait_for_close();

	if (!check_result->error.empty())
	{
		dlg->show_message(icon_index::error, title, check_result->error);
		return;
	}

	std::vector<view_element_ptr> controls = {
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

	if (check_result->has_errors)
	{
		controls.emplace_back(set_margin(set_padding(
			std::make_shared<text_element>(tt.index_maintenance_reset_recommended, ui::style::text_style::multiline), 8,
			8)));
	}

	if (dlg->show_modal(controls) == ui::close_result::ok)
	{
		dlg->show_status(icon_index::star, is_reset ? tt.resetting : tt.defragmenting);
		const auto maintenance_result = std::make_shared<database_result>();

		s._async.queue_database([&s, dlg, maintenance_result, is_reset](database& db)
		{
			try
			{
				db.maintenance(is_reset);
			}
			catch (const std::exception& e)
			{
				maintenance_result->error = str::utf8_cast(e.what());
			}

			s.queue_ui([dlg] { dlg->close(false); });
		});
		dlg->wait_for_close();

		if (maintenance_result->error.empty())
		{
			// Reset leaves an empty database, so the collection has to be re-read from the files.
			// That runs in the background with the normal indexing progress, not behind this dialog.
			s.invalidate_view(is_reset ? view_invalid::index_rebuild : view_invalid::index);
		}
		else
		{
			dlg->show_message(icon_index::error, title, maintenance_result->error);
		}
	}
};


static void index_settings_invoke(view_state& s, const ui::control_frame_ptr& parent,
                                  settings_t::index_t collection_settings)
{
	const auto dlg = make_dlg(parent);

	std::vector<view_element_ptr> controls;
	std::shared_ptr<ui::folder_picker_control> more_custom_index_paths;

	auto dlg_parent = dlg->_frame;
	const auto local_index = std::make_shared<ui::group_control>();
	const auto custom_index = std::make_shared<ui::group_control>();

	auto index_text = format_index_text(s);

	// Maintenance is immediate and irreversible, so it must not run while this dialog can
	// still be cancelled. The link closes the dialog as OK - committing the folder choices
	// the user can see - and maintenance starts once that has happened.
	const auto run_maintenance = std::make_shared<bool>(false);

	local_index->add(std::make_shared<text_element>(tt.collection_info));
	local_index->add(std::make_shared<link_element>(tt.more_collection_options_information,
	                                                [] { platform::open(docs_url); }));

	const auto local_folders = platform::local_folders();

	local_index->add(std::make_shared<ui::title_control>(tt.collection_options_local_folders_title));
	local_index->add(
		std::make_shared<ui::check_control>(dlg_parent, local_folders.pictures.text(), collection_settings.pictures));
	local_index->add(
		std::make_shared<ui::check_control>(dlg_parent, local_folders.video.text(), collection_settings.video));
	local_index->add(
		std::make_shared<ui::check_control>(dlg_parent, local_folders.music.text(), collection_settings.music));

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
	local_index->add(std::make_shared<link_element>(tt.defragment_and_compact, [run_maintenance, dlg_parent]
	{
		*run_maintenance = true;
		dlg_parent->close(false);
	}));

	custom_index->add(std::make_shared<ui::title_control>(tt.collection_options_custom_folders_title));
	custom_index->add(std::make_shared<text_element>(tt.collection_options_more_folders));

	auto more_folders_parts = str::split(collection_settings.more_folders, true,
	                                     [](const char c) { return c == '\n' || c == '\r'; });
	auto more_folders_text = str::combine(more_folders_parts, "\r\n", false);

	custom_index->add(
		more_custom_index_paths = std::make_shared<ui::folder_picker_control>(dlg_parent, more_folders_text, true));
	custom_index->add(std::make_shared<text_element>(tt.collection_options_custom_locations_help));
	custom_index->add(std::make_shared<text_element>(tt.collection_options_custom_folders_help));


	auto cols = std::make_shared<ui::col_control>();
	cols->add(set_margin(local_index));
	cols->add(set_margin(custom_index));

	controls.emplace_back(set_margin(
		std::make_shared<ui::title_control2>(dlg->_frame, icon_index::set, tt.command_collection_options,
		                                     tt.collection_options_info)));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame));

	pause_media pause(s);

	if (ui::close_result::ok == dlg->show_modal(controls, {99}))
	{
		// apply changes
		more_folders_parts = str::split(more_folders_text, false,
		                                [](const char c) { return c == '\n' || c == '\r'; });
		collection_settings.more_folders = str::combine(more_folders_parts, "\n", true);
		setting.collection = collection_settings;
	}

	dlg->_frame->destroy();
	s.invalidate_view(view_invalid::index | view_invalid::options);

	if (*run_maintenance)
	{
		index_maintenance(parent, s);
	}
}

static void customise_invoke(view_state& s, const ui::control_frame_ptr& parent)
{
	const auto dlg = make_dlg(parent);

	// Edit copies so Cancel discards and OK commits, like every other dialog.
	auto search = setting.search;
	auto sidebar_settings = setting.sidebar;

	auto dlg_parent = dlg->_frame;
	const auto searches = std::make_shared<ui::group_control>();
	const auto sidebar = std::make_shared<ui::group_control>();

	searches->add(std::make_shared<ui::title_control>(tt.customise_searches_title));
	searches->add(
		std::make_shared<ui::two_col_table_control>(dlg_parent, search.title, search.path,
		                                            search.count));

	sidebar->add(std::make_shared<ui::title_control>(tt.customise_sidebar_title));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_total, sidebar_settings.show_total_items));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_history, sidebar_settings.show_history));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_world_map,
	                                                 sidebar_settings.show_world_map));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_indexed_folders,
	                                                 sidebar_settings.show_indexed_folders));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_drives, sidebar_settings.show_drives));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_searches,
	                                                 sidebar_settings.show_favorite_searches));
	sidebar->add(
		std::make_shared<ui::check_control>(dlg->_frame, tt.customize_show_tags, sidebar_settings.show_tags));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.option_favorite_tags,
	                                                 sidebar_settings.show_favorite_tags_only));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_ratings,
	                                                 sidebar_settings.show_ratings));
	sidebar->add(std::make_shared<ui::check_control>(dlg->_frame, tt.customize_labels,
	                                                 sidebar_settings.show_labels));
	sidebar->add(set_margin(std::make_shared<text_element>(tt.customize_history_start_year)));
	sidebar->add(std::make_shared<ui::num_control>(dlg->_frame, std::string_view{},
	                                               sidebar_settings.history_start_year, true));

	auto cols = std::make_shared<ui::col_control>();
	cols->add(set_margin(searches));
	cols->add(set_margin(sidebar));

	std::vector<view_element_ptr> controls;
	controls.emplace_back(set_margin(
		std::make_shared<ui::title_control2>(dlg->_frame, icon_index::settings, tt.command_customise,
		                                     tt.customise_sidebar_desc)));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(cols);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame));

	pause_media pause(s);

	if (dlg->show_modal(controls, {111}) == ui::close_result::ok)
	{
		setting.search = search;
		setting.sidebar = sidebar_settings;

		s.invalidate_view(view_invalid::sidebar | view_invalid::options_save | view_invalid::command_state |
			view_invalid::tooltip | view_invalid::app_layout);
	}
};

static void email_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
{
	const auto title = tt.command_share_email;
	constexpr auto icon = icon_index::mail;
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

		// Edit a copy so Cancel discards and OK commits, matching every other options dialog.
		auto email_settings = setting.email;
		std::string validation_error;

		for (;;)
		{
			std::vector<view_element_ptr> controls;
			controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
				dlg->_frame, icon_index::mail, title, format_plural_text(tt.email_info_fmt, items), items.thumbs(),
				items.size())));
			controls.emplace_back(std::make_shared<divider_element>());
			controls.emplace_back(set_margin(std::make_shared<text_element>(tt.email_small_help)));
			controls.emplace_back(std::make_shared<ui::check_control>(dlg->_frame, tt.email_zip, email_settings.zip));
			controls.emplace_back(
				std::make_shared<ui::check_control>(dlg->_frame, tt.email_convert_to_jpeg, email_settings.convert));

			auto limit = std::make_shared<ui::check_control>(dlg->_frame, tt.email_limit_dimensions,
			                                                 email_settings.limit);
			limit->child(std::make_shared<ui::num_control>(dlg->_frame, std::string_view{}, email_settings.max_side));
			controls.emplace_back(limit);

			if (!validation_error.empty())
			{
				auto warning = std::make_shared<text_element>(
					validation_error, flex_item::stretch | view_element_style::important);
				warning->margin = {10, 10};
				warning->padding = {10, 10};
				warning->update_background_color();
				controls.emplace_back(std::move(warning));
			}

			controls.emplace_back(std::make_shared<divider_element>());
			controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_send));

			if (ui::close_result::ok != dlg->show_modal(controls)) return;

			// A limit below one pixel would scale every attachment to nothing. The dialog
			// reopens with the choices intact rather than discarding them.
			if (email_settings.limit && email_settings.max_side < 1)
			{
				validation_error = tt.dimension_must_be_positive;
				continue;
			}

			break;
		}

		setting.email = email_settings;

		{
			const auto zip = email_settings.zip;
			const auto scale = email_settings.limit ? email_settings.max_side : 0;
			const auto convert_to_jpeg = email_settings.convert;
			const auto file_paths = items.file_paths(false);

			record_feature_use(features::email);

			const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, file_paths.size(),
			                                                      tt.email_preparing);

			s.queue_async(async_queue::work, [&s, results, file_paths, zip, scale, convert_to_jpeg]
			{
				files _codecs;
				platform::attachments_t attachments;
				df::file_paths temp_file_paths;
				df::zip_file zip_file;
				df::file_path zip_path;
				bool is_valid = true;
				std::string error_message;

				if (zip)
				{
					zip_path = platform::temp_file("zip");
					temp_file_paths.emplace_back(zip_path);
					is_valid = zip_file.create(zip_path);

					if (!is_valid)
					{
						error_message = std::string(tt.email_failed);
					}
				}

				auto pos = 0;

				for (const auto& path : file_paths)
				{
					if (!is_valid || results->is_canceled()) break;

					auto format = str_format(tt.email_processing_fmt.sv(), path.name());
					results->message(format, pos++, file_paths.size());
					results->start_item(path.name());

					auto file_name = path.name();
					const auto is_jpeg = files::is_jpeg(path.name());
					auto attachment_path = path;
					auto attachment_status = item_status::success;

					if (scale || (convert_to_jpeg && !is_jpeg))
					{
						const auto ft = files::file_type_from_name(path);

						if (ft->has_trait(file_traits::bitmap))
						{
							image_edits edits;
							const auto ext = !is_jpeg && convert_to_jpeg ? ".jpg" : path.extension();
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
							else
							{
								is_valid = false;
								attachment_status = item_status::fail;
								error_message = update_result.format_error();
							}

							temp_file_paths.emplace_back(edited_path);
						}
					}

					if (is_valid && zip)
					{
						is_valid = zip_file.add(attachment_path, file_name);

						if (!is_valid)
						{
							attachment_status = item_status::fail;
							error_message = std::string(tt.email_failed);
						}
					}
					else if (is_valid)
					{
						attachments.emplace_back(file_name, attachment_path);
					}

					results->end_item(path.name(), attachment_status);
				}

				const auto was_canceled = results->is_canceled();
				if (was_canceled) is_valid = false;

				if (zip && is_valid)
				{
					if (zip_file.close())
					{
						attachments.emplace_back("items.zip", zip_path);
					}
					else
					{
						is_valid = false;
						error_message = std::string(tt.email_failed);
					}
				}

				if (is_valid)
				{
					results->message(tt.email_connecting_to_mapi);

					s.queue_ui([&s, attachments, results, temp_file_paths]
					{
						results->message(tt.email_sending);

						// MAPI shows the mail client's own modal UI, so it must run on the UI thread despite
						// blocking it. The second hop exists so the message above paints before that happens.
						s.queue_ui([attachments, results, temp_file_paths]
						{
							const auto send_result = platform::mapi_send({}, {}, {}, attachments);
							if (send_result == platform::mapi_send_result::sent)
							{
								results->complete();
							}
							else if (send_result == platform::mapi_send_result::canceled)
							{
								results->complete(tt.email_canceled);
							}
							else
							{
								results->complete(tt.email_failed);
							}

							for (const auto& path : temp_file_paths)
							{
								platform::delete_file(path);
							}
						});
					});
				}
				else
				{
					if (zip) zip_file.close();

					for (const auto& path : temp_file_paths)
					{
						platform::delete_file(path);
					}

					results->complete(was_canceled
						                  ? std::string_view{}
						                  : error_message.empty()
						                  ? tt.email_failed.sv()
						                  : error_message);
				}
			});

			results->wait_for_complete();
		}
	}
}

void app_frame::initialise_commands()
{
	update_command_text();

	_commands[commands::filter_photos]->text_can_change = true;
	_commands[commands::filter_videos]->text_can_change = true;
	_commands[commands::filter_audio]->text_can_change = true;
	_commands[commands::menu_group_toolbar]->text_can_change = true;
	// These toolbars are shared by several views, so their run buttons are renamed per view.
	// Without this the button keeps the text it was defined with and promises the wrong operation.
	_commands[commands::tool_run]->text_can_change = true;
	_commands[commands::edit_item_save_and_prev]->text_can_change = true;
	_commands[commands::edit_item_save_and_next]->text_can_change = true;

	_commands[commands::play]->icon_can_change = true;
	_commands[commands::view_fullscreen]->icon_can_change = true;
	_commands[commands::repeat_toggle]->icon_can_change = true;
	_commands[commands::playback_volume_toggle]->icon_can_change = true;
	_commands[commands::favorite]->icon_can_change = true;

	_commands[commands::option_highlight_large_items]->clr = ui::style::color::rank_background;
	_commands[commands::rate_rejected]->clr = color_rate_rejected;
	_commands[commands::label_approved]->clr = color_label_approved;
	_commands[commands::label_to_do]->clr = color_label_to_do;
	_commands[commands::label_select]->clr = color_label_select;
	_commands[commands::label_review]->clr = color_label_review;
	_commands[commands::label_second]->clr = color_label_second;

	const auto t = shared_from_this();

	add_command_invoke(commands::tool_adjust_date, [this]
	{
		_view_batch->mode(batch_tool_mode::adjust_date);
		update_toolbar_text(commands::tool_run, std::string(_view_batch->run_text().sv()));
		_state.view_mode(view_type::batch);
	});
	add_command_invoke(commands::tool_edit_metadata, [this]
	{
		_view_batch->mode(batch_tool_mode::metadata);
		reset_selector_selection_anchor();
		update_toolbar_text(commands::tool_run, std::string(_view_batch->run_text().sv()));
		_state.view_mode(view_type::batch);
	});
	add_command_invoke(commands::tool_edit_description, [this]
	{
		setting.set_artist = false;
		setting.set_caption = true;
		setting.set_album = false;
		setting.set_album_artist = false;
		setting.set_genre = false;
		setting.set_tv_show = false;
		setting.set_copyright_notice = false;
		setting.set_copyright_creator = false;
		setting.set_copyright_source = false;
		setting.set_copyright_credit = false;
		setting.set_copyright_url = false;
		setting.caption.clear();
		const auto display = _state.display_state();
		if (_state.selected_items().size() == 1 && display && display->is_one())
		{
			const auto md = display->_item1->metadata();
			if (md && !is_empty(md->description)) setting.caption = md->description.sv();
		}
		_view_batch->mode(batch_tool_mode::metadata);
		reset_selector_selection_anchor();
		update_toolbar_text(commands::tool_run, std::string(_view_batch->run_text().sv()));
		_state.view_mode(view_type::batch);
	});
	add_command_invoke(commands::exit, [this] { _app_frame->close(); });
	add_command_invoke(commands::playback_auto_play, [this] { setting.auto_play = !setting.auto_play; });
	add_command_invoke(commands::playback_auto_advance, [this] { setting.auto_advance = !setting.auto_advance; });
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

	add_command_invoke(commands::view_close, [this] { _view->exit(); });
	add_command_invoke(commands::edit_item_save, [this]
	{
		if (_state.view_mode() == view_type::locate) _view_locate->run();
		else _view_edit->save_current();
	});
	add_command_invoke(commands::edit_item_save_and_prev, [this]
	{
		if (_state.view_mode() == view_type::locate) _view_locate->run_and_next(false);
		else _view_edit->save_and_next(false);
	});
	add_command_invoke(commands::edit_item_save_and_next, [this]
	{
		if (_state.view_mode() == view_type::locate) _view_locate->run_and_next(true);
		else _view_edit->save_and_next(true);
	});
	add_command_invoke(commands::edit_item_save_as, [this] { _view_edit->save_as(); });
	add_command_invoke(commands::edit_item_auto_color, [this] { _view_edit->auto_color(); });
	add_command_invoke(commands::edit_item_auto_document, [this] { _view_edit->auto_document(); });
	add_command_invoke(commands::edit_item_auto_straighten, [this] { _view_edit->auto_straighten(); });
	add_command_invoke(commands::edit_item_preview, [this] { _view_edit->toggle_preview(); });
	add_command_invoke(commands::tool_convert, [this]
	{
		_view_batch->mode(batch_tool_mode::convert);
		update_toolbar_text(commands::tool_run, std::string(_view_batch->run_text().sv()));
		_state.view_mode(view_type::batch);
	});
	add_command_invoke(commands::tool_copy_to_folder,
	                   [this] { copy_move_invoke(_state, _app_frame, _view_frame, false); });
	add_command_invoke(commands::tool_delete, [this] { delete_items(_state.selected_items()); });
	add_command_invoke(commands::tool_desktop_background, [this] { desktop_background_invoke(_state, _app_frame); });
	add_command_invoke(commands::tool_edit, [this] { edit_invoke(_state); });
	add_command_invoke(commands::edit_copy, [this] { cut_copy_invoke(_state, _app_frame, _view_frame, false); });
	add_command_invoke(commands::edit_copy_item_path, [this] { copy_item_path_invoke(_state); });
	add_command_invoke(commands::edit_cut, [this] { cut_copy_invoke(_state, _app_frame, _view_frame, true); });
	add_command_invoke(commands::edit_paste, [this] { edit_paste_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::tool_eject, [this]
	{
		eject_invoke(_state, _app_frame, [this] { invalidate_view(view_invalid::sidebar_drives); });
	});
	add_command_invoke(commands::tool_file_properties,
	                   [this] { file_properties_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::browse_search, [this] { _search_edit->focus(); });
	add_command_invoke(commands::filter_items, [this] { _view_items->focus_rendered_filter(); });
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
	add_command_invoke(commands::options_collection, [this]
	{
		index_settings_invoke(_state, _app_frame, setting.collection);
	});
	add_command_invoke(commands::keyboard, [this] { show_keyboard_reference(_state, _app_frame, _commands); });
	add_command_invoke(commands::tool_locate, [this]
	{
		const auto can_process = _state.can_process_selection_and_mark_errors(
			_view_frame, df::process_items_type::can_save_metadata);
		if (can_process.fail())
		{
			const auto dlg = make_dlg(_app_frame);
			dlg->show_message(icon_index::error, tt.command_locate, can_process.to_string());
		}
		else
		{
			_state.view_mode(view_type::locate);
		}
	});
	add_command_invoke(commands::view_maximize, [this] { _pa->sys_command(ui::sys_command_type::MAXIMIZE); });
	add_command_invoke(commands::view_minimize, [this] { _pa->sys_command(ui::sys_command_type::MINIMIZE); });
	add_command_invoke(commands::tool_move_to_folder,
	                   [this] { copy_move_invoke(_state, _app_frame, _view_frame, true); });
	add_command_invoke(commands::view_show_sidebar, [this]
	{
		setting_invoke(_state, setting.show_sidebar, !setting.show_sidebar);
	});
	add_command_invoke(commands::tool_new_folder, [this] { new_folder_invoke(_state, _app_frame, _view_frame); });
#ifndef WINSTORE
	add_command_invoke(commands::info_new_version, [this] { show_update_dialog(_state, _app_frame); });
	add_command_invoke(commands::info_check_for_updates, [this] { show_update_dialog(_state, _app_frame); });
#endif
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
	add_command_invoke(commands::slideshow, [this] { _state.toggle_slideshow(_view_frame); });
	add_command_invoke(commands::print, [this] { print_invoke(_state, _app_frame, _view_frame); });
	add_command_invoke(commands::rate_none, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 0); });
	add_command_invoke(commands::rate_1, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 1); });
	add_command_invoke(commands::rate_2, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 2); });
	add_command_invoke(commands::rate_3, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 3); });
	add_command_invoke(commands::rate_4, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 4); });
	add_command_invoke(commands::rate_5, [this] { rate_items_invoke(_state, _app_frame, _view_frame, 5); });
	add_command_invoke(commands::rate_rejected, [this]
	{
		// Reject toggles like a flag; the stars keep 0 as their own clear.
		const auto& items = _state.selected_items().items();
		const auto all_rejected = !items.empty() &&
			std::ranges::all_of(items, [](const df::item_element_ptr& i) { return i->rating() == -1; });
		rate_items_invoke(_state, _app_frame, _view_frame, all_rejected ? 0 : -1);
	});
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
	add_command_invoke(commands::favorite_tags, [this] { favorite_tags_invoke(_state, _app_frame); });
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
	add_command_invoke(commands::tool_tag, [this] { _state.view_mode(view_type::tags); });
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
	add_command_invoke(commands::view_items, [this] { _view->exit(); });
	add_command_invoke(commands::large_font, [this] { font_invoke(_state, _app_frame); });
	add_command_invoke(commands::playback_volume_toggle, [this] { toggle_volume(); });
	add_command_invoke(commands::playback_volume200, [this] { setting.media_volume = media_volume_boost; });
	add_command_invoke(commands::playback_volume100, [this] { setting.media_volume = media_volumes[0]; });
	add_command_invoke(commands::playback_volume75, [this] { setting.media_volume = media_volumes[1]; });
	add_command_invoke(commands::playback_volume50, [this] { setting.media_volume = media_volumes[2]; });
	add_command_invoke(commands::playback_volume25, [this] { setting.media_volume = media_volumes[3]; });
	add_command_invoke(commands::playback_volume0, [this] { setting.media_volume = media_volumes[4]; });
	add_command_invoke(commands::view_zoom, [this] { zoom_invoke(_state, _app_frame); });
	add_command_invoke(commands::view_zoom_fit, [this] { zoom_fit_invoke(_state); });
	add_command_invoke(commands::view_zoom_fit_width,
	                   [this] { zoom_fit_variant_invoke(_state, df::zoom_scale_mode::fit_width); });
	add_command_invoke(commands::view_zoom_fill,
	                   [this] { zoom_fit_variant_invoke(_state, df::zoom_scale_mode::fill); });
	add_command_invoke(commands::view_zoom_toggle_fit, [this] { zoom_toggle_fit_invoke(_state); });
	add_command_invoke(commands::view_zoom_100, [this] { zoom_100_invoke(_state); });
	add_command_invoke(commands::view_zoom_in, [this] { zoom_step_invoke(_state, 1); });
	add_command_invoke(commands::view_zoom_pane_flip, [this]
	{
		if (const auto display = _state.display_state()) display->flip_zoom_pane();
	});
	add_command_invoke(commands::view_zoom_out, [this] { zoom_step_invoke(_state, -1); });
	add_command_invoke(commands::view_zoom_navigator_auto_hide,
	                   [this] { zoom_navigator_mode_invoke(_state, zoom_navigator_mode::auto_hide); });
	add_command_invoke(commands::view_zoom_navigator_off,
	                   [this] { zoom_navigator_mode_invoke(_state, zoom_navigator_mode::off); });

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
	add_command_invoke(commands::group_aspect_ratio, [this] { _state.group_order(group_by::aspect_ratio, {}); });
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
	add_command_invoke(commands::sort_date_created, [this] { _state.group_order({}, sort_by::date_created); });
	add_command_invoke(commands::sort_date_modified, [this] { _state.group_order({}, sort_by::date_modified); });

	add_command_invoke(commands::tool_import, [this] { _state.view_mode(view_type::import); });
	add_command_invoke(commands::tool_sync, [this] { _state.view_mode(view_type::sync); });

	add_command_invoke(commands::tool_run, [this]
	{
		if (_state.view_mode() == view_type::batch) _view_batch->run();
		else _view_rename->run();
	});

	add_command_invoke(commands::tool_refresh, [this]
	{
		if (_state.view_mode() == view_type::batch && _view_batch) _view_batch->refresh();
	});

	add_command_invoke(commands::import_analyze, [this] { _view_import->analyze(); });
	add_command_invoke(commands::import_run, [this] { _view_import->run(); });

	add_command_invoke(commands::locate_run, [this] { _view_locate->run(); });

	add_command_invoke(commands::sync_analyze, [this] { _view_sync->analyze(); });
	add_command_invoke(commands::sync_run, [this] { _view_sync->run(); });

	add_command_invoke(commands::tags_run, [this] { _view_tags->run(); });

	// Cancel stops the running task and leaves the view open; Close always exits the view.
	add_command_invoke(commands::view_cancel, [this] { if (_view) _view->cancel_operation(); });

	add_command_invoke(commands::english, [this]
	{
		setting.language = "en";
		tt.clear();
		language_changed("en");
	});

	_commands[commands::menu_main]->menu = [this]
	{
		std::vector<ui::command_ptr> result = {
			find_command(commands::view_fullscreen),
			find_command(commands::play),
			find_command(commands::slideshow),
			find_command(commands::browse_search),
			nullptr,
			find_command(commands::menu_navigate),
			find_command(commands::menu_open),
			find_command(commands::menu_tools),
			find_command(commands::menu_rate_or_label),
			find_command(commands::menu_select),
			find_command(commands::menu_group),
			find_command(commands::menu_options),
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
			find_command(commands::keyboard),
			find_command(commands::view_help),
#ifndef WINSTORE
			find_command(commands::info_check_for_updates),
#endif
			find_command(commands::exit)
		};
		return result;
	};
	_commands[commands::menu_options]->menu = [this]
	{
		std::vector<ui::command_ptr> result = {
			find_command(commands::options_general),
			find_command(commands::options_collection),
			find_command(commands::options_sidebar),
			find_command(commands::favorite_tags),
			nullptr,
			find_command(commands::menu_display_options),
			find_command(commands::playback_menu),
			find_command(commands::menu_language)
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
				command->text = str_format(tt.open_with_fmt.sv(), h.name);
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
						command->text = str_format(tt.open_with_fmt.sv(), t->text);
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
		result.emplace_back(nullptr);

		if (_state.has_gps())
		{
			for (const auto& link : all_map_links())
			{
				auto command = std::make_shared<ui::command>();
				command->icon = icon_index::location;
				command->text = str_format(tt.open_with_fmt.sv(), link.text);
				command->invoke = [link, this] { _state.open_gps_on_map(link.url); };
				result.emplace_back(command);
			}
		}
		else
		{
			result.emplace_back(find_command(commands::browse_open_googlemap));
		}

		result.emplace_back(nullptr);
		result.emplace_back(find_command(commands::browse_open_in_file_browser));
		result.emplace_back(find_command(commands::tool_file_properties));
		return result;
	};
	_commands[commands::menu_language]->menu = [this]
	{
		const auto lang_folder = known_path(platform::known_folder::running_app_folder).combine("languages");
		const auto folder_contents = platform::iterate_file_items(lang_folder, false);

		std::vector<ui::command_ptr> result;
		result.emplace_back(find_command(commands::english));
		result.emplace_back(nullptr);

		for (const auto& f : folder_contents.files)
		{
			const auto lang_path = lang_folder.combine_file(f.name);
			const auto extension = lang_path.extension();

			if (str::icmp(extension, ".po") == 0)
			{
				const auto lang_code = lang_path.file_name_without_extension();

				auto command = std::make_shared<ui::command>();
				command->text = language_name(lang_code);
				command->checked = setting.language == lang_code;
				command->invoke = [this, lang_path, lang_code]
				{
					setting.language = lang_code;

					const auto po_entries = load_po(lang_path);
					tt.load_lang(lang_path.name(), po_entries);
					language_changed(lang_code);
				};

				result.emplace_back(command);
			}
		}
		return result;
	};
	// One definition of what a tool is. The panel affordance adds the file operations it also
	// carries, because the context menus already list those at their own top level.
	const auto tools_menu = [this]
	{
		std::vector<ui::command_ptr> result = {
			find_command(commands::tool_edit),
			find_command(commands::tool_tag),
			find_command(commands::tool_locate),
			find_command(commands::tool_adjust_date),
			find_command(commands::tool_edit_metadata),
			nullptr,
			find_command(commands::tool_rotate_anticlockwise),
			find_command(commands::tool_rotate_clockwise),
			find_command(commands::tool_convert),
			find_command(commands::tool_save_current_video_frame),
			nullptr,
			find_command(commands::search_related),
			find_command(commands::tool_desktop_background),
			find_command(commands::tool_email),
			find_command(commands::tool_burn),
			find_command(commands::print),
		};
		return result;
	};
	_commands[commands::menu_tools]->menu = tools_menu;
	_commands[commands::menu_tools_toolbar]->menu = [this, tools_menu]
	{
		auto result = tools_menu();
		result.emplace_back(nullptr);
		result.emplace_back(find_command(commands::tool_delete));
		result.emplace_back(find_command(commands::tool_rename));
		result.emplace_back(find_command(commands::tool_copy_to_folder));
		result.emplace_back(find_command(commands::tool_move_to_folder));
		return result;
	};
	// The toolbar button and the menu entry open the same list; they only differ in their label.
	const auto group_menu = [this]
	{
		std::vector<ui::command_ptr> result = {
			find_command(commands::group_album),
			find_command(commands::group_aspect_ratio),
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
			find_command(commands::sort_date_created),
			find_command(commands::sort_date_modified),
			find_command(commands::sort_dates_descending),
			find_command(commands::sort_dates_ascending),
			nullptr,
			find_command(commands::group_shuffle),
			find_command(commands::group_toggle),
		};
		return result;
	};
	_commands[commands::menu_group]->menu = group_menu;
	_commands[commands::menu_group_toolbar]->menu = group_menu;
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
	_commands[commands::menu_zoom]->menu = [this]
	{
		std::vector<ui::command_ptr> result{
			find_command(commands::view_zoom),
			nullptr,
			find_command(commands::view_zoom_fit),
			find_command(commands::view_zoom_fit_width),
			find_command(commands::view_zoom_fill),
			find_command(commands::view_zoom_100),
			find_command(commands::view_zoom_in),
			find_command(commands::view_zoom_out),
			nullptr
		};
		const auto display = _state.display_state();
		const auto current_scale = display ? display->zoom_scale_percent() : 0;
		for (const auto scale : df::zoom_view_state::ladder())
		{
			auto command = std::make_shared<ui::command>();
			command->text = std::format("{}%", df::round(scale * 100.0));
			command->checked = current_scale == df::round(scale * 100.0);
			command->enable = display && display->can_zoom();
			command->invoke = [display, scale] { if (display) display->zoom_scale(scale); };
			result.emplace_back(std::move(command));
		}
		if (display && display->is_two())
		{
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::view_zoom_pane_flip));
		}
		result.emplace_back(nullptr);
		result.emplace_back(find_command(commands::menu_zoom_navigator));
		return result;
	};
	_commands[commands::menu_zoom_navigator]->menu = [this]
	{
		std::vector<ui::command_ptr> result{
			find_command(commands::view_zoom_navigator_auto_hide),
			find_command(commands::view_zoom_navigator_off)
		};
		return result;
	};
	_commands[commands::menu_playback]->menu = _commands[commands::playback_menu]->menu = [this]
	{
		std::vector<ui::command_ptr> result = {
			find_command(commands::playback_volume200),
			find_command(commands::playback_volume100),
			find_command(commands::playback_volume75),
			find_command(commands::playback_volume50),
			find_command(commands::playback_volume25),
			find_command(commands::playback_volume0),
			nullptr,
			find_command(commands::playback_auto_play),
			find_command(commands::playback_auto_advance),
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
			auto heading = std::make_shared<ui::command>();
			heading->text = tt.audio_output_title;
			heading->enable = false;
			result.emplace_back(std::move(heading));

			const auto play_id = _player->play_audio_device_id();

			for (const auto& d : devices)
			{
				auto command = std::make_shared<ui::command>();
				command->text = d.name;
				command->checked = play_id == d.id;
				command->invoke = [this, d]
				{
					_state.change_audio_device(d.id);
					setting.sound_device = d.id;
					invalidate_view(view_invalid::options_save | view_invalid::command_state);
				};

				result.emplace_back(command);
			}
		}

		const auto& display = _state.display_state();

		if (display && display->is_one() && display->_player_media_info.has_multiple_audio_streams)
		{
			result.emplace_back(nullptr);
			auto heading = std::make_shared<ui::command>();
			heading->text = tt.audio_tracks_title;
			heading->enable = false;
			result.emplace_back(std::move(heading));

			auto audio_track_number = 0;
			for (const auto& st : display->_player_media_info.streams)
			{
				if (st.type == av_stream_type::audio)
				{
					++audio_track_number;

					auto command = std::make_shared<ui::command>();
					command->text = format_audio_stream_name(st, audio_track_number);
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

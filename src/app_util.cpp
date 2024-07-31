#include "pch.h"
#include "util_strings.h"
#include "model.h"
#include "model_db.h"
#include "app_command_status.h"
#include "app_util.h"

#include "model_index.h"


void view_state::modify_items(const df::results_ptr& results, icon_index icon, const std::u8string_view title,
	const df::item_elements& items_to_modify, const metadata_edits& edits,
	const view_host_base_ptr& view)
{
	const auto can_process = can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	if (can_process.success())
	{
		detach_file_handles detach(*this);

		queue_async(async_queue::work, [this, items_to_modify, edits, results]()
			{
				result_scope rr(results);
				std::u8string message;
				files ff;

				file_encode_params encode_params;
				encode_params.jpeg_save_quality = setting.jpeg_save_quality;

				for (const auto& i : items_to_modify)
				{
					results->start_item(i->name());
					const auto update_result = ff.update(i->path(), edits, {}, encode_params, false, i->xmp());
					results->end_item(i->name(), to_status(update_result.code));

					if (!update_result.success())
					{
						message = update_result.format_error();
						break;
					}

					if (results->is_canceled())
						break;
				}

				rr.complete(message);
			});

		results->wait_for_complete();

		const df::item_set item_set(items_to_modify);
		item_index.queue_scan_modified_items(item_set);
	}
	else
	{
		results->show_message(can_process.to_string());
	}
}



static platform::file_op_result move_or_copy(const df::file_path source_path, const df::file_path dest_path,
	const bool is_move, const bool fail_if_exists)
{
	return is_move
		? platform::move_file(source_path, dest_path, fail_if_exists)
		: platform::copy_file(source_path, dest_path, fail_if_exists, false);
}

static bool ignore_existing(const df::file_path path_in, const df::file_path path_out, const bool already_exists,
	const bool overwrite_if_newer)
{
	auto result = already_exists;

	if (already_exists && overwrite_if_newer)
	{
		const auto source_info = platform::file_attributes(path_in);
		const auto dest_info = platform::file_attributes(path_out);

		result = source_info.modified <= dest_info.modified;
	}

	return result;
}

std::vector<rename_item> calc_item_renames(const df::item_set& items, const std::u8string_view template_name, const int start)
{
	std::vector<rename_item> results;
	auto seq = start;

	for (const auto& i : items.items())
	{
		auto md = i->metadata();
		auto original_name = i->base_name();
		auto name = format_sequence(original_name, template_name, seq);
		name = prop::replace_tokens(name, md, i->name(), i->calc_media_created());

		rename_item rename;
		rename.item = i;
		rename.original_name = original_name;
		rename.new_name = name;
		seq += 1;
		results.emplace_back(rename);
	}

	return results;
}

std::u8string format_sequence(const std::u8string_view original_name, const std::u8string_view template_name, const int seq)

{
	static auto numbers = u8"0123456789"sv;
	std::u8string result;

	if (!template_name.empty())
	{
		const auto reverse_template = std::u8string(template_name.rbegin(), template_name.rend());
		const auto reverse_name = std::u8string(original_name.rbegin(), original_name.rend());

		const auto org_len = reverse_name.size();
		auto i_div = seq;
		auto i = 0u;

		for (const auto c : reverse_template)
		{
			switch (c)
			{
			case L'#':
				result.push_back(numbers[i_div % 10]);
				i_div /= 10;
				break;
			case L'?':
				result.push_back((org_len > i) ? reverse_name[i] : ' ');
				break;
			default:
				result.push_back(c);
				break;
			}

			i += 1;
		}
	}

	return std::u8string(result.rbegin(), result.rend());
}

import_analysis_result import_analysis(const std::vector<folder_scan_item>& src_items,
	const import_options& options, const item_import_set& previous_imported,
	df::cancel_token token)
{
	import_analysis_result result;
	const auto now = platform::now();


	for (const auto& i : src_items)
	{
		if (token.is_cancelled()) break;

		const auto md = i.item.metadata.load();

		df::date_t created;

		if (md)
		{
			created = md->created();
		}

		const auto import_folder_out = replace_tokens(options.dest_structure, md, i.item.name, i.item.created());
		const auto path_in = i.folder.combine_file(i.item.name);
		const auto path_out = options.dest_folder.combine(import_folder_out).combine_file(path_in.name());
		const auto import_rec = item_import{ i.item.name, i.item.file_modified, i.item.size, now };
		const bool already_exists = path_out.exists();

		if (previous_imported.contains(import_rec))
		{
			result[path_out.folder()].emplace_back(path_in, path_out, import_action::already_imported, created,
				import_rec, already_exists, import_folder_out);
		}
		else if (ignore_existing(path_in, path_out, already_exists, options.overwrite_if_newer))
		{
			result[path_out.folder()].emplace_back(path_in, path_out, import_action::already_exists, created,
				import_rec, already_exists, import_folder_out);
		}
		else
		{
			result[path_out.folder()].emplace_back(path_in, path_out, import_action::import, created, import_rec,
				already_exists, import_folder_out);

			if (md)
			{
				const auto parts = split(md->sidecars, true);

				for (const auto& file_name : parts)
				{
					const auto sidecar_path_in = i.folder.combine_file(file_name);
					const auto sidecar_path_out = options.dest_folder.combine(import_folder_out).combine_file(
						path_in.name());
					const bool sidecar_already_exists = sidecar_path_out.exists();

					if (!ignore_existing(sidecar_path_in, sidecar_path_out, sidecar_already_exists,
						options.overwrite_if_newer))
					{
						result[sidecar_path_out.folder()].emplace_back(sidecar_path_in, sidecar_path_out,
							import_action::import, created, import_rec,
							sidecar_already_exists, import_folder_out);
					}
				}
			}
		}
	}

	return result;
}

import_result import_copy(index_state& index, item_results_ptr results, const import_analysis_result& src_items,
	const import_options& options, df::cancel_token token)
{
	import_result result;
	result_scope rr(results);

	std::vector<folder_scan_item> existing;
	std::vector<folder_scan_item> previous;
	df::unique_folders write_folders;

	for (const auto& ff_dest : src_items)
	{
		const auto folder_out = ff_dest.first;
		const bool folder_out_exists = folder_out.exists();
		const auto create_folder_result = folder_out_exists
			? platform::file_op_result{ platform::file_op_result_code::OK, {}, {} }
		: platform::create_folder(folder_out);

		if (create_folder_result.failed())
		{
			results->abort(
				create_folder_result.format_error(str::format(tt.error_create_folder_failed_fmt, folder_out)));
		}
		else
		{
			write_folders.emplace(folder_out);

			for (const auto& i : ff_dest.second)
			{
				if (results->is_canceled()) break;

				if (i.action == import_action::import)
				{
					results->start_item(i.source.name());

					const auto path_in = i.source;
					const auto path_out = i.destination;

					const auto fail_if_exists = !options.overwrite_if_newer;
					const auto move_or_copy_result = move_or_copy(path_in, path_out, options.is_move, fail_if_exists);

					if (move_or_copy_result.success())
					{
						if (options.set_created_date)
						{
							platform::created_date(path_out, i.created_date);
						}

						result.folder = path_out.folder();
						result.imports.emplace(i.import_rec);
					}

					results->end_item(i.source.name(), to_status(move_or_copy_result.code));
				}
			}
		}
	}

	index.queue_scan_folders(write_folders);

	std::u8string result_text;

	if (!existing.empty())
	{
		result_text += format_plural_text(tt.ignored_exist_already_fmt, existing.front().item.name, static_cast<int>(existing.size()), {},
			static_cast<int>(src_items.size()));
	}

	if (!previous.empty())
	{
		if (!result_text.empty()) result_text += u8"\n\n"sv;
		result_text += format_plural_text(tt.ignored_previous_fmt, previous.front().item.name, static_cast<int>(previous.size()), {},
			static_cast<int>(src_items.size()));
	}

	rr.complete(result_text);

	return result;
}

size_t count_imports(const std::vector<import_analysis_item>& items)
{
	size_t result = 0;

	for (const auto& i : items)
	{
		if (i.action == import_action::import &&
			!i.already_exists)
		{
			++result;
		}
	}

	return result;
}

size_t count_imports(const import_analysis_result& items)
{
	size_t result = 0;

	for (const auto& i : items)
	{
		result += count_imports(i.second);
	}

	return result;
}

size_t count_overwrites(const std::vector<import_analysis_item>& items)
{
	size_t result = 0;

	for (const auto& i : items)
	{
		if (i.action == import_action::import &&
			i.already_exists)
		{
			++result;
		}
	}

	return result;
}

size_t count_ignores(const std::vector<import_analysis_item>& items)
{
	size_t result = 0;

	for (const auto& i : items)
	{
		if (i.action != import_action::import)
		{
			++result;
		}
	}

	return result;
}


std::vector<import_source> calc_import_sources(view_state& s)
{
	std::vector<import_source> result;

	if (s.has_selection())
	{
		import_source source;
		source.text = str::format(tt.selected_items_fmt, format_plural_text(tt.title_item_count_fmt, s.selected_items()));
		source.items = s.selected_items();
		result.emplace_back(source);
	}

	const auto onedrive_camera_roll = known_path(platform::known_folder::onedrive_camera_roll);

	if (onedrive_camera_roll.exists())
	{
		result.emplace_back(std::u8string(onedrive_camera_roll.text()), onedrive_camera_roll);
	}

	auto drives = platform::scan_drives(true);
	const int drive_max = 5;

	if (drives.size() > drive_max)
	{
		drives.resize(drive_max);
	}

	for (const auto& d : drives)
	{
		if (d.type == platform::drive_type::removable)
		{
			const auto text = str::format(u8"{} {} {}"sv, d.name, d.vol_name, d.used);
			result.emplace_back(text, df::folder_path(d.name));
		}
	}

	return result;
}




std::u8string relative_combine(const std::u8string& relative, const str::cached name)
{
	auto result = relative;

	if (!result.empty() && result.back() != '\\')
	{
		result += '\\';
	}

	result += name;
	return result;
}

sync_analysis_result sync_analysis(const df::index_roots& local_roots, const df::folder_path remote_path,
	const bool sync_local_remote, const bool sync_remote_local,
	const bool sync_delete_local, const bool sync_delete_remote,
	const df::cancel_token& token)
{
	sync_analysis_result result;
	std::map<std::u8string, df::folder_path, df::iless> local_roots_by_relative;

	std::vector<sync_analysis_folder> local_folders_to_scan;

	for (const auto& f : local_roots.folders)
	{
		local_folders_to_scan.emplace_back(f, f);
	}

	while (!local_folders_to_scan.empty())
	{
		if (token.is_cancelled()) return {};

		const auto folder = local_folders_to_scan.back();
		local_folders_to_scan.pop_back();

		if (!is_excluded(local_roots, folder.path))
		{
			const auto contents = platform::iterate_file_items(folder.path, setting.show_hidden);

			for (const auto& file : contents.files)
			{
				if (token.is_cancelled()) break;

				auto& i = result[folder.relative][file.name.str()];
				i.local_path = folder.path.combine_file(file.name);
				i.local_fi = file;
				i.local_root = folder.root;
			}

			for (const auto& sub_folder : contents.folders)
			{
				auto relative = relative_combine(folder.relative, sub_folder.name);
				auto sub_folder_path = folder.path.combine(sub_folder.name);
				sync_analysis_folder unknown = { sub_folder_path, folder.root, relative };
				local_folders_to_scan.emplace_back(unknown);
				local_roots_by_relative[relative] = folder.root;
			}
		}
	}

	std::vector<sync_analysis_folder> remote_folders_to_scan;
	remote_folders_to_scan.emplace_back(remote_path, remote_path);

	while (!remote_folders_to_scan.empty())
	{
		if (token.is_cancelled()) return {};

		const auto folder = remote_folders_to_scan.back();
		remote_folders_to_scan.pop_back();

		if (!is_excluded(local_roots, folder.path))
		{
			const auto contents = platform::iterate_file_items(folder.path, setting.show_hidden);

			for (const auto& file : contents.files)
			{
				if (token.is_cancelled()) break;

				auto& i = result[folder.relative][file.name.str()];
				i.remote_path = folder.path.combine_file(file.name);
				i.remote_fi = file;
				i.remote_root = folder.root;
			}

			for (const auto& sub_folder : contents.folders)
			{
				sync_analysis_folder unknown = {
					folder.path.combine(sub_folder.name), remote_path,
					relative_combine(folder.relative, sub_folder.name)
				};
				remote_folders_to_scan.emplace_back(unknown);
			}
		}
	}

	for (auto&& i : result)
	{
		for (auto&& f : i.second)
		{
			const auto local_exists = !f.second.local_path.is_empty();
			const auto remote_exists = !f.second.remote_path.is_empty();
			const auto match_exists = local_exists && remote_exists;

			if (f.second.local_root.is_empty())
			{
				const auto found_relative = local_roots_by_relative.find(f.first);

				if (found_relative != local_roots_by_relative.end())
				{
					f.second.local_root = found_relative->second;
				}
				else if (!local_roots.folders.empty())
				{
					f.second.local_root = *local_roots.folders.begin();
				}
			}

			if (f.second.remote_root.is_empty())
			{
				f.second.remote_root = remote_path;
			}

			if (f.second.local_path.is_empty())
			{
				f.second.local_path = f.second.local_root.combine(i.first).combine_file(f.first);
			}

			if (f.second.remote_path.is_empty())
			{
				f.second.remote_path = f.second.remote_root.combine(i.first).combine_file(f.first);
			}

			if (match_exists)
			{
				const auto same_modified = f.second.local_fi.attributes.modified == f.second.remote_fi.attributes.
					modified;

				if (!same_modified)
				{
					const auto local_newer = f.second.local_fi.attributes.modified > f.second.remote_fi.attributes.
						modified;

					if (local_newer)
					{
						if (sync_local_remote)
						{
							f.second.action = sync_action::copy_remote;
						}
					}
					else
					{
						if (sync_remote_local)
						{
							f.second.action = sync_action::copy_local;
						}
					}
				}
			}
			else
			{
				if (local_exists)
				{
					if (sync_local_remote)
					{
						f.second.action = sync_action::copy_remote;
					}
					else if (sync_remote_local && sync_delete_local)
					{
						f.second.action = sync_action::delete_local;
					}
				}
				else if (remote_exists)
				{
					if (sync_remote_local)
					{
						f.second.action = sync_action::copy_local;
					}
					else if (sync_local_remote && sync_delete_remote)
					{
						f.second.action = sync_action::delete_remote;
					}
				}
			}
		}
	}

	return result;
}

void sync_copy(const std::shared_ptr<command_status>& status, const sync_analysis_result& analysis_result,
	const df::cancel_token& token)
{
	for (const auto& i : analysis_result)
	{
		for (const auto& f : i.second)
		{
			if (token.is_cancelled()) return;

			auto local_path = f.second.local_path;
			auto remote_path = f.second.remote_path;

			df::assert_true(!local_path.is_empty());
			df::assert_true(!remote_path.is_empty());

			const auto name = local_path.name();

			if (f.second.action != sync_action::none)
			{
				status->start_item(name);
			}

			switch (f.second.action)
			{
			case sync_action::none:
				break;
			case sync_action::copy_local:
				status->end_item(name, to_status(platform::copy_file(remote_path, local_path, false, true).code));
				break;
			case sync_action::copy_remote:
				status->end_item(name, to_status(platform::copy_file(local_path, remote_path, false, true).code));
				break;
			case sync_action::delete_local:
				status->end_item(name, to_status(platform::delete_file(local_path).code));
				break;
			case sync_action::delete_remote:
				status->end_item(name, to_status(platform::delete_file(remote_path).code));
				break;
			}
		}
	}
}

void toggle_collection_entry(settings_t::index_t& collection_settings, const df::folder_path folder, const bool is_remove)
{
	const auto local_folders = platform::local_folders();

	if (is_remove)
	{
		if (local_folders.pictures == folder) collection_settings.pictures = false;
		else if (local_folders.music == folder) collection_settings.music = false;
		else if (local_folders.video == folder) collection_settings.video = false;
		else if (local_folders.onedrive_pictures == folder) collection_settings.onedrive_pictures = false;
		else if (local_folders.onedrive_video == folder) collection_settings.onedrive_video = false;
		else if (local_folders.onedrive_music == folder) collection_settings.onedrive_music = false;
		else if (local_folders.dropbox_photos == folder) collection_settings.drop_box = false;

		std::u8string more_folders;

		for (const auto existing_folder_path : split_collection_folders(collection_settings.more_folders))
		{
			if (folder != df::folder_path(existing_folder_path))
			{
				if (!more_folders.empty()) more_folders += u8"\r\n"sv;
				more_folders += existing_folder_path;
			}
		}

		collection_settings.more_folders = more_folders;
	}
	else
	{
		// add
		if (local_folders.pictures == folder) collection_settings.pictures = true;
		else if (local_folders.music == folder) collection_settings.music = true;
		else if (local_folders.video == folder) collection_settings.video = true;
		else if (local_folders.onedrive_pictures == folder) collection_settings.onedrive_pictures = true;
		else if (local_folders.onedrive_video == folder) collection_settings.onedrive_video = true;
		else if (local_folders.onedrive_music == folder) collection_settings.onedrive_music = true;
		else if (local_folders.dropbox_photos == folder) collection_settings.drop_box = true;
		else
		{
			if (!collection_settings.more_folders.empty()) collection_settings.more_folders += u8"\r\n"sv;
			collection_settings.more_folders += folder.text();
		}
	}
}

std::vector<std::u8string> check_overwrite(const df::folder_path write_folder, const df::item_set& items,
	const std::u8string_view new_extension)
{
	std::vector<std::u8string> result;

	for (const auto f : items.folder_paths())
	{
		const auto dest = write_folder.combine(f.name());

		if (dest.exists())
		{
			result.emplace_back(dest.name());
		}
	}

	for (const auto f : items.file_paths())
	{
		const auto dest = write_folder.combine_file(f.name()).extension(
			new_extension.empty() ? f.extension() : new_extension);

		if (dest.exists())
		{
			result.emplace_back(dest.name());
		}
	}

	return result;
}

icon_index drive_icon(const platform::drive_type d)
{
	switch (d)
	{
	case platform::drive_type::removable: return icon_index::usb;
	case platform::drive_type::fixed: return icon_index::hard_drive;
	case platform::drive_type::remote: return icon_index::network;
	case platform::drive_type::cdrom: return icon_index::disk;
	case platform::drive_type::device: return icon_index::disk;
	default:;
	}

	return icon_index::hard_drive;
}

bool df::is_excluded(const df::index_roots& roots, const df::folder_path path)
{
	if (roots.excludes.contains(path)) return true;

	auto name = path.name();

	for (auto exclude : roots.exclude_wildcards)
	{
		if (str::wildcard_icmp(name, exclude))
			return true;
	}

	return false;
}


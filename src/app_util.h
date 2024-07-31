#pragma once


static constexpr std::u8string_view doc_template_url = u8"https://www.diffractor.com/docs/template"sv;

struct item_import;
struct item_import_hash;
struct item_import_eq;

class command_status;

using item_results_ptr = std::shared_ptr<command_status>;
using item_import_set = df::hash_set<item_import, item_import_hash, item_import_eq>;

class result_scope
{
	const df::results_ptr _r;
	bool _completed = false;

public:
	explicit result_scope(df::results_ptr r) : _r(std::move(r))
	{
	}

	~result_scope()
	{
		if (!_completed)
		{
			_r->complete();
		}
	}

	void complete(const std::u8string_view message = {})
	{
		_r->complete();
		_completed = true;
	}
};

struct rename_item
{
	df::item_element_ptr item;
	std::u8string original_name;
	std::u8string new_name;
};

std::vector<rename_item> calc_item_renames(const df::item_set& items, const std::u8string_view template_name, const int start);
std::u8string format_sequence(const std::u8string_view original_name, const std::u8string_view template_name, const int seq);

struct import_options
{
	df::folder_path dest_folder;
	std::u8string source_filter;
	std::u8string dest_structure;

	bool is_move = false;
	bool overwrite_if_newer = false;
	bool set_created_date = false;
	bool rename_different_attributes = false;
};

struct import_source
{
	std::u8string text;
	df::folder_path path;
	bool selected = false;
	df::item_set items;
};


enum class import_action
{
	import,
	already_exists,
	already_imported
};

struct import_result
{
	df::folder_path folder;
	item_import_set imports;
};

struct import_analysis_item
{
	df::file_path source;
	df::file_path destination;
	import_action action;
	df::date_t created_date;
	item_import import_rec;
	bool already_exists = false;
	std::u8string sub_folder;
};

using import_analysis_result = std::map<df::folder_path, std::vector<import_analysis_item>, df::iless>;

import_analysis_result import_analysis(const std::vector<folder_scan_item>& src_items,
	const import_options& options, const item_import_set& previous_imported,
	df::cancel_token token);

import_result import_copy(index_state& index, item_results_ptr results, const import_analysis_result& src_items,
	const import_options& options, df::cancel_token token);

std::vector<import_source> calc_import_sources(view_state& s);

size_t count_imports(const std::vector<import_analysis_item>& items);
size_t count_imports(const import_analysis_result& items);
size_t count_overwrites(const std::vector<import_analysis_item>& items);
size_t count_ignores(const std::vector<import_analysis_item>& items);

enum class sync_action
{
	none,
	copy_local,
	copy_remote,
	delete_local,
	delete_remote,
};

struct sync_analysis_item
{
	df::file_path local_path;
	df::file_path remote_path;

	platform::file_info local_fi;
	platform::file_info remote_fi;

	df::folder_path local_root;
	df::folder_path remote_root;

	sync_action action = sync_action::none;
};

struct sync_analysis_folder
{
	df::folder_path path;
	df::folder_path root;
	std::u8string relative;
};

using sync_analysis_items = std::map<std::u8string, sync_analysis_item, df::iless>;
using sync_analysis_result = std::map<std::u8string, sync_analysis_items, df::iless>;

sync_analysis_result sync_analysis(const df::index_roots& local_roots, const df::folder_path remote_path,
	const bool sync_local_remote, const bool sync_remote_local,
	const bool sync_delete_local, const bool sync_delete_remote,
	const df::cancel_token& token);

void sync_copy(const std::shared_ptr<command_status>& status, const sync_analysis_result& analysis_result, const df::cancel_token& token);

void toggle_collection_entry(settings_t::index_t& collection_settings, const df::folder_path folder, const bool is_remove);


std::vector<std::u8string> check_overwrite(const df::folder_path write_folder, const df::item_set& items,
	const std::u8string_view new_extension);

icon_index drive_icon(const platform::drive_type d);
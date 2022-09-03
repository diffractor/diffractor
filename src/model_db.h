// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model_items.h"

struct item_db_write;
struct sqlite3;

class metadata_packer;
class index_state;
class db_statement;

using item_writes_t = platform::queue<item_db_write>;

struct item_import
{
	str::cached name;
	df::date_t modified;
	df::file_size size;
	df::date_t imported;

	int compare(const item_import& other) const
	{
		const auto cam_diff = icmp(name, other.name);
		if (cam_diff != 0) return cam_diff;
		if (modified.to_int64() < other.modified.to_int64()) return -1;
		if (modified.to_int64() > other.modified.to_int64()) return 1;
		if (size.to_int64() < other.size.to_int64()) return -1;
		if (size.to_int64() > other.size.to_int64()) return 1;
		return 0;
	}
};

struct item_import_hash
{
	size_t operator()(const item_import& i) const
	{
		return crypto::hash_gen(i.name).append(i.modified.to_int64()).append(i.size.to_int64()).result();
	}
};

struct item_import_eq
{
	bool operator()(const item_import& l, const item_import& r) const
	{
		return l.compare(r) == 0;
	}
};

using item_import_set = df::hash_set<item_import, item_import_hash, item_import_eq>;

class database : public df::no_copy
{
	index_state& _state;
	df::file_path _db_path;
	uint32_t _db_thread_id = 0;
	sqlite3* _db = nullptr;

	std::unique_ptr<db_statement> find_web_request;
	std::unique_ptr<db_statement> find_folder_thumbnail;
	std::unique_ptr<db_statement> find_thumbnail;

	bool is_db_thread() const;

public:
	struct db_thumbnail
	{
		ui::const_image_ptr thumb;
		df::date_t last_indexed;

		db_thumbnail() noexcept = default;
		db_thumbnail(const db_thumbnail&) = delete;
		db_thumbnail& operator=(const db_thumbnail&) = delete;
		db_thumbnail(db_thumbnail&&) noexcept = default;
		db_thumbnail& operator=(db_thumbnail&&) noexcept = default;
	};


	database(index_state& s);
	~database();

	bool is_open() const;
	void load_index_values();

	bool has_errors() const;

	std::u8string web_service_cache(const std::u8string& key) const;
	void web_service_cache(const std::u8string& key, const std::u8string& value) const;

	item_import_set load_item_imports();
	void writes_item_imports(const item_import_set& items);

	void close();

	void clean(const std::vector<df::file_path>& indexed_items) const;
	db_thumbnail load_thumbnail(df::file_path id) const;
	db_thumbnail load_folder_thumbnail(str::cached folder) const;
	void load_thumbnails(const index_state& index, const df::item_set& items);
	void open();
	void open(df::folder_path folder, std::u8string_view file_name);
	void perform_writes();
	void perform_writes(std::deque<item_db_write> writes);
	void maintenance(bool is_reset);


	friend class bucket;
	friend class db_blob_builder;
};

// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include <Sqlite3.h>

#include "files.h"
#include "model_db.h"

#include "model_db_pack.h"
#include "model_index.h"

static int db_fails = 0;
using cached_statements = df::hash_map<std::u8string, struct sqlite3_stmt*>;

inline void db_trace_error(sqlite3* db, const std::u8string_view sql)
{
	df::log(__FUNCTION__, str::format(u8"Database error: {} [{}]", str::utf8_cast(sqlite3_errmsg(db)), sql));
	db_fails += 1;
}

inline int db_exec(sqlite3* db, const std::u8string& sql)
{
	const auto result = sqlite3_exec(db, std::bit_cast<const char*>(sql.c_str()), nullptr, nullptr, nullptr);

	if (SQLITE_OK != result)
	{
		db_trace_error(db, sql);
	}

	return result;
}

class db_statement
{
private:
	sqlite3* _db;
	sqlite3_stmt* _handle;

public:
	db_statement(sqlite3* db, const std::u8string& sql) : _db(db), _handle(nullptr)
	{
		compile(sql);
	}

	~db_statement()
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_finalize(_handle);
			_handle = nullptr;

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_finalize");
		}
	}

	void compile(std::u8string_view sql)
	{
		const int ret = sqlite3_prepare(_db, std::bit_cast<const char*>(sql.data()), sql.size(), &_handle, nullptr);

		if (ret != SQLITE_OK)
			db_trace_error(_db, u8"sqlite3_prepare");
	}

	void bind(int i, const std::u8string_view v) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_text(_handle, i, std::bit_cast<const char*>(v.data()), v.size(),
			                                  SQLITE_STATIC);

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_text");
		}
	}

	void bind(int i, int32_t n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_int(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_int");
		}
	}

	void bind(int i, uint32_t n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_int(_handle, i, static_cast<int>(n));

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_int");
		}
	}

	void bind(int i, double n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_double(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_double");
		}
	}

	void bind(int i, int64_t n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_int64(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_int64");
		}
	}

	void bind(int i, uint64_t n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_int64(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_int64");
		}
	}

	void bind(int i, df::cspan cs) const
	{
		if (_handle != nullptr && !cs.empty())
		{
			const int ret = sqlite3_bind_blob(_handle, i, cs.data, cs.size, SQLITE_STATIC);

			if (ret != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_bind_blob");
		}
	}

	void reset() const
	{
		if (_handle != nullptr)
		{
			if (sqlite3_reset(_handle) != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_reset");

			if (sqlite3_clear_bindings(_handle) != SQLITE_OK)
				db_trace_error(_db, u8"sqlite3_clear_bindings");
		}
	}

	bool read() const
	{
		if (_handle != nullptr)
		{
			const int result = sqlite3_step(_handle);

			switch (result)
			{
			case SQLITE_ROW:
				return true;
			case SQLITE_DONE:
				return false;
			default:
				db_trace_error(_db, u8"sqlite3_step");
			}
		}

		return false;
	}

	void exec() const
	{
		while (read())
		{
		}
	}

	int int32(int i) const
	{
		if (_handle != nullptr)
		{
			return sqlite3_column_int(_handle, i);
		}
		return 0;
	}

	int64_t int64(int i) const
	{
		if (_handle != nullptr)
		{
			return sqlite3_column_int64(_handle, i);
		}
		return 0;
	}

	std::u8string_view text(int i) const
	{
		if (_handle != nullptr)
		{
			return std::bit_cast<const char8_t*>(sqlite3_column_text(_handle, i));
		}
		return {};
	}

	df::blob blob(int i) const
	{
		if (_handle != nullptr)
		{
			const auto* const pData = static_cast<const uint8_t*>(sqlite3_column_blob(_handle, i));
			const auto len = sqlite3_column_bytes(_handle, i);
			return {pData, pData + len};
		}
		return {};
	}

	df::cspan data(int i) const
	{
		df::cspan r = {nullptr, 0};
		if (_handle != nullptr)
		{
			r.data = static_cast<const uint8_t*>(sqlite3_column_blob(_handle, i));
			r.size = sqlite3_column_bytes(_handle, i);
		}
		return r;
	}

	sqlite_int64 last_id() const
	{
		return sqlite3_last_insert_rowid(_db);
	}
};

class transaction
{
	sqlite3* _db;
	bool _acquired;

public:
	transaction(sqlite3* db, bool start = true) : _db(db), _acquired(false)
	{
		if (start)
		{
			const int ret = db_exec(_db, u8"BEGIN TRANSACTION");
			_acquired = ret == SQLITE_OK;
		}
	}

	~transaction()
	{
		if (_acquired)
		{
			db_exec(_db, u8"COMMIT");
		}
	}
};

static_assert(std::is_trivially_copyable_v<item_import>);
static_assert(std::is_move_constructible_v<db_item_t>);
static_assert(std::is_move_constructible_v<item_db_write>);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

database::database(index_state& s) : _state(s)
{
}

database::~database()
{
	close();
}

void database::open(const df::folder_path folder, const std::u8string_view file_name)
{
	sqlite3_initialize();

	df::scope_locked_inc l(_state.searching);
	_db_thread_id = platform::current_thread_id();
	_db_path = df::file_path(folder, file_name, u8".db");
	open();
}

static std::u8string load_create_sql()
{
	auto sql = load_resource(platform::resource_item::sql);
	return std::u8string(std::bit_cast<const char8_t*>(sql.data()), sql.size());
}

void database::open()
{
	df::assert_true(is_db_thread());

	df::scope_locked_inc slc(df::jobs_running);

	df::log(__FUNCTION__, str::format(u8"Open database: {}", _db_path));

	const auto rc = sqlite3_open(std::bit_cast<const char*>(_db_path.str().c_str()), &_db);

	if (rc)
	{
		df::log(__FUNCTION__, str::format(u8"Failed to open database: {}", str::utf8_cast(sqlite3_errmsg(_db))));
		sqlite3_close(_db);
		_db = nullptr;
	}
	else
	{
		sqlite3_busy_timeout(_db, 1000);

		const auto sql = load_create_sql();
		const auto create_result = db_exec(_db, sql);

		if (SQLITE_CORRUPT == create_result)
		{
			const auto message = str::format(u8"Database is corrupt: {}\n\nPath: {}",
			                                 str::utf8_cast(sqlite3_errmsg(_db)), _db_path);
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}
		if (SQLITE_OK != create_result)
		{
			const auto message = str::format(u8"Failed to create database: {}\n\nPath: {}",
			                                 str::utf8_cast(sqlite3_errmsg(_db)), _db_path);
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		load_index_values();
		df::log(__FUNCTION__, str::format(u8"Loaded index in {} ms", _state.stats.index_load_ms));
	}

	// schema upgrades
	sqlite3_exec(_db, "ALTER TABLE item_properties ADD COLUMN crc INTEGER;", nullptr, nullptr, nullptr);

	find_web_request = std::make_unique<db_statement>(_db, u8"select value from web_service_cache where key=?");
	find_folder_thumbnail = std::make_unique<db_statement>(
		_db, u8"select bitmap, last_scanned from item_thumbnails where folder=?");
	find_thumbnail = std::make_unique<db_statement>(
		_db, u8"select bitmap, last_scanned from item_thumbnails where folder=? AND name=?");

	_state.stats.database_size = platform::file_attributes(_db_path).size;
	_state.stats.database_path = _db_path;
	df::log(__FUNCTION__, str::format(u8"Index open {}", _state.stats.database_size));
}

bool database::is_open() const
{
	return _db != nullptr;
};

void database::close()
{
	find_web_request.reset();
	find_folder_thumbnail.reset();
	find_thumbnail.reset();

	if (_db != nullptr)
	{
		df::assert_true(is_db_thread());
		sqlite3_close(_db);
		_db = nullptr;
	}
}

inline void metadata_packer::pack(const prop::item_metadata_ptr& md)
{
	reset_to_header();

	if (!prop::is_null(md->album)) write(prop::album.id, md->album);
	if (!prop::is_null(md->album_artist)) write(prop::album_artist.id, md->album_artist);
	if (!prop::is_null(md->artist)) write(prop::artist.id, md->artist);
	if (!prop::is_null(md->audio_codec)) write(prop::audio_codec.id, md->audio_codec);
	if (!prop::is_null(md->bitrate)) write(prop::bitrate.id, md->bitrate);
	if (!prop::is_null(md->camera_manufacturer)) write(prop::camera_manufacturer.id, md->camera_manufacturer);
	if (!prop::is_null(md->camera_model)) write(prop::camera_model.id, md->camera_model);
	if (!prop::is_null(md->location_place)) write(prop::location_place.id, md->location_place);
	if (!prop::is_null(md->comment)) write(prop::comment.id, md->comment);
	if (!prop::is_null(md->copyright_creator)) write(prop::copyright_creator.id, md->copyright_creator);
	if (!prop::is_null(md->copyright_credit)) write(prop::copyright_credit.id, md->copyright_credit);
	if (!prop::is_null(md->copyright_notice)) write(prop::copyright_notice.id, md->copyright_notice);
	if (!prop::is_null(md->copyright_source)) write(prop::copyright_source.id, md->copyright_source);
	if (!prop::is_null(md->copyright_url)) write(prop::copyright_url.id, md->copyright_url);
	if (!prop::is_null(md->location_country)) write(prop::location_country.id, md->location_country);
	if (!prop::is_null(md->description)) write(prop::description.id, md->description);
	if (!prop::is_null(md->file_name)) write(prop::file_name.id, md->file_name);
	if (!prop::is_null(md->raw_file_name)) write(prop::raw_file_name.id, md->file_name);
	if (!prop::is_null(md->genre)) write(prop::genre.id, md->genre);
	if (!prop::is_null(md->lens)) write(prop::lens.id, md->lens);
	if (!prop::is_null(md->pixel_format)) write(prop::pixel_format.id, md->pixel_format);
	if (!prop::is_null(md->show)) write(prop::show.id, md->show);
	if (!prop::is_null(md->location_state)) write(prop::location_state.id, md->location_state);
	if (!prop::is_null(md->synopsis)) write(prop::synopsis.id, md->synopsis);
	if (!prop::is_null(md->composer)) write(prop::composer.id, md->composer);
	if (!prop::is_null(md->encoder)) write(prop::encoder.id, md->encoder);
	if (!prop::is_null(md->publisher)) write(prop::publisher.id, md->publisher);
	if (!prop::is_null(md->performer)) write(prop::performer.id, md->performer);
	if (!prop::is_null(md->title)) write(prop::title.id, md->title);
	if (!prop::is_null(md->tags)) write(prop::tag.id, md->tags);
	if (!prop::is_null(md->video_codec)) write(prop::video_codec.id, md->video_codec);
	if (!prop::is_null(md->game)) write(prop::game.id, md->game);
	if (!prop::is_null(md->system)) write(prop::system.id, md->system);
	if (!prop::is_null(md->label)) write(prop::label.id, md->label);
	if (!prop::is_null(md->doc_id)) write(prop::doc_id.id, md->doc_id);

	if (!prop::is_null(md->width) || !prop::is_null(md->height))
		write(prop::dimensions.id,
		      df::xy32::make(md->width, md->height));
	if (!prop::is_null(md->iso_speed)) write(prop::iso_speed.id, md->iso_speed);
	if (!prop::is_null(md->focal_length)) write(prop::focal_length.id, md->focal_length);
	if (!prop::is_null(md->focal_length_35mm_equivalent))
		write(prop::focal_length_35mm_equivalent.id,
		      md->focal_length_35mm_equivalent);
	if (!prop::is_null(md->rating)) write(prop::rating.id, md->rating);
	if (!prop::is_null(md->audio_sample_rate)) write(prop::audio_sample_rate.id, md->audio_sample_rate);
	if (!prop::is_null(md->audio_sample_type)) write(prop::audio_sample_type.id, md->audio_sample_type);
	if (!prop::is_null(md->season)) write(prop::season.id, md->season);
	if (!prop::is_null(md->track)) write(prop::track_num.id, md->track);
	if (!prop::is_null(md->disk)) write(prop::disk_num.id, md->disk);
	if (!prop::is_null(md->duration)) write(prop::duration.id, md->duration);
	if (!prop::is_null(md->episode)) write(prop::episode.id, md->episode);
	if (!prop::is_null(md->exposure_time)) write(prop::exposure_time.id, md->exposure_time);
	if (!prop::is_null(md->f_number)) write(prop::f_number.id, md->f_number);

	if (!prop::is_null(md->created_exif)) write(prop::created_exif.id, md->created_exif.to_int64());
	if (!prop::is_null(md->created_digitized)) write(prop::created_digitized.id, md->created_digitized.to_int64());
	if (!prop::is_null(md->created_utc)) write(prop::created_utc.id, md->created_utc.to_int64());
	if (!prop::is_null(md->year)) write(prop::year.id, md->year);

	if (md->coordinate.is_valid())
	{
		write(prop::latitude.id, md->coordinate.latitude());
		write(prop::longitude.id, md->coordinate.longitude());
	}

	if (md->orientation != ui::orientation::top_left && md->orientation != ui::orientation::none)
	{
		const auto val = static_cast<uint8_t>(md->orientation);
		write(prop::orientation.id, val);
	}
}


void database::clean(const std::vector<df::file_path>& indexed_items) const
{
	df::assert_true(is_db_thread());

	const auto today = platform::now().to_days();

	transaction t(_db);
	const db_statement
		update_properties(_db, u8"update item_properties set last_indexed = ? where folder=? and name=?");

	for (const auto& i : indexed_items)
	{
		update_properties.bind(1, today);
		update_properties.bind(2, i.folder().text());
		update_properties.bind(3, i.name());
		update_properties.exec();
		update_properties.reset();
	}

	db_exec(_db, str::format(u8"DELETE FROM item_properties where last_indexed < {}", today - 30));
	db_exec(_db, str::format(u8"DELETE FROM web_service_cache where created_date < {}", today - 7));
	db_exec(
		_db,
		u8"DELETE FROM item_thumbnails WHERE NOT EXISTS (SELECT 1 FROM item_properties WHERE item_properties.name = item_thumbnails.name AND item_properties.folder = item_thumbnails.folder);");
}

void metadata_unpacker::unpack(const prop::item_metadata_ptr& md)
{
	prop::key_ref t;

	while ((t = read_type()) != prop::null)
	{
		if (t == prop::title) read_val(md->title);
		else if (t == prop::description) read_val(md->description);
		else if (t == prop::comment) read_val(md->comment);
		else if (t == prop::synopsis) read_val(md->synopsis);
		else if (t == prop::composer) read_val(md->composer);
		else if (t == prop::encoder) read_val(md->encoder);
		else if (t == prop::publisher) read_val(md->publisher);
		else if (t == prop::performer) read_val(md->performer);
		else if (t == prop::genre) read_val(md->genre);
		else if (t == prop::copyright_credit) read_val(md->copyright_credit);
		else if (t == prop::copyright_notice) read_val(md->copyright_notice);
		else if (t == prop::copyright_creator) read_val(md->copyright_creator);
		else if (t == prop::copyright_source) read_val(md->copyright_source);
		else if (t == prop::copyright_url) read_val(md->copyright_url);
		else if (t == prop::file_name) read_val(md->file_name);
		else if (t == prop::raw_file_name) read_val(md->raw_file_name);
		else if (t == prop::pixel_format) read_val(md->pixel_format);
		else if (t == prop::bitrate) read_val(md->bitrate);
		else if (t == prop::orientation)
		{
			uint8_t val;
			read_val(val);
			md->orientation = static_cast<ui::orientation>(val);
		}
		else if (t == prop::dimensions)
		{
			df::xy32 xy;
			read_val(xy);
			md->width = xy.x;
			md->height = xy.y;
		}
		else if (t == prop::year) read_val(md->year);
		else if (t == prop::rating) read_val(md->rating);
		else if (t == prop::audio_sample_rate) read_val(md->audio_sample_rate);
		else if (t == prop::audio_sample_type) read_val(md->audio_sample_type);
		else if (t == prop::audio_channels) read_val(md->audio_channels);
		else if (t == prop::season) read_val(md->season);
		else if (t == prop::episode) read_val(md->episode);
		else if (t == prop::disk_num) read_val(md->disk);
		else if (t == prop::track_num) read_val(md->track);
		else if (t == prop::duration) read_val(md->duration);
		else if (t == prop::created_utc) read_val(md->created_utc);
		else if (t == prop::created_exif) read_val(md->created_exif);
		else if (t == prop::created_digitized) read_val(md->created_digitized);
			//else if (t == prop::modified) item.info.file_modified = df::date_t::from_time_stamp(p->n);
			//else if (t == prop::file_size) item.info.size = df::file_size(p->d);
		else if (t == prop::exposure_time) read_val(md->exposure_time);
		else if (t == prop::f_number) read_val(md->f_number);
		else if (t == prop::focal_length) read_val(md->focal_length);
		else if (t == prop::focal_length_35mm_equivalent) read_val(md->focal_length_35mm_equivalent);
		else if (t == prop::iso_speed) read_val(md->iso_speed);
		else if (t == prop::latitude) read_val(md->coordinate._latitude);
		else if (t == prop::longitude) read_val(md->coordinate._longitude);
		else if (t == prop::location_country) read_val(md->location_country);
		else if (t == prop::location_state) read_val(md->location_state);
		else if (t == prop::location_place) read_val(md->location_place);
		else if (t == prop::camera_manufacturer) read_val(md->camera_manufacturer);
		else if (t == prop::camera_model) read_val(md->camera_model);
		else if (t == prop::lens) read_val(md->lens);
		else if (t == prop::video_codec) read_val(md->video_codec);
		else if (t == prop::audio_codec) read_val(md->audio_codec);
		else if (t == prop::album_artist) read_val(md->album_artist);
		else if (t == prop::artist) read_val(md->artist);
		else if (t == prop::album) read_val(md->album);
		else if (t == prop::show) read_val(md->show);
		else if (t == prop::tag) read_val(md->tags);
		else if (t == prop::game) read_val(md->game);
		else if (t == prop::system) read_val(md->system);
		else if (t == prop::label) read_val(md->label);
		else if (t == prop::doc_id) read_val(md->doc_id);
	}
}

void database::load_index_values()
{
	df::assert_true(is_db_thread());

	df::measure_ms ms(_state.stats.index_load_ms);

	df::folder_path last_group_name;
	db_items_t cached_items;
	cached_items.reserve(1000);

	const db_statement items(
		_db,
		u8"select folder, name, properties, crc, media_position, last_scanned from item_properties order by folder");

	while (items.read() && !df::is_closing)
	{
		const auto folder = df::folder_path(items.text(0));
		const auto name = str::cache(items.text(1));
		const auto properties = items.data(2);
		const auto crc = static_cast<uint32_t>(items.int32(3));
		const auto media_position = items.int32(4);
		const auto last_scanned = df::date_t(items.int64(5));

		if (last_group_name != folder)
		{
			if (!cached_items.empty())
			{
				std::ranges::sort(cached_items, [](const db_item_t& left, const db_item_t& right)
				{
					return icmp(left.path, right.path) < 0;
				});
				_state.merge_folder(last_group_name, cached_items);
			}

			cached_items.clear();
			last_group_name = folder;
		}

		const auto* const ft = files::file_type_from_name(name);

		if (ft->can_cache())
		{
			metadata_unpacker unpacker(properties);

			db_item_t i;
			i.path = name;
			i.metadata_scanned = last_scanned;
			i.crc32c = static_cast<uint32_t>(crc);

			const auto has_properties = properties.size > 0;
			const auto has_med_pos = media_position != 0;

			if (has_properties || has_med_pos)
			{
				i.metadata = std::make_shared<prop::item_metadata>();

				if (has_properties)
				{
					unpacker.unpack(i.metadata);
				}

				if (has_med_pos)
				{
					i.metadata->media_position = media_position;
				}
			}

			cached_items.emplace_back(std::move(i));
		}
	}

	if (!cached_items.empty())
	{
		std::ranges::sort(cached_items, [](const db_item_t& left, const db_item_t& right)
		{
			return icmp(left.path, right.path) < 0;
		});
		_state.merge_folder(last_group_name, cached_items);
		cached_items.clear();
	}

	_state.cache_load_complete();
}

database::db_thumbnail database::load_thumbnail(const df::file_path id) const
{
	df::assert_true(is_db_thread());

	find_thumbnail->bind(1, id.folder().text());
	find_thumbnail->bind(2, id.name());

	db_thumbnail result;

	while (find_thumbnail->read())
	{
		result.thumb = load_image_file(find_thumbnail->blob(0));
		result.last_indexed = df::date_t(find_thumbnail->int64(1));
		break;
	}

	find_thumbnail->reset();
	return result;
}

database::db_thumbnail database::load_folder_thumbnail(const str::cached folder) const
{
	df::assert_true(is_db_thread());

	db_thumbnail result;

	find_folder_thumbnail->bind(1, folder);

	while (find_folder_thumbnail->read())
	{
		result.thumb = load_image_file(find_folder_thumbnail->blob(0));
		result.last_indexed = df::date_t(find_folder_thumbnail->int64(1));
		break;
	}

	find_folder_thumbnail->reset();
	return result;
}

std::u8string database::web_service_cache(const std::u8string& key) const
{
	df::assert_true(is_db_thread());

	std::u8string result;
	find_web_request->bind(1, key);

	while (find_web_request->read())
	{
		result = find_web_request->text(0);
	}

	return result;
}

void database::web_service_cache(const std::u8string& key, const std::u8string& value) const
{
	df::assert_true(is_db_thread());

	const auto today = platform::now().to_days();

	const db_statement insert_web_request(
		_db, u8"insert or replace into web_service_cache (key, value, created_date) values (?, ?, ?)");
	insert_web_request.bind(1, key);
	insert_web_request.bind(2, value);
	insert_web_request.bind(3, today);
	insert_web_request.exec();
}

item_import_set database::load_item_imports()
{
	df::assert_true(is_db_thread());

	item_import_set results;
	const db_statement items(_db, u8"select name, modified, size, imported from item_imports");

	while (items.read() && !df::is_closing)
	{
		item_import i;
		i.name = str::cache(items.text(0));
		i.modified = df::date_t(items.int64(1));
		i.size = df::file_size(items.int64(2));
		i.imported = df::date_t(items.int64(3));

		results.emplace(i);
	}

	return results;
}

void database::writes_item_imports(const item_import_set& writes)
{
	df::assert_true(is_db_thread());

	transaction t(_db);
	const db_statement insert_properties(
		_db, u8"insert or replace into item_imports (name, modified, size, imported) values (?, ?, ?, ?)");

	for (const auto& i : writes)
	{
		insert_properties.bind(1, i.name);
		insert_properties.bind(2, i.modified.to_int64());
		insert_properties.bind(3, i.size.to_int64());
		insert_properties.bind(4, i.imported.to_int64());
		insert_properties.exec();
		insert_properties.reset();
	}
}

bool database::is_db_thread() const
{
	return _db_thread_id == platform::current_thread_id();
}

void database::load_thumbnails(const index_state& index, const df::item_set& items)
{
	df::assert_true(is_db_thread());

	for (const auto& i : items.folders())
	{
		if (df::is_closing)
			break;

		if (i->db_thumbnail_pending())
		{
			std::vector<df::folder_path> folders;
			folders.emplace_back(i->path());

			for (auto idx = 0u; idx < folders.size() && !i->has_thumb(); idx++)
			{
				auto folder = folders[idx];
				auto t = load_folder_thumbnail(folder.text());

				if (is_valid(t.thumb))
				{
					i->thumbnail(std::move(t.thumb), t.last_indexed);
					break;
				}

				if (folders.size() < 100)
				{
					for (const auto& f : platform::select_folders(df::item_selector(folder), setting.show_hidden))
					{
						folders.emplace_back(folder.combine(f.name));
					}
				}
			}

			i->db_thumb_query_complete();
		}
	}

	for (const auto& i : items.items())
	{
		if (df::is_closing)
			break;

		if (i->db_thumbnail_pending())
		{
			auto thumbnail = load_thumbnail(i->path());
			i->thumbnail(std::move(thumbnail.thumb), thumbnail.last_indexed);
			i->db_thumb_query_complete();
		}
	}
}


void database::perform_writes()
{
	df::assert_true(is_db_thread());
	perform_writes(_state.db_writes().dequeue_all());
}

void database::perform_writes(std::deque<item_db_write> writes)
{
	const auto today = platform::now().to_days();

	transaction t(_db);
	const db_statement insert_properties(
		_db,
		u8"insert or replace into item_properties (folder, name, properties, hash, crc, media_position, last_scanned, last_indexed) values (?, ?, ?, ?, ?, ?, ?, ?)");
	const db_statement update_metadata_scanned(
		_db, u8"insert or replace into item_properties (folder, name, last_scanned, last_indexed) values (?, ?, ?, ?)");
	const db_statement update_hash(_db, u8"update item_properties set hash = ? where folder=? and name=?");
	const db_statement update_crc(_db, u8"update item_properties set crc = ? where folder=? and name=?");
	const db_statement update_media_position(
		_db, u8"update item_properties set media_position = ? where folder=? and name=?");
	const db_statement insert_thumbnails(
		_db, u8"insert or replace into item_thumbnails (folder, name, bitmap, last_scanned) values (?, ?, ?, ?)");

	metadata_packer packer;

	for (auto&& write : writes)
	{
		const auto path = write.path;

		if (write.md.has_value())
		{
			const auto md = write.md.value();

			if (md)
			{
				packer.pack(md);
			}

			// df::log(__FUNCTION__, "write to db " << id.Name << " " << id.Modified.to_int64() << " " << properties_row.count();

			insert_properties.bind(1, path.folder().text());
			insert_properties.bind(2, path.name());
			insert_properties.bind(3, packer.cdata());
			insert_properties.bind(4, df::cspan{nullptr, 0});
			insert_properties.bind(5, static_cast<int>(write.crc32c.value_or(0)));
			insert_properties.bind(6, static_cast<int>(write.media_position.value_or(md->media_position)));
			insert_properties.bind(7, write.metadata_scanned.value_or(df::date_t()).to_int64());
			insert_properties.bind(8, today);
			insert_properties.exec();
			insert_properties.reset();

			write.crc32c.reset();
			write.metadata_scanned.reset();
			write.media_position.reset();

			++_state.stats.items_saved;
		}

		if (write.metadata_scanned.has_value())
		{
			update_metadata_scanned.bind(1, path.folder().text());
			update_metadata_scanned.bind(2, path.name());
			update_metadata_scanned.bind(3, write.metadata_scanned.value().to_int64());
			update_metadata_scanned.bind(4, today);
			update_metadata_scanned.exec();
			update_metadata_scanned.reset();
		}

		if (write.crc32c.has_value())
		{
			update_crc.bind(1, static_cast<int>(write.crc32c.value()));
			update_crc.bind(2, path.folder().text());
			update_crc.bind(3, path.name());
			update_crc.exec();
			update_crc.reset();
		}

		if (write.media_position.has_value())
		{
			update_media_position.bind(1, static_cast<int>(write.media_position.value()));
			update_media_position.bind(2, path.folder().text());
			update_media_position.bind(3, path.name());
			update_media_position.exec();
			update_media_position.reset();
		}

		if (write.thumb.has_value() && is_valid(write.thumb.value()))
		{
			insert_thumbnails.bind(1, path.folder().text());
			insert_thumbnails.bind(2, path.name());
			insert_thumbnails.bind(3, write.thumb.value()->data());
			insert_thumbnails.bind(4, write.thumb_scanned.has_value() ? write.thumb_scanned.value().to_int64() : 0);
			insert_thumbnails.exec();
			insert_thumbnails.reset();

			++_state.stats.thumbs_saved;
		}
	}

	_state.stats.database_size = platform::file_attributes(_db_path).size;
}

bool database::has_errors() const
{
	return db_fails > 0 && _state.indexing == 0;
}

void database::maintenance(bool is_reset)
{
	df::assert_true(is_db_thread());

	if (is_reset)
	{
		close();

		const auto delete_result = platform::delete_file(_db_path);

		if (delete_result.success())
		{
			db_fails = 0;
			_state.save_all_cached_items();
		}

		open();
	}

	db_exec(_db, u8"vacuum;");
	_state.stats.database_size = platform::file_attributes(_db_path).size;

	close();
	open();
}

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: SQLite database layer. Manages persistent storage of indexed file metadata,
// thumbnails, and application state using SQLite database.

#include "pch.h"

#include <Sqlite3.h>

#include "files.h"
#include "model_db.h"

#include "model.h"
#include "model_db_pack.h"
#include "model_index.h"

static std::atomic<int> db_fails = 0;

inline void db_trace_error(sqlite3* db, const std::string_view sql)
{
	const auto code = sqlite3_errcode(db);
	df::log(__FUNCTION__, std::format("Database error: {} [{}]", str::utf8_cast(sqlite3_errmsg(db)), sql));

	// Only count faults that say the database itself is unhealthy. Contention and cancellation
	// are normal under load, and counting them left has_errors() permanently true, which
	// recommends deleting a database that is perfectly good.
	switch (code)
	{
	case SQLITE_BUSY:
	case SQLITE_LOCKED:
	case SQLITE_INTERRUPT:
	case SQLITE_CONSTRAINT:
		break;
	default:
		db_fails.fetch_add(1);
		break;
	}
}

inline int db_exec(sqlite3* db, const std::string& sql)
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
	sqlite3* _db;
	sqlite3_stmt* _handle;

public:
	db_statement(sqlite3* db, const std::string& sql) : _db(db), _handle(nullptr)
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
				db_trace_error(_db, "sqlite3_finalize");
		}
	}

	void compile(const std::string_view sql)
	{
		const int ret = sqlite3_prepare_v2(_db, std::bit_cast<const char*>(sql.data()), static_cast<int>(sql.size()),
		                                   &_handle, nullptr);

		if (ret != SQLITE_OK)
			db_trace_error(_db, "sqlite3_prepare_v2");
	}

	// False when the statement failed to compile. Every accessor below is then a silent no-op,
	// so a caller that must not mistake "no rows" for "could not ask" has to check this.
	bool is_valid() const
	{
		return _handle != nullptr;
	}

	void bind(const int i, const std::string_view v) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_text(_handle, i, std::bit_cast<const char*>(v.data()),
			                                  static_cast<int>(v.size()),
			                                  SQLITE_STATIC);

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_text {} {}", i, v));
		}
	}

	void bind(const int i, const int32_t n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_int(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_int {} {}", i, n));
		}
	}

	void bind(const int i, const uint32_t n) const
	{
		if (_handle != nullptr)
		{
			// Check for overflow before casting
			if (n > static_cast<uint32_t>(INT_MAX))
			{
				db_trace_error(_db, std::format("sqlite3_bind_int overflow: {}", n));
				return;
			}

			const int ret = sqlite3_bind_int(_handle, i, static_cast<int>(n));

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_int {} {}", i, n));
		}
	}

	void bind(const int i, const double n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_double(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_double {} {}", i, n));
		}
	}

	void bind(const int i, const int64_t n) const
	{
		if (_handle != nullptr)
		{
			const int ret = sqlite3_bind_int64(_handle, i, n);

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_int64 {} {}", i, n));
		}
	}

	void bind(const int i, const uint64_t n) const
	{
		if (_handle != nullptr)
		{
			// Check for overflow before casting
			if (n > static_cast<uint64_t>(LLONG_MAX))
			{
				db_trace_error(_db, std::format("sqlite3_bind_int64 overflow: {}", n));
				return;
			}

			const int ret = sqlite3_bind_int64(_handle, i, static_cast<sqlite3_int64>(n));

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_int64 {} {}", i, n));
		}
	}

	void bind(const int i, const df::cspan cs) const
	{
		if (_handle != nullptr && !cs.empty())
		{
			const int ret = sqlite3_bind_blob(_handle, i, cs.data, static_cast<int>(cs.size), SQLITE_STATIC);

			if (ret != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_blob {}", i));
		}
	}

	void bind_null(const int i) const
	{
		if (_handle != nullptr)
		{
			if (sqlite3_bind_null(_handle, i) != SQLITE_OK)
				db_trace_error(_db, std::format("sqlite3_bind_null {}", i));
		}
	}

	void reset() const
	{
		if (_handle != nullptr)
		{
			if (sqlite3_reset(_handle) != SQLITE_OK)
				db_trace_error(_db, "sqlite3_reset");

			if (sqlite3_clear_bindings(_handle) != SQLITE_OK)
				db_trace_error(_db, "sqlite3_clear_bindings");
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
				db_trace_error(_db, "sqlite3_step");
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

	int int32(const int i) const
	{
		if (_handle != nullptr)
		{
			return sqlite3_column_int(_handle, i);
		}
		return 0;
	}

	int64_t int64(const int i) const
	{
		if (_handle != nullptr)
		{
			return sqlite3_column_int64(_handle, i);
		}
		return 0;
	}

	std::string text(const int i) const
	{
		if (_handle != nullptr)
		{
			const auto* text_ptr = sqlite3_column_text(_handle, i);
			if (text_ptr != nullptr)
			{
				return std::string(std::bit_cast<const char*>(text_ptr));
			}
		}
		return {};
	}

	df::blob blob(const int i) const
	{
		if (_handle != nullptr)
		{
			const auto* const pData = static_cast<const uint8_t*>(sqlite3_column_blob(_handle, i));
			const auto len = sqlite3_column_bytes(_handle, i);
			return {pData, pData + len};
		}
		return {};
	}

	df::cspan data(const int i) const
	{
		df::cspan r = {nullptr, 0};
		if (_handle != nullptr)
		{
			r.data = static_cast<const uint8_t*>(sqlite3_column_blob(_handle, i));
			r.size = sqlite3_column_bytes(_handle, i);
		}
		return r;
	}

};

class transaction
{
	sqlite3* _db;
	bool _acquired;

public:
	transaction(sqlite3* db, const bool start = true) : _db(db), _acquired(false)
	{
		if (start)
		{
			const int ret = db_exec(_db, "BEGIN TRANSACTION"s);
			_acquired = ret == SQLITE_OK;
		}
	}

	~transaction() noexcept
	{
		if (_acquired)
		{
			try
			{
				db_exec(_db, "COMMIT"s);
			}
			catch (...)
			{
				// Log error but don't throw from destructor
				try
				{
					db_trace_error(_db, "COMMIT failed in destructor");
				}
				catch (...)
				{
					// formatting the diagnostic must not terminate the process
				}
			}
		}
	}
};

df_assert_pod(item_import);
df_assert_move_only(db_item_t);
df_assert_move_only(item_db_write);

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

void database::open(const df::folder_path folder, const std::string_view file_name)
{
	sqlite3_initialize();

	df::scope_locked_inc l(_state.searching);
	_db_thread_id = platform::current_thread_id();
	_db_path = df::file_path(folder, file_name, ".db");
	open();
}

static std::string load_create_sql()
{
	auto sql = load_resource(platform::resource_item::sql);
	return std::string(std::bit_cast<const char*>(sql.data()), sql.size());
}

// Bump when a release changes what a scan records, so cached metadata written by an older build
// can no longer be trusted. 1: scanning stopped storing reverse-geocoded place text as though a
// file had carried it, and older rows cannot be told apart from genuine stored text.
constexpr int db_metadata_version = 1;

void database::upgrade_cached_metadata()
{
	df::assert_true(is_db_thread());

	int stored_version = 0;

	{
		const db_statement user_version(_db, "PRAGMA user_version"s);
		if (user_version.read()) stored_version = user_version.int32(0);
	}

	if (stored_version >= db_metadata_version)
	{
		return;
	}

	// Only the metadata is dropped. Thumbnails, hashes, playback positions, and import history
	// stay, so the re-index re-reads files but never re-encodes a thumbnail.
	// Stamping the version over an invalidation that failed would mark metadata this build cannot
	// trust as upgraded, and nothing would ever re-read it. The update is one statement, so a
	// failure leaves the rows untouched and the older version stamp in place to retry.
	if (db_exec(_db, "UPDATE item_properties SET properties = NULL, last_scanned = NULL;"s) != SQLITE_OK)
	{
		df::log(__FUNCTION__, std::format("Failed to invalidate cached metadata written by version {}",
		                                  stored_version));
		return;
	}

	db_exec(_db, std::format("PRAGMA user_version = {};", db_metadata_version));

	df::log(__FUNCTION__, std::format("Cached metadata invalidated: version {} -> {}", stored_version,
	                                  db_metadata_version));
}

// Memory-mapped reads replace a syscall and a page copy with a page fault. Measured 1.7-1.8x on
// random thumbnail reads, which is where nearly all of this database's bytes are. 32-bit builds get
// none: their 2 GB address space is already the scarce resource next to decoded images.
constexpr int64_t db_mmap_size = sizeof(void*) >= 8 ? 1LL << 30 : 0;

// True only for codes that mean the file itself cannot be read as a database. Busy, locked, I/O,
// permission, and disk-full faults are environmental: the database is probably intact and
// replacing it would destroy a cache that is perfectly good.
static bool is_unusable_db_file(const int result)
{
	switch (result & 0xff)
	{
	case SQLITE_CORRUPT:
	case SQLITE_NOTADB:
	case SQLITE_FORMAT:
		return true;
	default:
		return false;
	}
}

// WAL keeps its index in a memory-mapped -shm file shared between processes. Network shares, and
// some virtualised or redirected profile filesystems, cannot provide one, and the failure arrives
// as an I/O or open error on the first statement that needs a write transaction.
static bool is_shared_memory_failure(sqlite3* db)
{
	switch (sqlite3_extended_errcode(db))
	{
	case SQLITE_CANTOPEN:
	case SQLITE_IOERR_SHMOPEN:
	case SQLITE_IOERR_SHMSIZE:
	case SQLITE_IOERR_SHMLOCK:
	case SQLITE_IOERR_SHMMAP:
		return true;
	default:
		return false;
	}
}

// Every column this build names, one statement per table. These are compiled and discarded, never
// run. A file whose schema cannot answer them is not one this build can use: the index select
// would fail to prepare and load an empty index without saying so.
static bool schema_is_usable(sqlite3* db)
{
	static constexpr std::string_view statements[] = {
		"select folder, name, properties, hash, media_position, flag, crc, last_scanned, last_indexed from item_properties",
		"select folder, name, bitmap, cover_art, last_scanned from item_thumbnails",
		"select key, created_date, value from web_service_cache",
		"select name, modified, size, imported from item_imports",
	};

	for (const auto sql : statements)
	{
		sqlite3_stmt* handle = nullptr;
		const auto result = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &handle, nullptr);
		sqlite3_finalize(handle);

		if (result != SQLITE_OK)
		{
			df::log(__FUNCTION__,
			        std::format("Schema check failed: {} [{}]", str::utf8_cast(sqlite3_errmsg(db)), sql));
			return false;
		}
	}

	return true;
}

// Opens the connection only. False when the file cannot be opened at all, which is a path or
// permission fault that replacing the file would not fix.
bool database::connect()
{
	const auto rc = sqlite3_open(std::bit_cast<const char*>(_db_path.str().c_str()), &_db);

	if (rc != SQLITE_OK)
	{
		// Get error message before closing
		std::string error_msg;

		if (_db != nullptr)
		{
			error_msg = str::utf8_cast(sqlite3_errmsg(_db));
			sqlite3_close(_db);
		}
		else
		{
			error_msg = "Failed to allocate database connection"s;
		}

		_db = nullptr;
		df::log(__FUNCTION__, std::format("Failed to open database: {}", error_msg));
		return false;
	}

	sqlite3_busy_timeout(_db, 1000);
	return true;
}

// The database and its write-ahead siblings. The main file goes first: while it survives, the
// -wal still holds committed frames that belong to it, and removing that alone would lose them.
platform::file_op_result database::delete_database_files() const
{
	// A database that was never created is already in the state the caller is asking for, and
	// reporting a failed delete would block the reset that doubles as a retry of the open.
	if (!_db_path.exists())
	{
		return {platform::file_op_result_code::OK};
	}

	const auto result = platform::delete_file(_db_path);

	if (result.success())
	{
		// A leftover write-ahead log is replayed into whatever database file appears next, which
		// would restore the very content that could not be read.
		for (const auto suffix : {"-wal"sv, "-shm"sv})
		{
			const std::string sibling_name = std::string(_db_path.name()) + std::string(suffix);
			const auto sibling = df::file_path(_db_path.folder(), std::string_view(sibling_name));

			if (sibling.exists())
			{
				const auto sibling_result = platform::delete_file(sibling);

				if (sibling_result.failed())
				{
					df::log(__FUNCTION__, sibling_result.format_error(std::format("Failed to delete {}", sibling)));
				}
			}
		}
	}

	return result;
}

// Applies the schema and loads the index into memory. Returns false only when the file on disk is
// one this build cannot use and `can_replace` allows a fresh one to be created in its place.
// Everything else throws, so a transient fault never costs the user their cache.
bool database::prepare_database(const bool can_replace)
{
	// The first statement to touch the file is where an unreadable header surfaces, so this one
	// takes part in the replace decision too.
	const auto configure_result = db_exec(_db, "PRAGMA cache_size=-32768; PRAGMA trusted_schema=OFF;"s);

	if (SQLITE_OK != configure_result)
	{
		if (can_replace && is_unusable_db_file(configure_result))
		{
			return false;
		}

		throw app_exception("Failed to configure database connection"s);
	}

	if (db_mmap_size > 0)
	{
		db_exec(_db, std::format("PRAGMA mmap_size={};", db_mmap_size));
	}

	const auto sql = load_create_sql();
	auto create_result = db_exec(_db, sql);

	if (SQLITE_OK != create_result && is_shared_memory_failure(_db))
	{
		// A rollback journal is slower and blocks readers behind writers, but this build opens one
		// connection on one thread, so nothing here depends on what WAL adds. Keeping the index on
		// a filesystem that cannot host a WAL is worth more than the throughput.
		df::log(__FUNCTION__, std::format("Shared memory unavailable ({}) - retrying with a rollback journal",
		                                  str::utf8_cast(sqlite3_errmsg(_db))));

		db_exec(_db, "PRAGMA journal_mode = DELETE;"s);
		create_result = db_exec(_db, sql);
	}

	if (SQLITE_OK != create_result)
	{
		const auto message = std::format("Failed to create database: {}\n\nPath: {}",
		                                 str::utf8_cast(sqlite3_errmsg(_db)), _db_path);
		df::log(__FUNCTION__, message);

		if (can_replace && is_unusable_db_file(create_result))
		{
			return false;
		}

		throw app_exception(message);
	}

	{
		const db_statement journal_mode(_db, "PRAGMA journal_mode"s);
		const auto mode = journal_mode.read() ? journal_mode.text(0) : std::string{};

		if (mode != "wal")
		{
			// Recorded rather than refused. The index is a rebuildable cache, and a corrupt file is
			// now replaced on open, so the weaker crash guarantee costs a re-index at worst.
			df::log(__FUNCTION__, std::format("Journal mode is '{}' rather than wal", mode));
		}
	}

	db_exec(_db, "PRAGMA optimize=0x10002;"s);

	// The periodic run below is what keeps statistics current on a connection that lives for the
	// whole session; the open above has just done the work for today.
	_optimized_day = platform::now().to_days();

	// Schema upgrades must run before the index load, which selects the columns they add.
	// Otherwise that select fails to prepare and the whole index loads empty and silent.
	sqlite3_exec(_db, "ALTER TABLE item_properties ADD COLUMN crc INTEGER;", nullptr, nullptr, nullptr);
	sqlite3_exec(_db, "ALTER TABLE item_thumbnails ADD COLUMN cover_art BLOB NULL;", nullptr, nullptr, nullptr);

	// Those upgrades report nothing when they fail, so the schema they were meant to reach is
	// checked rather than assumed.
	if (!schema_is_usable(_db))
	{
		if (can_replace)
		{
			return false;
		}

		throw app_exception(std::format("Database schema cannot be used by this build\n\nPath: {}", _db_path));
	}

	upgrade_cached_metadata();

	load_index_values();
	df::log(__FUNCTION__, std::format("Loaded index in {} ms", _state.stats.index_load_ms));

	find_web_request = std::make_unique<db_statement>(_db, "select value from web_service_cache where key=?"s);
	find_folder_thumbnail = std::make_unique<db_statement>(
		_db, "select bitmap, cover_art, last_scanned from item_thumbnails where folder=?"s);
	find_thumbnail = std::make_unique<db_statement>(
		_db, "select bitmap, cover_art, last_scanned from item_thumbnails where folder=? AND name=?"s);

	_state.stats.database_size = platform::file_attributes(_db_path).size;
	_state.stats.database_path = _db_path;
	df::log(__FUNCTION__, std::format("Index open {}", _state.stats.database_size));

	return true;
}

void database::open()
{
	df::assert_true(is_db_thread());

	df::scope_locked_inc slc(df::jobs_running);

	df::log(__FUNCTION__, std::format("Open database: {}", _db_path));

	if (!connect())
	{
		return;
	}

	if (!prepare_database(true))
	{
		// The cache holds nothing that re-indexing cannot rebuild, so a file this build cannot
		// read is replaced. Refusing to start would leave the user no way back in, and the
		// only repair - resetting the index - lives behind the window that failed to open.
		df::log(__FUNCTION__, std::format("Replacing unusable database: {}", _db_path));

		close();

		const auto delete_result = delete_database_files();

		if (delete_result.failed())
		{
			throw app_exception(delete_result.format_error(
				std::format("Failed to replace an unusable database\n\nPath: {}", _db_path)));
		}

		if (!connect())
		{
			throw app_exception(std::format("Failed to create database\n\nPath: {}", _db_path));
		}

		prepare_database(false);

		// The faults counted while reading the old file are resolved by the replacement, and
		// leaving them would keep recommending a reset that has effectively already happened.
		db_fails.store(0);
	}
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
		db_exec(_db, "PRAGMA optimize;"s);

		// close_v2 hands ownership to sqlite even if something is still outstanding, so the handle
		// is always released here. Keeping a non-null _db would let maintenance() delete the file
		// and reopen over a live connection.
		const auto close_result = sqlite3_close_v2(_db);

		if (close_result != SQLITE_OK)
		{
			db_trace_error(_db, "sqlite3_close_v2"s);
		}

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
	if (!prop::is_null(md->raw_file_name)) write(prop::raw_file_name.id, md->raw_file_name);
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

	// Properties added after v1.26.4 are written last. That release stops unpacking at the first
	// id it does not recognise, so anything written before these would be lost when an older
	// build reads a database this one has written.
	if (!prop::is_null(md->altitude)) write(prop::altitude.id, md->altitude);
	if (!prop::is_null(md->gps_speed)) write(prop::gps_speed.id, md->gps_speed);
}


void database::clean(const std::vector<df::file_path>& indexed_items) const
{
	df::assert_true(is_db_thread());

	if (!is_open()) return;

	const auto today = platform::now().to_days();

	transaction t(_db);
	const db_statement
		update_properties(_db, "update item_properties set last_indexed = ? where folder=? and name=?"s);

	for (const auto& i : indexed_items)
	{
		update_properties.bind(1, today);
		update_properties.bind(2, i.folder().text());
		update_properties.bind(3, i.name());
		update_properties.exec();
		update_properties.reset();
	}

	const db_statement delete_old_properties(_db, "DELETE FROM item_properties where last_indexed < ?"s);
	delete_old_properties.bind(1, today - 30);
	delete_old_properties.exec();

	const db_statement delete_old_cache(_db, "DELETE FROM web_service_cache where created_date < ?"s);
	delete_old_cache.bind(1, today - 7);
	delete_old_cache.exec();

	db_exec(
		_db,
		"DELETE FROM item_thumbnails WHERE NOT EXISTS (SELECT 1 FROM item_properties WHERE item_properties.name = item_thumbnails.name AND item_properties.folder = item_thumbnails.folder);"s);
}

void metadata_unpacker::unpack(const prop::item_metadata_ptr& md)
{
	while (!at_end())
	{
		const prop::key_ref t = read_type();

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
			uint8_t val{};
			read_val(val);
			md->orientation = static_cast<ui::orientation>(val);
		}
		else if (t == prop::dimensions)
		{
			df::xy32 xy{};
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
		else if (t == prop::altitude) read_val(md->altitude);
		else if (t == prop::gps_speed) read_val(md->gps_speed);
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
		else skip_val(); // written by a newer build - step over it rather than truncate the record
	}
}

void database::load_index_values() const
{
	df::assert_true(is_db_thread());

	df::measure_ms ms(_state.stats.index_load_ms);

	df::folder_path last_group_name;
	db_items_t cached_items;
	cached_items.reserve(1000);

	const db_statement items(
		_db,
		"select folder, name, properties, crc, media_position, last_scanned from item_properties order by folder"s);

	if (!items.is_valid())
	{
		// Reading no rows here is indistinguishable from an empty collection, and the index would
		// go on to re-scan everything and then let clean() delete the rows it could not read.
		throw app_exception(std::format("Failed to read the index: {}\n\nPath: {}",
		                                str::utf8_cast(sqlite3_errmsg(_db)), _db_path));
	}

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

	db_thumbnail result;

	// close() clears the cached statements and open() can throw before recreating them, so a
	// failed reopen after a reset must degrade to no cached thumbnail rather than crash.
	if (!find_thumbnail) return result;

	find_thumbnail->bind(1, id.folder().text());
	find_thumbnail->bind(2, id.name());

	if (find_thumbnail->read())
	{
		result.thumb = load_image_file(find_thumbnail->blob(0));
		result.cover_art = load_image_file(find_thumbnail->blob(1));
		result.last_indexed = df::date_t(find_thumbnail->int64(2));
	}

	find_thumbnail->reset();
	return result;
}

database::db_thumbnail database::load_folder_thumbnail(const str::cached folder) const
{
	df::assert_true(is_db_thread());

	db_thumbnail result;

	if (!find_folder_thumbnail) return result;

	find_folder_thumbnail->bind(1, folder);

	if (find_folder_thumbnail->read())
	{
		result.thumb = load_image_file(find_folder_thumbnail->blob(0));
		result.cover_art = load_image_file(find_folder_thumbnail->blob(1));
		result.last_indexed = df::date_t(find_folder_thumbnail->int64(2));
	}

	find_folder_thumbnail->reset();
	return result;
}

std::string database::web_service_cache(const std::string_view key) const
{
	df::assert_true(is_db_thread());

	std::string result;

	if (!find_web_request) return result;

	find_web_request->bind(1, key);

	while (find_web_request->read())
	{
		result = find_web_request->text(0);
	}

	find_web_request->reset();

	return result;
}

void database::web_service_cache(const std::string_view key, const std::string_view value) const
{
	df::assert_true(is_db_thread());

	if (!is_open()) return;

	const auto today = platform::now().to_days();
	transaction t(_db);

	const db_statement insert_web_request(
		_db, "insert or replace into web_service_cache (key, value, created_date) values (?, ?, ?)"s);
	insert_web_request.bind(1, key);
	insert_web_request.bind(2, value);
	insert_web_request.bind(3, today);
	insert_web_request.exec();

	// Rows only age out when the day changes, so this pass is pure overhead on every other write.
	if (_web_cache_pruned_day != today)
	{
		_web_cache_pruned_day = today;

		const db_statement delete_old_cache(_db, "DELETE FROM web_service_cache where created_date < ?"s);
		delete_old_cache.bind(1, today - 7);
		delete_old_cache.exec();
	}

	// The row cap is a hard bound, so it is enforced on every write.
	db_exec(_db,
	        "DELETE FROM web_service_cache WHERE rowid IN (SELECT rowid FROM web_service_cache ORDER BY created_date DESC, rowid DESC LIMIT -1 OFFSET 1000);"s);
}

item_import_set database::load_item_imports() const
{
	df::assert_true(is_db_thread());

	item_import_set results;

	if (!is_open()) return results;

	const db_statement items(_db, "select name, modified, size, imported from item_imports"s);

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

void database::writes_item_imports(const item_import_set& writes) const
{
	df::assert_true(is_db_thread());

	if (!is_open()) return;

	transaction t(_db);
	const db_statement insert_properties(
		_db, "insert or replace into item_imports (name, modified, size, imported) values (?, ?, ?, ?)"s);

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

database::thumbnail_requests database::make_thumbnail_requests(const df::item_set& items)
{
	df::assert_true(ui::is_ui_thread());
	thumbnail_requests requests;
	requests.reserve(items.size());

	for (const auto& item : items.items())
	{
		requests.emplace_back(item, item->path(), item->folder(), item->is_folder(), item->has_thumb());
	}

	return requests;
}

void database::load_thumbnails(const index_state& index, const thumbnail_requests& requests) const
{
	df::assert_true(is_db_thread());
	df::bump(df::db_perf.read_batches);
	df::perf_timer timer(df::db_perf.read_us, &df::db_perf.read_max_us);
	bool cover_art_loaded = false;
	index_state::thumbnail_results results;
	results.reserve(requests.size());

	for (const auto& request : requests)
	{
		if (df::is_closing)
			break;

		if (request.is_folder)
		{
			std::vector<df::folder_path> folders;
			folders.emplace_back(request.folder);

			for (auto idx = 0u; idx < folders.size() && !request.has_thumbnail; idx++)
			{
				auto folder = folders[idx];
				auto loaded = load_folder_thumbnail(folder.text());

				if (is_valid(loaded.thumb) || is_valid(loaded.cover_art))
				{
					cover_art_loaded |= ui::is_valid(loaded.cover_art);
					results.emplace_back(request.lifetime, request.path, std::move(loaded.thumb),
					                     std::move(loaded.cover_art), loaded.last_indexed);
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
		}
		else
		{
			auto loaded = load_thumbnail(request.path);
			cover_art_loaded |= ui::is_valid(loaded.cover_art);
			results.emplace_back(request.lifetime, request.path, std::move(loaded.thumb),
			                     std::move(loaded.cover_art), loaded.last_indexed);
		}
	}

	df::bump(df::db_perf.thumbnails_read, results.size());
	index.publish_thumbnails(std::move(results), cover_art_loaded);
}

void database::perform_writes()
{
	df::assert_true(is_db_thread());

	// Dequeued even with nothing to write to. These carry encoded thumbnails, so leaving them to
	// pile up while the index scans would spend memory on writes that can never be made.
	perform_writes(_state.db_writes().dequeue_all());

	// SQLite documents this for connections that stay open, which this one does for the whole
	// session: statistics gathered when the index was empty would otherwise be all the planner ever
	// sees. It is nearly a no-op on the days it finds nothing to re-analyse.
	if (is_open())
	{
		const auto today = platform::now().to_days();

		if (_optimized_day != today)
		{
			_optimized_day = today;
			db_exec(_db, "PRAGMA optimize;"s);
		}
	}
}

void database::perform_writes(std::deque<item_db_write> writes) const
{
	// The worker calls this every second. Opening a transaction and compiling seven statements to
	// write nothing is the idle cost of the whole database layer.
	if (!is_open() || writes.empty()) return;

	const auto today = platform::now().to_days();
	df::bump(df::db_perf.write_batches);
	df::bump(df::db_perf.items_written, writes.size());
	df::perf_timer timer(df::db_perf.write_us, &df::db_perf.write_max_us);

	transaction t(_db);
	// A metadata write is a partial update: it carries properties and scan state, and only
	// sometimes a CRC or a playback position. Upserting the named columns keeps the ones this
	// write knows nothing about, where `insert or replace` used to overwrite them with zero.
	const db_statement insert_properties(
		_db,
		"insert into item_properties (folder, name, properties, crc, media_position, last_scanned, last_indexed) values (?, ?, ?, ?, ?, ?, ?) "
		"on conflict(folder, name) do update set properties = excluded.properties, "
		"crc = coalesce(excluded.crc, item_properties.crc), "
		"media_position = coalesce(excluded.media_position, item_properties.media_position), "
		"last_scanned = excluded.last_scanned, last_indexed = excluded.last_indexed"s);
	const db_statement update_metadata_scanned(
		_db,
		"update item_properties set last_scanned = ?, last_indexed = ? where folder = ? and name = ?"s);
	const db_statement update_hash(_db, "update item_properties set hash = ? where folder=? and name=?"s);
	const db_statement update_crc(_db, "update item_properties set crc = ? where folder=? and name=?"s);
	const db_statement update_media_position(
		_db, "update item_properties set media_position = ? where folder=? and name=?"s);
	const db_statement insert_thumbnails(
		_db,
		"insert or replace into item_thumbnails (folder, name, bitmap, cover_art, last_scanned) values (?, ?, ?, ?, ?)"s);

	metadata_packer packer;

	for (auto&& write : writes)
	{
		const auto path = write.path;

		if (write.md.has_value())
		{
			const auto md = write.md.value();

			// A null metadata pointer means "this item has no properties". The packed payload
			// must then be an empty record, not whatever the previous item left in the packer.
			packer.reset_to_header();

			if (md)
			{
				packer.pack(md);
			}

			// df::log(__FUNCTION__, "write to db " << id.Name << " " << id.Modified.to_int64() << " " << properties_row.count();

			insert_properties.bind(1, path.folder().text());
			insert_properties.bind(2, path.name());
			insert_properties.bind(3, packer.cdata());

			if (write.crc32c.has_value()) insert_properties.bind(4, static_cast<int>(write.crc32c.value()));
			else insert_properties.bind_null(4);

			const auto media_position = write.media_position.has_value()
				                            ? write.media_position
				                            : (md && md->media_position != 0.0
					                               ? std::optional{md->media_position}
					                               : std::nullopt);

			if (media_position.has_value()) insert_properties.bind(5, static_cast<int>(media_position.value()));
			else insert_properties.bind_null(5);

			insert_properties.bind(6, write.metadata_scanned.value_or(df::date_t()).to_int64());
			insert_properties.bind(7, today);
			insert_properties.exec();
			insert_properties.reset();

			write.crc32c.reset();
			write.metadata_scanned.reset();
			write.media_position.reset();

			++_state.stats.items_saved;
		}

		if (write.metadata_scanned.has_value())
		{
			update_metadata_scanned.bind(1, write.metadata_scanned.value().to_int64());
			update_metadata_scanned.bind(2, today);
			update_metadata_scanned.bind(3, path.folder().text());
			update_metadata_scanned.bind(4, path.name());
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
			insert_thumbnails.bind(4, write.cover_art.has_value() && is_valid(write.cover_art.value())
				                          ? write.cover_art.value()->data()
				                          : df::cspan{});
			insert_thumbnails.bind(5, write.thumb_scanned.has_value() ? write.thumb_scanned.value().to_int64() : 0);
			insert_thumbnails.exec();
			insert_thumbnails.reset();

			df::bump(df::db_perf.thumbs_written);
			++_state.stats.thumbs_saved;
		}
	}

	_state.stats.database_size = platform::file_attributes(_db_path).size;
}

bool database::has_errors() const
{
	return db_fails.load() > 0 && _state.indexing == 0;
}

void database::maintenance(const bool is_reset)
{
	df::assert_true(is_db_thread());

	if (is_reset)
	{
		close();

		const auto delete_result = delete_database_files();

		if (delete_result.failed())
		{
			// Reopening the database that could not be deleted leaves the user where they started,
			// which is the honest outcome. Reporting success would hide an index that never reset.
			open();

			throw app_exception(delete_result.format_error(
				std::format("Failed to reset the index\n\nPath: {}", _db_path)));
		}

		db_fails.store(0);

		// Writes queued against the old database carry the scan timestamps the rebuild is
		// about to invalidate. Flushing them would let a rebuild interrupted by shutdown
		// leave those files looking scanned on the next launch.
		_state.db_writes().dequeue_all();

		open();
	}

	if (_db == nullptr)
	{
		throw app_exception(std::format("Index database is not open\n\nPath: {}", _db_path));
	}

	db_exec(_db, "vacuum;"s);
	_state.stats.database_size = platform::file_attributes(_db_path).size;

	close();
	open();
}

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for the file layer (files*) -- format detection, codec decode and encode, container listing, and the write path: staging, replacement, originals, collisions, flush failure and temp cleanup.

#include "pch.h"
#include "files.h"
#include "test.h"
#include "util_zip.h"
#include "metadata_exif.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "app_util.h"

#include "webp/decode.h"
#include "webp/encode.h"
#include "webp/mux.h"
#include <zlib.h>

#define LIBHEIF_STATIC_BUILD 1
#include <libheif/heif.h>

static void should_check_overwrite()
{
	const auto src_path = df::file_path(test_files_folder, "Test.jpg");
	const auto dest_folder = test_files_folder;

	df::item_set items;
	const auto item = std::make_shared<df::item_element>(src_path, df::index_file_item{});
	items.add(item);

	const auto overwrites = check_overwrite(dest_folder, items, {});
	assert_equal(true, !overwrites.empty(), "should detect existing file");

	const auto no_overwrites = check_overwrite(dest_folder, items, ".xyz");
	assert_equal(true, no_overwrites.empty(), "should not detect with different extension");
}

static void should_report_zip_create_failure()
{
	df::zip_file zip;
	const auto missing_folder = _temps.folder().combine("missing");
	const auto path = missing_folder.combine_file("items.zip");
	assert_equal(false, zip.create(path), "zip create failure reported");
	assert_equal(false, path.exists(), "failed zip was not created");
}

class recording_file final : public platform::file
{
	df::file_path _path;
	df::blob _prefix;
	uint64_t _size = 0;
	mutable uint64_t _pos = 0;

public:
	mutable uint64_t max_read = 0;
	mutable uint64_t read_calls = 0;

	recording_file(df::file_path path, df::blob prefix, const uint64_t size) :
		_path(std::move(path)), _prefix(std::move(prefix)), _size(size)
	{
	}

	uint64_t size() const override { return _size; }

	uint64_t read(uint8_t* buf, const uint64_t buf_size) const override
	{
		const auto available = _pos < _size ? std::min(buf_size, _size - _pos) : 0;
		memset(buf, 0, static_cast<size_t>(available));

		if (_pos < _prefix.size())
		{
			const auto copied = std::min<uint64_t>(available, _prefix.size() - _pos);
			memcpy(buf, _prefix.data() + static_cast<size_t>(_pos), static_cast<size_t>(copied));
		}

		_pos += available;
		max_read = std::max(max_read, available);
		++read_calls;
		return available;
	}

	uint64_t write(const uint8_t*, uint64_t) override { return 0; }
	bool flush() const override { return true; }

	uint64_t seek(const uint64_t pos, const whence w) const override
	{
		switch (w)
		{
		case whence::begin:
			_pos = pos;
			break;
		case whence::current:
			_pos += pos;
			break;
		case whence::end:
			_pos = _size + pos;
			break;
		}
		return _pos;
	}

	uint64_t pos() const override { return _pos; }
	bool trunc(uint64_t) const override { return false; }
	df::file_path path() const override { return _path; }
};

// The copy loop stops when read64k answers false, which it does for the end of the file and for a
// read that failed alike - so a truncated copy used to close as a successful entry. What is written
// is now held to what the file holds. A real short read cannot be staged here without a fault
// injection seam, so what this pins is the other half: that a file crossing several buffer loads
// still satisfies the check rather than being refused as short.
static void should_add_a_multi_chunk_file_to_a_zip()
{
	const auto source = _temps.next_path(".bin");

	// Three buffer loads and a remainder, so the loop runs more than once and ends part way.
	df::blob payload(64u * 1024u * 3u + 517u);
	for (auto i = 0_z; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i * 7 + (i >> 11));
	df::blob_save_to_file(payload, source);

	const auto archive = _temps.next_path(".zip");
	df::zip_file zip;
	assert_equal(true, zip.create(archive), "the archive is created");
	assert_equal(true, zip.add(source, "payload.bin"), "a file spanning several reads is added whole");
	assert_equal(true, zip.close(), "and the archive closes");

	const auto listed = df::zip_file::list(archive);
	assert_equal(1_z, listed.size(), "the archive holds the one entry");
	assert_equal(static_cast<uint64_t>(payload.size()), listed.front().uncompressed_size.to_int64(),
	             "and it holds every byte of the source");
}

static void should_round_trip_zip_entry_names()
{
	const auto source = _temps.next_path(".bin");
	const df::blob payload = {1, 2, 3};
	df::blob_save_to_file(payload, source);

	const auto check_name = [&](const std::string& name)
	{
		const auto archive = _temps.next_path(".zip");
		df::zip_file zip;
		assert_equal(true, zip.create(archive), "the archive is created");
		assert_equal(true, zip.add(source, name, df::date_t(2024, 1, 2, 3, 4, 6)), "the long-name entry is added");
		assert_equal(true, zip.close(), "the archive closes");

		const auto listed = df::zip_file::list(archive);
		assert_equal(1_z, listed.size(), "the archive holds one long-name entry");
		assert_equal(name.size(), listed.front().filename.size(), "the complete entry name length is listed");
		assert_equal(true, listed.front().filename == name, "the complete entry name is listed exactly");
	};

	check_name(std::string(255, 'a'));
	check_name(std::string(256, 'b'));
	check_name(std::string(257, 'c'));
	check_name(std::string(UINT16_MAX, 'd'));

	df::zip_file zip;
	assert_equal(true, zip.create(_temps.next_path(".zip")), "the archive for an oversized name is created");
	assert_equal(false, zip.add(source, std::string(static_cast<size_t>(UINT16_MAX) + 1u, 'e'),
	                            df::date_t(2024, 1, 2, 3, 4, 6)),
	             "an entry name beyond the ZIP 16-bit length is refused");
}

static void should_round_trip_zip_entry_dates()
{
	const auto source = _temps.next_path(".bin");
	const df::blob payload = {1, 2, 3};
	df::blob_save_to_file(payload, source);

	const std::array dates = {
		df::date_t(2024, 1, 31, 23, 58, 56),
		df::date_t(2024, 12, 31, 1, 2, 4),
		df::date_t(2024, 2, 29, 6, 8, 10),
		df::date_t(1975, 6, 15, 12, 0, 0),
		df::date_t(1601, 1, 1, 0, 0, 0),
	};

	const std::array expected = {
		df::date_t(2024, 1, 31, 23, 58, 56),
		df::date_t(2024, 12, 31, 1, 2, 4),
		df::date_t(2024, 2, 29, 6, 8, 10),
		df::date_t(1980, 1, 1, 0, 0, 0).local_to_system(),
		df::date_t(1980, 1, 1, 0, 0, 0).local_to_system(),
	};

	const auto archive = _temps.next_path(".zip");
	df::zip_file zip;
	assert_equal(true, zip.create(archive), "the date archive is created");

	for (auto i = 0_z; i < dates.size(); ++i)
	{
		assert_equal(true, zip.add(source, std::format("dated-{}.bin", i), dates[i]), "the dated entry is added");
	}

	assert_equal(true, zip.close(), "the date archive closes");

	const auto listed = df::zip_file::list(archive);
	assert_equal(dates.size(), listed.size(), "every dated entry is present");

	for (auto i = 0_z; i < dates.size(); ++i)
	{
		assert_equal(expected[i].date().year, listed[i].created.date().year, "zip date year");
		assert_equal(expected[i].date().month, listed[i].created.date().month, "zip date month");
		assert_equal(expected[i].date().day, listed[i].created.date().day, "zip date day");
		assert_equal(expected[i].date().hour, listed[i].created.date().hour, "zip date hour");
		assert_equal(expected[i].date().minute, listed[i].created.date().minute, "zip date minute");
		assert_equal(expected[i].date().second, listed[i].created.date().second, "zip date second");
	}
}

static void should_create_original_before_replace()
{
	const auto destination = _temps.next_path(".jpg");
	const auto replacement = _temps.next_path(".jpg");
	const df::blob original = {1, 2, 3};
	const df::blob updated = {4, 5, 6};
	df::blob_save_to_file(original, destination);
	df::blob_save_to_file(updated, replacement);

	const auto result = platform::replace_file(destination, replacement, true);
	const auto original_path = df::file_path(destination.folder(),
	                                         std::string(destination.file_name_without_extension()) + ".original",
	                                         destination.extension());
	assert_equal(true, result.success(), "replacement with backup succeeds");
	assert_equal(true, original_path.exists(), "original backup exists");
	assert_equal(true, df::blob_from_file(original_path) == original, "backup contains original bytes");
	assert_equal(true, result.coherent_handle != nullptr, "replacement returns coherent handle");
	df::blob actual(updated.size());
	result.coherent_handle->seek(0, platform::file::whence::begin);
	actual.resize(static_cast<size_t>(result.coherent_handle->read(actual.data(), actual.size())));
	assert_equal(true, actual == updated, "destination contains updated bytes");
	platform::delete_file(original_path);
}

static void should_report_move_or_copy_collision_paths()
{
	const auto root = _temps.folder().combine(std::format("move-copy-{}", platform::tick_count()));
	const auto source = root.combine("source");
	const auto target = root.combine("target");
	const auto source_folder = source.combine("album");
	const auto occupied_folder = target.combine("album");
	platform::create_folder(source_folder);
	platform::create_folder(occupied_folder);

	const auto source_file = source.combine_file("photo.txt");
	const auto occupied_file = target.combine_file("photo.txt");
	const df::blob contents = {1};
	df::blob_save_to_file(contents, source_file);
	df::blob_save_to_file(contents, occupied_file);
	df::blob_save_to_file(contents, source_folder.combine_file("inside.txt"));

	const auto result = platform::move_or_copy({source_file}, {source_folder}, target, false);
	assert_equal(true, result.success(), "copy with collisions succeeds");
	assert_equal(uint64_t{1}, static_cast<uint64_t>(result.created_files.files.size()), "one created file reported");
	assert_equal(uint64_t{1}, static_cast<uint64_t>(result.created_files.folders.size()), "one created folder reported");
	assert_equal(true, result.created_files.files.front() != occupied_file, "renamed file path reported");
	assert_equal(true, result.created_files.folders.front() != occupied_folder, "renamed folder path reported");
	assert_equal(true, result.created_files.files.front().exists(), "reported file exists");
	assert_equal(true, result.created_files.folders.front().exists(), "reported folder exists");
	platform::delete_items({}, {root}, false);
}

#ifndef _WIN32
static void should_refuse_linux_copy_identity_overwrites()
{
	const auto root = _temps.next_folder("linux-copy-identity");
	const auto source = root.combine_file("source.bin");
	const auto alias = root.combine_file("alias.bin");
	const df::blob bytes = {1, 2, 3, 4};
	df::blob_save_to_file(bytes, source);
	std::filesystem::create_hard_link(platform::to_stream_path(source), platform::to_stream_path(alias));

	const auto same_path = platform::copy_file(source, source, false, false);
	assert_equal(true, same_path.failed(), "copy to self fails");
	assert_equal(true, df::blob_from_file(source) == bytes, "copy to self preserves source");

	const auto hard_link = platform::copy_file(source, alias, false, false);
	assert_equal(true, hard_link.failed(), "copy to hard-link alias fails");
	assert_equal(true, df::blob_from_file(source) == bytes, "hard-link source bytes survive");
	assert_equal(true, df::blob_from_file(alias) == bytes, "hard-link destination bytes survive");
}

static void should_report_linux_copy_flush_failures()
{
	const auto root = _temps.next_folder("linux-copy-flush");
	const auto source = root.combine_file("source.bin");
	const auto destination = root.combine_file("destination.bin");
	const df::blob source_bytes = {9, 8, 7, 6, 5, 4};
	const df::blob original_destination = {1, 2, 3};
	df::blob_save_to_file(source_bytes, source);
	df::blob_save_to_file(original_destination, destination);

	platform::linux_copy_file_test_failures partial;
	partial.max_write_bytes = 2;
	partial.interrupt_first_write = true;
	platform::test_linux_copy_file_failures(partial);
	const auto partial_result = platform::copy_file(source, root.combine_file("partial.bin"), true, false);
	platform::test_linux_clear_copy_file_failures();
	assert_equal(true, partial_result.success(), "partial and interrupted writes complete");
	assert_equal(true, df::blob_from_file(root.combine_file("partial.bin")) == source_bytes,
	             "partial write copy has every byte");

	platform::linux_copy_file_test_failures write_failure;
	write_failure.fail_write_after_calls = 0;
	platform::test_linux_copy_file_failures(write_failure);
	const auto write_result = platform::copy_file(source, destination, false, false);
	platform::test_linux_clear_copy_file_failures();
	assert_equal(true, write_result.failed(), "write failure is reported");
	assert_equal(true, df::blob_from_file(destination) == original_destination,
	             "write failure preserves prior destination");

	platform::linux_copy_file_test_failures close_failure;
	close_failure.fail_destination_close = true;
	platform::test_linux_copy_file_failures(close_failure);
	const auto close_result = platform::copy_file(source, destination, false, false);
	platform::test_linux_clear_copy_file_failures();
	assert_equal(true, close_result.failed(), "late close failure is reported");
	assert_equal(true, df::blob_from_file(destination) == original_destination,
	             "late close failure preserves prior destination");
}

static void should_claim_linux_auto_rename_destinations_atomically()
{
	const auto root = _temps.next_folder("linux-auto-rename");
	const auto source_folder = root.combine("source");
	const auto target = root.combine("target");
	platform::create_folder(source_folder);
	platform::create_folder(target);

	const auto source = source_folder.combine_file("photo.txt");
	const df::blob source_bytes = {7};
	const df::blob competing_bytes = {3};
	df::blob_save_to_file(source_bytes, source);

	bool competed = false;
	platform::test_linux_before_claim_file_path = [&](const df::file_path candidate)
	{
		if (!competed)
		{
			competed = true;
			df::blob_save_to_file(competing_bytes, candidate);
		}
	};

	const auto result = platform::move_or_copy({source}, {}, target, false);
	platform::test_linux_before_claim_file_path = {};

	assert_equal(true, result.success(), "copy retries after a raced file candidate");
	assert_equal(1_z, result.created_files.files.size(), "one copied file is reported");
	assert_equal(true, result.created_files.files.front() != target.combine_file("photo.txt"),
	             "the raced name was not reported as copied");
	assert_equal(true, df::blob_from_file(target.combine_file("photo.txt")) == competing_bytes,
	             "competing file bytes survive");
	assert_equal(true, df::blob_from_file(result.created_files.files.front()) == source_bytes,
	             "reported file has source bytes");

	const auto replace_source = source_folder.combine_file("replace.txt");
	const auto replace_target = target.combine_file("replace.txt");
	df::blob_save_to_file(source_bytes, replace_source);
	df::blob_save_to_file(competing_bytes, replace_target);
	const auto replace = platform::move_or_copy({replace_source}, {}, target, false, true);
	assert_equal(true, replace.success(), "explicit replace still succeeds");
	assert_equal(true, df::blob_from_file(replace_target) == source_bytes, "replace writes source bytes");
}

static void should_reject_linux_recursive_copy_into_descendant()
{
	const auto root = _temps.next_folder("linux-descendant-copy");
	const auto source = root.combine("source");
	const auto child = source.combine("child");
	platform::create_folder(child);
	df::blob_save_to_file(df::blob{1}, source.combine_file("photo.txt"));

	const auto result = platform::move_or_copy({}, {source}, child, false);
	assert_equal(true, result.failed(), "copy into descendant fails");
	assert_equal(false, platform::exists(child.combine("source")), "descendant target is not created");
	assert_equal(true, platform::exists(source.combine_file("photo.txt")), "source remains available");
}

static void should_delete_linux_directory_symlinks()
{
	const auto root = _temps.next_folder("linux-delete-symlink");
	const auto source = root.combine("source");
	const auto target = root.combine("target");
	const auto link = source.combine("linked");
	platform::create_folder(source);
	platform::create_folder(target);
	df::blob_save_to_file(df::blob{1}, source.combine_file("file.txt"));
	df::blob_save_to_file(df::blob{2}, target.combine_file("target.txt"));
	std::filesystem::create_directory_symlink(platform::to_stream_path(target), platform::to_stream_path(link));

	const auto result = platform::delete_items({}, {source}, false);
	assert_equal(true, result.success(), std::format("delete succeeds ({})", result.format_error()));
	assert_equal(false, platform::exists(source), "source folder is removed");
	assert_equal(true, platform::exists(target.combine_file("target.txt")), "symlink target survives");
}

static void should_move_or_copy_linux_directory_symlinks()
{
	const auto root = _temps.next_folder("linux-copy-symlink");
	const auto source = root.combine("source");
	const auto target = root.combine("target");
	const auto link_target = root.combine("linked-target");
	platform::create_folder(source);
	platform::create_folder(target);
	platform::create_folder(link_target);
	std::filesystem::create_directory_symlink(platform::to_stream_path(link_target),
	                                          platform::to_stream_path(source.combine("linked")));

	const auto result = platform::move_or_copy({}, {source}, target, false);
	assert_equal(true, result.success(), std::format("copy succeeds ({})", result.format_error()));
	assert_equal(1_z, result.created_files.folders.size(), "one folder reported");
	const auto copied_link = result.created_files.folders.front().combine("linked");
	assert_equal(true, std::filesystem::is_symlink(platform::to_stream_path(copied_link)),
	             "directory symlink is recreated");
	assert_equal(platform::to_stream_path(link_target).string(),
	             std::filesystem::read_symlink(platform::to_stream_path(copied_link)).string(),
	             "link target spelling is preserved");
}

static void should_move_or_copy_into_its_own_folder_with_auto_rename()
{
	const auto root = _temps.next_folder("linux-copy-self-parent");
	const auto file = root.combine_file("photo.txt");
	const auto folder = root.combine("album");
	df::blob_save_to_file(df::blob{7}, file);
	platform::create_folder(folder);
	df::blob_save_to_file(df::blob{8}, folder.combine_file("inside.txt"));

	const auto file_result = platform::move_or_copy({file}, {}, root, false);
	assert_equal(true, file_result.success(), std::format("file copy succeeds ({})", file_result.format_error()));
	assert_equal(1_z, file_result.created_files.files.size(), "one copied file reported");
	assert_equal(true, file_result.created_files.files.front() != file, "copy uses a new file name");
	assert_equal(true, df::blob_from_file(file_result.created_files.files.front()) == df::blob{7},
	             "copied file has the source bytes");

	const auto folder_result = platform::move_or_copy({}, {folder}, root, false);
	assert_equal(true, folder_result.success(), std::format("folder copy succeeds ({})", folder_result.format_error()));
	assert_equal(1_z, folder_result.created_files.folders.size(), "one copied folder reported");
	assert_equal(true, folder_result.created_files.folders.front() != folder, "copy uses a new folder name");
	assert_equal(true, platform::exists(folder_result.created_files.folders.front().combine_file("inside.txt")),
	             "copied folder has the source contents");
}

static void should_move_or_copy_stop_on_inner_linux_folder_collision()
{
	const auto root = _temps.next_folder("linux-inner-collision");
	const auto source = root.combine("album");
	const auto target = root.combine("target");
	platform::create_folder(source);
	platform::create_folder(target);
	df::blob_save_to_file(df::blob{5}, source.combine_file("inside.txt"));

	bool competed = false;
	platform::test_linux_before_copy_folder_file_path = [&](const df::file_path candidate)
	{
		if (!competed)
		{
			competed = true;
			df::blob_save_to_file(df::blob{6}, candidate);
		}
	};

	const auto result = platform::move_or_copy({}, {source}, target, false);
	platform::test_linux_before_copy_folder_file_path = {};

	assert_equal(true, result.failed(), "inner collision fails the claimed tree");
	assert_equal(false, platform::exists(target.combine("album (2)")), "inner collision does not retry the top folder");
	assert_equal(true, df::blob_from_file(target.combine("album").combine_file("inside.txt")) == df::blob{6},
	             "competing inner file survives");
}

static void should_move_linux_no_replace_without_renameat_or_links()
{
	const auto root = _temps.next_folder("linux-move-fallback");
	const auto source = root.combine_file("source.txt");
	const auto destination = root.combine_file("destination.txt");
	const df::blob bytes = {4, 3, 2};
	df::blob_save_to_file(bytes, source);

	platform::test_linux_move_file_failures({true, true});
	const auto result = platform::move_file(source, destination, true);
	platform::test_linux_clear_move_file_failures();

	assert_equal(true, result.success(), std::format("fallback move succeeds ({})", result.format_error()));
	assert_equal(false, platform::exists(source), "source is removed after the exclusive copy");
	assert_equal(true, df::blob_from_file(destination) == bytes, "destination has source bytes");
}

static void should_refuse_or_preserve_linux_replace_permissions()
{
	const auto root = _temps.next_folder("linux-copy-permissions");
	const auto source = root.combine_file("source.txt");
	const auto read_only = root.combine_file("read-only.txt");
	const auto private_file = root.combine_file("private.txt");
	const df::blob source_bytes = {9};
	const df::blob original_bytes = {1};
	df::blob_save_to_file(source_bytes, source);
	df::blob_save_to_file(original_bytes, read_only);
	df::blob_save_to_file(original_bytes, private_file);

	namespace fs = std::filesystem;
	fs::permissions(platform::to_stream_path(read_only),
	                fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
	                fs::perm_options::replace);
	const auto refused = platform::copy_file(source, read_only, false, false);
	assert_equal(true, refused.failed(), "read-only destination refuses replace");
	assert_equal(true, df::blob_from_file(read_only) == original_bytes, "read-only destination bytes survive");

	fs::permissions(platform::to_stream_path(private_file), fs::perms::owner_read | fs::perms::owner_write,
	                fs::perm_options::replace);
	const auto replaced = platform::copy_file(source, private_file, false, false);
	assert_equal(true, replaced.success(), std::format("replace succeeds ({})", replaced.format_error()));
	const auto mode = fs::status(platform::to_stream_path(private_file)).permissions() & fs::perms::all;
	assert_equal(true, mode == (fs::perms::owner_read | fs::perms::owner_write),
	             "replace preserves destination permissions");
}

static void should_move_or_copy_reject_linux_destination_alias_inside_source()
{
	const auto root = _temps.next_folder("linux-copy-alias-descendant");
	const auto source = root.combine("source");
	const auto nested = source.combine("a").combine("b");
	const auto target = root.combine("target-link");
	platform::create_folder(nested);
	df::blob_save_to_file(df::blob{1}, source.combine_file("top.txt"));
	df::blob_save_to_file(df::blob{2}, source.combine("a").combine_file("mid.txt"));
	std::filesystem::create_directory_symlink(platform::to_stream_path(nested), platform::to_stream_path(target));

	const auto result = platform::move_or_copy({}, {source}, target, false);
	assert_equal(true, result.failed(), "alias into source is rejected");
	assert_equal(false, platform::exists(nested.combine("source")), "copy does not create output inside source");
	assert_equal(true, platform::exists(source.combine_file("top.txt")), "source remains available");
}
#endif

static void should_fail_replace_when_flush_fails()
{
	const auto result = platform::replacement_flush_result(false, "flush failed");
	assert_equal(true, result.failed(), "failed flush stops replacement");
	assert_equal("flush failed", result.error_message, "flush error preserved");

#ifdef _WIN32
	// Holding the replacement open is what makes the flush fail, and that is Windows' mandatory
	// locking. A POSIX open descriptor denies nothing, so there is no failure here to observe.
	const auto destination = _temps.next_path(".bin");
	const auto replacement = _temps.next_path(".bin");
	const df::blob original = {1, 2, 3};
	const df::blob updated = {4, 5, 6};
	df::blob_save_to_file(original, destination);
	df::blob_save_to_file(updated, replacement);
	auto locked = platform::open_file(replacement, platform::file_open_mode::read_write);
	assert_equal(true, locked != nullptr, "replacement locked");

	const auto real_result = platform::replace_file(destination, replacement);
	locked.reset();
	assert_equal(true, real_result.failed(), "real flush failure stops replacement");
	assert_equal(true, !real_result.error_message.empty(), "real flush error reported");
	assert_equal(true, df::blob_from_file(destination) == original, "destination unchanged after flush failure");
#endif
}

#ifdef _WIN32
// The failure this cleans up after is induced by holding the destination open, which denies the
// write only where locking is mandatory. POSIX has no equivalent, so the failure cannot be staged
// here and the cleanup it proves has no way to be observed.
static void should_cleanup_failed_update_temps()
{
	const auto src_path = df::file_path(test_files_folder, "Test.jpg");
	// A private folder: enumerating the shared suite temp folder would scan every file every other
	// test has left there.
	const auto scratch = _temps.next_folder("update-temps");
	const auto destination = _temps.next_path_in(scratch, ".jpg");
	platform::copy_file(src_path, destination, false, false);
	std::vector<str::cached> files_before;
	for (const auto& file : platform::iterate_file_items(scratch, false).files)
	{
		files_before.emplace_back(file.name);
	}

	auto locked = platform::open_file(destination, platform::file_open_mode::read_write);
	assert_equal(true, locked != nullptr, "destination locked");

	metadata_edits edits;
	edits.rating = 3;
	files ff;
	const auto result = ff.update(destination, edits, {}, {}, false, {});
	locked.reset();

	assert_equal(true, result.failed(), "locked update fails");
	const auto contents = platform::iterate_file_items(scratch, false);
	const auto leaked = std::ranges::any_of(contents.files, [&files_before](const platform::file_info& file)
	{
		return std::ranges::find(files_before, file.name) == files_before.end() &&
			str::starts(file.name, "diffractor_");
	});
	assert_equal(false, leaked, "failed update removes temporary files");
}
#endif

static void should_settle_transport_stream_extension_by_header()
{
	assert_equal(true, files::has_media_header_rule(".ts"), "the transport stream extension has a header rule");
	assert_equal(true, files::has_media_header_rule("m2ts"), "the rule ignores a leading dot");
	assert_equal(false, files::has_media_header_rule(".mp4"), "an unambiguous extension is left to the decoder");

	std::array<uint8_t, files::media_header_probe_bytes> header{};

	const auto write_packets = [&header](const size_t start, const size_t packet_size)
	{
		header.fill(0);
		for (auto i = size_t{0}; i < 4; ++i) header[start + i * packet_size] = 0x47;
	};

	write_packets(0, 188);
	assert_equal(true, files::media_header_matches(".ts", {header.data(), header.size()}),
	             "a broadcast packet run is accepted");

	write_packets(4, 192);
	assert_equal(true, files::media_header_matches(".m2ts", {header.data(), header.size()}),
	             "an M2TS timestamp prefix is accepted");

	write_packets(0, 204);
	assert_equal(true, files::media_header_matches(".ts", {header.data(), header.size()}),
	             "a Reed-Solomon packet run is accepted");

	// A capture that begins mid-packet still aligns further in, and ffmpeg would find it, so refusing
	// it here would hide real video.
	write_packets(97, 188);
	assert_equal(true, files::media_header_matches(".ts", {header.data(), header.size()}),
	             "a stream that starts mid-packet is accepted");

	// A TypeScript file that opens with 'G' (0x47) matches the sync byte but not the packet run.
	const std::string_view typescript = "Get the exported type before anything else is imported;\n";
	header.fill(0);
	std::memcpy(header.data(), typescript.data(), typescript.size());
	assert_equal(false, files::media_header_matches(".ts", {header.data(), typescript.size()}),
	             "TypeScript source is not mistaken for a transport stream");

	assert_equal(true, files::media_header_matches(".mp4", {header.data(), typescript.size()}),
	             "an extension with no rule always matches");
}

static df::blob make_test_xmp_packet(const std::string_view body)
{
	const auto xml = std::format(
		"<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
		"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
		"xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
		"<rdf:Description rdf:about=\"\" "
		"xmlns:dc=\"http://purl.org/dc/elements/1.1/\">{}</rdf:Description>"
		"</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>",
		body);
	return {std::bit_cast<const uint8_t*>(xml.data()), std::bit_cast<const uint8_t*>(xml.data() + xml.size())};
}

static ui::surface_ptr make_gradient_surface(int cx, int cy);

static void should_fail_webp_save_when_metadata_chunk_insertion_fails()
{
	files ff;
	const auto surface = make_gradient_surface(16, 16);
	file_encode_params params;
	params.webp_lossless = true;

	const auto assert_failed_chunk = [&](const std::string_view fourcc, const metadata_parts& metadata,
	                                     const ui::const_surface_ptr& source)
	{
		files_test_hooks::fail_next_webp_chunk(fourcc);
		const auto saved = save_webp(source, metadata, params);
		assert_equal(false, is_valid(saved), std::format("{} insertion failure fails the save", fourcc));
	};

	metadata_parts icc;
	icc.icc = {1, 2, 3, 4};
	assert_failed_chunk("ICCP", icc, surface);

	metadata_parts exif;
	exif.exif = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 0x2a, 0};
	assert_failed_chunk("EXIF", exif, surface);

	metadata_parts orientation_metadata;
	const auto rotated = std::make_shared<ui::surface>();
	rotated->copy(*surface, surface->dimensions());
	rotated->orientation(ui::orientation::right_top);
	assert_failed_chunk("EXIF", orientation_metadata, rotated);

	metadata_parts xmp;
	xmp.xmp = make_test_xmp_packet(
		"<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">webp</rdf:li></rdf:Alt></dc:title>");
	assert_failed_chunk("XMP ", xmp, surface);
}

static void should_scan_d64()
{
	constexpr auto file_name = "Ace of Aces (Europe).D64";
	const auto load_path = test_files_folder.combine("retro").combine_file(file_name);
	const auto loaded = df::blob_from_file(load_path);
	const auto contents = files::list_disk(loaded);

	assert_equal(4_z, contents.size(), "d64", file_name);
	assert_equal("147 \"ACE OF ACES+    \" PRG", contents[0].line, "d64", file_name);
}

static void should_scan_archive()
{
	constexpr auto file_name = "benchmarks.zip";
	const auto load_path = test_files_folder.combine_file(file_name);
	const auto contents = files::list_archive(load_path);

	assert_equal(2_z, contents.size(), "archive", file_name);
	assert_equal("PXL_20240404_074316577.jpg", contents[0].filename, "archive", file_name);
}

static void should_detect_tiff_by_version()
{
	// The byte-order mark alone is shared with plenty of non-TIFF files, so the
	// version word decides: 42 is classic TIFF and 43 is BigTIFF.
	constexpr uint8_t little_endian_42[] = {'I', 'I', 42, 0, 8, 0, 0, 0};
	constexpr uint8_t big_endian_42[] = {'M', 'M', 0, 42, 0, 0, 0, 8};
	constexpr uint8_t big_endian_43[] = {'M', 'M', 0, 43, 0, 8, 0, 0};
	constexpr uint8_t not_tiff[] = {'I', 'I', 'B', 'M', 0, 0, 0, 0};

	assert_equal(true, files::detect_format({little_endian_42, std::size(little_endian_42)}) ==
	             detected_format::TIFF, "little endian tiff");
	assert_equal(true, files::detect_format({big_endian_42, std::size(big_endian_42)}) ==
	             detected_format::TIFF, "big endian tiff");
	assert_equal(true, files::detect_format({big_endian_43, std::size(big_endian_43)}) ==
	             detected_format::TIFF, "big endian bigtiff");
	assert_equal(true, files::detect_format({not_tiff, std::size(not_tiff)}) ==
	             detected_format::Unknown, "not tiff");
}

// Each still format is one entry in files_formats.cpp. Its file type still has to take its place in
// the extension table, and its signature has to be the only one that claims it - a format added to
// one and not the other is what this exists to catch.
static void should_register_every_still_format()
{
	std::set<detected_format> seen;

	for (const auto& still : still_formats())
	{
		const auto name = std::string(str::split(still.extensions, true).front());
		assert_equal(true, seen.insert(still.format).second, std::format("{} has one entry", name));
		assert_equal(true, still.matches != nullptr && still.scan != nullptr,
		             std::format("{} is detected and scanned", name));
		assert_equal(true, still.encoded == ui::image_format::Unknown || still.decode != nullptr,
		             std::format("{} kept encoded can still be decoded", name));

		for (const auto ext : str::split(still.extensions, true))
		{
			const auto* const ft = files::file_type_from_name(std::format("x.{}", ext));
			assert_equal(true, ft->extension == still.extensions && ft->text == still.description &&
			             ft->traits == still.traits, std::format("{} registers as its still format", ext));
		}
	}

	assert_equal(true, find_still_format(detected_format::Unknown) == nullptr, "unknown is not a format");
}

static void should_scan_and_load_bitmap_psd()
{
	// A minimal uncompressed 1-bit-per-pixel bitmap-mode psd. Photoshop stores
	// bitmap mode inverted, so a set bit is black and a clear bit is white.
	constexpr uint8_t bitmap_psd[] = {
		'8', 'B', 'P', 'S', 0, 1, // signature and version
		0, 0, 0, 0, 0, 0, // reserved
		0, 1, // channels
		0, 0, 0, 2, // rows
		0, 0, 0, 16, // columns
		0, 1, // depth
		0, 0, // mode - bitmap
		0, 0, 0, 0, // colour mode data length
		0, 0, 0, 0, // image resource length
		0, 0, 0, 0, // layer and mask length
		0, 0, // compression - none
		0b1010'1010, 0b0000'1111, // row 0
		0b0000'0000, 0b1111'1111, // row 1
	};

	mem_read_stream scan_stream({bitmap_psd, std::size(bitmap_psd)});
	const auto scanned = scan_photo(scan_stream);

	assert_equal(true, scanned.success, "bitmap psd scanned");
	assert_equal(16u, scanned.width, "bitmap psd width");
	assert_equal(2u, scanned.height, "bitmap psd height");
	assert_equal("mono"_c, scanned.pixel_format, "bitmap psd pixel format");

	mem_read_stream load_stream({bitmap_psd, std::size(bitmap_psd)});
	const auto surface = load_psd(load_stream);

	assert_equal(true, is_valid(surface), "bitmap psd loaded");
	assert_equal(16, static_cast<int>(surface->width()), "bitmap psd surface width");
	assert_equal(2, static_cast<int>(surface->height()), "bitmap psd surface height");

	const auto* const row0 = std::bit_cast<const uint32_t*>(surface->pixels_line(0));
	const auto* const row1 = std::bit_cast<const uint32_t*>(surface->pixels_line(1));

	constexpr uint32_t black = 0x000000;
	constexpr uint32_t white = 0xFFFFFF;
	const auto rgb = [](const uint32_t pixel) { return pixel & 0xFFFFFF; };

	assert_equal(black, rgb(row0[0]), "bitmap psd 0,0 is black");
	assert_equal(white, rgb(row0[1]), "bitmap psd 1,0 is white");
	assert_equal(white, rgb(row0[8]), "bitmap psd 8,0 is white");
	assert_equal(black, rgb(row0[12]), "bitmap psd 12,0 is black");
	assert_equal(white, rgb(row1[0]), "bitmap psd 0,1 is white");
	assert_equal(black, rgb(row1[15]), "bitmap psd 15,1 is black");
}

static void append_be16(std::vector<uint8_t>& b, const uint16_t v)
{
	b.push_back(static_cast<uint8_t>(v >> 8));
	b.push_back(static_cast<uint8_t>(v));
}

static void append_be32(std::vector<uint8_t>& b, const uint32_t v)
{
	b.push_back(static_cast<uint8_t>(v >> 24));
	b.push_back(static_cast<uint8_t>(v >> 16));
	b.push_back(static_cast<uint8_t>(v >> 8));
	b.push_back(static_cast<uint8_t>(v));
}

static void append_photoshop_resource(std::vector<uint8_t>& out, uint16_t id, std::string_view name,
                                      df::cspan payload);

static df::blob make_lab_psd(const std::vector<uint8_t>& l, const std::vector<uint8_t>& a,
                             const std::vector<uint8_t>& b)
{
	std::vector<uint8_t> psd;
	psd.insert(psd.end(), {'8', 'B', 'P', 'S'});
	append_be16(psd, 1);
	psd.insert(psd.end(), 6, 0);
	append_be16(psd, 3);
	append_be32(psd, 1);
	append_be32(psd, static_cast<uint32_t>(l.size()));
	append_be16(psd, 8);
	append_be16(psd, 9);
	append_be32(psd, 0);
	append_be32(psd, 0);
	append_be32(psd, 0);
	append_be16(psd, 0);
	psd.insert(psd.end(), l.begin(), l.end());
	psd.insert(psd.end(), a.begin(), a.end());
	psd.insert(psd.end(), b.begin(), b.end());
	return {psd.begin(), psd.end()};
}

static void should_convert_psd_lab_samples_to_lab_coordinates()
{
	const auto psd = make_lab_psd({0, 64, 128, 191, 255}, {128, 128, 128, 128, 128}, {128, 128, 128, 128, 128});
	mem_read_stream stream(psd);
	const auto surface = load_psd(stream);

	assert_equal(true, ui::is_valid(surface), "Lab PSD loaded");

	const auto black = surface->get_pixel(0, 0);
	const auto gray = surface->get_pixel(1, 0);
	const auto mid25 = surface->get_pixel(1, 0);
	const auto mid50 = surface->get_pixel(2, 0);
	const auto mid75 = surface->get_pixel(3, 0);
	const auto white = surface->get_pixel(4, 0);

	assert_equal(true, ui::get_r(black) < 4 && ui::get_g(black) < 4 && ui::get_b(black) < 4,
	             "Lab black is black");
	assert_equal(true, std::abs(static_cast<int>(ui::get_r(mid50)) - static_cast<int>(ui::get_g(mid50))) <= 2 &&
	             std::abs(static_cast<int>(ui::get_g(mid50)) - static_cast<int>(ui::get_b(mid50))) <= 2,
	             "Lab neutral gray is neutral");
	assert_near(59.0, static_cast<double>(ui::get_r(mid25)), 1.0, "Lab L25 sRGB");
	assert_near(119.0, static_cast<double>(ui::get_r(mid50)), 1.0, "Lab L50 sRGB");
	assert_near(185.0, static_cast<double>(ui::get_r(mid75)), 1.0, "Lab L75 sRGB");
	assert_equal(true, ui::get_r(white) > 250 && ui::get_g(white) > 250 && ui::get_b(white) > 250,
	             "Lab white is white");
}

class counting_read_stream final : public read_stream
{
	df::blob _prefix;

public:
	size_t max_read = 0;

	counting_read_stream(df::blob prefix, const uint64_t size) : _prefix(std::move(prefix))
	{
		_file_size = size;
	}

	template <typename T>
	T peek(const uint64_t pos)
	{
		check_range(pos, sizeof(T));
		T result{};
		if (pos < _prefix.size())
		{
			const auto copied = std::min<uint64_t>(sizeof(T), _prefix.size() - pos);
			memcpy(&result, _prefix.data() + static_cast<size_t>(pos), static_cast<size_t>(copied));
		}
		return result;
	}

	uint8_t peek8(const uint64_t pos) override { return peek<uint8_t>(pos); }
	uint16_t peek16(const uint64_t pos) override { return peek<uint16_t>(pos); }
	uint32_t peek32(const uint64_t pos) override { return peek<uint32_t>(pos); }
	uint64_t peek64(const uint64_t pos) override { return peek<uint64_t>(pos); }
	pack128 peek128(const uint64_t pos) override { return peek<pack128>(pos); }
	df::blob read_all() override { return {}; }

	df::blob read(const uint64_t pos, const size_t len) override
	{
		df::blob result(len);
		read(pos, result.data(), len);
		return result;
	}

	void read(const uint64_t pos, uint8_t* buffer, const size_t len) override
	{
		check_range(pos, len);
		memset(buffer, 0, len);
		if (pos < _prefix.size())
		{
			const auto copied = std::min<uint64_t>(len, _prefix.size() - pos);
			memcpy(buffer, _prefix.data() + static_cast<size_t>(pos), static_cast<size_t>(copied));
		}
		max_read = std::max(max_read, len);
	}
};

static df::blob make_psd_prefix(const uint16_t channels, const uint32_t rows, const uint32_t columns,
                                const uint16_t depth, const uint16_t mode, const uint32_t color_len,
                                const uint32_t resource_len)
{
	std::vector<uint8_t> psd;
	psd.insert(psd.end(), {'8', 'B', 'P', 'S'});
	append_be16(psd, 1);
	psd.insert(psd.end(), 6, 0);
	append_be16(psd, channels);
	append_be32(psd, rows);
	append_be32(psd, columns);
	append_be16(psd, depth);
	append_be16(psd, mode);
	append_be32(psd, color_len);
	append_be32(psd, resource_len);
	return {psd.begin(), psd.end()};
}

static void should_bound_psd_resource_allocations()
{
	const auto prefix = make_psd_prefix(3, 1, 1, 8, 3, 0, 16u * 1024u * 1024u + 1u);
	counting_read_stream stream(prefix.clone(), prefix.size() + 16u * 1024u * 1024u + 1u);
	const auto scanned = scan_psd(stream);

	assert_equal(true, scanned.success, "oversized resource section is a bounded scan");
	assert_equal(true, scanned.metadata.iptc.empty(), "oversized resource is not published");
	assert_equal(true, stream.max_read < df::two_fifty_six_k, "oversized resource bytes are skipped, not allocated");
}

static void should_read_small_psd_metadata_after_large_unused_resource()
{
	const df::blob exif = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 0x2a, 0};
	std::vector<uint8_t> resources;
	append_photoshop_resource(resources, 0x0422, "", exif);
	const df::blob large_unused(16u * 1024u * 1024u, 0);
	append_photoshop_resource(resources, 0x03ed, "", large_unused);

	const auto prefix = make_psd_prefix(3, 1, 1, 8, 3, 0, static_cast<uint32_t>(resources.size()));
	auto data = prefix.clone();
	data.insert(data.end(), resources.begin(), resources.end());
	counting_read_stream stream(std::move(data), prefix.size() + resources.size());
	const auto scanned = scan_psd(stream);

	assert_equal(true, scanned.success, "large unused resource section scans");
	assert_equal(true, scanned.metadata.exif == exif, "small EXIF resource is still read");
	assert_equal(true, stream.max_read < df::two_fifty_six_k, "unused large resource is skipped");
}

static void should_load_valid_psd_indexed_palette()
{
	std::vector<uint8_t> psd;
	psd.insert(psd.end(), {'8', 'B', 'P', 'S'});
	append_be16(psd, 1);
	psd.insert(psd.end(), 6, 0);
	append_be16(psd, 1);
	append_be32(psd, 1);
	append_be32(psd, 1);
	append_be16(psd, 8);
	append_be16(psd, 2);
	append_be32(psd, 768);
	std::array<uint8_t, 768> palette = {};
	palette[7] = 10;
	palette[256 + 7] = 20;
	palette[512 + 7] = 30;
	psd.insert(psd.end(), palette.begin(), palette.end());
	append_be32(psd, 0); // resources length
	append_be32(psd, 0); // layer and mask length
	append_be16(psd, 0); // raw compression
	psd.push_back(7); // palette index

	const df::blob bytes{psd.begin(), psd.end()};
	mem_read_stream stream(bytes);
	const auto surface = load_psd(stream);
	assert_equal(true, ui::is_valid(surface), "indexed psd loaded");
	const auto c = surface->get_pixel(0, 0);
	assert_equal(30u, ui::get_r(c), "palette red");
	assert_equal(20u, ui::get_g(c), "palette green");
	assert_equal(10u, ui::get_b(c), "palette blue");
}

static void should_keep_dimensions_from_truncated_gif()
{
	// A valid GIF89a header followed by an application extension declaring an 11-byte
	// identifier the file does not contain. The block walk cannot complete, but the
	// dimensions were read from the header before it and must survive it - otherwise the
	// scan is recorded as a failure and the file is re-scanned on every index pass.
	constexpr uint8_t truncated_gif[] = {
		'G', 'I', 'F', '8', '9', 'a',
		0x40, 0x00, // width 64
		0x20, 0x00, // height 32
		0x00, 0x00, 0x00, // packed fields (no global colour table), background, aspect
		0x21, 0xFF, 0x0B, // application extension promising 11 bytes that are not there
	};

	mem_read_stream stream({truncated_gif, std::size(truncated_gif)});
	const auto scanned = scan_photo(stream);

	assert_equal(true, scanned.success, "truncated gif scanned");
	assert_equal(64u, scanned.width, "truncated gif width");
	assert_equal(32u, scanned.height, "truncated gif height");
}

static std::vector<uint8_t> make_tiff_with_dimensions(const uint32_t width, const uint32_t height)
{
	std::vector<uint8_t> buf;

	const auto put16 = [&buf](const uint16_t v)
	{
		buf.push_back(static_cast<uint8_t>(v));
		buf.push_back(static_cast<uint8_t>(v >> 8));
	};
	const auto put32 = [&buf](const uint32_t v)
	{
		buf.push_back(static_cast<uint8_t>(v));
		buf.push_back(static_cast<uint8_t>(v >> 8));
		buf.push_back(static_cast<uint8_t>(v >> 16));
		buf.push_back(static_cast<uint8_t>(v >> 24));
	};
	const auto put_entry = [put16, put32](const uint16_t tag, const uint32_t value)
	{
		put16(tag);
		put16(4); // FMT_ULONG
		put32(1);
		put32(value);
	};

	put16(0x4949);
	put16(42);
	put32(8); // IFD0 offset

	put16(2); // entry count
	put_entry(0x0100, width); // ImageWidth
	put_entry(0x0101, height); // ImageLength
	put32(0); // no IFD1

	return buf;
}

static void should_reject_absurd_tiff_dimensions()
{
	const auto valid = make_tiff_with_dimensions(64, 32);
	mem_read_stream valid_stream({valid.data(), valid.size()});
	const auto scanned_valid = scan_photo(valid_stream);

	assert_equal(true, scanned_valid.success, "valid tiff scanned");
	assert_equal(64u, scanned_valid.width, "valid tiff width");
	assert_equal(32u, scanned_valid.height, "valid tiff height");

	// These are unvalidated 32-bit file fields. Cast into sizei this one turns negative, and
	// the decode budget it is later checked against then passes.
	const auto absurd = make_tiff_with_dimensions(0xFFFFFFFFu, 0xFFFFFFFFu);
	mem_read_stream absurd_stream({absurd.data(), absurd.size()});
	const auto scanned_absurd = scan_photo(absurd_stream);

	assert_equal(false, scanned_absurd.success, "absurd tiff rejected");
	assert_equal(0u, scanned_absurd.width, "absurd tiff width cleared");
	assert_equal(0u, scanned_absurd.height, "absurd tiff height cleared");
}

// Indexing must not pay for an embedded thumbnail it will not use: extracting one reads the
// thumbnail's bytes off the file and copies them (EXIF/TIFF) or fully decodes them (HEIF). The scan
// asks for one only when a thumbnail is wanted, and this pins both halves of that - nothing when it
// is not, and still a thumbnail when it is.
static void should_extract_embedded_thumbnails_only_on_demand()
{
	// Each is over the 256 KB in-memory limit, so the scan reaches them through the seek-and-read
	// stream rather than a span into an already-resident blob.
	for (const auto* const name : {"Nikon.JPG", "IMG_0096.JPG", "melnik.heic"})
	{
		const auto path = test_files_folder.combine_file(name);

		files ff;
		const auto indexed = ff_scan_file(ff, path);
		const auto wanted = ff_scan_and_load_thumb(ff, path);

		assert_equal(true, indexed.success, name, "metadata scan succeeded");
		assert_equal(false, is_valid(indexed.thumbnail_image) || is_valid(indexed.thumbnail_surface),
		             name, "metadata scan extracted no embedded thumbnail");

		// The metadata a scan reports must not depend on whether a thumbnail was asked for.
		assert_equal(wanted.width, indexed.width, name, "width");
		assert_equal(wanted.height, indexed.height, name, "height");
		assert_equal(static_cast<int>(wanted.orientation), static_cast<int>(indexed.orientation),
		             name, "orientation");
		assert_equal(wanted.created_utc, indexed.created_utc, name, "created");

		assert_equal(true, is_valid(wanted.thumbnail_image) || is_valid(wanted.thumbnail_surface),
		             name, "on-demand scan produced a thumbnail");
	}
}

// libheif's plugin priority would decode HEIC's HEVC with libde265. FFmpeg's decoder gives the same
// pixels in about two thirds of the time, so HEVC images are named to it - and only HEVC images,
// because a named decoder takes whatever codec the image holds. HEVC decoding is bit-exact by
// specification, so the two decoders must agree. They do on 64-bit; libde265's 32-bit build does not
// (on this thumbnail 32,462 of 589,824 samples, off by up to 132, while FFmpeg's 32-bit decode matches
// both 64-bit ones), which is a second reason HEVC goes to FFmpeg and why only 64-bit compares them.
static void should_decode_heic_hevc_with_ffmpeg()
{
	const auto* const hevc_decoder = heif_hevc_decoder_id();
	assert_equal("ffmpeg"sv, std::string_view(hevc_decoder ? hevc_decoder : ""), "libheif carries FFmpeg's HEVC decoder");

	const auto chosen = [](const std::string_view name)
	{
		file_read_stream stream;
		if (!stream.open(test_files_folder.combine_file(name))) return std::string{"unreadable"};
		const auto* const id = heif_primary_decoder_id(stream);
		return std::string(id ? id : "");
	};

	assert_equal("ffmpeg"s, chosen("melnik.heic"), "a grid of HEVC tiles is steered to FFmpeg");
	assert_equal(""s, chosen("hato.profile0.10bpc.yuv420.avif"), "an AV1 image keeps libheif's choice");

	const auto file = platform::open_file(test_files_folder.combine_file("melnik.heic"), platform::file_open_mode::read);
	assert_equal(true, file != nullptr, "HEIC fixture opened");
	if (!file) return;

	std::vector<uint8_t> bytes(static_cast<size_t>(file->size()));
	assert_equal(static_cast<uint64_t>(bytes.size()), file->read(bytes.data(), bytes.size()), "HEIC fixture read");

	// The embedded thumbnail is one HEVC picture, which is the unit either decoder works in.
	const auto decode_thumbnail = [&bytes](const char* const decoder_id)
	{
		std::vector<uint8_t> pixels;
		auto* const ctx = heif_context_alloc();
		auto* const options = heif_decoding_options_alloc();
		heif_image_handle* primary = nullptr;
		heif_image_handle* thumbnail = nullptr;
		heif_image* image = nullptr;
		heif_item_id thumbnail_id = 0;
		options->decoder_id = decoder_id;

		if (heif_context_read_from_memory_without_copy(ctx, bytes.data(), bytes.size(), nullptr).code == heif_error_Ok &&
			heif_context_get_primary_image_handle(ctx, &primary).code == heif_error_Ok &&
			heif_image_handle_get_list_of_thumbnail_IDs(primary, &thumbnail_id, 1) == 1 &&
			heif_image_handle_get_thumbnail(primary, thumbnail_id, &thumbnail).code == heif_error_Ok &&
			heif_decode_image(thumbnail, &image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, options).code ==
			heif_error_Ok)
		{
			int stride = 0;
			const auto* const plane = heif_image_get_plane_readonly(image, heif_channel_interleaved, &stride);
			const auto height = heif_image_get_height(image, heif_channel_interleaved);
			if (plane && stride > 0 && height > 0) pixels.assign(plane, plane + static_cast<size_t>(stride) * height);
		}

		heif_image_release(image);
		heif_image_handle_release(thumbnail);
		heif_image_handle_release(primary);
		heif_decoding_options_free(options);
		heif_context_free(ctx);
		return pixels;
	};

	const auto by_ffmpeg = decode_thumbnail("ffmpeg");
	const auto by_libde265 = decode_thumbnail("libde265");

	assert_equal(true, !by_ffmpeg.empty() && !by_libde265.empty(), "both decoders decode the HEVC picture");

	if constexpr (sizeof(void*) == 8)
	{
		assert_equal(true, by_ffmpeg == by_libde265, "and give the same pixels");
	}
}

// Defined below with the other JPEG helpers; the orientation tests need a stream that carries no
// Exif of its own, which is exactly what encoding one here produces.
static ui::surface_ptr make_gradient_surface(int cx, int cy);

// Regression: the JPEG decoder must call jpeg_save_markers so read_header can
// recover the embedded EXIF orientation from the APP1 marker.
static void should_read_jpeg_orientation()
{
	const auto load_path = test_files_folder.combine_file("exif-rotated.jpg");
	const auto data = df::blob_from_file(load_path);

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header(data), "read jpeg header");
	assert_equal(ui::orientation::right_top, decoder._orientation_out, "decoder recovers EXIF orientation");

	// files holds ONE decoder for every image it reads, so each header has to state what its own
	// stream carries. Assigning the orientation only when an Exif marker was found left the
	// previous file's value in place for a stream with none - which is most embedded RAW
	// thumbnails - and drew them at that rotation.
	files ff;
	const auto plain = ff.surface_to_image(make_gradient_surface(32, 32), {}, {}, ui::image_format::JPEG);
	assert_equal(true, decoder.read_header(plain->data()), "a jpeg carrying no orientation reads");
	assert_equal(ui::orientation::top_left, decoder._orientation_out,
	             "and is upright rather than keeping the last one's rotation");
}

static df::blob make_iptc_caption(const std::string_view caption)
{
	df::blob result;
	result.push_back(0x1c);
	result.push_back(0x02);
	result.push_back(120);
	result.push_back(static_cast<uint8_t>(caption.size() >> 8));
	result.push_back(static_cast<uint8_t>(caption.size()));
	result.insert(result.end(), caption.begin(), caption.end());
	return result;
}

static void append_photoshop_resource(std::vector<uint8_t>& out, const uint16_t id, const std::string_view name,
                                      const df::cspan payload)
{
	out.insert(out.end(), {'8', 'B', 'I', 'M'});
	append_be16(out, id);
	out.push_back(static_cast<uint8_t>(name.size()));
	out.insert(out.end(), name.begin(), name.end());
	if ((name.size() & 1u) == 0) out.push_back(0);
	append_be32(out, static_cast<uint32_t>(payload.size));
	out.insert(out.end(), payload.begin(), payload.end());
	if (payload.size & 1u) out.push_back(0);
}

static df::blob jpeg_with_app13_resources(const ui::const_image_ptr& jpeg, const df::blob& resources)
{
	std::vector<uint8_t> out;
	const auto& source = jpeg->data();
	out.insert(out.end(), source.data(), source.data() + 2);
	out.push_back(0xff);
	out.push_back(0xed);
	const auto len = static_cast<uint16_t>(resources.size() + 2u);
	append_be16(out, len);
	out.insert(out.end(), resources.begin(), resources.end());
	out.insert(out.end(), source.data() + 2, source.data() + source.size());
	return {out.begin(), out.end()};
}

static void append_jpeg_marker(std::vector<uint8_t>& out, const uint8_t marker, const df::cspan payload)
{
	out.push_back(0xff);
	out.push_back(marker);
	const auto len = static_cast<uint16_t>(payload.size + 2u);
	append_be16(out, len);
	out.insert(out.end(), payload.begin(), payload.end());
}

static df::blob jpeg_with_metadata_markers(const ui::const_image_ptr& jpeg, const std::vector<df::blob>& payloads,
                                           const std::vector<uint8_t>& markers)
{
	std::vector<uint8_t> out;
	const auto& source = jpeg->data();
	out.insert(out.end(), source.data(), source.data() + 2);
	for (auto i = 0_z; i < payloads.size(); ++i) append_jpeg_marker(out, markers[i], payloads[i]);
	out.insert(out.end(), source.data() + 2, source.data() + source.size());
	return {out.begin(), out.end()};
}

static void should_parse_jpeg_photoshop_iptc_resources()
{
	files ff;
	const auto jpeg = ff.surface_to_image(make_gradient_surface(8, 8), {}, {}, ui::image_format::JPEG);
	assert_equal(true, is_valid(jpeg), "jpeg encoded");

	const auto iptc = make_iptc_caption("resource caption");
	std::vector<uint8_t> resources;
	constexpr std::string_view sig = "Photoshop 3.0\0"sv;
	resources.insert(resources.end(), sig.begin(), sig.end());
	const df::blob other = {1, 2, 3};
	append_photoshop_resource(resources, 0x03ed, "name", other);
	append_photoshop_resource(resources, 0x0404, "iptc", iptc);

	const auto wrapped = jpeg_with_app13_resources(jpeg, {resources.begin(), resources.end()});
	mem_read_stream stream(wrapped);
	const auto scanned = scan_jpg(stream);
	prop::item_metadata md;
	metadata_iptc::parse(md, scanned.metadata.iptc);

	assert_equal("resource caption", md.description, "named IPTC resource parsed");

	const auto marker = make_photoshop_iptc_resource(iptc, true);
	metadata_parts parsed;
	assert_equal(true, parse_photoshop_resources(parsed, marker, true), "written marker parses independently");
	assert_equal(true, parsed.iptc == iptc, "written marker length and padding preserve payload");
}

static void should_keep_app13_from_overriding_jpeg_app1_and_app2_metadata()
{
	files ff;
	const auto jpeg = ff.surface_to_image(make_gradient_surface(8, 8), {}, {}, ui::image_format::JPEG);
	assert_equal(true, is_valid(jpeg), "jpeg encoded");

	const df::blob app1_xmp = {'a', 'p', 'p', '1', '-', 'x', 'm', 'p'};
	const df::blob app13_xmp = {'a', 'p', 'p', '1', '3', '-', 'x', 'm', 'p'};
	const df::blob app1_exif = {'I', 'I', 0x2a, 0, 8, 0, 0, 0};
	const df::blob app13_exif = {'M', 'M', 0, 0x2a};
	const df::blob app2_icc = {'i', 'c', 'c', '-', 'a', 'p', 'p', '2'};
	const df::blob app13_icc = {'i', 'c', 'c', '-', 'a', 'p', 'p', '1', '3'};

	std::vector<uint8_t> app13;
	constexpr std::string_view sig = "Photoshop 3.0\0"sv;
	app13.insert(app13.end(), sig.begin(), sig.end());
	append_photoshop_resource(app13, 0x0424, "", app13_xmp);
	append_photoshop_resource(app13, 0x0422, "", app13_exif);
	append_photoshop_resource(app13, 0x040f, "", app13_icc);

	df::blob xmp_marker;
	xmp_marker.insert(xmp_marker.end(), xmp_signature.begin(), xmp_signature.end());
	xmp_marker.insert(xmp_marker.end(), app1_xmp.begin(), app1_xmp.end());
	df::blob exif_marker;
	exif_marker.insert(exif_marker.end(), exif_signature.begin(), exif_signature.end());
	exif_marker.insert(exif_marker.end(), app1_exif.begin(), app1_exif.end());
	df::blob icc_marker;
	icc_marker.insert(icc_marker.end(), icc_signature.begin(), icc_signature.end());
	icc_marker.push_back(1);
	icc_marker.push_back(1);
	icc_marker.insert(icc_marker.end(), app2_icc.begin(), app2_icc.end());

	std::vector<df::blob> payloads;
	payloads.emplace_back(std::move(xmp_marker));
	payloads.emplace_back(std::move(exif_marker));
	payloads.emplace_back(std::move(icc_marker));
	payloads.emplace_back(app13.begin(), app13.end());
	const auto wrapped = jpeg_with_metadata_markers(jpeg, payloads, {0xe1, 0xe1, 0xe2, 0xed});
	mem_read_stream stream(wrapped);
	const auto scanned = scan_jpg(stream);
	assert_equal(true, scanned.metadata.xmp == app1_xmp, "APP1 XMP remains authoritative");
	assert_equal(true, scanned.metadata.exif == app1_exif, "APP1 EXIF remains authoritative");
	assert_equal(true, scanned.metadata.icc == app2_icc, "APP13 ICC is not appended to APP2 ICC");
}

static void should_preserve_non_iptc_jpeg_photoshop_resources()
{
	files ff;
	const auto jpeg = ff.surface_to_image(make_gradient_surface(8, 8), {}, {}, ui::image_format::JPEG);
	assert_equal(true, is_valid(jpeg), "jpeg encoded");

	const auto iptc = make_iptc_caption("resource caption");
	const df::blob other = {9, 8, 7, 6, 5};
	std::vector<uint8_t> app13;
	constexpr std::string_view sig = "Photoshop 3.0\0"sv;
	app13.insert(app13.end(), sig.begin(), sig.end());
	append_photoshop_resource(app13, 0x0404, "iptc", iptc);
	append_photoshop_resource(app13, 0x03ed, "keep", other);

	const auto wrapped = jpeg_with_app13_resources(jpeg, {app13.begin(), app13.end()});
	mem_read_stream stream(wrapped);
	const auto scanned = scan_jpg(stream);
	const auto encoded = save_jpeg(make_gradient_surface(8, 8), scanned.metadata, {});
	assert_equal(true, is_valid(encoded), "jpeg re-encoded");

	mem_read_stream out_stream(encoded->data());
	const auto rescanned = scan_jpg(out_stream);
	std::vector<uint8_t> expected;
	append_photoshop_resource(expected, 0x03ed, "keep", other);
	assert_equal(true, rescanned.metadata.photoshop_resources == df::blob{expected.begin(), expected.end()},
	             "non-IPTC Photoshop resource survives re-encode");
}

static void should_roundtrip_split_jpeg_photoshop_resources()
{
	files ff;
	const auto jpeg = ff.surface_to_image(make_gradient_surface(8, 8), {}, {}, ui::image_format::JPEG);
	assert_equal(true, is_valid(jpeg), "jpeg encoded");

	const df::blob first_payload(40000, 0x31);
	const df::blob second_payload(40000, 0x32);
	std::vector<uint8_t> first;
	std::vector<uint8_t> second;
	constexpr std::string_view sig = "Photoshop 3.0\0"sv;
	first.insert(first.end(), sig.begin(), sig.end());
	second.insert(second.end(), sig.begin(), sig.end());
	append_photoshop_resource(first, 0x03ed, "first", first_payload);
	append_photoshop_resource(second, 0x03ee, "second", second_payload);

	std::vector<df::blob> payloads;
	payloads.emplace_back(first.begin(), first.end());
	payloads.emplace_back(second.begin(), second.end());
	const auto wrapped = jpeg_with_metadata_markers(jpeg, payloads, {0xed, 0xed});
	mem_read_stream stream(wrapped);
	const auto scanned = scan_jpg(stream);

	std::vector<uint8_t> expected;
	append_photoshop_resource(expected, 0x03ed, "first", first_payload);
	append_photoshop_resource(expected, 0x03ee, "second", second_payload);
	assert_equal(true, scanned.metadata.photoshop_resources == df::blob{expected.begin(), expected.end()},
	             "split APP13 resources are joined in order");

	const auto encoded = save_jpeg(make_gradient_surface(8, 8), scanned.metadata, {});
	assert_equal(true, is_valid(encoded), "split APP13 resources re-encode");
	mem_read_stream out_stream(encoded->data());
	const auto rescanned = scan_jpg(out_stream);
	assert_equal(true, rescanned.metadata.photoshop_resources == df::blob{expected.begin(), expected.end()},
	             "split APP13 resources survive re-encode");
}

static df::blob split_app13_payload(const std::vector<uint8_t>& joined, const size_t split_at)
{
	constexpr std::string_view sig = "Photoshop 3.0\0"sv;
	std::vector<df::blob> payloads;
	for (auto pos = 0_z; pos < joined.size(); pos += split_at)
	{
		std::vector<uint8_t> payload;
		payload.insert(payload.end(), sig.begin(), sig.end());
		const auto count = std::min(split_at, joined.size() - pos);
		payload.insert(payload.end(), joined.begin() + static_cast<ptrdiff_t>(pos),
		               joined.begin() + static_cast<ptrdiff_t>(pos + count));
		payloads.emplace_back(payload.begin(), payload.end());
	}

	files ff;
	const auto jpeg = ff.surface_to_image(make_gradient_surface(8, 8), {}, {}, ui::image_format::JPEG);
	return jpeg_with_metadata_markers(jpeg, payloads, std::vector<uint8_t>(payloads.size(), 0xed));
}

static void should_join_straddled_jpeg_photoshop_resources()
{
	const df::blob first_payload(65490, 0x41);
	const df::blob second_payload(5000, 0x42);
	std::vector<uint8_t> joined;
	append_photoshop_resource(joined, 0x03ed, "first", first_payload);
	append_photoshop_resource(joined, 0x03ee, "second", second_payload);

	const auto wrapped = split_app13_payload(joined, 65519);
	mem_read_stream stream(wrapped);
	const auto scanned = scan_jpg(stream);
	assert_equal(true, scanned.metadata.photoshop_resources == df::blob{joined.begin(), joined.end()},
	             "straddled APP13 record survives the byte-level join");
}

static void should_cap_joined_jpeg_photoshop_resources()
{
	const df::blob payload(1200, 0x43);
	std::vector<uint8_t> joined;
	append_photoshop_resource(joined, 0x03ed, "first", payload);
	append_photoshop_resource(joined, 0x03ee, "second", payload);

	files_test_hooks::set_jpeg_photoshop_resource_cap(1600);
	const df::scope_exit restore_cap([] { files_test_hooks::set_jpeg_photoshop_resource_cap(0); });

	const auto wrapped = split_app13_payload(joined, 800);
	mem_read_stream stream(wrapped);
	const auto scanned = scan_jpg(stream);

	std::vector<uint8_t> expected;
	append_photoshop_resource(expected, 0x03ed, "first", payload);
	assert_equal(true, scanned.metadata.photoshop_resources == df::blob{expected.begin(), expected.end()},
	             "joined APP13 payload is capped at a complete resource");
}

// IFD1 describes the embedded thumbnail, which cameras commonly store already upright while the
// primary image is not. Taking its orientation over IFD0's displayed the photograph at its
// thumbnail's rotation; it may only stand in where the primary image gave none.
static void should_prefer_primary_orientation_over_the_thumbnail_ifd()
{
	// A little-endian TIFF whose IFD0 carries one orientation and points at an IFD1 carrying
	// another. Nothing else: the scanner reads orientation from both and needs no more.
	const auto build_exif = [](const uint16_t ifd0_orientation, const uint16_t ifd1_orientation)
	{
		std::vector<uint8_t> b;
		const auto put16 = [&b](const uint16_t v)
		{
			b.push_back(static_cast<uint8_t>(v));
			b.push_back(static_cast<uint8_t>(v >> 8));
		};
		const auto put32 = [&b](const uint32_t v)
		{
			for (auto i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
		};
		const auto orientation_ifd = [&](const uint16_t orientation)
		{
			put16(1); // one entry
			put16(0x0112); // Orientation
			put16(3); // FMT_USHORT
			put32(1);
			put16(orientation);
			put16(0); // the value field is four bytes wide
		};

		put16(0x4949);
		put16(0x002a);
		put32(8); // IFD0 follows the header

		constexpr uint32_t ifd1_offset = 8 + 2 + 12 + 4;
		orientation_ifd(ifd0_orientation);
		put32(ifd1_offset);
		orientation_ifd(ifd1_orientation);
		put32(0); // no further IFD
		return b;
	};

	files ff;
	const auto jpeg = ff.surface_to_image(make_gradient_surface(32, 32), {}, {}, ui::image_format::JPEG);

	const auto scan_with_exif = [&](const uint16_t ifd0_orientation, const uint16_t ifd1_orientation)
	{
		const auto tiff = build_exif(ifd0_orientation, ifd1_orientation);
		const auto& source = jpeg->data();

		std::vector<uint8_t> out;
		out.insert(out.end(), source.data(), source.data() + 2); // SOI

		const auto payload = static_cast<uint16_t>(2 + 6 + tiff.size());
		out.push_back(0xFF);
		out.push_back(0xE1);
		out.push_back(static_cast<uint8_t>(payload >> 8));
		out.push_back(static_cast<uint8_t>(payload));

		constexpr uint8_t signature[] = {'E', 'x', 'i', 'f', 0, 0};
		out.insert(out.end(), std::begin(signature), std::end(signature));
		out.insert(out.end(), tiff.begin(), tiff.end());
		out.insert(out.end(), source.data() + 2, source.data() + source.size());

		const auto path = _temps.next_path(".jpg");
		df::blob_save_to_file(df::blob(out.begin(), out.end()), path);
		return ff_scan_file(ff, path).orientation;
	};

	assert_equal(static_cast<int>(ui::orientation::right_top),
	             static_cast<int>(scan_with_exif(6, 1)),
	             "the primary image's rotation is what the photograph is shown at");
	assert_equal(static_cast<int>(ui::orientation::top_left),
	             static_cast<int>(scan_with_exif(1, 6)),
	             "and an upright primary is not rotated by its thumbnail");
}

static void should_use_thumbnail_orientation_only_when_primary_is_absent()
{
	const auto build_exif = []()
	{
		std::vector<uint8_t> b;
		const auto put16 = [&b](const uint16_t v)
		{
			b.push_back(static_cast<uint8_t>(v));
			b.push_back(static_cast<uint8_t>(v >> 8));
		};
		const auto put32 = [&b](const uint32_t v)
		{
			for (auto i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
		};

		put16(0x4949);
		put16(0x002a);
		put32(8); // IFD0 follows the header

		constexpr uint32_t ifd1_offset = 8 + 2 + 4;
		put16(0); // IFD0 has no primary orientation
		put32(ifd1_offset);

		put16(1); // IFD1 carries the thumbnail-orientation fallback
		put16(0x0112); // Orientation
		put16(3); // FMT_USHORT
		put32(1);
		put16(6); // right_top
		put16(0);
		put32(0); // no further IFD
		return b;
	};

	files ff;
	const auto plain = ff.surface_to_image(make_gradient_surface(32, 32), {}, {}, ui::image_format::JPEG);
	const auto tiff = build_exif();
	const auto& source = plain->data();

	std::vector<uint8_t> jpeg;
	jpeg.insert(jpeg.end(), source.data(), source.data() + 2); // SOI

	const auto payload = static_cast<uint16_t>(2 + 6 + tiff.size());
	jpeg.push_back(0xFF);
	jpeg.push_back(0xE1);
	jpeg.push_back(static_cast<uint8_t>(payload >> 8));
	jpeg.push_back(static_cast<uint8_t>(payload));

	constexpr uint8_t signature[] = {'E', 'x', 'i', 'f', 0, 0};
	jpeg.insert(jpeg.end(), std::begin(signature), std::end(signature));
	jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
	jpeg.insert(jpeg.end(), source.data() + 2, source.data() + source.size());

	mem_read_stream scan_stream({jpeg.data(), jpeg.size()});
	const auto scanned = scan_photo(scan_stream);

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header({jpeg.data(), jpeg.size()}), "decoder reads IFD1-only jpeg");
	assert_equal(static_cast<int>(ui::orientation::right_top),
	             static_cast<int>(scanned.orientation),
	             "scan uses IFD1 orientation when IFD0 is absent");
	assert_equal(static_cast<int>(scanned.orientation),
	             static_cast<int>(decoder._orientation_out),
	             "decoder and scanner agree on IFD1-only orientation");
}

static std::vector<uint8_t> make_tiff_with_orientations(const uint16_t ifd0_orientation,
                                                        const uint16_t ifd1_orientation)
{
	std::vector<uint8_t> b;
	const auto put16 = [&b](const uint16_t v)
	{
		b.push_back(static_cast<uint8_t>(v));
		b.push_back(static_cast<uint8_t>(v >> 8));
	};
	const auto put32 = [&b](const uint32_t v)
	{
		for (auto i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
	};
	const auto put_entry = [&](const uint16_t tag, const uint16_t fmt, const uint32_t count, const uint32_t value)
	{
		put16(tag);
		put16(fmt);
		put32(count);
		put32(value);
	};
	const auto put_orientation = [&](const uint16_t orientation)
	{
		put16(0x0112); // Orientation
		put16(3); // FMT_USHORT
		put32(1);
		put16(orientation);
		put16(0);
	};

	put16(0x4949);
	put16(42);
	put32(8);

	constexpr uint32_t ifd1_offset = 8 + 2 + 3 * 12 + 4;
	put16(3);
	put_entry(0x0100, 4, 1, 32); // ImageWidth
	put_entry(0x0101, 4, 1, 16); // ImageLength
	put_orientation(ifd0_orientation);
	put32(ifd1_offset);
	put16(1);
	put_orientation(ifd1_orientation);
	put32(0);
	return b;
}

static void should_prefer_primary_tiff_orientation_over_the_thumbnail_ifd()
{
	const auto scan_orientation = [](const uint16_t ifd0_orientation, const uint16_t ifd1_orientation)
	{
		const auto tiff = make_tiff_with_orientations(ifd0_orientation, ifd1_orientation);
		mem_read_stream stream({tiff.data(), tiff.size()});
		return scan_photo(stream).orientation;
	};

	assert_equal(static_cast<int>(ui::orientation::right_top),
	             static_cast<int>(scan_orientation(6, 1)),
	             "the primary TIFF orientation is kept");
	assert_equal(static_cast<int>(ui::orientation::top_left),
	             static_cast<int>(scan_orientation(1, 6)),
	             "the thumbnail TIFF orientation does not rotate an upright primary");
}

// The payload of the first DQT segment, which is the table the encoder quantized with.
static df::blob first_dqt(const df::cspan jpeg)
{
	for (size_t i = 2; i + 4 < jpeg.size;)
	{
		if (jpeg.data[i] != 0xFF) break;

		const auto marker = jpeg.data[i + 1];

		if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
		{
			i += 2;
			continue;
		}

		const size_t len = (static_cast<size_t>(jpeg.data[i + 2]) << 8) | jpeg.data[i + 3];
		if (marker == 0xDB) return {jpeg.data + i + 4, jpeg.data + i + 2 + len};
		if (marker == 0xDA) break; // start of scan - no table declarations follow
		i += 2 + len;
	}

	return {};
}

static ui::surface_ptr make_gradient_surface(const int cx, const int cy)
{
	auto surface = std::make_shared<ui::surface>();
	surface->alloc(cx, cy, ui::texture_format::RGB);

	for (auto y = 0; y < cy; ++y)
		for (auto x = 0; x < cx; ++x)
			surface->set_pixel(x, y, ui::rgba((x * 4) & 0xFF, (y * 4) & 0xFF, ((x ^ y) * 4) & 0xFF));

	return surface;
}

// Editing a JPEG must re-encode against the source's own quantization tables, so an untouched block
// quantizes back to itself instead of being re-quantized to whatever the quality slider says.
static void should_reuse_source_jpeg_tables()
{
	files ff;
	const auto surface = make_gradient_surface(64, 64);

	file_encode_params coarse;
	coarse.jpeg_save_quality = 40;
	const auto source = ff.surface_to_image(surface, {}, coarse, ui::image_format::JPEG);
	const auto source_dqt = first_dqt(source->data());
	assert_equal(false, source_dqt.empty(), "source declares a quantization table");

	file_encode_params matched;
	matched.jpeg_save_quality = 95;
	matched.jpeg_source = source->data();
	const auto re_encoded = ff.surface_to_image(surface, {}, matched, ui::image_format::JPEG);
	assert_equal(true, first_dqt(re_encoded->data()) == source_dqt, "re-encode adopts the source tables");

	file_encode_params unmatched;
	unmatched.jpeg_save_quality = 95;
	const auto control = ff.surface_to_image(surface, {}, unmatched, ui::image_format::JPEG);
	assert_equal(false, first_dqt(control->data()) == source_dqt, "quality still applies without a source");
}

// Lossless rotation must refuse rather than trim. Trimming silently drops up to a whole MCU of edge
// pixels the user saw in the preview; refusing sends the save down the re-encode path instead.
static void should_refuse_imperfect_lossless_rotate()
{
	files ff;

	// 4:2:0 chroma puts the MCU grid on 16 pixels, so 20 rows cannot rotate losslessly.
	const auto aligned = ff.surface_to_image(make_gradient_surface(32, 32), {}, {}, ui::image_format::JPEG);
	const auto unaligned = ff.surface_to_image(make_gradient_surface(32, 20), {}, {}, ui::image_format::JPEG);

	jpeg_encoder aligned_encoder;
	const jpeg_decoder_x aligned_decoder;
	assert_equal(false, aligned_decoder.transform(aligned->data(), aligned_encoder, simple_transform::rot_90).empty(),
	             "aligned rotate stays lossless");

	jpeg_encoder unaligned_encoder;
	const jpeg_decoder_x unaligned_decoder;
	assert_equal(true,
	             unaligned_decoder.transform(unaligned->data(), unaligned_encoder, simple_transform::rot_90).empty(),
	             "unaligned rotate refuses rather than trimming");
}

// files holds ONE long-lived encoder for every image it writes, and handle_error_exit throws out of
// libjpeg. An encode abandoned mid-scan therefore leaves global_state at CSTATE_SCANNING, and
// without a reset on entry every later encode - every thumbnail, every Convert - would ERREXIT with
// "Improper call to JPEG library" for the rest of the session.
static void should_reuse_jpeg_encoder_after_abandoned_encode()
{
	jpeg_encoder encoder;
	const auto surface = make_gradient_surface(32, 32);
	const auto dimensions = surface->dimensions();

	// start() leaves the encoder mid-compress, which is the state a throw out of jpeg_write_scanlines
	// or a marker write hands back.
	encoder.start(dimensions.cx, dimensions.cy, ui::orientation::top_left, {}, {});

	const auto encoded = encoder.encode(dimensions.cx, dimensions.cy, surface->pixels(),
	                                    static_cast<uint32_t>(surface->stride()), ui::orientation::top_left, {}, {});

	assert_equal(false, encoded.empty(), "encoder still usable after an abandoned encode");
}

// Offset of the start-of-scan marker, or 0 when there is none.
static size_t sos_offset(const df::cspan jpeg)
{
	for (size_t i = 2; i + 4 < jpeg.size;)
	{
		if (jpeg.data[i] != 0xFF) break;

		const auto marker = jpeg.data[i + 1];

		if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
		{
			i += 2;
			continue;
		}

		if (marker == 0xDA) return i;

		i += 2 + ((static_cast<size_t>(jpeg.data[i + 2]) << 8) | jpeg.data[i + 3]);
	}

	return 0;
}

// A JPEG that ends inside its entropy data makes jpeg_read_coefficients suspend and hand back a
// null coefficient array, which transupp indexes straight into a crash. The rotate must refuse -
// and leave both codecs usable, because files holds one decoder and one encoder for every file.
static void should_survive_truncated_lossless_rotate()
{
	files ff;

	const auto whole = ff.surface_to_image(make_gradient_surface(256, 256), {}, {}, ui::image_format::JPEG);
	const auto& data = whole->data();
	const auto sos = sos_offset(data);

	assert_equal(true, sos > 0 && sos + 64 < data.size(), "test image has scan data to truncate");

	const std::vector<uint8_t> truncated(data.data(), data.data() + sos + 64);

	jpeg_encoder encoder;
	const jpeg_decoder_x decoder;

	assert_equal(true,
	             decoder.transform({truncated.data(), truncated.size()}, encoder, simple_transform::rot_90).empty(),
	             "truncated rotate refuses");

	assert_equal(false, decoder.transform(data, encoder, simple_transform::rot_90).empty(),
	             "decoder and encoder stay usable after the refusal");
}

// jpeg_read_coefficients holds every coefficient of the full-resolution image whatever scale the
// caller asked for, and transupp requests a second array the same size to rotate into. The decode
// budget divides by the scale factor, so it describes none of that - a rotate has to be measured
// against what its coefficients cost, or a hundred-megapixel photograph reaches libjpeg asking for
// gigabytes.
static void should_refuse_an_over_budget_lossless_rotate()
{
	files ff;
	const auto image = ff.surface_to_image(make_gradient_surface(256, 256), {}, {}, ui::image_format::JPEG);

	jpeg_encoder encoder;
	jpeg_decoder_x decoder;

	assert_equal(true, decoder.read_header(image->data()), "header read");
	assert_equal(false, decoder.buffers_whole_image(), "a baseline single-scan jpeg decodes a block at a time");

	// 256 x 256 at 4:2:0 is 32 x 32 luma blocks and 16 x 16 of each chroma, at 128 bytes a block.
	constexpr int64_t expected = (32 * 32 + 2 * 16 * 16) * 128;
	assert_equal(static_cast<uint64_t>(expected), static_cast<uint64_t>(decoder.coefficient_bytes()),
	             "a rotate holds the whole image however little it would decode");

	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });

	df::max_decode_bytes = expected * 2 - 1;
	auto over_budget = false;
	assert_equal(true, decoder.transform(image->data(), encoder, simple_transform::rot_90, &over_budget).empty(),
	             "a rotate one byte over its workspace refuses");
	assert_equal(true, over_budget, "and says the budget is why, which is not an imperfect rotate");

	df::max_decode_bytes = expected * 2;
	assert_equal(false, decoder.transform(image->data(), encoder, simple_transform::rot_90, &over_budget).empty(),
	             "the same rotate is taken when source and workspace both fit");
	assert_equal(false, over_budget, "and is not reported as over budget");
}

// A lossless rotate refused for its budget must not turn into a re-encode. The re-encode holds the
// decoded picture and a rotated copy - more than the rotate was refused for - and gives up quality
// besides, so a JPEG that could have turned losslessly came back lossy from a check meant to bound
// memory. The budget here fits the decode the re-encode would start with, but not the rotate.
static void should_not_re_encode_a_rotate_refused_for_its_budget()
{
	const auto load_path = test_files_folder.combine_file("Lossless0.jpg");
	const auto save_path = _temps.next_path();
	const auto source = df::blob_from_file(load_path);

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header({source.data(), source.size()}), "header read");
	const auto dimensions = decoder.dimensions();
	const auto rotate_bytes = decoder.coefficient_bytes() * 2;

	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });

	df::max_decode_bytes = rotate_bytes - 1;
	assert_equal(true, static_cast<int64_t>(dimensions.cx) * dimensions.cy * 4 <= df::max_decode_bytes,
	             "the decode a re-encode starts with fits the budget");

	image_edits edits;
	const quadd crop(dimensions);
	edits.crop_bounds(crop.transform(simple_transform::rot_90));

	files ff;
	const auto result = ff.update(load_path, save_path, {}, edits, {}, false, {});

	assert_equal(false, result.success(), "the rotate is refused rather than re-encoded");
	assert_equal(false, save_path.exists(), "and nothing is written in its place");
}

// libjpeg reduces while it decodes, which is what makes an enormous stitch affordable as a
// thumbnail - but a progressive stream must first buffer every coefficient of the full-resolution
// image, and no decode scale touches that. Charging only the reduced output let a stream whose
// coefficients alone run to gigabytes pass a budget that said kilobytes.
static void should_charge_a_progressive_jpeg_for_its_coefficients()
{
	files ff;

	// The fixture lives in excluded1 so that adding it does not change the indexed item counts.
	const auto progressive = ff.load(test_files_folder.combine("excluded1").combine_file("Progressive.jpg"), false);
	assert_equal(true, is_valid(progressive.i), "the progressive fixture loads");

	const auto baseline = ff.surface_to_image(make_gradient_surface(256, 256), {}, {}, ui::image_format::JPEG);
	assert_equal(true, is_valid(baseline), "and a baseline control of the same shape encodes");

	// Both are 256 x 256 at 4:2:0. An eighth-scale decode writes 32 x 32 pixels either way, but the
	// progressive one also holds 32 x 32 luma blocks and 16 x 16 of each chroma at 128 bytes a
	// block, whole and unscaled, before it can emit a single row.
	constexpr sizei thumbnail{32, 32};
	constexpr int64_t scaled_output_bytes = 32ll * 32 * 4;
	constexpr int64_t coefficient_bytes = (32 * 32 + 2 * 16 * 16) * 128;

	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });

	df::max_decode_bytes = scaled_output_bytes + coefficient_bytes - 1;
	assert_equal(false, is_valid(ff.image_to_surface(progressive.i, thumbnail)),
	             "a progressive thumbnail one byte short of its coefficients is refused");
	assert_equal(true, is_valid(ff.image_to_surface(baseline, thumbnail)),
	             "a baseline thumbnail of the same picture is not");

	df::max_decode_bytes = scaled_output_bytes + coefficient_bytes;
	assert_equal(true, is_valid(ff.image_to_surface(progressive.i, thumbnail)),
	             "and the progressive one is taken once its coefficients fit");
}

static void should_rotate_lossless()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file("Lossless0.jpg");

	image_edits edits;
	const quadd crop(sizei(640, 480));
	edits.crop_bounds(crop.transform(simple_transform::rot_90));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto expected = extract_properties(test_files_folder.combine_file("Lossless90.jpg"));
	const auto actual = extract_properties(save_path);

	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);
}

// Every 8-bit YCbCr JPEG belongs on the GPU NV12 path; read_nv12 averages whatever chroma the
// source carries down to one pair per 2x2 block. Only formats it cannot pack take the RGB path.
static bool jpeg_uses_nv12(files& ff, const char* const name)
{
	const auto loaded = ff.load(test_files_folder.combine_file(name), false);
	assert_equal(true, is_valid(loaded.i), "loaded jpeg");

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header(loaded.i->data()), "read jpeg header");

	const auto result = decoder.can_render_nv12();
	decoder.start_decompress(1, result, true);
	decoder.close();

	return result;
}

static void should_render_ycbcr_jpeg_as_nv12()
{
	files ff;

	assert_equal(true, jpeg_uses_nv12(ff, "exif-rotated.jpg"), "4:2:0 renders as nv12");
	assert_equal(true, jpeg_uses_nv12(ff, "Small.jpg"), "4:2:2 renders as nv12");
	assert_equal(false, jpeg_uses_nv12(ff, "cmyk.jpg"), "cmyk avoids nv12");
}

// A 1:8 decode - what a very large image gets - returns one iMCU row per call, which for a source
// with no vertical chroma subsampling is a single luma row. read_nv12 averaged two chroma rows per
// output pair out of a buffer holding one, and indexed the pair from the call rather than from the
// image, so each output row took the wrong source row mixed with scratch libjpeg had never written.
//
// The fixture is eight-pixel bands alternating saturated and neutral, so at 1:8 every decoded row
// is one band and a correct decode reads the even, saturated ones. The assertion is on the distance
// between Cr and Cb, which is what each half of the defect destroys: reading the odd band collapses
// it to zero, and mixing in an unwritten row halves it. It is also what the unknown scratch value
// cancels out of - an absolute threshold depends on whatever the heap happened to hold, which is
// how the first version of this test passed against the bug it was written for.
static void should_decode_scaled_422_jpeg_as_nv12()
{
	files ff;

	// Re-encoding through a 4:2:2 source adopts its sampling factors, giving a 4:2:2 image large
	// enough for the smallest scale factor.
	const auto donor = ff.load(test_files_folder.combine_file("Small.jpg"), false);
	assert_equal(true, is_valid(donor.i), "loaded 4:2:2 source");

	// The encoder consumes the surface as BGRX, so this is a saturated primary rather than the blue
	// the argument order reads as. Which primary it is does not matter, only that it is far off
	// neutral in chroma while the alternating band sits exactly on it.
	constexpr auto extent = 1024;
	const auto saturated = ui::rgba(0, 0, 255);
	const auto neutral = ui::rgba(128, 128, 128);

	const auto banded = std::make_shared<ui::surface>();
	banded->alloc(extent, extent, ui::texture_format::RGB);

	for (auto y = 0; y < extent; ++y)
	{
		const auto band = ((y / 8) & 1) == 0 ? saturated : neutral;

		for (auto x = 0; x < extent; ++x)
		{
			banded->set_pixel(x, y, band);
		}
	}

	file_encode_params params;
	params.jpeg_source = donor.i->data();

	const auto encoded = ff.surface_to_image(banded, {}, params, ui::image_format::JPEG);
	assert_equal(true, is_valid(encoded), "encoded jpeg");

	// Without this the test is vacuous: 4:2:0 returns two luma rows per call, never reaches the
	// defect, and averages the two bands together as its own correct answer.
	const auto encoded_path = _temps.next_path(".jpg");
	df::blob_save_to_file(encoded->data(), encoded_path);
	assert_equal("yuv422", ff_scan_file(ff, encoded_path).pixel_format.sv(), "re-encoded as 4:2:2");

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header(encoded->data()), "read jpeg header");
	assert_equal(true, decoder.can_render_nv12(), "scaled 4:2:2 renders as nv12");
	assert_equal(true, decoder.start_decompress(8, true, false), "1:8 decompress starts");

	const auto dims = decoder.dimensions_out();
	ui::surface nv12;
	nv12.alloc({dims.cx & ~1, dims.cy & ~1}, ui::texture_format::NV12);

	const auto decoded = decoder.read_nv12(nv12.pixels(), static_cast<int>(nv12.stride()),
	                                       static_cast<int>(nv12.size()), {});
	decoder.close();

	assert_equal(true, decoded, "decoded nv12 at 1:8");

	auto worst = 255;

	for (auto y = 0u; y < nv12.height() / 2; ++y)
	{
		const auto* const row = nv12.pixels() + nv12.stride() * (nv12.height() + y);

		for (auto x = 0u; x < nv12.width(); x += 2)
		{
			worst = std::min(worst, std::abs(static_cast<int>(row[x + 1]) - static_cast<int>(row[x])));
		}
	}

	assert_equal(true, worst >= 130, std::format("every chroma row reads its own band - lowest |Cr-Cb| {}", worst));
}

// Fixtures for the deep-precision and transfer-function paths are four flat horizontal bands, so a
// correct decode lands on known 8-bit values and a truncating or unconverted one visibly does not.
static void should_decode_bands(const char* const name, const std::initializer_list<int> expected, const int tolerance)
{
	// The fixtures live in excluded1 so that adding them does not change the indexed item counts.
	files ff;
	const auto loaded = ff.load(test_files_folder.combine("excluded1").combine_file(name), false);
	assert_equal(true, is_valid(loaded.i), std::format("loaded {}", name));

	const auto surface = loaded.to_surface();
	assert_equal(true, is_valid(surface), std::format("decoded {}", name));

	const auto cy = surface->height();
	auto band = 0;

	for (const auto want : expected)
	{
		const auto* const row = surface->pixels_line(cy * band / 4 + cy / 8);
		const auto message = std::format("{} band {}: wanted {}, got {},{},{},{}", name, band, want,
		                                 row[0], row[1], row[2], row[3]);

		for (auto c = 0; c < 3; c++)
		{
			assert_equal(true, std::abs(static_cast<int>(row[c]) - want) <= tolerance, message);
		}

		++band;
	}
}

// 12-bit lossy and 16-bit lossless JPEGs used to fail to load outright.
static void should_decode_deep_precision_jpeg(const char* const name)
{
	should_decode_bands(name, {0, 85, 170, 255}, 2);
}

// Scaling 16-bit samples gives 1, 33, 65, 97 where the old truncation gave 0, 32, 64, 96, so this
// only tells the two apart if it demands the exact value.
static void should_scale_16bit_png()
{
	should_decode_bands("deep16.png", {1, 33, 65, 97}, 0);
}

// gamma.png declares gAMA 1.0, so its linear samples need an ~1/2.2 encode for an sRGB display.
static void should_apply_png_gamma()
{
	should_decode_bands("gamma.png", {0, 136, 186, 224}, 2);
}

static void append_png_be32(df::blob& out, const uint32_t value)
{
	out.push_back(static_cast<uint8_t>(value >> 24));
	out.push_back(static_cast<uint8_t>(value >> 16));
	out.push_back(static_cast<uint8_t>(value >> 8));
	out.push_back(static_cast<uint8_t>(value));
}

static void insert_png_chunk_before_iend(df::blob& png, const char (&type)[5], const df::cspan data)
{
	df::blob chunk;
	append_png_be32(chunk, static_cast<uint32_t>(data.size));
	const auto type_start = chunk.size();
	chunk.insert(chunk.end(), type, type + 4);
	chunk.insert(chunk.end(), data.data, data.data + data.size);

	auto crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, chunk.data() + type_start, static_cast<uInt>(4 + data.size));
	append_png_be32(chunk, crc);

	assert_equal(true, png.size() >= 12u, "png has an IEND chunk");
	png.insert(png.end() - 12, chunk.begin(), chunk.end());
}

// SRC-019 - pixel decode must finish metadata reading so eXIf after IDAT still orients the loaded
// surface.
static void should_read_png_orientation_after_idat()
{
	files ff;
	const auto image = ff.surface_to_image(make_gradient_surface(16, 8), {}, {}, ui::image_format::PNG);
	const auto& encoded = image->data();
	df::blob png(encoded.data(), encoded.data() + encoded.size());

	auto exif = make_orientation_exif(ui::orientation::right_top);
	exif.insert(exif.begin(), exif_signature.begin(), exif_signature.end());
	insert_png_chunk_before_iend(png, "eXIf", {exif.data(), exif.size()});

	const auto path = _temps.next_path(".png");
	df::blob_save_to_file(png, path);

	const auto loaded = load_png(df::blob_from_file(path));
	assert_equal(true, is_valid(loaded), "png loaded");
	assert_equal(static_cast<int>(ui::orientation::right_top), static_cast<int>(loaded->orientation()),
	             "trailing eXIf orientation");
}

static void should_decode_12bit_gray_jpeg()
{
	should_decode_deep_precision_jpeg("deep12gray.jpg");
}

static void should_decode_12bit_colour_jpeg()
{
	should_decode_deep_precision_jpeg("deep12.jpg");
}

static void should_decode_16bit_gray_jpeg()
{
	should_decode_deep_precision_jpeg("deep16gray.jpg");
}

static void should_decode_16bit_colour_jpeg()
{
	should_decode_deep_precision_jpeg("deep16.jpg");
}

static void should_decode_thin_jpeg_when_planar_is_preferred()
{
	files ff;

	const auto decode = [&](const int cx, const int cy, const sizei target_extent, const std::string_view name)
	{
		const auto encoded = ff.surface_to_image(make_gradient_surface(cx, cy), {}, {}, ui::image_format::JPEG);
		assert_equal(true, is_valid(encoded), std::format("{} encoded", name));

		const auto packed = ff.image_to_surface(encoded, target_extent, false, {}, decode_intent::display);
		assert_equal(true, ui::is_valid(packed), std::format("{} packed decode succeeds", name));

		const auto planar_preferred = ff.image_to_surface(encoded, target_extent, true, {}, decode_intent::display);
		assert_equal(true, ui::is_valid(planar_preferred), std::format("{} planar-preferred decode succeeds", name));
		assert_equal(true, planar_preferred->format() == ui::texture_format::RGB,
		             std::format("{} falls back to packed RGB", name));
		assert_equal(true, packed->dimensions() == planar_preferred->dimensions(),
		             std::format("{} keeps packed dimensions", name));
		assert_equal(packed->orientation(), planar_preferred->orientation(),
		             std::format("{} keeps orientation", name));
	};

	decode(1, 8, {}, "one-pixel width");
	decode(8, 1, {}, "one-pixel height");
	decode(8, 8, {1, 1}, "scaled to one pixel");
}

// The pixel format is what the properties panel and list rows show, and it is indexed for search.
// Chroma subsampling is a headline property of a JPEG, so it belongs in that name - and it has to
// use the same words HEIF, WebP and video already use or a search for one finds only some of them.
static void should_report_jpeg_chroma_subsampling()
{
	files ff;

	const auto reported = [&ff](const char* const name)
	{
		return ff_scan_file(ff, test_files_folder.combine_file(name)).pixel_format;
	};

	assert_equal("yuv420", reported("exif-rotated.jpg").sv(), "4:2:0 jpeg");
	assert_equal("yuv422", reported("Small.jpg").sv(), "4:2:2 jpeg");
	assert_equal("ycck", reported("cmyk.jpg").sv(), "adobe ycck jpeg");
}

struct webp_chunk
{
	std::string tag;
	df::blob data;
};

static std::vector<webp_chunk> read_webp_chunks(const df::file_path path)
{
	file_read_stream fs;
	assert_equal(true, fs.open(path), "open webp");

	const auto bytes = fs.read_all();
	assert_equal(true, bytes.size() > 12u, "webp larger than the RIFF header");

	std::vector<webp_chunk> result;

	for (auto pos = size_t{12}; pos + 8 <= bytes.size();)
	{
		uint32_t len = 0;
		memcpy(&len, bytes.data() + pos + 4, 4);

		const auto payload = pos + 8;
		if (len > bytes.size() - payload) break; // subtract, so a 32-bit size_t cannot wrap

		result.emplace_back(std::string(reinterpret_cast<const char*>(bytes.data() + pos), 4),
		                    df::blob(bytes.begin() + payload, bytes.begin() + payload + len));
		pos = payload + len + (len & 1);
	}

	return result;
}

// Regression guard for the WebP metadata rewrite. The XMP handler used to re-emit
// chunks grouped by category and then truncate the file, so a save could move the
// XMP packet on top of image or alpha data and destroy the picture. Saving metadata
// must leave every non-XMP chunk byte-identical and in its original position, and
// the image must still decode unchanged.
static void should_preserve_webp_chunks_on_metadata_save()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("lake.webp");
	const auto save_path = _temps.next_path(".webp");

	const auto original = ff.load(load_path, false);
	assert_equal(false, original.is_empty(), "original webp loaded");

	metadata_edits first;
	first.rating = 3;
	ff.update(load_path, save_path, first, {}, {}, false, {});
	const auto after_first = read_webp_chunks(save_path);

	metadata_edits second;
	second.rating = 5;
	ff.update(save_path, second, {}, {}, false, {});
	const auto after_second = read_webp_chunks(save_path);

	assert_equal(after_first.size(), after_second.size(), "chunk count unchanged");
	assert_equal(true, after_first.size() >= 3u, "extended webp has image and metadata chunks");
	assert_equal(true, std::ranges::any_of(after_first, [](const webp_chunk& c) { return c.tag == "XMP "; }),
	             "xmp chunk written");

	for (auto i = size_t{0}; i < after_first.size() && i < after_second.size(); ++i)
	{
		assert_equal(after_first[i].tag, after_second[i].tag, std::format("chunk {} tag", i));

		if (after_first[i].tag != "XMP ")
		{
			assert_equal(true, after_first[i].data == after_second[i].data,
			             std::format("chunk {} {} bytes unchanged", i, after_first[i].tag));
		}
	}

	const auto reloaded = ff.load(save_path, false);
	assert_equal(false, reloaded.is_empty(), "webp still decodes");
	assert_equal(original.i->width(), reloaded.i->width(), "width preserved");
	assert_equal(original.i->height(), reloaded.i->height(), "height preserved");

	const auto scanned = ff_scan_file(ff, save_path);
	assert_equal(5, scanned.to_props()->rating, "rating written");
}

// Regression guard for the WebP save-quality path. The editor maps the user's
// save settings (setting.webp_lossless / setting.webp_quality) onto
// file_encode_params before writing; a swapped or ignored knob would silently
// bloat or degrade the images users save. Verify the encoder honours both:
// lossless produces a substantially larger file than a heavily compressed lossy
// encode of the same pixels, and a higher quality produces a larger file than
// the lowest quality - proving each parameter actually takes effect.
static void should_honor_webp_save_quality()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto source = ff.load(load_path, false);
	assert_equal(false, source.is_empty(), "source loaded");

	const auto lossless_path = _temps.next_path(".webp");
	{
		file_encode_params params;
		params.webp_lossless = true;
		ff.update(load_path, lossless_path, {}, {}, params, false, {});
	}

	const auto low_quality_path = _temps.next_path(".webp");
	{
		file_encode_params params;
		params.webp_lossless = false;
		params.webp_quality = 1;
		ff.update(load_path, low_quality_path, {}, {}, params, false, {});
	}

	const auto high_quality_path = _temps.next_path(".webp");
	{
		file_encode_params params;
		params.webp_lossless = false;
		params.webp_quality = 95;
		ff.update(load_path, high_quality_path, {}, {}, params, false, {});
	}

	const auto lossless_size = platform::file_attributes(lossless_path).size;
	const auto low_quality_size = platform::file_attributes(low_quality_path).size;
	const auto high_quality_size = platform::file_attributes(high_quality_path).size;

	assert_equal(true, lossless_size > 0 && low_quality_size > 0 && high_quality_size > 0, "webp files written");

	// Lossless keeps every detail, so it must be much larger than a heavily
	// compressed lossy encode of the same pixels.
	assert_equal(true, lossless_size > low_quality_size, "webp lossless larger than low quality");

	// Higher quality retains more detail, so it must be larger than the lowest
	// quality - this proves webp_quality is applied (and not treated as a bool).
	assert_equal(true, high_quality_size > low_quality_size, "webp high quality larger than low quality");
}

// The WebP loader used to tag every surface ARGB, which forces the renderer down
// the alpha-blended path for images that are entirely opaque. Verify the decoded
// format follows the bitstream: opaque in, RGB out; alpha in, ARGB out.
static void should_tag_webp_surface_alpha()
{
	const auto opaque_path = test_files_folder.combine_file("lake.webp");
	const auto opaque_data = df::blob_from_file(opaque_path);
	assert_equal(false, opaque_data.empty(), "opaque webp read");

	const auto opaque_surface = load_webp(opaque_data);
	assert_equal(true, is_valid(opaque_surface), "opaque webp decoded");
	assert_equal(true, opaque_surface->format() == ui::texture_format::RGB, "opaque webp surface is RGB");

	const auto opaque_scan = scan_webp(opaque_data, true);
	assert_equal(1u, static_cast<uint32_t>(opaque_scan.frames.size()), "opaque webp frame count");
	assert_equal(true, opaque_scan.frames[0]->format() == ui::texture_format::RGB, "opaque webp scan is RGB");

	const auto transparent = std::make_shared<ui::surface>();
	const auto* const pixels = transparent->alloc(16, 16, ui::texture_format::ARGB);
	assert_equal(true, pixels != nullptr, "alpha surface allocated");

	for (auto y = 0; y < 16; ++y)
	{
		auto* const line = std::bit_cast<ui::color32*>(transparent->pixels_line(y));

		for (auto x = 0; x < 16; ++x)
		{
			// BGRA in memory: alpha ramps across the row so the encode keeps an alpha plane.
			line[x] = (static_cast<ui::color32>(x * 16) << 24) | 0x00FF8040u;
		}
	}

	file_encode_params params;
	params.webp_lossless = true;
	const auto encoded = save_webp(transparent, {}, params);
	assert_equal(true, is_valid(encoded), "alpha webp encoded");

	const auto decoded = load_webp(encoded->data());
	assert_equal(true, is_valid(decoded), "alpha webp decoded");
	assert_equal(true, decoded->format() == ui::texture_format::ARGB, "alpha webp surface is ARGB");
}

static void should_decode_webp_at_the_requested_size()
{
	const auto data = df::blob_from_file(test_files_folder.combine_file("lake.webp"));

	const auto full = load_webp(data);
	assert_equal(true, is_valid(full), "the fixture decodes");

	const auto native = full->dimensions();
	const sizei target{native.cx / 4, native.cy / 4};

	// libwebp rescales inside the decoder, so the surface comes back at the size asked for rather
	// than at the file's size with a downscale still owed. Every caller of image_to_surface asks
	// for a bounded size, so this is the ordinary path, not a special one.
	const auto scaled = load_webp(data, false, target);
	assert_equal(true, is_valid(scaled), "a reduced decode succeeds");
	assert_equal(true, scaled->dimensions().cx <= target.cx, "the decode fits inside the requested width");
	assert_equal(true, scaled->dimensions().cy <= target.cy, "and inside the requested height");
	assert_equal(true, scaled->dimensions().cx > native.cx / 8, "and is not scaled past the target");
	assert_equal(true, scaled->format() == full->format(), "and keeps the format the file earned");

	// Aspect is preserved rather than stretched to the box, so a reduced decode frames the picture
	// the same way the full one does.
	const auto full_aspect = static_cast<double>(native.cx) / native.cy;
	const auto scaled_aspect = static_cast<double>(scaled->dimensions().cx) / scaled->dimensions().cy;
	assert_near(full_aspect, scaled_aspect, 0.02, "the reduced decode keeps the aspect ratio");

	// A target at or above the source is left alone, so nothing is ever enlarged into.
	const auto larger = load_webp(data, false, {native.cx * 2, native.cy * 2});
	assert_equal(native.cx, larger->dimensions().cx, "a larger target does not enlarge");

	// The reduced decode must produce the same picture as decoding in full and scaling afterwards,
	// which is the path it replaces. What it saves is the colour conversion and the scaler pass;
	// libwebp still walks the whole bitstream, so on a small file that saving is inside the noise and
	// is deliberately not asserted here.
	files ff;
	const auto scaled_after = ff.fit_within(load_webp(data, false), target);
	assert_equal(scaled_after->dimensions().cx, scaled->dimensions().cx,
	             "a reduced decode agrees with decode-then-scale on width");
	assert_equal(scaled_after->dimensions().cy, scaled->dimensions().cy,
	             "a reduced decode agrees with decode-then-scale on height");
}

static void should_decode_opaque_lossy_webp_as_nv12()
{
	const auto data = df::blob_from_file(test_files_folder.combine_file("lake.webp"));
	const auto rgb = load_webp(data, false);
	const auto nv12 = load_webp(data, true);

	assert_equal(true, is_valid(rgb) && rgb->format() == ui::texture_format::RGB, "webp RGB fallback decoded");
	assert_equal(true, is_valid(nv12) && nv12->format() == ui::texture_format::NV12, "webp NV12 decoded");
	assert_equal(true, nv12->size() * 2 < rgb->size(), "webp NV12 uses less than half the RGB surface memory");
	assert_equal(true, nv12->color_space() == ui::color_space::rec601_limited, "webp NV12 color space");

	const auto converted = std::make_shared<ui::surface>();
	av_scaler scaler;
	assert_equal(true, scaler.convert_yuv_surface(*nv12, converted), "webp NV12 converts for comparison");

	uint64_t total_difference = 0;
	const auto dimensions = rgb->dimensions();

	for (auto y = 0; y < dimensions.cy; ++y)
	{
		const auto* const expected = rgb->pixels_line(y);
		const auto* const actual = converted->pixels_line(y);

		for (auto x = 0; x < dimensions.cx * 4; x += 4)
		{
			for (auto channel = 0; channel < 3; ++channel)
			{
				total_difference += std::abs(static_cast<int>(expected[x + channel]) - actual[x + channel]);
			}
		}
	}

	const auto average_difference = static_cast<double>(total_difference) / (dimensions.cx * dimensions.cy * 3);
	assert_equal(true, average_difference < 3.0,
	             std::format("webp NV12 average RGB difference: {}", average_difference));

	// VP8 encodes YCbCr 4:2:0, so an NV12 surface is handed over as planes rather than converted to
	// packed pixels for libwebp to convert straight back. The picture has to survive that, which is
	// what catches a wrong plane order, a missed de-interleave or a skipped range conversion.
	{
		file_encode_params planar_params;
		planar_params.webp_quality = thumbnail_webp_quality;
		planar_params.webp_fast = true;

		const auto planar_encoded = save_webp(nv12, {}, planar_params);
		assert_equal(true, is_valid(planar_encoded), "nv12 encodes as webp through the planar path");

		files planar_ff;
		const auto round_trip = planar_ff.image_to_surface(planar_encoded, {}, false);
		assert_equal(true, ui::is_valid(round_trip), "the planar webp decodes again");
		assert_equal(true, round_trip->dimensions() == dimensions, "the planar webp keeps its size");

		uint64_t planar_difference = 0;

		for (auto y = 0; y < dimensions.cy; ++y)
		{
			const auto* const expected = rgb->pixels_line(y);
			const auto* const actual = round_trip->pixels_line(y);

			for (auto x = 0; x < dimensions.cx * 4; x += 4)
			{
				for (auto channel = 0; channel < 3; ++channel)
				{
					planar_difference += std::abs(static_cast<int>(expected[x + channel]) - actual[x + channel]);
				}
			}
		}

		const auto planar_average = static_cast<double>(planar_difference) / (dimensions.cx * dimensions.cy * 3);
		// Measured 2.07: one further lossy generation of an already lossy source. A wrong plane order
		// or a missed de-interleave is a colour swap, which lands an order of magnitude above this.
		assert_equal(true, planar_average < 6.0,
		             std::format("planar webp round trip average difference: {}", planar_average));
	}

	files ff;
	const auto image = std::make_shared<ui::image>(df::cspan(data), dimensions, ui::image_format::WEBP,
	                                              ui::orientation::top_left);
	const auto dispatched = ff.image_to_surface(image, {}, true);
	assert_equal(true, is_valid(dispatched) && dispatched->format() == ui::texture_format::NV12,
	             "webp image dispatch preserves NV12");

	const auto target_extent = sizei{32, 32};
	const auto scaled = ff.image_to_surface(image, target_extent, true);
	assert_equal(true, is_valid(scaled) && scaled->format() == ui::texture_format::RGB,
	             "webp NV12 downscale produces target RGB");
	assert_equal(true, ui::scale_dimensions(dimensions, target_extent) == scaled->dimensions(),
	             "webp downscale honors target extent");
}

// A JPEG thumbnail used to be decoded YCbCr -> packed by libjpeg, reduced, then converted packed ->
// YCbCr again by libwebp before VP8 saw it. Two conversions, the first at the DCT-scaled size, both
// discarded. Keeping the planes throughout is what removes them, and the reduction has to stay
// planar for that to hold - which is what this pins.
static void should_thumbnail_a_jpeg_without_leaving_yuv()
{
	files ff;
	const auto data = df::blob_from_file(test_files_folder.combine_file("Test.jpg"));
	assert_equal(true, !data.empty(), "loaded jpeg bytes");

	constexpr sizei ceiling{256, 256};
	const auto surface = ff.image_to_surface(data, ceiling, true, decode_intent::thumbnail);

	assert_equal(true, ui::is_valid(surface), "thumbnail decode produced a surface");
	assert_equal(true, surface->format() == ui::texture_format::NV12, "a 4:2:0 jpeg stays planar to the encoder");
	assert_equal(true, surface->color_space() == ui::color_space::rec601_full, "jpeg planes are full range");

	const auto extent = surface->dimensions();
	assert_equal(true, extent.cx <= ceiling.cx && extent.cy <= ceiling.cy, "the planar reduction honours the ceiling");
	assert_equal(true, ((extent.cx | extent.cy) & 1) == 0, "the planar reduction lands on an even extent");

	const auto thumb = ff.surface_to_thumbnail(surface);
	assert_equal(true, is_valid(thumb) && thumb->format() == ui::image_format::WEBP,
	             "the planar surface encodes as webp");

	// The range conversion is the one thing planes do not carry across: libwebp's YUV is limited
	// range and VP8 signals none, so a full-range source handed over untouched comes back with
	// crushed blacks. Compare against the packed decode of the same file, which never left full range.
	const auto packed = ff.image_to_surface(data, ceiling, false, decode_intent::thumbnail);
	const auto decoded = ff.image_to_surface(thumb, {}, false);

	assert_equal(true, ui::is_valid(packed) && ui::is_valid(decoded), "both comparison surfaces decoded");

	// The planar reduction rounds down to an even extent, so the two can differ by a pixel on an
	// axis. Compare the region they share.
	const auto common = sizei{
		std::min(packed->dimensions().cx, decoded->dimensions().cx),
		std::min(packed->dimensions().cy, decoded->dimensions().cy)
	};

	assert_equal(true, packed->dimensions().cx - common.cx <= 1 && packed->dimensions().cy - common.cy <= 1,
	             "the thumbnail is within a pixel of the packed decode on each axis");

	uint64_t difference = 0;

	for (auto y = 0; y < common.cy; ++y)
	{
		const auto* const expected = packed->pixels_line(y);
		const auto* const actual = decoded->pixels_line(y);

		for (auto x = 0; x < common.cx * 4; x += 4)
		{
			for (auto channel = 0; channel < 3; ++channel)
			{
				difference += std::abs(static_cast<int>(expected[x + channel]) - actual[x + channel]);
			}
		}
	}

	// Measured 3.82 with the range conversion and 8.05 without it, so this separates the two cleanly.
	const auto average = static_cast<double>(difference) / (common.cx * common.cy * 3);
	assert_equal(true, average < 5.5, std::format("planar thumbnail average difference: {}", average));
}

// The caller decides the pixel format, because only it knows whether these pixels are bound for a
// texture, for a luma plane or for a packed reader. setting.use_yuv is the durable record of a
// driver fault and reaches the decoders only through ui::yuv_textures_enabled at the display call
// site - a decoder that reads it directly would also force packed on an analysis or thumbnail
// decode that never goes near a texture.
static void should_let_the_caller_choose_the_decoded_pixel_format()
{
	const auto saved = setting.use_yuv;
	const df::scope_exit restore([saved] { setting.use_yuv = saved; });

	files ff;
	const auto webp = df::blob_from_file(test_files_folder.combine_file("lake.webp"));
	const auto jpeg = ff.load(test_files_folder.combine_file("exif-rotated.jpg"), false);
	assert_equal(true, is_valid(jpeg.i), "loaded jpeg");

	const auto webp_yuv = load_webp(webp, true);
	const auto jpeg_yuv = jpeg.to_surface({}, true);
	assert_equal(true, is_valid(webp_yuv) && webp_yuv->format() == ui::texture_format::NV12,
	             "webp nv12 when the caller allows it");
	assert_equal(true, is_valid(jpeg_yuv) && jpeg_yuv->format() == ui::texture_format::NV12,
	             "jpeg nv12 when the caller allows it");

	const auto webp_packed = load_webp(webp, false);
	const auto jpeg_packed = jpeg.to_surface({}, false);
	assert_equal(true, is_valid(webp_packed) && webp_packed->format() == ui::texture_format::RGB,
	             "webp rgb when the caller refuses planar");
	assert_equal(true, is_valid(jpeg_packed) && jpeg_packed->format() == ui::texture_format::RGB,
	             "jpeg rgb when the caller refuses planar");

	// The latch belongs to the texture upload, not to the decode. A thumbnail or analysis decode on
	// a machine whose driver faulted must still be free to take the cheaper planar path.
	setting.use_yuv = false;
	const auto webp_latched = load_webp(webp, true);
	const auto jpeg_latched = jpeg.to_surface({}, true);
	assert_equal(true, is_valid(webp_latched) && webp_latched->format() == ui::texture_format::NV12,
	             "webp still honours the caller with the fault latch set");
	assert_equal(true, is_valid(jpeg_latched) && jpeg_latched->format() == ui::texture_format::NV12,
	             "jpeg still honours the caller with the fault latch set");
}

// A thumbnail scaled to fit a box is regularly odd on one axis. decode_jpeg crops those to even and
// still reaches the GPU as NV12; a webp decoder that instead falls back to RGB costs 4 bytes per
// pixel rather than 1.5, for the majority of a collection's thumbnails.
static void should_decode_odd_sized_webp_as_nv12()
{
	constexpr sizei odd_extent{321, 215};
	const auto surface = std::make_shared<ui::surface>();
	assert_equal(true, surface->alloc(odd_extent, ui::texture_format::RGB) != nullptr, "allocated odd surface");

	for (auto y = 0; y < odd_extent.cy; ++y)
	{
		for (auto x = 0; x < odd_extent.cx; ++x)
		{
			surface->set_pixel(x, y, ui::rgba(x & 0xff, y & 0xff, (x + y) & 0xff));
		}
	}

	file_encode_params params;
	params.webp_quality = thumbnail_webp_quality;
	params.webp_fast = true;

	const auto encoded = save_webp(surface, {}, params);
	assert_equal(true, is_valid(encoded), "encoded odd webp");

	const auto decoded = load_webp(encoded->data(), true);
	assert_equal(true, is_valid(decoded) && decoded->format() == ui::texture_format::NV12,
	             "odd webp decodes as nv12");
	assert_equal(320, decoded->dimensions().cx, "odd webp width cropped to even");
	assert_equal(214, decoded->dimensions().cy, "odd webp height cropped to even");
}

// The shell returns 32-bit BGRA even for photo thumbnails, which are opaque, so a stored format
// chosen from the surface tag sent every cloud thumbnail down the PNG branch at several times the
// bytes. An opaque thumbnail must carry no alpha plane, or it also loses the NV12 decode path.
static void should_keep_thumbnail_alpha_only_when_needed()
{
	files ff;
	const auto loaded = ff.load(test_files_folder.combine_file("Test.jpg"), false);
	const auto photo = loaded.to_surface(setting.thumbnail_max_dimension, false, {}, decode_intent::thumbnail);
	assert_equal(true, ui::is_valid(photo), "loaded photo surface");

	const auto extent = photo->dimensions();
	const auto opaque = std::make_shared<ui::surface>();
	const auto translucent = std::make_shared<ui::surface>();
	assert_equal(true, opaque->alloc(extent, ui::texture_format::ARGB) != nullptr, "allocated opaque surface");
	assert_equal(true, translucent->alloc(extent, ui::texture_format::ARGB) != nullptr,
	             "allocated translucent surface");

	for (auto y = 0; y < extent.cy; ++y)
	{
		for (auto x = 0; x < extent.cx; ++x)
		{
			const auto c = photo->get_pixel(x, y);
			const auto rgb = ui::rgba(ui::get_r(c), ui::get_g(c), ui::get_b(c));
			opaque->set_pixel(x, y, rgb);
			translucent->set_pixel(x, y, x < extent.cx / 2 ? rgb & 0x00ffffff : rgb);
		}
	}

	const auto opaque_thumb = ff.surface_to_thumbnail(opaque);
	const auto translucent_thumb = ff.surface_to_thumbnail(translucent);

	assert_equal(true, is_valid(opaque_thumb) && opaque_thumb->format() == ui::image_format::WEBP,
	             "opaque thumbnail is stored as webp");
	assert_equal(true, is_valid(translucent_thumb) && translucent_thumb->format() == ui::image_format::WEBP,
	             "translucent thumbnail is stored as webp");

	// An opaque thumbnail that still carries an alpha plane cannot take the NV12 path.
	const auto opaque_surface = ff.image_to_surface(opaque_thumb, {}, true);
	assert_equal(true, ui::is_valid(opaque_surface) && opaque_surface->format() == ui::texture_format::NV12,
	             "opaque thumbnail drops its alpha plane and decodes as nv12");

	const auto translucent_surface = ff.image_to_surface(translucent_thumb, {}, true);
	assert_equal(true, ui::is_valid(translucent_surface) &&
	             translucent_surface->format() == ui::texture_format::ARGB,
	             "translucent thumbnail keeps its alpha plane");
	assert_equal(true, ui::get_a(translucent_surface->get_pixel(extent.cx / 4, extent.cy / 2)) < 128,
	             "transparent half survives the thumbnail round trip");
	assert_equal(true, ui::get_a(translucent_surface->get_pixel(extent.cx * 3 / 4, extent.cy / 2)) > 200,
	             "opaque half survives the thumbnail round trip");

	const auto png = save_png(translucent, {});
	assert_equal(true, is_valid(png), "encoded png reference");
	assert_equal(true, translucent_thumb->data().size() * 2 < png->data().size(),
	             "webp thumbnail is far smaller than the png it replaces");
}

static void should_refuse_truncated_webp_decode()
{
	const auto data = df::blob_from_file(test_files_folder.combine_file("lake.webp"));
	auto truncated_size = 0_z;

	for (auto size = 12_z; size < data.size(); ++size)
	{
		WebPBitstreamFeatures features;
		if (WebPGetFeatures(data.data(), size, &features) == VP8_STATUS_OK)
		{
			truncated_size = size;
			break;
		}
	}

	assert_equal(true, truncated_size > 0, "truncated webp retains a readable header");
	assert_equal(true, !is_valid(load_webp({data.data(), truncated_size})),
	             "truncated webp does not return an allocated partial surface");
}

static df::blob make_test_animated_webp()
{
	constexpr auto width = 16;
	constexpr auto height = 16;
	WebPAnimEncoderOptions options;
	if (!WebPAnimEncoderOptionsInit(&options)) return {};

	auto* const encoder = WebPAnimEncoderNew(width, height, &options);
	if (!encoder) return {};
	const df::releaser<WebPAnimEncoder> encoder_releaser(encoder, [](auto* i) { WebPAnimEncoderDelete(i); });

	WebPConfig config;
	if (!WebPConfigInit(&config)) return {};
	config.lossless = 1;
	config.quality = 100;

	std::array<uint32_t, width * height> pixels;
	const auto add_frame = [&](const uint32_t color, const int timestamp)
	{
		pixels.fill(color);
		WebPPicture picture;
		if (!WebPPictureInit(&picture)) return false;
		const df::scope_exit free_picture([&picture] { WebPPictureFree(&picture); });
		picture.width = width;
		picture.height = height;
		picture.use_argb = true;
		return WebPPictureImportBGRA(&picture, std::bit_cast<const uint8_t*>(pixels.data()), width * 4) &&
			WebPAnimEncoderAdd(encoder, &picture, timestamp, &config);
	};

	if (!add_frame(0xff102040, 0) || !add_frame(0xffc08020, 100) ||
		!WebPAnimEncoderAdd(encoder, nullptr, 350, nullptr))
	{
		return {};
	}

	WebPData encoded;
	WebPDataInit(&encoded);
	if (!WebPAnimEncoderAssemble(encoder, &encoded)) return {};
	const df::scope_exit clear_encoded([&encoded] { WebPDataClear(&encoded); });
	return {encoded.bytes, encoded.bytes + encoded.size};
}

static void should_bound_and_time_animated_webp()
{
	const auto data = make_test_animated_webp();
	assert_equal(true, !data.empty(), "animated webp encoded");

	const auto decoded = scan_webp(data, true);
	assert_equal(2u, static_cast<uint32_t>(decoded.frames.size()), "animated webp frame count");
	assert_equal(true, std::abs(decoded.frames[0]->time() - 0.1) < 0.001, "animated webp first timestamp");
	assert_equal(true, std::abs(decoded.frames[1]->time() - 0.35) < 0.001, "animated webp second timestamp");

	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });
	const auto frame_bytes = 16ll * 16ll * 4ll;
	df::max_decode_bytes = frame_bytes * 3;
	const auto bounded = scan_webp(data, true);
	assert_equal(1u, static_cast<uint32_t>(bounded.frames.size()),
	             "animated webp budget includes two decoder canvases");

	auto corrupt = data.clone();
	auto frame = 0;
	for (auto offset = 12_z; offset + 32 < corrupt.size();)
	{
		const auto chunk_size = static_cast<size_t>(corrupt[offset + 4]) |
			(static_cast<size_t>(corrupt[offset + 5]) << 8) |
			(static_cast<size_t>(corrupt[offset + 6]) << 16) |
			(static_cast<size_t>(corrupt[offset + 7]) << 24);

		if (memcmp(corrupt.data() + offset, "ANMF", 4) == 0 && ++frame == 2)
		{
			corrupt[offset + 32] ^= 0xff;
			break;
		}

		offset += 8 + chunk_size + (chunk_size & 1);
	}

	df::max_decode_bytes = restore_budget;
	const auto malformed = scan_webp(corrupt, true);
	assert_equal(true, malformed.frames.size() < 2, "malformed animated webp terminates on decode failure");
}

// A file larger than can be held in memory used to be read as its first 100 MiB. The codec decoded
// what it was given and filled the missing rows itself, so the frame ended in a black tail - and on
// the edit path that frame was re-encoded straight over the user's original.
static void should_refuse_a_file_too_large_to_hold()
{
	const auto path = _temps.next_path(".jpg");

	// A valid JPEG header followed by enough bytes to pass the ceiling. Nothing decodes it; the
	// refusal happens on the file's size, before a byte is read.
	{
		const auto source = df::blob_from_file(test_files_folder.combine_file("Test.jpg"));
		assert_equal(false, source.empty(), "the fixture loaded");

		std::ofstream f(platform::to_stream_path(path), std::ios::binary | std::ios::trunc);
		f.write(std::bit_cast<const char*>(source.data()), static_cast<std::streamsize>(source.size()));

		const std::vector<char> filler(1024 * 1024, 0);
		auto written = source.size();

		while (written <= static_cast<size_t>(df::max_blob_size))
		{
			f.write(filler.data(), static_cast<std::streamsize>(filler.size()));
			written += filler.size();
		}
	}

	assert_equal(true, platform::file_attributes(path).size > static_cast<uint64_t>(df::max_blob_size),
	             "the fixture is past the ceiling");

	files ff;
	const auto loaded = ff.load(path, false);

	assert_equal(false, loaded.success, "an oversized file does not load");
	assert_equal(true, loaded.reason == file_load_result::failure::too_large,
	             "and says so as a property of the file rather than as an unreadable one");
	assert_equal(false, is_valid(loaded.i), "no truncated image is handed back");

	// The read itself refuses too, so no caller can obtain a prefix by accident. A caller that
	// asks for one by name still gets exactly what it asked for.
	auto threw = false;

	try
	{
		df::blob_from_file(path);
	}
	catch (const app_exception&)
	{
		threw = true;
	}

	assert_equal(true, threw, "reading the whole file refuses rather than truncating");
	assert_equal(1024_z, df::blob_head_from_file(path, 1024).size(), "an explicit prefix is still served");

	platform::delete_file(path);
}

static void should_convert_raw_to_jpeg()
{
	const auto load_path = test_files_folder.combine("raw").combine_file("Screws.CR2");
	const auto save_path = _temps.next_path(".jpg");

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path);

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	assert_equal(expected->tags, actual->tags, "tags");
	assert_equal(expected->title, actual->title, "title");
	assert_equal(expected->description, actual->description, "description");
	assert_equal(expected->width, actual->width, "width");
	assert_equal(expected->height, actual->height, "height");
}

// Diffractor reads these and cannot write any of them, so there is no round trip to lean on: the
// fixtures are written by tools/make_test_images.py and the decode is checked against what that
// script drew. A gradient with a red corner is what makes a transposed, mirrored or channel-swapped
// decode fail here rather than merely look wrong later.
static void should_decode_a_read_only_format(const std::string_view name)
{
	files ff;
	const auto path = test_formats_folder.combine_file(name);
	const auto loaded = ff.load(path, false);

	// These decode during load rather than on demand, so the surface is what comes back.
	const auto surface = loaded.s;
	assert_equal(true, is_valid(surface), std::format("{} decoded", name));

	if (!is_valid(surface)) return;

	assert_equal(32, surface->dimensions().cx, std::format("{} width", name));
	assert_equal(24, surface->dimensions().cy, std::format("{} height", name));

	// The greyscale fixture carries one channel, so only the geometry above is comparable.
	if (name.ends_with(".pgm")) return;

	// Read by memory position: a surface is BGRA in memory, while ui::color32 spells the same bytes
	// the other way round, so naming the channels here is what keeps the check legible.
	const auto* const first = surface->pixels_line(0);
	const auto* const last = surface->pixels_line(surface->dimensions().cy - 1) +
		static_cast<size_t>(surface->dimensions().cx - 1) * 4;

	assert_equal(true, first[2] > 200 && first[1] < 60 && first[0] < 60,
	             std::format("{} top left is the red corner", name));
	assert_equal(true, last[0] > 100 && last[0] < 160, std::format("{} carries the blue channel", name));
}

static void should_save(const std::string_view ext, const bool should_support_metadata)
{
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file("Test.jpg");

	files ff;
	constexpr image_edits color;
	ff.update(load_path, save_path, {}, color, {}, false, {});

	const auto expected = extract_properties(load_path);
	const auto actual = extract_properties(save_path);

	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);

	if (should_support_metadata)
	{
		assert_metadata(*expected, *actual, save_path.name());
	}
}

static void should_not_rewrite_unchanged_file()
{
	const auto path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), path, false, false);
	const auto modified_before = platform::file_attributes(path).modified;

	files ff;
	const auto result = ff.update(path, {}, {}, {}, false, {});

	assert_equal(true, result.success(), "unchanged save succeeds");
	assert_equal(modified_before, platform::file_attributes(path).modified, "unchanged save preserves modified time");
}

// The three readers of what a write produced are all served from one scan taken behind the write.
// This is the display's share of it: the bytes come back as an image, so nothing reads the file
// again to draw what was just saved.
static void should_return_written_image()
{
	const auto path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), path, false, false);

	files ff;
	const auto before = ff.load(path, false);

	rescan_spec rescan;
	rescan.wanted = true;
	rescan.load_thumbnail = true;
	rescan.want_image = true;
	rescan.file_type = files::file_type_from_name(path);

	metadata_edits edits;
	edits.rating = 3;
	const auto result = ff.update(path, edits, {}, {}, false, {}, rescan);

	assert_equal(true, result.success(), std::format("rating saved ({})", result.format_error()));
	assert_equal(true, result.scanned, "write scanned what it wrote");
	assert_equal(true, result.loaded.success, "write returned the image it wrote");
	assert_equal(before.i->width(), result.loaded.i->width(), "written image width");
	assert_equal(before.i->height(), result.loaded.i->height(), "written image height");

	// The scan only wraps the written bytes when a caller asked for them, and asking for a thumbnail
	// is a separate request. Without this the assertions above pass on the thumbnail's half of the
	// gate and say nothing about want_image.
	rescan_spec image_only;
	image_only.wanted = true;
	image_only.load_thumbnail = false;
	image_only.want_image = true;
	image_only.file_type = files::file_type_from_name(path);

	metadata_edits more_edits;
	more_edits.rating = 4;
	const auto image_only_result = ff.update(path, more_edits, {}, {}, false, {}, image_only);

	assert_equal(true, image_only_result.success(),
	             std::format("rating saved ({})", image_only_result.format_error()));
	assert_equal(true, image_only_result.loaded.success, "want_image alone returns the written image");
	assert_equal(before.i->width(), image_only_result.loaded.i->width(), "image-only written width");
	assert_equal(before.i->height(), image_only_result.loaded.i->height(), "image-only written height");
}

// The AV display's share. A container can be gigabytes, so it takes the handle rather than the
// bytes; reading it back is what proves the handle refers to the swapped-in file and not the stage.
static void should_hand_over_written_handle()
{
	const auto path = _temps.next_path(".mp3");
	platform::copy_file(test_files_folder.combine_file("Colorblind.mp3"), path, false, false);

	files ff;

	rescan_spec rescan;
	rescan.want_handle = true;

	metadata_edits edits;
	edits.rating = 3;
	const auto result = ff.update(path, edits, {}, {}, false, {}, rescan);

	assert_equal(true, result.success(), std::format("rating saved ({})", result.format_error()));
	assert_equal(true, result.staged, "edit staged and swapped");
	assert_equal(true, result.display_handle != nullptr, "write handed over its open handle");

	const auto expected = df::blob_from_file(path);
	df::blob actual;
	actual.resize(expected.size());
	result.display_handle->seek(0, platform::file::whence::begin);
	actual.resize(static_cast<size_t>(result.display_handle->read(actual.data(), actual.size())));

	assert_equal(true, expected == actual, "handle reads back the written bytes");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// File handle lifetime
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_keep_file_handles_detached_until_last_operation()
{
	null_state_strategy state_strategy;
	deferred_async_strategy async;
	const location_cache locations;
	index_state index(async, locations);
	view_state state(state_strategy, async, index, make_test_player());
	const auto initial_detach_count = df::file_handles_detached.load();

	auto first = std::make_shared<detach_file_handles>(state);
	auto second = std::make_shared<detach_file_handles>(state);
	assert_equal(initial_detach_count + 2, df::file_handles_detached.load(), "both operations detached");

	first.reset();
	assert_equal(initial_detach_count + 1, df::file_handles_detached.load(), "later operation remains detached");
	assert_equal(0ull, static_cast<uint64_t>(async.pending_worker_count(async_queue::scan_modified_items)),
	             "no intermediate rescan");

	second.reset();
	assert_equal(initial_detach_count, df::file_handles_detached.load(), "final operation restores handles");
	assert_equal(1ull, static_cast<uint64_t>(async.pending_worker_count(async_queue::scan_modified_items)),
	             "one final rescan");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Decoder robustness
///////////////////////////////////////////////////////////////////////////////////////////////////

static bool write_binary_file(const df::file_path path, const uint8_t* const data, const int size)
{
	auto f = open_file(path, platform::file_open_mode::create);

	if (!f)
	{
		return false;
	}

	const auto written = f->write(data, size);
	f.reset();

	return written == static_cast<uint64_t>(size) && path.exists() && platform::file_attributes(path).size ==
		static_cast<uint64_t>(size);
}

static file_scan_result scan_recorded_jpeg(files& ff, const df::blob& prefix, const uint64_t reported_size,
                                           const bool load_thumbnail, std::shared_ptr<recording_file>& recorded)
{
	const auto path = _temps.next_path(".jpg");
	recorded = std::make_shared<recording_file>(path, prefix.clone(), reported_size);
	return ff.scan_file(recorded, path, load_thumbnail, files::file_type_from_name(path), {},
	                    {64, 64});
}

static df::blob blob_slice(const df::blob& bytes, const size_t offset, const size_t len)
{
	return {bytes.begin() + static_cast<ptrdiff_t>(offset), bytes.begin() + static_cast<ptrdiff_t>(offset + len)};
}

static void should_apply_encoded_ceiling_to_bitmap_thumbnail_scans()
{
	const auto source = df::blob_from_file(test_files_folder.combine_file("Test.jpg"));
	assert_equal(true, source.size() > sizeof(pack128), "jpeg fixture loaded");

	files ff;

	std::shared_ptr<recording_file> below;
	const auto below_scan = scan_recorded_jpeg(ff, source, source.size(), true, below);
	assert_equal(true, below_scan.success, "below-ceiling thumbnail scan reads metadata");
	assert_equal(true, is_valid(below_scan.thumbnail_image), "below-ceiling thumbnail is available");
	assert_equal(static_cast<uint64_t>(source.size()), below->max_read, "below-ceiling read is the whole file");

	std::shared_ptr<recording_file> at_ceiling;
	const auto at_scan = scan_recorded_jpeg(ff, source, df::max_blob_size, true, at_ceiling);
	assert_equal(true, at_scan.success, "at-ceiling thumbnail scan reads metadata");
	assert_equal(static_cast<uint64_t>(df::max_blob_size), at_ceiling->max_read,
	             "at-ceiling thumbnail read is still admitted");

	std::shared_ptr<recording_file> above_thumbnail;
	const auto above_scan = scan_recorded_jpeg(ff, source, static_cast<uint64_t>(df::max_blob_size) + 1u, true,
	                                           above_thumbnail);
	assert_equal(true, above_scan.success, "above-ceiling thumbnail scan keeps header metadata");
	assert_equal(false, is_valid(above_scan.thumbnail_image), "above-ceiling scan does not publish a partial thumbnail");
	assert_equal(true, above_thumbnail->max_read < static_cast<uint64_t>(df::max_blob_size),
	             "above-ceiling scan never reads or allocates the whole file");

	std::shared_ptr<recording_file> above_metadata;
	const auto metadata_scan = scan_recorded_jpeg(ff, source, static_cast<uint64_t>(df::max_blob_size) + 1u, false,
	                                              above_metadata);
	assert_equal(true, metadata_scan.success, "above-ceiling metadata-only scan still succeeds");
	assert_equal(false, is_valid(metadata_scan.thumbnail_image), "metadata-only scan has no thumbnail");
	assert_equal(true, above_metadata->max_read < static_cast<uint64_t>(df::max_blob_size),
	             "metadata-only scan remains bounded");
}

static void should_reset_sliding_reader_when_reopened()
{
	const auto a = _temps.next_path(".bin");
	const auto b = _temps.next_path(".bin");
	const df::blob bytes_a = {0, 1, 2, 3, 4, 5, 6, 7};
	const df::blob bytes_b = {20, 21, 22, 23, 24, 25, 26, 27, 28, 29};
	df::blob_save_to_file(bytes_a, a);
	df::blob_save_to_file(bytes_b, b);

	file_read_stream stream;
	assert_equal(true, stream.open(a), "open first file");
	assert_equal(true, blob_slice(bytes_a, 2, 4) == stream.read(2, 4), "first file read populates the window");

	assert_equal(true, stream.open(b), "reopen without close");
	assert_equal(true, blob_slice(bytes_b, 2, 4) == stream.read(2, 4), "reopen discards cached bytes");
	assert_equal(true, blob_slice(bytes_b, 6, 4) == stream.read(6, 4), "boundary-crossing read comes from reopened file");

	stream.close();
	assert_equal(true, stream.open(a), "reopen after close");
	assert_equal(true, blob_slice(bytes_a, 1, 5) == stream.read(1, 5), "close clears cached range and storage");

	assert_equal(false, stream.open(platform::file_ptr{}), "failed open is reported");
	assert_equal(true, stream.open(b), "open after failed open");
	assert_equal(true, blob_slice(bytes_b, 0, 3) == stream.read(0, 3), "failed open left no stale cached state");
}

static void should_not_crash(const std::string_view name)
{
	const auto path = test_files_folder.combine_file(name);
	auto blob = blob_from_file(path);
	const auto ext = name.substr(df::find_ext(name));

	// Without this the whole test degrades silently: a missing or empty fixture makes both
	// loops below no-ops, and "did not crash" would be reported for work that never ran.
	assert_equal(true, blob.size() > 16u, std::format("{} fixture is readable", name));

	const auto original = std::vector<uint8_t>(blob.begin(), blob.end());

	files ff;

	auto* const data = blob.data();
	const auto size = blob.size();

	auto corrupted = 0u;

	for (auto i = 0u; i < size && !df::is_closing; i++)
	{
		const auto v = data[i];
		data[i] = 0xff;

		prop::item_metadata md;
		mem_read_stream stream(blob);
		const auto info = scan_photo(stream);

		metadata_exif::parse(md, info.metadata.exif);
		metadata_iptc::parse(md, info.metadata.iptc);
		metadata_xmp::parse(md, info.metadata.xmp);

		data[i] = v;
		++corrupted;
	}

	constexpr auto truncation_steps = 32;
	auto truncated = 0u;

	for (auto i = 0u; i < truncation_steps && !df::is_closing; i++)
	{
		const auto save_path = _temps.next_path(ext);
		const auto byte_count = df::mul_div(static_cast<int>(size), i, truncation_steps);
		assert_equal(true, write_binary_file(save_path, data, byte_count),
		             std::format("{} truncation fixture {} was written", name, i));
		ff_scan_and_load_thumb(ff, save_path);
		++truncated;
	}

	if (df::is_closing)
	{
		assert_equal(true, corrupted > 0u, std::format("{} sweep started before shutdown", name));
		return;
	}

	assert_equal(static_cast<int>(size), static_cast<int>(corrupted),
	             std::format("{} every byte was corrupted and scanned", name));
	assert_equal(truncation_steps, static_cast<int>(truncated),
	             std::format("{} every truncation was scanned", name));
	assert_equal(true, std::equal(original.begin(), original.end(), blob.begin(), blob.end()),
	             std::format("{} bytes restored after the sweep", name));
}

void register_files_tests(view_state& state, test_registry& tests)
{
	//
	// Write path
	//
	tests.add("Should check overwrite"s, should_check_overwrite);
	tests.add("Should report zip create failure"s, should_report_zip_create_failure);
	tests.add("Should add a multi chunk file to a zip"s, should_add_a_multi_chunk_file_to_a_zip);
	// SRC-010 - long names must not be consumed as a truncated C string.
	tests.add("Should round trip zip entry names"s, should_round_trip_zip_entry_names);
	// SRC-011 - application months are 1-12 while minizip takes 0-11.
	tests.add("Should round trip zip entry dates"s, should_round_trip_zip_entry_dates);
	tests.add("Should create original before replace"s, should_create_original_before_replace);
	tests.add("Should report move or copy collision paths"s, should_report_move_or_copy_collision_paths);
#ifndef _WIN32
	// PLAT-007 - Linux replacement copy truncated source/destination aliases before reading.
	tests.add("Should refuse Linux copy identity overwrites"s, should_refuse_linux_copy_identity_overwrites);
	// PLAT-007 - Linux copy ignored short writes and delayed close failures.
	tests.add("Should report Linux copy flush failures"s, should_report_linux_copy_flush_failures);
	// PLAT-008 - Linux auto-rename used a pre-check then an overwrite-capable copy.
	tests.add("Should move or copy by claiming Linux auto rename destinations atomically"s,
	          should_claim_linux_auto_rename_destinations_atomically);
	// PLAT-009 - Linux recursive folder copy could copy into its own destination.
	tests.add("Should move or copy reject Linux recursive copy into descendant"s,
	          should_reject_linux_recursive_copy_into_descendant);
	// G21 review - directory symlinks must be listed so delete can unlink them.
	tests.add("Should delete Linux directory symlinks"s, should_delete_linux_directory_symlinks);
	// G21 review - folder copy must not silently drop directory symlinks.
	tests.add("Should move or copy Linux directory symlinks"s, should_move_or_copy_linux_directory_symlinks);
	// G21 review - copying into the source's own parent should auto-rename, not fail on identity.
	tests.add("Should move or copy into its own folder with auto rename"s,
	          should_move_or_copy_into_its_own_folder_with_auto_rename);
	// G21 review - an inner collision belongs to the claimed tree, not the outer name picker.
	tests.add("Should move or copy stop on inner Linux folder collision"s,
	          should_move_or_copy_stop_on_inner_linux_folder_collision);
	// G21 review - filesystems without renameat2/link support still need a safe no-replace move.
	tests.add("Should move Linux no replace without renameat or links"s,
	          should_move_linux_no_replace_without_renameat_or_links);
	// G21 review - replacement copy must respect destination writeability and mode.
	tests.add("Should refuse or preserve Linux replace permissions"s,
	          should_refuse_or_preserve_linux_replace_permissions);
	// G21 review - containment checks must follow aliases into the source subtree.
	tests.add("Should move or copy reject Linux destination alias inside source"s,
	          should_move_or_copy_reject_linux_destination_alias_inside_source);
#endif
	tests.add("Should fail replace when flush fails"s, should_fail_replace_when_flush_fails);
#ifdef _WIN32
	tests.add("Should cleanup failed update temps"s, should_cleanup_failed_update_temps);
#endif
	tests.add("Should not rewrite unchanged file"s, should_not_rewrite_unchanged_file);
	tests.add("Should return written image"s, should_return_written_image);
	tests.add("Should hand over written handle"s, should_hand_over_written_handle);
	tests.add("Should save .png"s, [] { should_save(".png", true); });
	tests.add("Should save .jpg"s, [] { should_save(".jpg", true); });
	tests.add("Should save .webp"s, [] { should_save(".webp", true); });

	constexpr std::string_view read_only_formats[] = {
		"gradient.bmp", "gradient.tga", "gradient.sgi", "gradient.pcx", "gradient.ppm", "gradient.pgm"
	};

	for (auto name : read_only_formats)
	{
		tests.add(std::format("Should decode {}", name), [name] { should_decode_a_read_only_format(name); });
	}

	//
	// Format detection
	//
	tests.add("Should settle a transport stream extension by header"s,
	          should_settle_transport_stream_extension_by_header);
	tests.add("Should detect tiff by version"s, should_detect_tiff_by_version);
	tests.add("Should register every still format"s, should_register_every_still_format);

	//
	// Containers
	//
	tests.add("Should scan d64"s, should_scan_d64);
	tests.add("Should scan archive"s, should_scan_archive);

	//
	// Codec decode
	//
	tests.add("Should scan and load bitmap psd"s, should_scan_and_load_bitmap_psd);
	// SRC-026 - PSD stores Lab as byte L plus biased a/b samples
	tests.add("Should convert psd lab samples to lab coordinates"s,
	          should_convert_psd_lab_samples_to_lab_coordinates);
	// SRC-028 - PSD resource sections are metadata, not unbounded allocation requests
	tests.add("Should bound psd resource allocations"s, should_bound_psd_resource_allocations);
	// SRC-028 - large unused resources do not hide bounded metadata resources
	tests.add("Should read small psd metadata after large unused resource"s,
	          should_read_small_psd_metadata_after_large_unused_resource);
	// SRC-028 - the only PSD colour table allocation needed by the loader is the valid palette
	tests.add("Should load valid psd indexed palette"s, should_load_valid_psd_indexed_palette);
	tests.add("Should keep dimensions from truncated gif"s, should_keep_dimensions_from_truncated_gif);
	tests.add("Should reject absurd tiff dimensions"s, should_reject_absurd_tiff_dimensions);
	tests.add("Should extract embedded thumbnails only on demand"s,
	          should_extract_embedded_thumbnails_only_on_demand);
	tests.add("Should decode heic hevc with ffmpeg"s, should_decode_heic_hevc_with_ffmpeg);

	//
	// JPEG
	//
	tests.add("Should read jpeg orientation"s, should_read_jpeg_orientation);
	// SRC-034 - APP13 can hold named and reordered Photoshop resources
	tests.add("Should parse jpeg photoshop iptc resources"s,
	          should_parse_jpeg_photoshop_iptc_resources);
	// Review G04 - APP13 PSIR must not override APP1/APP2 metadata
	tests.add("Should keep app13 from overriding jpeg app1 and app2 metadata"s,
	          should_keep_app13_from_overriding_jpeg_app1_and_app2_metadata);
	// Review G04 - non-IPTC Photoshop resources survive a JPEG re-encode
	tests.add("Should preserve non iptc jpeg photoshop resources"s,
	          should_preserve_non_iptc_jpeg_photoshop_resources);
	// Review G04 - Photoshop resources may span consecutive APP13 segments
	tests.add("Should roundtrip split jpeg photoshop resources"s,
	          should_roundtrip_split_jpeg_photoshop_resources);
	// Review G04 - XMP Toolkit splits Photoshop resources without regard to resource boundaries
	tests.add("Should join straddled jpeg photoshop resources"s,
	          should_join_straddled_jpeg_photoshop_resources);
	// Review G04 - crafted APP13 runs are capped before they grow without bound
	tests.add("Should cap joined jpeg photoshop resources"s,
	          should_cap_joined_jpeg_photoshop_resources);
	tests.add("Should prefer primary orientation over the thumbnail ifd"s,
	          should_prefer_primary_orientation_over_the_thumbnail_ifd);
	tests.add("Should use thumbnail orientation only when primary is absent"s,
	          should_use_thumbnail_orientation_only_when_primary_is_absent);
	tests.add("Should prefer primary tiff orientation over the thumbnail ifd"s,
	          should_prefer_primary_tiff_orientation_over_the_thumbnail_ifd);
	tests.add("Should reuse source jpeg tables"s, should_reuse_source_jpeg_tables);
	tests.add("Should refuse imperfect lossless rotate"s, should_refuse_imperfect_lossless_rotate);
	tests.add("Should survive truncated lossless rotate"s, should_survive_truncated_lossless_rotate);
	tests.add("Should refuse an over budget lossless rotate"s, should_refuse_an_over_budget_lossless_rotate);
	tests.add("Should not re-encode a rotate refused for its budget"s,
	          should_not_re_encode_a_rotate_refused_for_its_budget);
	tests.add("Should charge a progressive jpeg for its coefficients"s,
	          should_charge_a_progressive_jpeg_for_its_coefficients);
	tests.add("Should reuse jpeg encoder after abandoned encode"s,
	          should_reuse_jpeg_encoder_after_abandoned_encode);
	tests.add("Should rotate lossless"s, should_rotate_lossless);
	tests.add("Should render ycbcr jpeg as nv12"s, should_render_ycbcr_jpeg_as_nv12);
	tests.add("Should decode scaled 422 jpeg as nv12"s, should_decode_scaled_422_jpeg_as_nv12);
	tests.add("Should report jpeg chroma subsampling"s, should_report_jpeg_chroma_subsampling);
	tests.add("Should decode 12bit gray jpeg"s, should_decode_12bit_gray_jpeg);
	tests.add("Should decode 12bit colour jpeg"s, should_decode_12bit_colour_jpeg);
	tests.add("Should decode 16bit gray jpeg"s, should_decode_16bit_gray_jpeg);
	tests.add("Should decode 16bit colour jpeg"s, should_decode_16bit_colour_jpeg);
	// SRC-029 - one-pixel axes cannot form NV12
	tests.add("Should decode thin jpeg when planar is preferred"s,
	          should_decode_thin_jpeg_when_planar_is_preferred);

	//
	// PNG
	//
	tests.add("Should scale 16bit png"s, should_scale_16bit_png);
	tests.add("Should apply png gamma"s, should_apply_png_gamma);
	// SRC-019 - PNG metadata may be stored after IDAT.
	tests.add("Should read png orientation after idat"s, should_read_png_orientation_after_idat);

	//
	// WebP
	//
	tests.add("Should honor webp save quality"s, should_honor_webp_save_quality);
	tests.add("Should tag webp surface alpha"s, should_tag_webp_surface_alpha);
	tests.add("Should decode opaque lossy webp as nv12"s, should_decode_opaque_lossy_webp_as_nv12);
	tests.add("Should decode webp at the requested size"s, should_decode_webp_at_the_requested_size);
	tests.add("Should let the caller choose the decoded pixel format"s,
	          should_let_the_caller_choose_the_decoded_pixel_format);
	tests.add("Should thumbnail a jpeg without leaving yuv"s, should_thumbnail_a_jpeg_without_leaving_yuv);
	tests.add("Should decode odd sized webp as nv12"s, should_decode_odd_sized_webp_as_nv12);
	tests.add("Should keep thumbnail alpha only when needed"s, should_keep_thumbnail_alpha_only_when_needed);
	tests.add("Should refuse truncated webp decode"s, should_refuse_truncated_webp_decode);
	tests.add("Should bound and time animated webp"s, should_bound_and_time_animated_webp);
	tests.add("Should preserve webp chunks on metadata save"s, should_preserve_webp_chunks_on_metadata_save);
	// SRC-031 - requested WebP metadata chunks are all-or-fail
	tests.add("Should fail webp save when metadata chunk insertion fails"s,
	          should_fail_webp_save_when_metadata_chunk_insertion_fails);

	//
	// RAW
	//
	tests.add("Should convert raw to jpeg"s, should_convert_raw_to_jpeg);
	tests.add("Should refuse a file too large to hold"s, should_refuse_a_file_too_large_to_hold);
	// SRC-017 - encoded-byte ceiling applies to bitmap thumbnail scans
	tests.add("Should apply encoded ceiling to bitmap thumbnail scans"s,
	          should_apply_encoded_ceiling_to_bitmap_thumbnail_scans);

	//
	// File handle lifetime
	//
	tests.add("Should keep file handles detached until last operation"s,
	          should_keep_file_handles_detached_until_last_operation);
	// SRC-032 - sliding reader reopen clears the cached window
	tests.add("Should reset sliding reader when reopened"s, should_reset_sliding_reader_when_reopened);

	//
	// Decoder robustness
	//
#ifndef _DEBUG
	tests.add("Should not crash on JPEG"s, [] { should_not_crash("Small.jpg"); });
	tests.add("Should not crash on GIF"s, [] { should_not_crash("tuesday.gif"); });
	tests.add("Should not crash on TIFF"s, [] { should_not_crash("Small.tif"); });
	tests.add("Should not crash on PNG"s, [] { should_not_crash("Cube.png"); });
	tests.add("Should not crash on WEBP"s, [] { should_not_crash("lake.webp"); });
#endif
}

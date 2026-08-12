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

static void should_fail_replace_when_flush_fails()
{
	const auto result = platform::replacement_flush_result(false, "flush failed");
	assert_equal(true, result.failed(), "failed flush stops replacement");
	assert_equal("flush failed", result.error_message, "flush error preserved");

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
}

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

// Regression: the JPEG decoder must call jpeg_save_markers so read_header can
// recover the embedded EXIF orientation from the APP1 marker.
static void should_read_jpeg_orientation()
{
	const auto load_path = test_files_folder.combine_file("exif-rotated.jpg");
	const auto data = df::blob_from_file(load_path);

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header(data), "read jpeg header");
	assert_equal(ui::orientation::right_top, decoder._orientation_out, "decoder recovers EXIF orientation");
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

	assert_equal(true, !is_valid(save_webp(nv12, {}, {})), "webp encoder rejects NV12 rather than reading it as BGRX");

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

// setting.use_yuv is what the Advanced option, safe start and the D3D11 driver-fault fallback
// all turn off, so a decoder that ignores it leaves every one of them with no effect. A user on
// a driver that faults creating NV12 textures then has no way out of the fault.
static void should_honor_the_yuv_texture_setting()
{
	const auto saved = setting.use_yuv;
	const df::scope_exit restore([saved] { setting.use_yuv = saved; });

	files ff;
	const auto webp = df::blob_from_file(test_files_folder.combine_file("lake.webp"));
	const auto jpeg = ff.load(test_files_folder.combine_file("exif-rotated.jpg"), false);
	assert_equal(true, is_valid(jpeg.i), "loaded jpeg");

	setting.use_yuv = true;
	const auto webp_on = load_webp(webp, true);
	const auto jpeg_on = jpeg.to_surface({}, true);
	assert_equal(true, is_valid(webp_on) && webp_on->format() == ui::texture_format::NV12, "webp nv12 while on");
	assert_equal(true, is_valid(jpeg_on) && jpeg_on->format() == ui::texture_format::NV12, "jpeg nv12 while on");

	setting.use_yuv = false;
	const auto webp_off = load_webp(webp, true);
	const auto jpeg_off = jpeg.to_surface({}, true);
	assert_equal(true, is_valid(webp_off) && webp_off->format() == ui::texture_format::RGB, "webp rgb while off");
	assert_equal(true, is_valid(jpeg_off) && jpeg_off->format() == ui::texture_format::RGB, "jpeg rgb while off");
}

// A thumbnail scaled to fit a box is regularly odd on one axis. decode_jpeg crops those to even and
// still reaches the GPU as NV12; a webp decoder that instead falls back to RGB costs 4 bytes per
// pixel rather than 1.5, for the majority of a collection's thumbnails.
static void should_decode_odd_sized_webp_as_nv12()
{
	const auto saved = setting.use_yuv;
	const df::scope_exit restore([saved] { setting.use_yuv = saved; });
	setting.use_yuv = true;

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
	const auto saved = setting.use_yuv;
	const df::scope_exit restore([saved] { setting.use_yuv = saved; });
	setting.use_yuv = true;

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

static void write_binary_file(const df::file_path path, const uint8_t* const data, const int size)
{
	const auto f = open_file(path, platform::file_open_mode::create);

	if (f)
	{
		f->write(data, size);
	}
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
		write_binary_file(save_path, data, df::mul_div(static_cast<int>(size), i, truncation_steps));
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
	tests.add("Should create original before replace"s, should_create_original_before_replace);
	tests.add("Should report move or copy collision paths"s, should_report_move_or_copy_collision_paths);
	tests.add("Should fail replace when flush fails"s, should_fail_replace_when_flush_fails);
	tests.add("Should cleanup failed update temps"s, should_cleanup_failed_update_temps);
	tests.add("Should not rewrite unchanged file"s, should_not_rewrite_unchanged_file);
	tests.add("Should return written image"s, should_return_written_image);
	tests.add("Should hand over written handle"s, should_hand_over_written_handle);
	tests.add("Should save .png"s, [] { should_save(".png", true); });
	tests.add("Should save .jpg"s, [] { should_save(".jpg", true); });
	tests.add("Should save .webp"s, [] { should_save(".webp", true); });

	//
	// Format detection
	//
	tests.add("Should settle a transport stream extension by header"s,
	          should_settle_transport_stream_extension_by_header);
	tests.add("Should detect tiff by version"s, should_detect_tiff_by_version);

	//
	// Containers
	//
	tests.add("Should scan d64"s, should_scan_d64);
	tests.add("Should scan archive"s, should_scan_archive);

	//
	// Codec decode
	//
	tests.add("Should scan and load bitmap psd"s, should_scan_and_load_bitmap_psd);
	tests.add("Should keep dimensions from truncated gif"s, should_keep_dimensions_from_truncated_gif);
	tests.add("Should reject absurd tiff dimensions"s, should_reject_absurd_tiff_dimensions);
	tests.add("Should extract embedded thumbnails only on demand"s,
	          should_extract_embedded_thumbnails_only_on_demand);

	//
	// JPEG
	//
	tests.add("Should read jpeg orientation"s, should_read_jpeg_orientation);
	tests.add("Should reuse source jpeg tables"s, should_reuse_source_jpeg_tables);
	tests.add("Should refuse imperfect lossless rotate"s, should_refuse_imperfect_lossless_rotate);
	tests.add("Should survive truncated lossless rotate"s, should_survive_truncated_lossless_rotate);
	tests.add("Should reuse jpeg encoder after abandoned encode"s,
	          should_reuse_jpeg_encoder_after_abandoned_encode);
	tests.add("Should rotate lossless"s, should_rotate_lossless);
	tests.add("Should render ycbcr jpeg as nv12"s, should_render_ycbcr_jpeg_as_nv12);
	tests.add("Should report jpeg chroma subsampling"s, should_report_jpeg_chroma_subsampling);
	tests.add("Should decode 12bit gray jpeg"s, should_decode_12bit_gray_jpeg);
	tests.add("Should decode 12bit colour jpeg"s, should_decode_12bit_colour_jpeg);
	tests.add("Should decode 16bit gray jpeg"s, should_decode_16bit_gray_jpeg);
	tests.add("Should decode 16bit colour jpeg"s, should_decode_16bit_colour_jpeg);

	//
	// PNG
	//
	tests.add("Should scale 16bit png"s, should_scale_16bit_png);
	tests.add("Should apply png gamma"s, should_apply_png_gamma);

	//
	// WebP
	//
	tests.add("Should honor webp save quality"s, should_honor_webp_save_quality);
	tests.add("Should tag webp surface alpha"s, should_tag_webp_surface_alpha);
	tests.add("Should decode opaque lossy webp as nv12"s, should_decode_opaque_lossy_webp_as_nv12);
	tests.add("Should honor the yuv texture setting"s, should_honor_the_yuv_texture_setting);
	tests.add("Should decode odd sized webp as nv12"s, should_decode_odd_sized_webp_as_nv12);
	tests.add("Should keep thumbnail alpha only when needed"s, should_keep_thumbnail_alpha_only_when_needed);
	tests.add("Should refuse truncated webp decode"s, should_refuse_truncated_webp_decode);
	tests.add("Should bound and time animated webp"s, should_bound_and_time_animated_webp);
	tests.add("Should preserve webp chunks on metadata save"s, should_preserve_webp_chunks_on_metadata_save);

	//
	// RAW
	//
	tests.add("Should convert raw to jpeg"s, should_convert_raw_to_jpeg);

	//
	// File handle lifetime
	//
	tests.add("Should keep file handles detached until last operation"s,
	          should_keep_file_handles_detached_until_last_operation);

	//
	// Decoder robustness
	//
#ifndef _DEBUG
	tests.add("Should not crash on JPEG"s, [] { should_not_crash("small.jpg"); });
	tests.add("Should not crash on GIF"s, [] { should_not_crash("tuesday.gif"); });
	tests.add("Should not crash on TIFF"s, [] { should_not_crash("small.tif"); });
	tests.add("Should not crash on PNG"s, [] { should_not_crash("cube.png"); });
	tests.add("Should not crash on WEBP"s, [] { should_not_crash("lake.webp"); });
#endif
}

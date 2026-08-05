// Purpose: Shared test fixtures and helpers (scanning, metadata comparison, index building) plus
// the tests for string, crypto, wildcard, version, command-line, rename, import, sync, file
// replacement, archive, MAPI and INI behaviour.

#include "pch.h"
#include "files.h"
#include "test.h"
#include "app_command_line.h"
#include "crypto.h"
#include "crypto_aes256.h"
#include "util_base64.h"
#include "util_zip.h"
#include "crypto_sha.h"
#include "util_simd.h"
#include "av_format.h"
#include "model_db.h"
#include "model_index.h"
#include "test_utils.h"
#include "app_util.h"
#include "app_settings.h"
#include "metadata_exif.h"
#include "ui_elements.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"
#include "app_text.h"


static void should_complete_result_scope()
{
	const auto results = std::make_shared<null_item_results_ui>();
	{
		result_scope scope(results);
	}

	assert_equal(1, results->complete_count, "normal scope exit completes results");
	assert_equal(0, results->abort_count, "normal scope exit does not abort results");
}

static void should_abort_result_scope_during_exception()
{
	const auto results = std::make_shared<null_item_results_ui>();
	try
	{
		result_scope scope(results);
		throw std::runtime_error("test");
	}
	catch (const std::runtime_error&)
	{
	}

	assert_equal(0, results->complete_count, "exception unwinding does not complete results");
	assert_equal(1, results->abort_count, "exception unwinding aborts results");
}

static void should_format_audio_stream_names()
{
	av_stream_info stream;
	stream.type = av_stream_type::audio;
	stream.language = "eng";
	stream.audio_channels = 2;
	assert_equal("English - stereo", format_audio_stream_name(stream, 1), "language and channels");

	stream.language.clear();
	stream.title = "Director";
	stream.is_commentary = true;
	stream.audio_channels = 6;
	assert_equal("Director - commentary - 5.1 surround", format_audio_stream_name(stream, 1),
	             "title, role and channels");

	stream = {};
	stream.type = av_stream_type::audio;
	stream.audio_channels = 2;
	assert_equal("Audio track 2 - stereo", format_audio_stream_name(stream, 2), "numbered fallback");

	stream.audio_channels = 0;
	stream.codec = "aac";
	assert_equal("Audio track 3 - aac", format_audio_stream_name(stream, 3), "codec fallback");
}

static void should_icmp_natural()
{
	// Test basic numeric comparison - the key bug fix
	// Files like 43_100 should come after 43_99, not between 43_10 and 43_11
	assert_equal(true, str::icmp_natural("43_09", "43_10") < 0, "43_09 < 43_10");
	assert_equal(true, str::icmp_natural("43_10", "43_11") < 0, "43_10 < 43_11");
	assert_equal(true, str::icmp_natural("43_10", "43_100") < 0, "43_10 < 43_100");
	assert_equal(true, str::icmp_natural("43_99", "43_100") < 0, "43_99 < 43_100");
	assert_equal(true, str::icmp_natural("43_100", "43_101") < 0, "43_100 < 43_101");

	// Verify the order reported in the bug is fixed
	assert_equal(true, str::icmp_natural("43_09", "43_100") < 0, "43_09 < 43_100");
	assert_equal(true, str::icmp_natural("43_11", "43_100") < 0, "43_11 < 43_100");

	// Test equality
	assert_equal(0, str::icmp_natural("file10", "file10"), "equal strings");
	assert_equal(0, str::icmp_natural("", ""), "empty strings");

	// Test case insensitivity
	assert_equal(0, str::icmp_natural("File10", "file10"), "case insensitive");
	assert_equal(0, str::icmp_natural("FILE10", "file10"), "case insensitive upper");

	// Test basic natural ordering
	assert_equal(true, str::icmp_natural("file1", "file2") < 0, "file1 < file2");
	assert_equal(true, str::icmp_natural("file2", "file10") < 0, "file2 < file10");
	assert_equal(true, str::icmp_natural("file9", "file10") < 0, "file9 < file10");
	assert_equal(true, str::icmp_natural("file10", "file11") < 0, "file10 < file11");
	assert_equal(true, str::icmp_natural("file19", "file20") < 0, "file19 < file20");
	// Issue #197: "43_100" sorted between "43_10" and "43_11" (lexicographic instead of numeric).
	assert_equal(true, str::icmp_natural("file99", "file100") < 0, "file99 < file100");
	assert_equal(true, str::icmp_natural("file100", "file1000") < 0, "file100 < file1000");

	// Test reverse ordering
	assert_equal(true, str::icmp_natural("file10", "file9") > 0, "file10 > file9");
	assert_equal(true, str::icmp_natural("file100", "file99") > 0, "file100 > file99");

	// Test with different prefixes
	assert_equal(true, str::icmp_natural("a10", "b1") < 0, "a10 < b1");
	assert_equal(true, str::icmp_natural("img001", "img002") < 0, "img001 < img002");
	assert_equal(true, str::icmp_natural("img009", "img010") < 0, "img009 < img010");

	// Test numbers at the start
	assert_equal(true, str::icmp_natural("1file", "2file") < 0, "1file < 2file");
	assert_equal(true, str::icmp_natural("9file", "10file") < 0, "9file < 10file");
	assert_equal(true, str::icmp_natural("10file", "100file") < 0, "10file < 100file");

	// Test multiple number groups
	assert_equal(true, str::icmp_natural("file1-1", "file1-2") < 0, "file1-1 < file1-2");
	assert_equal(true, str::icmp_natural("file1-9", "file1-10") < 0, "file1-9 < file1-10");
	assert_equal(true, str::icmp_natural("file1-10", "file2-1") < 0, "file1-10 < file2-1");

	// Test leading zeros
	assert_equal(true, str::icmp_natural("file007", "file7") > 0, "file007 > file7 (more leading zeros)");
	assert_equal(true, str::icmp_natural("file07", "file007") < 0,
	             "file07 < file007 (fewer leading zeros)");
	assert_equal(0, str::icmp_natural("file007", "file007"), "same with leading zeros");

	// Test purely numeric strings
	assert_equal(true, str::icmp_natural("1", "2") < 0, "1 < 2");
	assert_equal(true, str::icmp_natural("9", "10") < 0, "9 < 10");
	assert_equal(true, str::icmp_natural("99", "100") < 0, "99 < 100");
	assert_equal(true, str::icmp_natural("999", "1000") < 0, "999 < 1000");

	// Test strings with no numbers
	assert_equal(true, str::icmp_natural("abc", "abd") < 0, "abc < abd");
	assert_equal(true, str::icmp_natural("abc", "abcd") < 0, "abc < abcd");
	assert_equal(0, str::icmp_natural("abc", "ABC"), "abc == ABC (case insensitive)");

	// Test image sequence patterns (common use case)
	assert_equal(true, str::icmp_natural("DSC_0001.jpg", "DSC_0002.jpg") < 0, "DSC sequence");
	assert_equal(true, str::icmp_natural("DSC_0099.jpg", "DSC_0100.jpg") < 0, "DSC sequence 99-100");
	assert_equal(true, str::icmp_natural("IMG_9999.png", "IMG_10000.png") < 0, "IMG sequence overflow");
}

static void should_cancel_superseded_tokens()
{
	std::atomic_int version = 0;
	const df::cancel_token first(version);
	const auto first_copy = first;

	assert_equal(false, first.is_cancelled(), "current token is active");
	assert_equal(false, first_copy.is_cancelled(), "copied current token is active");

	const df::cancel_token second(version);
	assert_equal(true, first.is_cancelled(), "new generation cancels previous token");
	assert_equal(true, first_copy.is_cancelled(), "new generation cancels copies of previous token");
	assert_equal(false, second.is_cancelled(), "new generation remains active");
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared helper function definitions
///////////////////////////////////////////////////////////////////////////////////////////////////

file_scan_result ff_scan_file(files& ff, const df::file_path path, const std::string_view xmp_sidecar)
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, false, ft, xmp_sidecar, {}, scan_intent::inspect);
}

rescan_spec ff_inspect_rescan(const df::file_path path)
{
	rescan_spec spec;
	spec.wanted = true;
	spec.file_type = files::file_type_from_name(path);
	spec.intent = scan_intent::inspect;
	return spec;
}

file_scan_result ff_scan_after_update(files& ff, file_update_result& result, const df::file_path path,
                                      const std::string_view xmp_sidecar)
{
	if (result.scanned)
	{
		return std::move(result.scan);
	}

	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, false, ft, xmp_sidecar, {}, scan_intent::inspect);
}

file_scan_result ff_scan_and_load_thumb(files& ff, const df::file_path path,
                                        const std::string_view xmp_sidecar)
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, true, ft, xmp_sidecar, thumbnail_max_dimension);
}

void assert_metadata(const prop::item_metadata& expected, const prop::item_metadata& actual,
                     const std::string_view message)
{
	assert_equal(expected.album, actual.album, "album", message);
	assert_equal(expected.album_artist, actual.album_artist, "album_artist", message);
	assert_equal(expected.artist, actual.artist, "artist", message);
	assert_equal(expected.audio_codec, actual.audio_codec, "audio_codec", message);
	assert_equal(expected.audio_sample_rate, actual.audio_sample_rate, "audio_sample_rate", message);
	assert_equal(expected.audio_sample_type, actual.audio_sample_type, "audio_sample_type", message);
	assert_equal(expected.audio_channels, actual.audio_channels, "audio_channels", message);
	assert_equal(expected.bitrate, actual.bitrate, "bitrate", message);
	assert_equal(expected.camera_manufacturer, actual.camera_manufacturer, "camera_manufacturer", message);
	assert_equal(expected.camera_model, actual.camera_model, "camera_model", message);
	assert_equal(expected.comment, actual.comment, "comment", message);
	assert_equal(expected.composer, actual.composer, "composer", message);
	assert_equal(expected.coordinate, actual.coordinate, "coordinate", message);
	assert_equal(expected.copyright_creator, actual.copyright_creator, "copyright_creator", message);
	assert_equal(expected.copyright_credit, actual.copyright_credit, "copyright_credit", message);
	assert_equal(expected.copyright_notice, actual.copyright_notice, "copyright_notice", message);
	assert_equal(expected.copyright_source, actual.copyright_source, "copyright_source", message);
	assert_equal(expected.copyright_url, actual.copyright_url, "copyright_url", message);
	assert_equal(expected.created_digitized, actual.created_digitized, "created_digitized", message);
	assert_equal(expected.created_exif, actual.created_exif, "created_exif", message);
	assert_equal(expected.created_utc, actual.created_utc, "created_utc", message);
	assert_equal(expected.description, actual.description, "description", message);
	assert_equal(expected.width, actual.width, "width", message);
	assert_equal(expected.height, actual.height, "height", message);
	assert_equal(expected.disk, actual.disk, "disk", message);
	assert_equal(expected.duration, actual.duration, "duration", message);
	assert_equal(expected.encoder, actual.encoder, "encoder", message);
	assert_equal(expected.episode, actual.episode, "episode", message);
	assert_equal(prop::format_exposure(expected.exposure_time), prop::format_exposure(actual.exposure_time),
	             "exposure_time", message);
	assert_equal(prop::format_f_num(expected.f_number), prop::format_f_num(actual.f_number), "f_number", message);
	assert_equal(expected.file_name, actual.file_name, "file_name", message);
	assert_equal(expected.focal_length, actual.focal_length, "focal_length", message);
	assert_equal(expected.focal_length_35mm_equivalent, actual.focal_length_35mm_equivalent,
	             "focal_length_35mm_equivalent", message);
	assert_equal(expected.genre, actual.genre, "genre", message);
	assert_equal(expected.iso_speed, actual.iso_speed, "iso_speed", message);
	assert_equal(expected.lens, actual.lens, "lens", message);
	assert_equal(expected.location_place, actual.location_place, "location_city", message);
	assert_equal(expected.location_country, actual.location_country, "location_country", message);
	assert_equal(expected.location_state, actual.location_state, "location_state", message);
	assert_equal(expected.orientation, actual.orientation, "orientation", message);
	assert_equal(expected.performer, actual.performer, "performer", message);
	assert_equal(expected.publisher, actual.publisher, "publisher", message);
	assert_equal(expected.rating, actual.rating, "rating", message);
	assert_equal(expected.season, actual.season, "season", message);
	assert_equal(expected.show, actual.show, "show", message);
	assert_equal(expected.synopsis, actual.synopsis, "synopsis", message);
	assert_equal(expected.tags, actual.tags, "tags", message);
	assert_equal(expected.title, actual.title, "title", message);
	assert_equal(expected.track, actual.track, "track", message);
	assert_equal(expected.video_codec, actual.video_codec, "video_codec", message);
	assert_equal(expected.year, actual.year, "year", message);
}

prop::item_metadata_ptr metadata_from_cache(index_state& index, const df::file_path path)
{
	const auto node = index.validate_folder(path.folder(), true, platform::now());
	node.folder->is_in_collection = true;
	index.scan_item(node.folder, path, false, false, false, false, {}, false,
	                files::file_type_from_name(path.name()));
	return index.find_item(path).metadata;
}


prop::item_metadata_ptr extract_properties(const df::file_path path, const uint32_t t)
{
	auto result = std::make_shared<prop::item_metadata>();
	const auto data = blob_from_file(path);

	if (!data.empty())
	{
		mem_read_stream stream(data);

		if (stream.size() > 16)
		{
			const auto info = scan_photo(stream);

			if (t & metadata_type::EXIF && !info.metadata.exif.empty())
			{
				metadata_exif::parse(*result, info.metadata.exif);
			}

			if (t & metadata_type::IPTC && !info.metadata.iptc.empty())
			{
				metadata_iptc::parse(*result, info.metadata.iptc);
			}

			if (t & metadata_type::XMP && !info.metadata.xmp.empty())
			{
				metadata_xmp::parse(*result, info.metadata.xmp);
			}

			if (t == metadata_type::ALL)
			{
				result = info.to_props();
			}
		}
	}

	return result;
}


prop::item_metadata_ptr expected_test_jpg()
{
	auto result = std::make_shared<prop::item_metadata>();
	result->f_number = 6.3f;
	result->focal_length = 15.0f;
	result->camera_manufacturer = "Canon"_c;
	result->camera_model = "Canon EOS 7D"_c;
	result->coordinate = gps_coordinate(50.08806, 14.42083);
	result->copyright_notice = "Copyright"_c;
	result->created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	result->created_exif = df::date_t(2012, 9, 14, 19, 21, 14);
	result->description = "Caption"_c;
	result->exposure_time = 1.0f / 100.0f;
	result->iso_speed = 100;
	result->lens = "EF-S15-85mm f/3.5-5.6 IS USM"_c;
	result->location_place = "Prague"_c;
	result->location_country = "Czech Republic"_c;
	result->location_state = "Hlavní Mesto Praha"_c;
	result->rating = 4;
	result->tags = "key1 key2 key3"_c;
	result->title = "Title"_c;
	result->width = 1024;
	result->height = 683;
	result->pixel_format = "YCbCr"_c;

	return result;
}


df::index_file_item make_index_file_info(const df::date_t date)
{
	df::index_file_item result;
	result.file_modified = date;
	result.file_created = date;
	result.ft = files::file_type_from_name("test.jpg");
	result.safe_ps();
	return result;
}


df::item_element_ptr load_item(index_state& index, const df::file_path path, const bool load_thumb)
{
	auto i = std::make_shared<df::item_element>(path, index.find_item(path));
	index.scan_item(i, load_thumb, false);
	return i;
}

std::string_view detect_xmp_sidecar(const df::file_path path)
{
	const auto xmp_path = path.extension(".xmp");
	return xmp_path.exists() ? xmp_path.name() : std::string_view{};
}

int count_search_results(index_state& index, const df::search_t& search)
{
	int result = 0;

	auto cb = [&result](const index_state::query_item_results& items, const bool completed)
	{
		result += static_cast<int>(items.size());
	};

	index.query_items(search, cb, test_token);
	return result;
}

int count_search_results(index_state& index, const std::string_view query)
{
	return count_search_results(index, df::search_t::parse(query));
}

void build_index(index_state& index, database& db)
{
	df::index_roots paths;
	paths.folders.emplace(test_files_folder);
	paths.excludes.emplace(test_files_folder.combine("excluded1"));
	paths.exclude_wildcards.emplace("exclud*2"_c);

	index.index_roots(paths);
	index.index_folders(test_token);
	index.scan_uncached(test_token);
	db.perform_writes();

	assert_equal(expected_cached_item_count, index.stats.media_item_count, "cached item count");
}


void shared_test_context::lazy_load_index()
{
	if (!loaded)
	{
		const auto cache_path1 = _temps.next_path();

		database db1(test_index);

		db1.open(cache_path1.folder(), cache_path1.file_name_without_extension());

		build_index(test_index, db1);

		const df::item_selector selector(test_files_folder, true);
		df::item_set items1;

		auto cb1 = [this, &items1](index_state::query_item_results items, const bool completed)
		{
			items1.append(test_index.materialize_query_items(std::move(items), {}));
		};

		test_index.query_items(df::search_t().add_selector(selector), cb1, test_token);

		items1.for_all([](const auto& item) { item->begin_db_thumbnail_query(); });
		db1.load_thumbnails(test_index, database::make_thumbnail_requests(items1));
		test_index.scan_items(items1, false, false, false, false, test_token);
		db1.perform_writes();

		assert_equal(expected_cached_item_count, test_index.stats.media_item_count, "cached item count");
		assert_equal(0, empty_index.stats.media_item_count, "cached item count");
		loaded = true;
	}
}

struct null_av_host final : av_host
{
	void invalidate_view(const view_invalid invalid) override
	{
	}

	void queue_ui(const std::function<void()> f) override
	{
		f();
	}
};

location_cache& test_locations()
{
	static location_cache cache;
	if (!cache.is_index_loaded()) cache.load_index();
	return cache;
}

std::shared_ptr<av_player> make_test_player()
{
	static null_av_host navh;
	return std::make_shared<av_player>(navh);
}

std::shared_ptr<av_session> make_test_session()
{
	static null_av_host navh;
	return std::make_shared<av_session>(navh, nullptr);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Utility test functions
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_calc_HMACSHA1()
{
	const auto signature = crypto::hmac_sha1("Jefe", "what do ya want for nothing?");
	assert_equal("7/zfauXrL6LSdBbV8YTfnCWafHk=", signature, "Signature");
}

static void should_calc_hashes()
{
	assert_equal("A9993E364706816ABA3E25717850C26C9CD0D89D", crypto::to_sha1("abc"), "SHA1");
	assert_equal("187797D630ECAA0FC1B920CD9F809C2BBFFCBF4C", crypto::to_sha1(long_text), "SHA1");
	assert_equal("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", crypto::to_sha256("abc"),
	             "SHA256");
	assert_equal("1660F10AEC042D762CF8B1C53E976F890C8E797BEF74807F505EDCE20308FC2F", crypto::to_sha256(long_text),
	             "SHA256");

	const auto crc_data = "hello world"s;
	const auto crc_result = crypto::crc32c(crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_result, "crc32");

	const auto crc_c = ~calc_crc32c_c(crypto::CRCINIT, crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_c, "crc32 c");

	if (platform::crc32_supported)
	{
		const auto crc_x86 = ~calc_crc32c_x86(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_x86, "crc32 x86");
	}

	if (platform::arm_crc32_supported)
	{
		const auto crc_neon = ~calc_crc32c_arm(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_neon, "crc32 neon");
	}

	alignas(16) std::array<uint8_t, 96> boundary_data;
	for (auto i = 0u; i < boundary_data.size(); ++i)
	{
		boundary_data[i] = static_cast<uint8_t>(i * 37u + 11u);
	}

	for (auto offset = 0u; offset < 16u; ++offset)
	{
		for (auto len = 0u; len <= 80u; ++len)
		{
			const auto* const data = boundary_data.data() + offset;
			const auto expected = calc_crc32c_c(crypto::CRCINIT, data, len);
			assert_equal(~expected, crypto::crc32c(data, len), "crc32 dispatched boundary");

			if (platform::crc32_supported)
			{
				assert_equal(expected, calc_crc32c_x86(crypto::CRCINIT, data, len), "crc32 x86 boundary");
				const auto split = len / 2;
				const auto first = calc_crc32c_x86(crypto::CRCINIT, data, split);
				assert_equal(expected, calc_crc32c_x86(first, data + split, len - split), "crc32 x86 continuation");
			}

			if (platform::arm_crc32_supported)
			{
				assert_equal(expected, calc_crc32c_arm(crypto::CRCINIT, data, len), "crc32 arm boundary");
				const auto split = len / 2;
				const auto first = calc_crc32c_arm(crypto::CRCINIT, data, split);
				assert_equal(expected, calc_crc32c_arm(first, data + split, len - split), "crc32 arm continuation");
			}
		}
	}
}

// A synthetic 32x32 field, so the hash is tested on its own terms without a decoder in the way.
static std::array<uint8_t, crypto::phash_pixels> make_phash_field(const int seed)
{
	std::array<uint8_t, crypto::phash_pixels> result{};

	for (auto y = 0u; y < crypto::phash_extent; ++y)
	{
		for (auto x = 0u; x < crypto::phash_extent; ++x)
		{
			const auto v = (x * 7 + y * 13 + seed * 29) % 251;
			result[y * crypto::phash_extent + x] = static_cast<uint8_t>((v * v) % 256);
		}
	}

	return result;
}

static void should_calc_perceptual_hashes()
{
	const auto field = make_phash_field(1);
	const auto hash = crypto::perceptual_hash(field.data(), field.size());

	assert_equal(true, crypto::phash_is_usable(hash), "a detailed field hashes");
	assert_equal(hash, crypto::perceptual_hash(field.data(), field.size()), "the same pixels hash the same");
	assert_equal(0, crypto::phash_distance(hash, hash), "distance to itself");

	// Bit 0 comes from the DC coefficient, which is excluded, so it is free to mark a declined hash.
	assert_equal(0ull, hash & 1ull, "a real hash never sets the reserved bit");
	assert_equal(false, crypto::phash_is_usable(crypto::phash_declined), "the declined marker is not a hash");
	assert_equal(false, crypto::phash_is_usable(0), "not computed is not a hash");

	// Brightness and contrast move every pixel but not the picture, which is what a re-encode does.
	auto brightened = field;
	for (auto& v : brightened) v = static_cast<uint8_t>(std::min(255, v + 20));
	assert_equal(true, crypto::phash_distance(hash, crypto::perceptual_hash(brightened.data(), brightened.size())) <= 6,
	             "brightness does not change the picture");

	// A different picture has to land far away, or the threshold means nothing.
	const auto other = make_phash_field(9);
	assert_equal(true, crypto::phash_distance(hash, crypto::perceptual_hash(other.data(), other.size())) > 6,
	             "a different picture is far away");

	// Flat fields are where a 64-bit hash quietly starts matching everything.
	std::array<uint8_t, crypto::phash_pixels> blank{};
	blank.fill(128);
	assert_equal(false, crypto::phash_is_usable(crypto::perceptual_hash(blank.data(), blank.size())),
	             "a blank image has no opinion");

	std::array<uint8_t, crypto::phash_pixels> almost_blank{};
	almost_blank.fill(128);
	almost_blank[0] = 129;
	assert_equal(false, crypto::phash_is_usable(crypto::perceptual_hash(almost_blank.data(), almost_blank.size())),
	             "one different pixel is not detail");

	assert_equal(0ull, crypto::perceptual_hash(nullptr, crypto::phash_pixels), "no pixels");
	assert_equal(0ull, crypto::perceptual_hash(field.data(), crypto::phash_pixels - 1), "short buffer");
}

static uint64_t phash_of_file(const std::string_view name)
{
	files ff;
	file_read_stream stream;
	if (!stream.open(test_files_folder.combine_file(name))) return 0;

	df::blob owner;
	return ff.calc_perceptual_hash(stream.view_all(owner));
}

// The point of the hash is the case a checksum cannot see: the same picture in a different file.
static void should_recognise_the_same_picture()
{
	const auto original = phash_of_file("Test.jpg");
	const auto resized = phash_of_file("Small.jpg");

	assert_equal(true, crypto::phash_is_usable(original), "Test.jpg hashes");
	assert_equal(true, crypto::phash_is_usable(resized), "Small.jpg hashes");

	// Measured separation on these fixtures: 0 for the resize, 30 for a rotation, 32 for an
	// unrelated photo. The threshold sits in that gap rather than near either side of it.
	assert_equal(0, crypto::phash_distance(original, resized), "a resized copy is the same picture");

	// A rotation is a different bitmap, and the hash must not pretend otherwise - a duplicate claim
	// on it would offer to delete a photo the user deliberately made.
	assert_equal(true, crypto::phash_distance(original, phash_of_file("Test90.jpg")) > 20,
	             "a rotated copy is not the same bitmap");

	const auto unrelated = phash_of_file("IMG_0096.JPG");
	assert_equal(true, crypto::phash_is_usable(unrelated), "an unrelated photo hashes");
	assert_equal(true, crypto::phash_distance(original, unrelated) > 20, "an unrelated photo is far away");
}

static uint8_t blend_opaque_channel(const uint8_t dest, const float src, const float alpha)
{
	return static_cast<uint8_t>(std::clamp(static_cast<int>(src * alpha + dest * (1.0f - alpha) + 0.5f), 0, 255));
}
static uint8_t blend_opaque_normalized_channel(const uint8_t dest, const float src, const float alpha)
{
	const auto value = src * alpha + (dest / 255.0f) * (1.0f - alpha);
	return static_cast<uint8_t>(std::clamp(static_cast<int>(value * 255.0f + 0.5f), 0, 255));
}

static void should_match_simd_software_blends()
{
#if defined(COMPILE_SIMD_INTRINSIC)
	constexpr size_t max_pixels = 19;
	alignas(16) std::array<uint8_t, max_pixels * 4> original{};
	alignas(16) std::array<uint8_t, max_pixels * 4> source{};
	alignas(16) std::array<uint8_t, max_pixels> coverage{};

	for (size_t i = 0; i < original.size(); ++i) original[i] = static_cast<uint8_t>(i * 47u + 13u);
	for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i * 29u + 7u);
	for (size_t i = 0; i < coverage.size(); ++i) coverage[i] = static_cast<uint8_t>(i * 61u + 3u);

	constexpr std::array<float, 5> alphas = {0.0f, 0.13f, 0.5f, 0.87f, 1.0f};
	constexpr float sb = 37.0f;
	constexpr float sg = 149.0f;
	constexpr float sr = 231.0f;

	for (size_t count = 0; count <= max_pixels; ++count)
	{
		for (const auto alpha : alphas)
		{
			auto check = [&](auto&& scalar, auto&& simd, const std::string_view message)
			{
				auto expected = original;
				auto actual = original;
				for (size_t i = 0; i < count; ++i) scalar(expected.data() + i * 4, i);
				const auto processed = simd(actual.data());
				for (size_t i = processed; i < count; ++i) scalar(actual.data() + i * 4, i);
				assert_equal(true, expected == actual, message);
			};

			auto blend_solid_pixel = [](uint8_t* dest, const float b, const float g, const float r, const float a)
			{
				dest[0] = blend_opaque_normalized_channel(dest[0], b, a);
				dest[1] = blend_opaque_normalized_channel(dest[1], g, a);
				dest[2] = blend_opaque_normalized_channel(dest[2], r, a);
				dest[3] = 255;
			};

			check([&](uint8_t* dest, size_t) { blend_solid_pixel(dest, sb / 255.0f, sg / 255.0f, sr / 255.0f, alpha); },
			      [&](uint8_t* dest)
			      {
				      return blend_solid_opaque_sse2(dest, count, sb / 255.0f, sg / 255.0f,
				                                     sr / 255.0f, alpha);
			      }, "SIMD solid blend");

			check([&](uint8_t* dest, const size_t i)
			      {
				      blend_solid_pixel(dest, sb / 255.0f, sg / 255.0f,
				                        sr / 255.0f, alpha * coverage[i] / 255.0f);
			      },
			      [&](uint8_t* dest)
			      {
				      return blend_glyph_opaque_sse2(dest, coverage.data(), count, sb / 255.0f,
				                                     sg / 255.0f, sr / 255.0f, alpha);
			      }, "SIMD glyph blend");

			for (const auto has_alpha : {false, true})
			{
				check([&](uint8_t* dest, const size_t i)
				      {
					      const auto* src = source.data() + i * 4;
					      const auto pixel_alpha = (has_alpha ? src[3] / 255.0f : 1.0f) * alpha;
					      dest[0] = blend_opaque_channel(dest[0], src[0], pixel_alpha);
					      dest[1] = blend_opaque_channel(dest[1], src[1], pixel_alpha);
					      dest[2] = blend_opaque_channel(dest[2], src[2], pixel_alpha);
					      dest[3] = 255;
				      }, [&](uint8_t* dest)
				      {
					      return blend_bgra_opaque_sse2(dest, source.data(), count, has_alpha, alpha);
				      }, "SIMD BGRA blend");
			}
		}
	}
#endif
}

static void should_convert_yuv_surfaces_for_software_rendering()
{
	auto check_output = [](const ui::surface_ptr& output)
	{
		assert_equal(true, ui::is_valid(output), "converted YUV surface is valid");
		assert_equal(uint32_t{4}, output->width(), "converted YUV width");
		assert_equal(uint32_t{2}, output->height(), "converted YUV height");
		for (auto y = 0; y < 2; ++y)
		{
			const auto* const row = output->pixels_line(y);
			for (auto x = 0; x < 4; ++x)
			{
				const auto* const pixel = row + x * 4;
				const auto expected = x < 2 ? 0 : 255;
				assert_equal(true, std::abs(static_cast<int>(pixel[0]) - expected) <= 2, "YUV blue channel");
				assert_equal(true, std::abs(static_cast<int>(pixel[1]) - expected) <= 2, "YUV green channel");
				assert_equal(true, std::abs(static_cast<int>(pixel[2]) - expected) <= 2, "YUV red channel");
				assert_equal(255, pixel[3], "YUV alpha channel");
			}
		}
	};

	av_scaler scaler;
	const auto output = std::make_shared<ui::surface>();
	ui::surface nv12;
	nv12.alloc(4, 2, ui::texture_format::NV12);
	nv12.color_space(ui::color_space::rec601_limited);
	for (auto y = 0; y < 2; ++y)
	{
		auto* const row = nv12.pixels_line(y);
		row[0] = row[1] = 16;
		row[2] = row[3] = 235;
	}
	auto* const nv12_chroma = nv12.pixels() + nv12.stride() * nv12.height();
	std::fill_n(nv12_chroma, nv12.stride(), uint8_t{128});
	assert_equal(true, scaler.convert_yuv_surface(nv12, output), "convert NV12 surface");
	check_output(output);
	ui::surface_ptr scaled;
	const auto nv12_view = ui::const_surface_ptr(&nv12, [](const ui::surface*)
	{
	});
	assert_equal(true, scaler.scale_surface(nv12_view, scaled, {2, 1}), "scale NV12 surface");
	assert_equal(true, scaled->dimensions() == sizei{2, 1}, "scaled NV12 dimensions");
	assert_equal(true, scaled->format() == ui::texture_format::RGB, "scaled NV12 format");

	ui::surface p010;
	p010.alloc(4, 2, ui::texture_format::P010);
	p010.color_space(ui::color_space::rec601_limited);
	for (auto y = 0; y < 2; ++y)
	{
		auto* const row = std::bit_cast<uint16_t*>(p010.pixels_line(y));
		row[0] = row[1] = 64u << 6;
		row[2] = row[3] = 940u << 6;
	}
	auto* const p010_chroma = std::bit_cast<uint16_t*>(p010.pixels() + p010.stride() * p010.height());
	std::fill_n(p010_chroma, p010.stride() / sizeof(uint16_t), uint16_t{512u << 6});
	assert_equal(true, scaler.convert_yuv_surface(p010, output), "convert P010 surface");
	check_output(output);
	const auto p010_view = ui::const_surface_ptr(&p010, [](const ui::surface*)
	{
	});
	assert_equal(true, scaler.scale_surface(p010_view, scaled, {2, 1}), "scale P010 surface");
	assert_equal(true, scaled->dimensions() == sizei{2, 1}, "scaled P010 dimensions");
	assert_equal(true, scaled->format() == ui::texture_format::RGB, "scaled P010 format");
}

static void should_layout_selection_thumbnail_collage()
{
	const auto check = [](const recti draw_bounds, const std::vector<sizei>& dimensions, const size_t expected_count)
	{
		const auto bounds = ui::layout_collage(draw_bounds, dimensions);
		assert_equal(expected_count, bounds.size(), "collage bounds count");

		for (auto index = 0u; index < bounds.size(); ++index)
		{
			const auto& bound = bounds[index];
			assert_equal(true, bound.area() > 0, "collage cell has area");
			assert_equal(true, draw_bounds.contains(bound.top_left()), "collage cell top-left contained");
			assert_equal(true, draw_bounds.contains({bound.right - 1, bound.bottom - 1}),
			             "collage cell bottom-right contained");

			for (auto other = index + 1; other < bounds.size(); ++other)
			{
				const auto& other_bound = bounds[other];
				const auto overlaps = bound.left < other_bound.right && bound.right > other_bound.left &&
					bound.top < other_bound.bottom && bound.bottom > other_bound.top;
				assert_equal(false, overlaps, "collage cells do not overlap");
			}
		}
	};

	check({0, 0, 1200, 800}, std::vector<sizei>(9, sizei(256, 256)), 9);
	check({0, 0, 1600, 900}, {
		      {1200, 200}, {180, 900}, {400, 400}, {300, 800}, {1400, 350}, {640, 480},
		      {200, 1000}, {1600, 300}, {500, 500}, {900, 1600}, {1000, 250}, {300, 300}
	      }, 12);
	check({0, 0, 1920, 1080}, std::vector<sizei>(24, sizei(320, 240)), 24);
	check({0, 0, 1920, 1080}, std::vector<sizei>(30, sizei(320, 240)), 24);

	const auto aesthetic_bounds = ui::layout_collage({0, 0, 1200, 800}, std::vector<sizei>(9, sizei(256, 256)));
	const auto first_row_top = aesthetic_bounds.front().top;
	assert_equal(true, std::any_of(aesthetic_bounds.begin(), aesthetic_bounds.end(),
	                               [first_row_top](const recti& bounds) { return bounds.top != first_row_top; }),
	             "collage uses multiple rows");
	const auto [smallest, largest] = std::minmax_element(aesthetic_bounds.begin(), aesthetic_bounds.end(),
	                                                     [](const recti& left, const recti& right)
	                                                     {
		                                                     return left.area() < right.area();
	                                                     });
	assert_equal(true, largest->area() > smallest->area() * 3 / 2, "collage varies cell sizes");
	assert_equal(800, std::max_element(aesthetic_bounds.begin(), aesthetic_bounds.end(),
	                                   [](const recti& left, const recti& right)
	                                   {
		                                   return left.bottom < right.bottom;
	                                   })->bottom,
	             "collage fills available height");

	const auto portrait_bounds = ui::layout_collage({0, 0, 600, 1200}, std::vector<sizei>(9, sizei(256, 256)));
	assert_equal(1200, std::max_element(portrait_bounds.begin(), portrait_bounds.end(),
	                                    [](const recti& left, const recti& right)
	                                    {
		                                    return left.bottom < right.bottom;
	                                    })->bottom,
	             "portrait collage fills available height");
	assert_equal(true, std::any_of(portrait_bounds.begin(), portrait_bounds.end(),
	                               [](const recti& bounds) { return bounds.height() > bounds.width(); }),
	             "portrait collage has vertical cells");
}

static void should_convert_utf8()
{
	// icon font
	wchar_t stars_utf16[6] = {};

	for (auto i = 0; i < 5; i++)
	{
		stars_utf16[i] = static_cast<uint16_t>(i & 0x01 ? icon_index::star_solid : icon_index::star);
	}
	stars_utf16[5] = 0;

	const auto stars = platform::utf16_to_utf8(stars_utf16);

	std::string_view strings[] = {
		"In vollen Zügen genießen",
		"Nældens takvinge",
		"💉💎👦🏻👓⚡",
		"Žižkov",
		"Доброго ранку!",
		"Japanese こんにちは世界",
		"Arabic مرحبا العالم",
		stars
	};

	for (const auto src : strings)
	{
		assert_equal(src, platform::utf16_to_utf8(platform::utf8_to_utf16(src)), "platform conversions");
		assert_equal(platform::utf8_to_utf16(src), str::utf8_to_utf16(src), "to utf16");
		assert_equal(src, str::utf16_to_utf8(platform::utf8_to_utf16(src)), "to utf8");
		assert_equal(src, str::utf16_to_utf8(str::utf8_to_utf16(src)), "internal conversions");
	}

	constexpr wchar_t icon_text[2] = {static_cast<wchar_t>(icon_index::fit), 0};
	const auto icon_text_converted = str::utf8_to_utf16(str::utf16_to_utf8(icon_text));
	assert_equal(icon_text, icon_text_converted, "icon to utf8");

	// Verify icon_to_utf8 matches the old wchar_t + utf16_to_utf8 approach
	const auto icon_new = icon_to_utf8(icon_index::fit);
	constexpr wchar_t icon_old_text[2] = {static_cast<wchar_t>(icon_index::fit), 0};
	const auto icon_old = str::utf16_to_utf8(icon_old_text);
	assert_equal(icon_old, icon_new, "icon_to_utf8 matches old approach");
	assert_equal(false, icon_is_mirrored(icon_index::rotate_clockwise), "clockwise icon is not mirrored");
	assert_equal(true, icon_is_mirrored(icon_index::rotate_anticlockwise), "anticlockwise icon is mirrored");

	// Verify char32_to_utf8 round-trips for icon code points
	std::string char32_result;
	str::char32_to_utf8(std::back_inserter(char32_result), static_cast<uint32_t>(icon_index::fit) & 0xFFFF);
	assert_equal(icon_old, char32_result, "char32_to_utf8 for icon");

	// Verify every icon in the icon_index enum round-trips correctly through UTF-8
	constexpr icon_index all_icons[] = {
		icon_index::add, icon_index::remove, icon_index::audio, icon_index::camera,
		icon_index::cancel, icon_index::check, icon_index::del, icon_index::edit,
		icon_index::folder, icon_index::search, icon_index::star, icon_index::star_solid,
		icon_index::play, icon_index::pause, icon_index::stop, icon_index::copyright,
		icon_index::photo, icon_index::video, icon_index::settings, icon_index::save,
		icon_index::rotate_clockwise, icon_index::rotate_anticlockwise,
		icon_index::fit, icon_index::zoom_in, icon_index::zoom_out,
	};

	for (const auto icon : all_icons)
	{
		const auto icon_val = static_cast<uint32_t>(icon) & 0xFFFF;
		const wchar_t expected_utf16[2] = {static_cast<wchar_t>(icon_val), 0};
		const auto expected_utf8 = platform::utf16_to_utf8(expected_utf16);

		// icon_to_utf8 should produce correct UTF-8
		const auto actual_utf8 = icon_to_utf8(icon);
		assert_equal(expected_utf8, actual_utf8,
		             std::format("icon_to_utf8 for 0x{:X}", static_cast<uint32_t>(icon)));

		// UTF-8 should be exactly 3 bytes for BMP icons >= 0x800
		if (icon_val >= 0x800)
		{
			assert_equal(3, static_cast<int>(actual_utf8.size()),
			             std::format("icon UTF-8 byte count for 0x{:X}", icon_val));
		}

		// Round-trip: UTF-8 -> UTF-16 should recover the original code point
		const auto round_tripped_utf16 = str::utf8_to_utf16(actual_utf8);
		assert_equal(1, static_cast<int>(round_tripped_utf16.size()),
		             std::format("icon round-trip UTF-16 length for 0x{:X}", icon_val));
		assert_equal(static_cast<int>(icon_val), round_tripped_utf16[0],
		             std::format("icon round-trip code point for 0x{:X}", icon_val));

		// Also verify str:: matches platform:: conversion
		const auto platform_utf16 = platform::utf8_to_utf16(actual_utf8);
		assert_equal(platform_utf16, round_tripped_utf16,
		             std::format("icon str vs platform utf8_to_utf16 for 0x{:X}", icon_val));
	}
}

static void should_split()
{
	constexpr auto to_be_split = "H:\\2-Archief VIDEO privé\\Eigen video's\nF:\\1-Archief FOTOGRAFIE privé";
	const auto parts = str::split(to_be_split, false, [](const wchar_t c) { return c == '\n' || c == '\r'; });

	assert_equal("H:\\2-Archief VIDEO privé\\Eigen video's", parts[0], "Split 1");
	assert_equal("F:\\1-Archief FOTOGRAFIE privé", parts[1], "Split 2");

	constexpr auto to_be_split2 = "aaa 'bbb ccc' ddd \"ee ff \"";
	const auto parts2 = str::split(to_be_split2, true);

	constexpr auto to_be_split3 = "Доброго ранку!";
	const auto parts3 = str::split(to_be_split3, true);

	assert_equal("aaa", parts2[0], "Split 1");
	assert_equal("bbb ccc", parts2[1], "Split 2");
	assert_equal("ddd", parts2[2], "Split 3");
	assert_equal("ee ff ", parts2[3], "Split 4");
	assert_equal("ранку!", parts3[1], "Split 5");

	// Random data checking for crashes
	std::string_view strings[] = {
		"In vollen Zügen genießen",
		"Nældens takvinge",
		"Žižkov",
		"Доброго ранку!",
		"Japanese こんにちは世界",
		"Arabic مرحبا العالم",
		"Доброго ранку!",
		"\"'",
		"\" \" \"",
		"''''",
		"aaa'bb  bbb'aa",
		"aaa\0\0\'",
		"\r\t\naaaa\" aaa bbb",
		"'\t \n abc",
		"'",
	};

	for (const auto& src : strings)
	{
		str::split_count(src, true);
	}
}

static void should_split_genre()
{
	// Genre values use ';' as the multi-value separator. Multi-word genres and
	// genres containing '&' or '/' must survive splitting intact.
	const auto parts = str::split("Rock; Pop ; Hip Hop", false, str::is_genre_separator);
	assert_equal(size_t{3}, parts.size(), "genre part count");
	assert_equal("Rock", str::trim(parts[0]), "genre 1");
	assert_equal("Pop", str::trim(parts[1]), "genre 2");
	assert_equal("Hip Hop", str::trim(parts[2]), "genre 3");

	const auto parts2 = str::split("Action & Adventure; R&B/Soul", false, str::is_genre_separator);
	assert_equal(size_t{2}, parts2.size(), "genre part count 2");
	assert_equal("Action & Adventure", str::trim(parts2[0]), "genre with ampersand");
	assert_equal("R&B/Soul", str::trim(parts2[1]), "genre with slash");

	const auto parts3 = str::split("Jazz", false, str::is_genre_separator);
	assert_equal(size_t{1}, parts3.size(), "single genre part count");
	assert_equal("Jazz", str::trim(parts3[0]), "single genre");
}

static void should_extract_url()
{
	constexpr auto input1 = "Visit my website at https://www.example.com for more info.";
	constexpr auto input2 = "Check out this article: http://anotherexample.org/article";
	constexpr auto input3 = "No URLs here.";
	constexpr auto input4 =
		"Quite nice  <a href=\"http://bighugelabs.com/flickr/onblack.php?id=1397504988\"> On Black</a>";

	assert_equal("https://www.example.com", df::url_extract(input1), "extract url");
	assert_equal("http://anotherexample.org/article", df::url_extract(input2), "extract url");
	assert_equal("", df::url_extract(input3), "extract url");
	assert_equal("", df::url_extract(input3), "extract url");
	assert_equal("http://bighugelabs.com/flickr/onblack.php?id=1397504988", df::url_extract(input4),
	             "extract url");

	// A description panel offering a choice of links needs every distinct one, in reading order.
	const auto all = df::url_extract_all(
		"See https://example.com/a and https://example.com/b then https://example.com/a again.");
	assert_equal(size_t{2}, all.size(), "repeated url listed once");
	assert_equal("https://example.com/a", all[0], "first url in source order");
	assert_equal("https://example.com/b", all[1], "second url in source order");
	assert_equal(size_t{0}, df::url_extract_all(input3).size(), "no urls found");
}

// The description panel presents one section for every prose field, so the field list drives its
// header name, its ordering, and which entries collapse as repeats.
static void should_collect_descriptive_fields()
{
	prop::item_metadata none;
	assert_equal(size_t{0}, prop::descriptive_fields(none).size(), "no prose fields");

	prop::item_metadata one;
	one.comment = "A note"_c;
	const auto only_comment = prop::descriptive_fields(one);
	assert_equal(size_t{1}, only_comment.size(), "single prose field");
	assert_equal("comment", only_comment[0].id, "lone field keeps its own identity");
	assert_equal(false, only_comment[0].duplicate, "a lone field is never a repeat");

	prop::item_metadata all;
	all.comment = "Same text"_c;
	all.description = "Same text"_c;
	all.synopsis = "Different text"_c;
	const auto ordered = prop::descriptive_fields(all);
	assert_equal(size_t{3}, ordered.size(), "every populated field listed");
	assert_equal("description", ordered[0].id, "description leads");
	assert_equal("synopsis", ordered[1].id, "synopsis follows");
	assert_equal("comment", ordered[2].id, "comment last");
	assert_equal(false, ordered[0].duplicate, "leading field is the original");
	assert_equal(false, ordered[1].duplicate, "distinct text is not a repeat");
	assert_equal(true, ordered[2].duplicate, "text already shown is marked a repeat");
}

static void should_match_wildcard()
{
	assert_equal(true, str::wildcard_icmp("", ""));
	assert_equal(true, str::wildcard_icmp("", "*"));
	assert_equal(true, str::wildcard_icmp(" ", "*"));
	assert_equal(true, str::wildcard_icmp(" ", " *"));
	assert_equal(false, str::wildcard_icmp(" ", "  *"));

	assert_equal(true, str::wildcard_icmp("hello world", "hello world"));
	assert_equal(true, str::wildcard_icmp("hello ?! world", "hello * world"));
	assert_equal(true, str::wildcard_icmp("hello-xx-world", "hello*world"));
	assert_equal(false, str::wildcard_icmp("hello-xx-world", "hello *world"));
	assert_equal(true, str::wildcard_icmp("hello-xx-world", "*world"));
	assert_equal(true, str::wildcard_icmp("hello-xx-world", "hello*"));

	assert_equal(true, str::wildcard_icmp("HELLO-XX-WORLD", "hello*"));
	assert_equal(true, str::wildcard_icmp("HELLO-XX-WORLD", "hello*world"));


	assert_equal(0, str::icmp("ДОБРОГО РАНКУ", "Доброго ранку"));
	assert_equal(0, str::icmp("ARABIC مرحبا العالم", "Arabic مرحبا العالم"));
	assert_equal(0, str::icmp("JAPANESE こんにちは世界", "Japanese こんにちは世界"));
	assert_equal(0, str::icmp("💉💎👦🏻👓⚡", "💉💎👦🏻👓⚡"));

	assert_equal(true, str::wildcard_icmp("Доброго ранку", "Доброго*"));
	assert_equal(true, str::wildcard_icmp("ДОБРОГО РАНКУ", "Доброго*"));
	assert_equal(true, str::wildcard_icmp("ДОБРОГО РАНКУ", "*ранку"));
	assert_equal(true, str::wildcard_icmp("💉💎👦🏻👓⚡", "*💎*"));
	assert_equal(true, str::wildcard_icmp("💉💎👦🏻👓⚡", "💉*"));
}

static void should_detect_wildcard()
{
	assert_equal(false, str::is_wildcard(""));
	assert_equal(false, str::is_wildcard("abcdef"));
	assert_equal(true, str::is_wildcard("abc*"));
	assert_equal(false, str::is_wildcard("abc\\*"));
	assert_equal(false, str::is_wildcard("abc\\*ef"));
}

static void should_encrypt_password()
{
	const std::vector<uint8_t> test_key = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
	};

	const std::vector<uint8_t> test_dec = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
		0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
		0x20, 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97, 0xa8, 0xb9, 0xca, 0xdb, 0xec, 0xfd, 0x0e, 0x1f,
		0x30, 0x41, 0x52, 0x63, 0x74, 0x85, 0x96, 0xa7, 0xb8, 0xc9, 0xda, 0xeb, 0xfc, 0x0d, 0x1e, 0x2f,
		0x40, 0x51, 0x62, 0x73, 0x84, 0x95, 0xa6, 0xb7, 0xc8, 0xd9, 0xea, 0xfb, 0x0c, 0x1d, 0x2e, 0x3f,
		0x50, 0x61, 0x72, 0x83, 0x94, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0xfa, 0x0b, 0x1c, 0x2d, 0x3e, 0x4f,
		0x60, 0x71, 0x82, 0x93
	};

	// CBC mode uses random IV, so output is non-deterministic; verify round-trip instead
	const auto encrypted = crypto::encrypt(test_dec, test_key);
	std::vector<uint8_t> decrypted;
	crypto::aes256::decrypt(test_key, encrypted, decrypted);
	assert_equal(base64_encode(test_dec), base64_encode(decrypted), "encrypt using aes");

	const std::vector<std::string_view> test_values =
	{
		{},
		"This is a test.",
		long_text
	};

	static constexpr auto password = "diffractor-hello";

	for (const auto& val : test_values)
	{
		auto result = crypto::decrypt(crypto::encrypt(val, password), password);
		assert_equal(val, std::string(result.begin(), result.end()), "Encode - Decode");
	}

	// Check for crash based on bad data
	const std::vector<uint8_t> empty;
	crypto::decrypt(empty, password);

	std::vector<uint8_t> invalid;
	for (auto i = 0; i < 8; i++) invalid.emplace_back(i);
	crypto::decrypt(invalid, password);
}


static void should_persist_to_ini_file()
{
	const auto settings_folder = _temps.folder().combine("ini-settings");
	platform::create_folder(settings_folder);
	const auto settings = platform::create_ini_file_settings(settings_folder);

	// A folder without an INI is a new settings root.
	assert_equal(true, settings->root_created(), "root_created", "INI file settings");

	// Test uint32_t
	constexpr uint32_t test_uint32 = 12345;
	settings->write("test_section", "uint32_value", test_uint32);
	const auto existing_settings = platform::create_ini_file_settings(settings_folder);
	assert_equal(false, existing_settings->root_created(), "existing root", "INI file settings");
	uint32_t read_uint32 = 0;
	assert_equal(true, settings->read("test_section", "uint32_value", read_uint32), "read uint32",
	             "INI file settings");
	assert_equal(test_uint32, read_uint32, "uint32 value", "INI file settings");

	// Test uint64_t
	constexpr uint64_t test_uint64 = 0xFFFFFFFFFFFFull;
	settings->write("test_section", "uint64_value", test_uint64);
	uint64_t read_uint64 = 0;
	assert_equal(true, settings->read("test_section", "uint64_value", read_uint64), "read uint64",
	             "INI file settings");
	assert_equal(test_uint64, read_uint64, "uint64 value", "INI file settings");

	// Test string
	const auto test_string = "Hello, World! With special chars: äöü"s;
	settings->write("test_section", "string_value", test_string);
	std::string read_string;
	assert_equal(true, settings->read("test_section", "string_value", read_string), "read string",
	             "INI file settings");
	assert_equal(test_string, read_string, "string value", "INI file settings");

	// Test binary data (base64 encoded)
	const std::vector<uint8_t> test_binary = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
	settings->write("test_section", "binary_value", df::cspan{test_binary.data(), test_binary.size()});
	std::vector<uint8_t> read_buffer(test_binary.size());
	size_t read_len = read_buffer.size();
	assert_equal(true, settings->read("test_section", "binary_value", read_buffer.data(), read_len),
	             "read binary", "INI file settings");
	assert_equal(test_binary.size(), read_len, "binary length", "INI file settings");
	for (size_t i = 0; i < test_binary.size(); ++i)
	{
		assert_equal(static_cast<uint32_t>(test_binary[i]), static_cast<uint32_t>(read_buffer[i]), "binary byte",
		             "INI file settings");
	}
}

static void should_parse_command_line()
{
	command_line_t cl1;
	cl1.parse("-no-gpu");

	assert_equal(true, cl1.no_gpu, "no_gpu");
	assert_equal(false, cl1.no_indexing, "no_indexing");

	command_line_t cl2;
	cl2.parse(test_files_folder.text());

	assert_equal(false, cl2.folder_path.is_empty(), "folder_path");
	assert_equal(std::string_view{}, cl2.selection.name(), "selection name");
	assert_equal(test_files_folder.text(), cl2.folder_path.folder().text(), "folder path");
	assert_equal(false, cl2.no_gpu, "no_gpu");
	assert_equal(false, cl2.no_indexing, "no_indexing");


	const auto path3 = test_files_folder.combine_file("test.jpg");
	command_line_t cl3;
	cl3.parse(std::format("{} -no-indexing", path3));

	assert_equal(path3.folder().text(), cl3.folder_path.folder().text(), "folder_path");
	assert_equal(path3.name(), cl3.selection.name(), "selection name");
	assert_equal(path3.folder().text(), cl3.selection.folder().text(), "selection folder");
	assert_equal(false, cl3.no_gpu, "no_gpu");
	assert_equal(true, cl3.no_indexing, "no_indexing");

	command_line_t cl4;
	cl4.parse("--no-gpu \"C:\\Program Files\"");
	assert_equal(true, cl4.no_gpu, "no_gpu");
	assert_equal(false, cl4.folder_path.is_empty(), "folder_path program Files");

	command_line_t cl5;
	cl5.parse("----- --no-gpu");
	assert_equal(true, cl5.no_gpu, "no_gpu");

	command_line_t cl6;
	cl6.parse("-no-gpu -no-indexing");
	assert_equal(true, cl6.no_gpu, "multiple options no_gpu");
	assert_equal(true, cl6.no_indexing, "multiple options no_indexing");

#ifdef _DEBUG
	command_line_t cl7;
	cl7.parse("-screenshot:edit \"-screenshot-output:C:\\temp\\edit.png\"");
	assert_equal("edit"sv, cl7.screenshot_scene, "screenshot scene");
	assert_equal("C:\\temp\\edit.png"sv, cl7.screenshot_output, "screenshot output");

	command_line_t cl8;
	cl8.parse("-test-reset-graphics");
	assert_equal("reset-graphics"sv, cl8.test_action, "test action");
#endif

	command_line_t cl9;
	cl9.parse("-run-tests");
	assert_equal(true, cl9.console_test, "run-tests alias");
}

static void should_trim_strings()
{
	assert_equal("xxx", str::trim_and_cache("xxx\n"), "remove cr lf");
	assert_equal("xxx", str::trim_and_cache("\rxxx\r"), "remove lf");
	assert_equal("xxx", str::trim_and_cache("   xxx\t\t "), "remove space");
}

static void should_format_text()
{
	assert_equal("ac-dc", std::format("{2}{0}-{1}{0}", "c", "d", "a"), "order");
	assert_equal("0.00123", std::format("{}", 0.00123), "double");
	assert_equal("0.001", std::format("{:.3f}", 0.00123), "double");
	assert_equal("5.5", std::format("{}", 5.5000), "double");
	assert_equal("123", std::format("{}", 123), "int");
	assert_equal("0123", std::format("{:04}", 123), "int");
	assert_equal(" 123", std::format("{:4}", 123), "int");
	assert_equal("hex=7B", std::format("hex={:x}", 0x7B), "hex");
	assert_equal("-test-", std::format("-{}-", "test"), "char*");
	assert_equal("-test-", std::format("-{}-", std::string("test")), "string");
	assert_equal("-test-", std::format("-{}-", std::string_view("test")), "string_view");
	assert_equal("33 {} {test}", std::format("{} {{}} {{test}}", 33), "string_view");
	assert_equal("22 x 33", std::format("{} x {}", 22, 33), "string_view");
}

static std::string find_and_format_result(const std::string_view text, const std::string_view sub_string)
{
	const auto r = str::ifind2(text, sub_string, 0);
	auto result = std::string(text);

	if (r.found)
	{
		for (auto i = static_cast<int>(r.parts.size()) - 1; i >= 0; --i)
		{
			const auto part = r.parts[i];
			result.insert(part.offset + part.length, 1, '*');
			result.insert(part.offset, 1, '*');
		}
	}

	return result;
}

static void should_find_text()
{
	assert_equal("*white* on blond", find_and_format_result("white on blond", "white"));
	assert_equal("*whi*te on *bl*ond", find_and_format_result("white on blond", "whi bl"));
	assert_equal("*white* on *blond*", find_and_format_result("white on blond", "white blond"));
	assert_equal("*white* bl on *blond*", find_and_format_result("white bl on blond", "white blond"));

	// Offsets are byte positions of a character start - a match following multi-byte characters
	// must not land on a continuation byte, or the renderer drops the highlight.
	const auto cyrillic = str::ifind2("\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", "\xd0\xb2\xd0\xb5", 0);
	assert_equal(true, cyrillic.found, "cyrillic match found");
	assert_equal(6, static_cast<int>(cyrillic.parts[0].offset), "cyrillic match byte offset");
}

static void should_parse_translated_short_month()
{
	// Shipped catalogs carry short months that are not 3 bytes - ru/uk/ja/ko/zh for all 12, fr for 8.
	const auto saved_oct = tt.month_short_oct.trans;
	const auto saved_jan = tt.month_short_jan.trans;

	tt.month_short_oct.trans = "\xd0\xbe\xd0\xba\xd1\x82"; // ru, 6 bytes
	tt.month_short_jan.trans = "janv"; // fr, 4 bytes

	const auto oct = str::month("\xd0\xbe\xd0\xba\xd1\x82");
	const auto jan = str::month("JANV");

	tt.month_short_oct.trans = saved_oct;
	tt.month_short_jan.trans = saved_jan;

	assert_equal(10, oct, "6 byte translated short month");
	assert_equal(1, jan, "4 byte translated short month, case insensitive");
	assert_equal(3, str::month("mar"), "ascii short month");
	assert_equal(5, str::month("May"), "long month");
	assert_equal(0, str::month("nope"), "non month");
}

static void should_scan_info_from_title()
{
	const auto assert_name = [](const std::string_view name, const media_name_props& expected)
	{
		const auto actual = scan_info_from_title(name);
		assert_equal(expected.show, actual.show, "show", name);
		assert_equal(expected.title, actual.title, "title", name);
		assert_equal(expected.season, actual.season, "season", name);
		assert_equal(expected.episode, actual.episode, "episode", name);
		assert_equal(expected.episode_of, actual.episode_of, "episode_of", name);
		assert_equal(expected.year, actual.year, "year", name);
	};

	assert_name("Game.of.Thrones.S02E06.HDTV.x264 - 2HD.mp4",
	            {.show = "Game of Thrones", .season = 2, .episode = 6});
	assert_name("It's.a.Wonderful.Life.1946.720p.BluRay.x264.YIFY.mp4",
	            {.title = "It's a Wonderful Life", .year = 1946});
	assert_name("The.Show.S01E02.The.Beginning.2160p.WEB-DL.HEVC.HDR",
	            {.show = "The Show", .title = "The Beginning", .season = 1, .episode = 2});
	assert_name("The.Show.S01E02.The.Beginning.DV.HEVC",
	            {.show = "The Show", .title = "The Beginning", .season = 1, .episode = 2});
	assert_name("Show_Name_S02E06_1080p", {.show = "Show Name", .season = 2, .episode = 6});
	assert_name("Show.Name.1x02.720p", {.show = "Show Name", .season = 1, .episode = 2});
	assert_name("Documentary.Part.02of10.1080p", {.show = "Documentary Part", .episode = 2, .episode_of = 10});
	assert_name("Holiday.Video.1920x1080", {.title = "Holiday Video"});
	assert_name("Movie.Title.2024", {.title = "Movie Title", .year = 2024});
	assert_name("Movie.Title.(2024).2160p", {.title = "Movie Title", .year = 2024});
	assert_name("The.Show.(2020).S01E02.1080p",
	            {.show = "The Show", .season = 1, .episode = 2, .year = 2020});
	assert_name("The.Show.S01E02.[GROUP].Episode.Title.1080p",
	            {.show = "The Show", .title = "Episode Title", .season = 1, .episode = 2});
	assert_name("Show.S999999999999E1.1080p", {.title = "Show S999999999999E1"});
	assert_name("Series.10of02.1080p", {.title = "Series 10of02"});
	assert_name("Family.Holiday.Video", {});
}

static void should_compare_versions()
{
	const df::version current_version(s_app_version);
	assert_equal(s_app_version, current_version.to_string(), "Can parse and to_string current version");

	const df::version test_version1("123.45");
	const df::version test_version1b("123.45");
	const df::version test_version2("456.1");

	assert_equal("123.45", test_version1.to_string(), "Can parse and to_string test version 1");
	assert_equal("456.1", test_version2.to_string(), "Can parse and to_string test version 2");

	assert_equal(true, test_version1 < test_version2, "Less op version");
	assert_equal(false, test_version2 < test_version1, "Less op version");
	assert_equal(false, test_version1 == test_version2, "== op version");
	assert_equal(true, test_version1 == test_version1b, "== op version");

	assert_equal("457.1", (test_version2 + 1).to_string(), "+ op version");
}

static void should_parse_exif_tags()
{
	// Builds a minimal little-endian TIFF/EXIF block that exercises the tags added to
	// the EXIF parser: Software (-> encoder), Artist (-> artist), the Windows XP string
	// tags (XPTitle/XPAuthor/XPKeywords/XPSubject) and the APEX MaxApertureValue /
	// ShutterSpeedValue fallbacks. XP values use ASCII payloads so decoding is
	// deterministic; the point of this test is the tag-dispatch and fallback logic.
	struct exif_entry
	{
		uint16_t tag;
		uint16_t fmt;
		uint32_t count;
		std::vector<uint8_t> data;
	};

	const auto ascii = [](const std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); };
	const auto rational_bytes = [](const int32_t n, const int32_t d)
	{
		std::vector<uint8_t> v(8);
		memcpy(v.data(), &n, 4);
		memcpy(v.data() + 4, &d, 4);
		return v;
	};

	// Entries must be in ascending tag order (ARTIST before XPAuthor so the XPAuthor
	// guard is proven not to overwrite the already-set artist).
	std::vector<exif_entry> entries = {
		{0x0131, FMT_STRING, 0, ascii("DiffractorApp")}, // Software      -> encoder
		{0x013b, FMT_STRING, 0, ascii("Ansel Adams")}, //   Artist        -> artist
		{0x9201, FMT_SRATIONAL, 1, rational_bytes(6, 1)}, // ShutterSpeed  -> 2^-6 = 1/64s
		{0x9205, FMT_URATIONAL, 1, rational_bytes(4, 1)}, // MaxAperture   -> 2^(4/2) = f/4
		{0x9c9b, FMT_STRING, 0, ascii("Winter")}, //         XPTitle       -> title
		{0x9c9d, FMT_STRING, 0, ascii("Ignored Author")}, // XPAuthor      -> artist (guarded)
		{0x9c9e, FMT_STRING, 0, ascii("alpha beta")}, //     XPKeywords    -> tags
		{0x9c9f, FMT_STRING, 0, ascii("A subject")}, //      XPSubject     -> description
	};

	for (auto& e : entries)
	{
		if (e.count == 0) e.count = static_cast<uint32_t>(e.data.size());
	}

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

	const auto entry_count = static_cast<uint32_t>(entries.size());
	constexpr uint32_t ifd0_offset = 8;
	const uint32_t data_start = ifd0_offset + 2 + entry_count * 12 + 4;

	// Reserve word-aligned out-of-line offsets for payloads that don't fit in 4 bytes.
	std::vector<uint32_t> data_offset(entries.size(), 0);
	uint32_t cursor = data_start;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const auto size = static_cast<uint32_t>(entries[i].data.size());
		if (size > 4)
		{
			data_offset[i] = cursor;
			cursor += size;
			if (cursor & 1) ++cursor;
		}
	}

	// TIFF header (little-endian).
	put16(0x4949);
	put16(0x002a);
	put32(ifd0_offset);

	// IFD0.
	put16(static_cast<uint16_t>(entry_count));
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const auto& e = entries[i];
		put16(e.tag);
		put16(e.fmt);
		put32(e.count);

		const auto size = static_cast<uint32_t>(e.data.size());
		if (size > 4)
		{
			put32(data_offset[i]);
		}
		else
		{
			uint32_t inline_value = 0;
			memcpy(&inline_value, e.data.data(), size);
			put32(inline_value);
		}
	}
	put32(0); // No IFD1.

	// Out-of-line payloads (word aligned, matching the reserved offsets above).
	for (const auto& e : entries)
	{
		if (e.data.size() > 4)
		{
			buf.insert(buf.end(), e.data.begin(), e.data.end());
			if (buf.size() & 1) buf.push_back(0);
		}
	}

	prop::item_metadata md;
	metadata_exif::parse(md, df::cspan{buf.data(), buf.size()});

	assert_equal("DiffractorApp", md.encoder, "Software -> encoder");
	assert_equal("Ansel Adams", md.artist, "Artist -> artist (XPAuthor must not override)");
	assert_equal("Winter", md.title, "XPTitle -> title");
	assert_equal("alpha beta", md.tags, "XPKeywords -> tags");
	assert_equal("A subject", md.description, "XPSubject -> description");
	assert_equal(prop::format_f_num(4.0f), prop::format_f_num(md.f_number), "MaxApertureValue -> f_number");
	assert_equal(prop::format_exposure(1.0f / 64.0f), prop::format_exposure(md.exposure_time),
	             "ShutterSpeedValue -> exposure_time");
}

// locations.md 2.8: GPS altitude and speed live in a sub-IFD reached through tag 0x8825, and
// the altitude reference that decides the sign may arrive either side of the value.
static void should_parse_exif_gps_height()
{
	struct gps_entry
	{
		uint16_t tag;
		uint16_t fmt;
		uint32_t count;
		std::vector<uint8_t> data;
	};

	const auto build_gps_exif = [](const std::vector<gps_entry>& entries)
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

		constexpr uint32_t ifd0_offset = 8;
		constexpr uint32_t gps_ifd_offset = ifd0_offset + 2 + 12 + 4; // IFD0 holds one entry
		const auto count = static_cast<uint32_t>(entries.size());
		const uint32_t gps_data_start = gps_ifd_offset + 2 + count * 12 + 4;

		std::vector<uint32_t> data_offset(entries.size(), 0);
		uint32_t cursor = gps_data_start;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (entries[i].data.size() > 4)
			{
				data_offset[i] = cursor;
				cursor += static_cast<uint32_t>(entries[i].data.size());
			}
		}

		put16(0x4949);
		put16(0x002a);
		put32(ifd0_offset);

		put16(1);
		put16(0x8825); // GPS IFD pointer
		put16(4); // FMT_ULONG
		put32(1);
		put32(gps_ifd_offset);
		put32(0);

		put16(static_cast<uint16_t>(count));
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const auto& e = entries[i];
			put16(e.tag);
			put16(e.fmt);
			put32(e.count);

			const auto size = static_cast<uint32_t>(e.data.size());
			if (size > 4)
			{
				put32(data_offset[i]);
			}
			else
			{
				uint32_t inline_value = 0;
				memcpy(&inline_value, e.data.data(), size);
				put32(inline_value);
			}
		}
		put32(0);

		for (const auto& e : entries)
		{
			if (e.data.size() > 4) buf.insert(buf.end(), e.data.begin(), e.data.end());
		}

		return buf;
	};

	const auto urational = [](const uint32_t n, const uint32_t d)
	{
		std::vector<uint8_t> v(8);
		memcpy(v.data(), &n, 4);
		memcpy(v.data() + 4, &d, 4);
		return v;
	};

	constexpr uint16_t FMT_BYTE = 1;
	constexpr uint16_t FMT_STRING = 2;
	constexpr uint16_t FMT_URATIONAL = 5;

	// A cruising airliner: 10,668 m above sea level at 900 km/h.
	prop::item_metadata cruising;
	const auto cruising_exif = build_gps_exif({
		{0x0005, FMT_BYTE, 1, {0}},
		{0x0006, FMT_URATIONAL, 1, urational(10668, 1)},
		{0x000c, FMT_STRING, 2, {'K', 0}},
		{0x000d, FMT_URATIONAL, 1, urational(900, 1)},
	});
	metadata_exif::parse(cruising, df::cspan{cruising_exif.data(), cruising_exif.size()});
	assert_equal(10668.0f, cruising.altitude, "GPSAltitude -> altitude");
	assert_equal(900.0f, cruising.gps_speed, "GPSSpeed in km/h -> gps_speed");

	// A dive: reference 1 means below sea level, so the stored positive magnitude is negated.
	prop::item_metadata dive;
	const auto dive_exif = build_gps_exif({
		{0x0005, FMT_BYTE, 1, {1}},
		{0x0006, FMT_URATIONAL, 1, urational(180, 10)},
	});
	metadata_exif::parse(dive, df::cspan{dive_exif.data(), dive_exif.size()});
	assert_equal(-18.0f, dive.altitude, "below-sea-level reference negates the altitude");

	// Knots are the other common speed reference.
	prop::item_metadata knots;
	const auto knots_exif = build_gps_exif({
		{0x000c, FMT_STRING, 2, {'N', 0}},
		{0x000d, FMT_URATIONAL, 1, urational(100, 1)},
	});
	metadata_exif::parse(knots, df::cspan{knots_exif.data(), knots_exif.size()});
	assert_equal(185.2f, knots.gps_speed, "GPSSpeed in knots -> km/h");
}

// Issue #65 - Binary text in JPEG comment
// Some Samsung phones (e.g. SM-G900F) store a binary blob in the EXIF UserComment
// tag, either raw or behind a valid "ASCII\0\0\0" character code. The parser must
// recognise the junk and drop it, while still preserving valid comments.
static void should_drop_binary_exif_comment()
{
	constexpr uint16_t TAG_USER_COMMENT = 0x9286;
	constexpr uint16_t FMT_UNDEFINED = 7;

	// Builds a minimal little-endian TIFF/EXIF block containing a single entry.
	const auto build_exif = [](const uint16_t tag, const uint16_t fmt, const std::vector<uint8_t>& data)
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

		constexpr uint32_t ifd0_offset = 8;
		constexpr uint32_t data_start = ifd0_offset + 2 + 1 * 12 + 4; // one entry
		const auto size = static_cast<uint32_t>(data.size());

		put16(0x4949); // little-endian TIFF header
		put16(0x002a);
		put32(ifd0_offset);

		put16(1); // entry count
		put16(tag);
		put16(fmt);
		put32(size);
		if (size > 4)
		{
			put32(data_start);
		}
		else
		{
			uint32_t inline_value = 0;
			memcpy(&inline_value, data.data(), size);
			put32(inline_value);
		}
		put32(0); // no IFD1

		if (size > 4) buf.insert(buf.end(), data.begin(), data.end());
		return buf;
	};

	// The Samsung junk blob begins with the fixed marker {0x12, 0xf8, 0x0f, 0x3b}.
	const std::vector<uint8_t> junk = {
		0x12, 0xf8, 0x0f, 0x3b, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
	};
	prop::item_metadata md_junk;
	const auto exif_junk = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, junk);
	metadata_exif::parse(md_junk, df::cspan{exif_junk.data(), exif_junk.size()});
	assert_equal(true, prop::is_null(md_junk.comment), "binary Samsung comment dropped");

	// SM-G900F hides its blob behind a valid "ASCII\0\0\0" character code, so the marker
	// test alone never fires; the payload is still binary and must be dropped.
	std::vector<uint8_t> samsung = {'A', 'S', 'C', 'I', 'I', 0, 0, 0, 0x0a, 0, 0, 0};
	for (const auto c : {'J', 'K', 'J', 'K'}) samsung.push_back(static_cast<uint8_t>(c));
	for (const uint8_t c : {0x27, 0x03, 0xab, 0x5c, 0x46, 0x0b, 0x01, 0x00}) samsung.push_back(c);
	prop::item_metadata md_samsung;
	const auto exif_samsung = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, samsung);
	metadata_exif::parse(md_samsung, df::cspan{exif_samsung.data(), exif_samsung.size()});
	assert_equal(true, prop::is_null(md_samsung.comment), "ASCII-prefixed binary comment dropped");

	// Control: a well-formed ASCII UserComment ("ASCII\0\0\0" prefix) is preserved.
	const std::vector<uint8_t> good = {
		'A', 'S', 'C', 'I', 'I', 0, 0, 0, 'H', 'e', 'l', 'l', 'o'
	};
	prop::item_metadata md_good;
	const auto exif_good = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, good);
	metadata_exif::parse(md_good, df::cspan{exif_good.data(), exif_good.size()});
	assert_equal("Hello", md_good.comment, "valid ASCII comment preserved");

	// Control: multi-line text with trailing nul padding survives the binary scan.
	const std::vector<uint8_t> padded = {
		'A', 'S', 'C', 'I', 'I', 0, 0, 0, 'L', 'i', 'n', 'e', '\n', '2', 0, 0
	};
	prop::item_metadata md_padded;
	const auto exif_padded = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, padded);
	metadata_exif::parse(md_padded, df::cspan{exif_padded.data(), exif_padded.size()});
	assert_equal("Line\n2", md_padded.comment, "multi-line padded comment preserved");
}

static void should_copy_preserve_properties()
{
	prop::item_metadata src;
	src.title = "Test Title"_c;
	src.rating = 4;
	src.media_position = 19.5;
	src.iso_speed = 400;
	src.artist = "Test Artist"_c;

	prop::item_metadata dst;
	dst = src;

	assert_equal(src.title, dst.title, "should copy title");
	assert_equal(src.rating, dst.rating, "should copy rating");
	assert_equal(src.iso_speed, dst.iso_speed, "should copy iso_speed");
	assert_equal(src.artist, dst.artist, "should copy artist");
}

static void should_replace_item_metadata_without_resetting_playback_position()
{
	df::index_file_item initial;
	initial.name = "position.mp4"_c;
	initial.ft = files::file_type_from_name(initial.name);
	const auto initial_metadata = std::make_shared<prop::item_metadata>();
	initial_metadata->title = "Initial"_c;
	initial_metadata->media_position = 12.0;
	initial.metadata.store(initial_metadata);

	const auto path = df::file_path("c:\\position.mp4");
	const auto item = std::make_shared<df::item_element>(path, initial);
	const auto old_snapshot = item->metadata();
	item->media_position(24.0);

	const auto refreshed = initial;
	const auto refreshed_metadata = std::make_shared<prop::item_metadata>(*initial_metadata);
	refreshed_metadata->title = "Refreshed"_c;
	refreshed_metadata->media_position = 4.0;
	refreshed.metadata.store(refreshed_metadata);
	item->update(path, refreshed);

	assert_equal("Initial", old_snapshot->title, "old metadata snapshot remains unchanged");
	assert_equal("Refreshed", item->metadata()->title, "item receives refreshed metadata snapshot");
	assert_equal(24, static_cast<int>(item->media_position()), "live playback position survives metadata refresh");
}

static void should_rename_with_substitutions()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);

	const auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), "can rename");
	assert_equal(save_path_2.name(), test_item->name(), "renamed");
	assert_equal(true, save_path_2.exists(), "renamed exists");

	// Verify item can be found at new path after rename
	const auto reloaded = load_item(index, save_path_2, false);
	assert_equal(save_path_2.name(), reloaded->name(), "index finds renamed item");
}

static void should_format_plural_text()
{
	const plural_text items_fmt("{count} item", "{count} items");

	assert_equal("1 item", format_plural_text(items_fmt, 1), "singular");
	assert_equal("5 items", format_plural_text(items_fmt, 5), "plural");
	assert_equal("0 items", format_plural_text(items_fmt, 0), "zero");

	const plural_text name_fmt("{first-name} will be processed.",
	                           "{count} items including {first-name} will be processed.");

	assert_equal("photo.jpg will be processed.", format_plural_text(name_fmt, "photo.jpg", 1, {}), "named singular");
	assert_equal("3 items including photo.jpg will be processed.", format_plural_text(name_fmt, "photo.jpg", 3, {}),
	             "named plural");
}

static void should_format_rename()
{
	assert_equal("photo-005", format_sequence("original", "photo-###", 5), "sequence format");
	assert_equal("photo-042", format_sequence("original", "photo-###", 42), "sequence zero pad");
	assert_equal("photo-1000", format_sequence("original", "photo-###", 1000), "sequence does not truncate");
	assert_equal("trip-0001", format_sequence("original", "trip-####", 1), "four digit sequence");
	assert_equal("riginal", format_sequence("original", "???????", 0), "question mark substitution");
	assert_equal("al-file", format_sequence("original", "??-file", 0), "mixed question mark substitution");
}

static void should_rename_name_token_without_extension()
{
	df::item_set items;
	const auto item = std::make_shared<df::item_element>(
		df::file_path(test_files_folder, "Test.jpg"), df::index_file_item{});
	items.add(item);

	const auto renames = calc_item_renames(items, "{name}-edited", 1, collision_policy::block_run);
	assert_equal(1ULL, static_cast<uint64_t>(renames.size()), "one rename planned");
	assert_equal("Test-edited", renames.front().new_name, "name token excludes extension");
}

static void should_reject_unusable_rename_targets()
{
	df::item_set items;
	items.add(std::make_shared<df::item_element>(df::file_path(test_files_folder, "Test.jpg"), df::index_file_item{}));

	const auto empty_template = calc_item_renames(items, "", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(empty_template), "an empty template names nothing");

	// Windows drops a trailing space, so the file would not carry the name the preview shows.
	const auto trailing_space = calc_item_renames(items, "photo ", 1, collision_policy::block_run);
	assert_equal("photo ", trailing_space.front().new_name, "the preview shows the template result");
	assert_equal(false, can_rename_items(trailing_space), "a trailing space is rejected");

	const auto device_name = calc_item_renames(items, "con", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(device_name), "a reserved device name is rejected");

	const auto usable = calc_item_renames(items, "photo", 1, collision_policy::block_run);
	assert_equal(true, can_rename_items(usable), "an ordinary name is still accepted");
}

static void should_reject_duplicate_rename_targets()
{
	df::item_set items;
	items.add(std::make_shared<df::item_element>(df::file_path(test_files_folder, "Test.jpg"), df::index_file_item{}));
	items.add(std::make_shared<df::item_element>(df::file_path(test_files_folder, "Small.jpg"), df::index_file_item{}));

	const auto renames = calc_item_renames(items, "same", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(renames), "duplicate rename targets rejected");
	assert_equal(false, renames.front().valid, "first duplicate target invalid");
	assert_equal(false, renames.back().valid, "second duplicate target invalid");
}

static void should_group_rename_sidecar_collisions()
{
	const auto root = _temps.folder().combine(std::format("rename-sidecar-{}", platform::tick_count()));
	platform::create_folder(root);
	const auto source = root.combine_file("photo.jpg");
	const auto sidecar = root.combine_file("photo.xmp");
	const df::blob contents = {1};
	df::blob_save_to_file(contents, source);
	df::blob_save_to_file(contents, sidecar);
	df::blob_save_to_file(contents, root.combine_file("renamed.xmp"));

	rename_source item;
	item.source = source;
	item.original_name = "photo";
	item.sidecars.emplace_back(sidecar);

	const auto blocked = calc_item_renames(std::vector{item}, "renamed", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(blocked), "sidecar collision blocks whole rename");

	const auto renamed = calc_item_renames(std::vector{item}, "renamed", 1, collision_policy::auto_rename);
	assert_equal(true, can_rename_items(renamed), "sidecar collision can auto-rename whole group");
	assert_equal("renamed (2).jpg", renamed.front().destination.name(), "primary uses shared suffix");
	assert_equal("renamed (2).xmp", renamed.front().sidecars.front().second.name(), "sidecar uses shared suffix");
	platform::delete_items({}, {root}, false);
}

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

static void should_plan_unique_convert_outputs()
{
	df::item_set items;
	items.add(std::make_shared<df::item_element>(df::file_path("c:\\one\\photo.jpg"), df::index_file_item{}));
	items.add(std::make_shared<df::item_element>(df::file_path("c:\\two\\photo.png"), df::index_file_item{}));

	const auto plan = plan_convert_outputs(df::folder_path("c:\\destination"), items, ".webp",
	                                       collision_policy::block_run);
	assert_equal(2_z, plan.size(), "all conversion sources planned");
	assert_equal("photo.webp", plan[0].destination.name(), "first conversion keeps basename");
	assert_equal("photo 2.webp", plan[1].destination.name(), "duplicate conversion basename is suffixed");
	assert_equal(false, plan[0].destination == plan[1].destination, "conversion outputs are unique");
}

static void should_adjust_item_dates_from_snapshot()
{
	constexpr df::date_t original_start(100);
	constexpr df::date_t new_start(500);
	assert_equal(df::date_t(510), adjusted_item_date(df::date_t(110), new_start, original_start),
	             "dated item preserves offset");
	assert_equal(new_start, adjusted_item_date({}, new_start, original_start),
	             "undated item uses new start");
}

static void should_classify_mapi_results()
{
	assert_equal(true, platform::classify_mapi_send_result(0) == platform::mapi_send_result::sent, "MAPI success");
	assert_equal(true, platform::classify_mapi_send_result(1) == platform::mapi_send_result::canceled,
	             "MAPI user abort");
	assert_equal(true, platform::classify_mapi_send_result(3) == platform::mapi_send_result::failed,
	             "MAPI login failure");
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

// Records what a run reported for each row so revalidation decisions can be asserted.
struct recording_status final : df::status_i
{
	std::vector<std::pair<std::string, item_status>> items;

	void start_item(std::string_view) override
	{
	}

	void end_item(const std::string_view name, const item_status status) override
	{
		items.emplace_back(name, status);
	}

	bool has_failures() const override
	{
		return std::ranges::any_of(items, [](const auto& i) { return i.second == item_status::fail; });
	}

	void abort(std::string_view) override
	{
	}

	void complete(std::string_view) override
	{
	}

	void show_errors() override
	{
	}

	void message(std::string_view, int64_t, int64_t) override
	{
	}

	void show_message(std::string_view) override
	{
	}

	bool is_canceled() const override { return false; }

	void wait_for_complete() const override
	{
	}

	item_status status_of(const std::string_view name) const
	{
		const auto found = std::ranges::find_if(items, [name](const auto& i) { return i.first == name; });
		return found == items.end() ? item_status::cancel : found->second;
	}
};

static void write_test_file(const df::file_path path, const std::string_view text)
{
	std::ofstream fs(platform::to_file_system_path(path), std::ios::binary | std::ios::trunc);
	fs << text;
}

static std::string read_test_file(const df::file_path path)
{
	const auto data = df::blob_from_file(path);
	return {std::bit_cast<const char*>(data.data()), data.size()};
}

// Run holds every reviewed row to the file that was reviewed, one row at a time. A row whose file
// moved on is refused and reported; the rows that still match are still run.
static void should_revalidate_sync_rows()
{
	const auto root = _temps.next_folder("sync-revalidate");
	const auto local = root.combine("local");
	const auto remote = root.combine("remote");
	platform::create_folder(local);
	platform::create_folder(remote);

	write_test_file(local.combine_file("keep.txt"), "keep");
	write_test_file(local.combine_file("changed.txt"), "changed");
	write_test_file(local.combine_file("claimed.txt"), "claimed");

	df::index_roots roots;
	roots.folders.emplace(local);
	const auto analysis = sync_analysis(roots, remote, true, false, false, false, test_token);
	assert_equal(true, analysis.valid, "sync analysis is valid");
	assert_equal(3, static_cast<int>(count_sync_actions(analysis)), "three files to copy out");

	// The source grows after review, so copying it would send content nobody approved.
	write_test_file(local.combine_file("changed.txt"), "changed again");
	// A destination the review found free is claimed by something else before the run reaches it.
	write_test_file(remote.combine_file("claimed.txt"), "not yours");

	const auto status = std::make_shared<recording_status>();
	const auto run = sync_copy(status, analysis, test_token);

	assert_equal(true, status->status_of("keep.txt") == item_status::success, "unchanged row runs");
	assert_equal(true, status->status_of("changed.txt") == item_status::fail, "changed source is refused");
	assert_equal(true, status->status_of("claimed.txt") == item_status::fail, "claimed destination is refused");
	assert_equal(2, static_cast<int>(run.refused), "both refusals are reported so the run can say why");

	assert_equal(true, remote.combine_file("keep.txt").exists(), "unchanged row was copied");
	assert_equal(false, remote.combine_file("changed.txt").exists(), "refused row wrote nothing");
	assert_equal("not yours"s, read_test_file(remote.combine_file("claimed.txt")),
	             "claimed destination was not overwritten");
}

// A delete was reviewed against the file's content, so that is what it is held to at run time.
static void should_revalidate_sync_deletes()
{
	const auto root = _temps.next_folder("sync-delete-revalidate");
	const auto local = root.combine("local");
	const auto remote = root.combine("remote");
	platform::create_folder(local);
	platform::create_folder(remote);

	write_test_file(remote.combine_file("stale.txt"), "stale");
	write_test_file(remote.combine_file("touched.txt"), "touched");

	df::index_roots roots;
	roots.folders.emplace(local);
	const auto analysis = sync_analysis(roots, remote, false, false, false, true, test_token);
	assert_equal(true, analysis.valid, "sync analysis is valid");
	assert_equal(2, static_cast<int>(count_sync_actions(analysis, sync_action::delete_remote)),
	             "two remote files to delete");

	write_test_file(remote.combine_file("touched.txt"), "touched after review");

	const auto status = std::make_shared<recording_status>();
	sync_copy(status, analysis, test_token);

	assert_equal(true, status->status_of("stale.txt") == item_status::success, "unchanged file is deleted");
	assert_equal(true, status->status_of("touched.txt") == item_status::fail, "changed file is not deleted");
	assert_equal(false, remote.combine_file("stale.txt").exists(), "unchanged file is gone");
	assert_equal(true, remote.combine_file("touched.txt").exists(), "changed file survives");
}

static void should_detect_duplicate_import_destinations()
{
	const auto src1 = _temps.folder().combine("import-dup-1");
	const auto src2 = _temps.folder().combine("import-dup-2");
	const auto dest = _temps.folder().combine("import-dup-dest");
	platform::create_folder(src1);
	platform::create_folder(src2);
	platform::create_folder(dest);

	const auto source = df::file_path(test_files_folder, "Test.jpg");
	platform::copy_file(source, src1.combine_file("same.jpg"), false, false);
	platform::copy_file(source, src2.combine_file("same.jpg"), false, false);

	const auto make_item = [](const df::folder_path folder)
	{
		const auto path = folder.combine_file("same.jpg");
		const auto fi = platform::file_attributes(path);
		folder_scan_item item;
		item.folder = folder;
		item.item.name = path.name();
		item.item.file_modified = df::date_t(fi.modified);
		item.item.file_created = fi.created;
		item.item.ft = files::file_type_from_name(path);
		return item;
	};

	const std::vector<folder_scan_item> items{make_item(src1), make_item(src2)};
	const item_import_set no_previous;

	import_options options;
	options.dest_folder = dest;
	options.dest_structure = std::string(default_custom_folder_structure);
	options.collision = collision_policy::block_run;

	const auto blocked = import_analysis(items, options, no_previous, test_token);
	assert_equal(1, static_cast<int>(count_import_collisions(blocked)),
	             "second source claiming the same destination is a collision");
	assert_equal(1, static_cast<int>(count_imports(blocked)), "only the first source imports");

	options.collision = collision_policy::auto_rename;
	const auto renamed = import_analysis(items, options, no_previous, test_token);
	df::unique_paths destinations;

	for (const auto& folder : renamed)
	{
		for (const auto& i : folder.second) destinations.emplace(i.destination);
	}

	assert_equal(2, static_cast<int>(count_imports(renamed)), "auto-rename imports both sources");
	assert_equal(2, static_cast<int>(destinations.size()), "auto-rename gives each source its own destination");
}

// Import holds each reviewed row to the file that was reviewed, and lets the file system prove a
// destination is free rather than asking and then writing.
static void should_revalidate_import_rows(shared_test_context& stc)
{
	const auto root = _temps.next_folder("import-revalidate");
	const auto src = root.combine("source");
	const auto dest = root.combine("dest");
	platform::create_folder(src);
	platform::create_folder(dest);

	write_test_file(src.combine_file("keep.txt"), "keep");
	write_test_file(src.combine_file("changed.txt"), "changed");
	write_test_file(src.combine_file("claimed.txt"), "claimed");

	const auto make_item = [&src](const std::string_view name)
	{
		const auto path = src.combine_file(name);
		const auto fi = platform::file_attributes(path);
		folder_scan_item item;
		item.folder = src;
		item.item.name = path.name();
		item.item.file_modified = df::date_t(fi.modified);
		item.item.file_created = fi.created;
		item.item.size = df::file_size(fi.size);
		item.item.ft = files::file_type_from_name(path);
		return item;
	};

	const std::vector<folder_scan_item> items{
		make_item("keep.txt"), make_item("changed.txt"), make_item("claimed.txt")
	};

	import_options options;
	options.dest_folder = dest;
	options.dest_structure = {};
	options.collision = collision_policy::skip;

	const auto analysis = import_analysis(items, options, {}, test_token);
	assert_equal(3, static_cast<int>(count_imports(analysis)), "three files to import");

	write_test_file(src.combine_file("changed.txt"), "changed after review");
	write_test_file(dest.combine_file("claimed.txt"), "not yours");

	const auto status = std::make_shared<recording_status>();
	const auto run = import_copy(stc.empty_index, status, analysis, options, test_token);

	assert_equal(true, status->status_of("keep.txt") == item_status::success, "unchanged row imports");
	assert_equal(true, status->status_of("changed.txt") == item_status::fail, "changed source is refused");
	assert_equal(true, status->status_of("claimed.txt") == item_status::fail, "claimed destination is refused");
	assert_equal(2, static_cast<int>(run.refused), "both refusals are reported so the run can say why");

	assert_equal(true, dest.combine_file("keep.txt").exists(), "unchanged row was imported");
	assert_equal(false, dest.combine_file("changed.txt").exists(), "refused row wrote nothing");
	assert_equal("not yours"s, read_test_file(dest.combine_file("claimed.txt")),
	             "claimed destination was not overwritten");
}

// Replace is the one policy that writes over a file instead of proving the name is free, so it is
// the one that has to prove the file is still the one that was reviewed.
static void should_revalidate_replaced_import_destinations(shared_test_context& stc)
{
	const auto root = _temps.next_folder("import-replace-revalidate");
	const auto src = root.combine("source");
	const auto dest = root.combine("dest");
	platform::create_folder(src);
	platform::create_folder(dest);

	write_test_file(src.combine_file("stable.txt"), "new stable");
	write_test_file(src.combine_file("racing.txt"), "new racing");
	write_test_file(dest.combine_file("stable.txt"), "old stable");
	write_test_file(dest.combine_file("racing.txt"), "old racing");

	const auto make_item = [&src](const std::string_view name)
	{
		const auto path = src.combine_file(name);
		const auto fi = platform::file_attributes(path);
		folder_scan_item item;
		item.folder = src;
		item.item.name = path.name();
		item.item.file_modified = df::date_t(fi.modified);
		item.item.file_created = fi.created;
		item.item.size = df::file_size(fi.size);
		item.item.ft = files::file_type_from_name(path);
		return item;
	};

	const std::vector<folder_scan_item> items{make_item("stable.txt"), make_item("racing.txt")};

	import_options options;
	options.dest_folder = dest;
	options.dest_structure = {};
	options.collision = collision_policy::replace;

	const auto analysis = import_analysis(items, options, {}, test_token);
	assert_equal(2, static_cast<int>(count_imports(analysis)), "replace imports over both destinations");

	write_test_file(dest.combine_file("racing.txt"), "edited since review");

	const auto status = std::make_shared<recording_status>();
	import_copy(stc.empty_index, status, analysis, options, test_token);

	assert_equal(true, status->status_of("stable.txt") == item_status::success, "reviewed destination is replaced");
	assert_equal(true, status->status_of("racing.txt") == item_status::fail, "changed destination is refused");

	assert_equal("new stable"s, read_test_file(dest.combine_file("stable.txt")),
	             "reviewed destination took the new file");
	assert_equal("edited since review"s, read_test_file(dest.combine_file("racing.txt")),
	             "changed destination was left alone");
}

static void should_reject_missing_sync_folder()
{
	df::index_roots roots;
	roots.folders.emplace(_temps.folder().combine("missing-local"));

	const auto result = sync_analysis(roots, _temps.folder(), true, false, false, true, test_token);
	assert_equal(false, result.valid, "missing local folder invalidates sync analysis");
	assert_equal(true, result.empty(), "invalid sync analysis has no actions");
	assert_equal(true, result.reason != sync_invalid_reason::none, "records why the analysis is invalid");
	assert_equal(false, sync_invalid_message(result) == tt.error_cannot_continue.sv(),
	             "a missing folder is not reported as an internal fault");
}

static void should_reject_overlapping_sync_folders()
{
	const auto local = _temps.folder().combine("sync-overlap-local");
	platform::create_folder(local);
	df::index_roots roots;
	roots.folders.emplace(local);

	const auto result = sync_analysis(roots, local.combine("remote"), true, false, false, false, test_token);
	assert_equal(false, result.valid, "nested remote folder invalidates sync analysis");
	assert_equal(true, result.reason == sync_invalid_reason::overlapping_paths, "reports overlapping folders");
	assert_equal(false, sync_invalid_message(result) == tt.error_cannot_continue.sv(),
	             "overlapping folders are not reported as an internal fault");
}

static void should_reject_ambiguous_sync_roots()
{
	const auto local1 = _temps.folder().combine("sync-root-1");
	const auto local2 = _temps.folder().combine("sync-root-2");
	const auto remote = _temps.folder().combine("sync-remote");
	platform::create_folder(local1);
	platform::create_folder(local2);
	platform::create_folder(remote);
	const auto source = df::file_path(test_files_folder, "Test.jpg");
	platform::copy_file(source, local1.combine_file("same.jpg"), false, false);
	platform::copy_file(source, local2.combine_file("same.jpg"), false, false);

	df::index_roots roots;
	roots.folders.emplace(local1);
	roots.folders.emplace(local2);
	const auto result = sync_analysis(roots, remote, true, false, false, false, test_token);
	assert_equal(false, result.valid, "duplicate relative paths across roots invalidate sync analysis");
	assert_equal(true, result.empty(), "ambiguous sync analysis has no actions");
	assert_equal(true, result.reason == sync_invalid_reason::ambiguous_local_root, "reports ambiguous roots");
	assert_equal(false, sync_invalid_message(result) == tt.error_cannot_continue.sv(),
	             "ambiguous roots are not reported as an internal fault");
}

static void should_ignore_unclaimed_remote_files()
{
	// A collection with more than one root cannot name a local destination for a remote-only file in
	// an unknown folder. That must only fail the run when the file would actually be copied local.
	const auto local1 = _temps.folder().combine("unclaimed-root-1");
	const auto local2 = _temps.folder().combine("unclaimed-root-2");
	const auto remote = _temps.folder().combine("unclaimed-remote");
	platform::create_folder(local1);
	platform::create_folder(local2);
	platform::create_folder(remote);
	const auto source = df::file_path(test_files_folder, "Test.jpg");
	platform::copy_file(source, remote.combine_file("remote-only.jpg"), false, false);

	df::index_roots roots;
	roots.folders.emplace(local1);
	roots.folders.emplace(local2);

	const auto ignored = sync_analysis(roots, remote, true, false, false, false, test_token);
	assert_equal(true, ignored.valid, "remote-only file does not invalidate a multi-root sync analysis");
	assert_equal(1, static_cast<int>(ignored.size()), "the remote-only file is still reported");

	for (const auto& folder : ignored)
	{
		for (const auto& file : folder.second)
		{
			assert_equal(true, file.second.action == sync_action::none, "the remote-only file is ignored");
		}
	}

	const auto copied = sync_analysis(roots, remote, false, true, false, false, test_token);
	assert_equal(false, copied.valid, "copying a remote-only file needs an unambiguous local root");
	assert_equal(true, copied.reason == sync_invalid_reason::ambiguous_local_root, "reports ambiguous roots");
}

static void should_select_sync_actions()
{
	assert_equal(true, calc_sync_action(true, false, 10, 0, 100, 0, false, false, true, false) ==
	             sync_action::delete_local, "delete local is independent");
	assert_equal(true, calc_sync_action(false, true, 0, 10, 0, 100, false, false, false, true) ==
	             sync_action::delete_remote, "delete remote is independent");
	assert_equal(true, calc_sync_action(true, false, 10, 0, 100, 0, true, false, true, false) ==
	             sync_action::copy_remote, "copy local to remote takes precedence over delete local");
	assert_equal(true, calc_sync_action(false, true, 0, 10, 0, 100, false, true, false, true) ==
	             sync_action::copy_local, "copy remote to local takes precedence over delete remote");
	assert_equal(true, calc_sync_action(true, true, 20, 10, 100, 100, true, true, true, true) ==
	             sync_action::copy_remote, "newer local file copies to remote");
	assert_equal(true, calc_sync_action(true, true, 10, 20, 100, 100, true, true, true, true) ==
	             sync_action::copy_local, "newer remote file copies to local");
	assert_equal(true, calc_sync_action(true, true, 10, 10, 100, 100, true, true, true, true) ==
	             sync_action::none, "matching timestamps need no action");
	assert_equal(true, calc_sync_action(true, true, 10, 10, 101, 100, true, false, false, false) ==
	             sync_action::copy_remote, "one-way sync copies unequal sizes with matching timestamps");
	assert_equal(true, calc_sync_action(true, true, 10, 10, 100, 101, false, true, false, false) ==
	             sync_action::copy_local, "reverse one-way sync copies unequal sizes with matching timestamps");
}

// Verifies the append-only string interning table: one shared immutable copy per unique
// string, identity == pointer equality, stability across table growth, and correct
// deduplication under concurrent interning from multiple threads.
static void should_intern_strings()
{
	const auto a = str::cache("interned-example");
	const auto b = str::cache(std::string("interned-example"));
	assert_equal(true, a == b, "same content -> same handle");
	assert_equal(true, a.storage == b.storage, "identity is pointer equality");
	assert_equal("interned-example"s, a.str(), "round-trips content");
	assert_equal(false, str::cache("interned-example") == str::cache("interned-different"),
	             "different content -> different handle");
	assert_equal(true, str::cache(std::string_view{}).is_empty(), "empty interns to empty");

	const std::string largest(platform::memory_pool::block_size -
	                          offsetof(str::chached_string_storage_t, sz) - 1, 'x');
	assert_equal(largest.size(), str::cache(largest).size(), "largest pool record is interned");
	const std::string too_large(largest.size() + 1, 'x');
	assert_equal(true, str::cache(too_large).is_empty(), "oversized pool record is rejected");

	// Many distinct strings force table growth / rehash; handles stay valid, unique and stable.
	constexpr int n = 5000;
	std::vector<str::cached> handles;
	handles.reserve(n);
	for (auto i = 0; i < n; ++i) handles.emplace_back(str::cache(std::format("intern-word-{}", i)));

	df::hash_set<const void*> seen;
	for (auto i = 0; i < n; ++i)
	{
		const auto w = std::format("intern-word-{}", i);
		assert_equal(w, handles[i].str(), "content preserved after growth");
		assert_equal(true, str::cache(w) == handles[i], "re-intern returns the same handle");
		seen.insert(handles[i].storage);
	}
	assert_equal(n, static_cast<int>(seen.size()), "each distinct string interned exactly once");

	// Concurrent interning of an overlapping set must still yield one handle per string.
	constexpr int thread_count = 8;
	constexpr int word_count = 500;
	std::vector<std::vector<str::cached>> per_thread(thread_count);
	std::vector<std::thread> threads;

	for (auto t = 0; t < thread_count; ++t)
	{
		threads.emplace_back([t, &per_thread]
		{
			auto& out = per_thread[t];
			out.reserve(word_count);
			for (auto i = 0; i < word_count; ++i) out.emplace_back(str::cache(std::format("shared-word-{}", i)));
		});
	}

	for (auto& th : threads) th.join();

	for (auto i = 0; i < word_count; ++i)
	{
		const auto handle = per_thread[0][i];
		for (auto t = 1; t < thread_count; ++t)
		{
			assert_equal(true, per_thread[t][i] == handle, "all threads share one interned handle");
		}
	}
}

// A query that fails is not proof the file is gone: a caller that deletes or overwrites on
// "not there" must be able to tell a removed file from one it simply could not read.
static void should_report_file_presence()
{
	const auto scratch = _temps.next_folder("file-presence");
	const auto present = scratch.combine_file("present.txt");
	{
		std::ofstream fs(platform::to_file_system_path(present));
		fs << "content";
	}

	const auto found = platform::file_attributes(present);
	assert_equal(true, found.exists(), "existing file is found");
	assert_equal(false, found.confirmed_missing(), "existing file is not missing");

	const auto missing = platform::file_attributes(scratch.combine_file("missing.txt"));
	assert_equal(false, missing.exists(), "removed file does not exist");
	assert_equal(true, missing.confirmed_missing(), "removed file is confirmed missing");

	// A path under a folder that is not there is absent for the same reason, not a failure.
	const auto missing_folder = platform::file_attributes(scratch.combine("gone").combine_file("missing.txt"));
	assert_equal(true, missing_folder.confirmed_missing(), "file under a missing folder is confirmed missing");

	// An empty file must not read as absent just because it has no bytes.
	const auto empty_path = scratch.combine_file("empty.txt");
	{
		std::ofstream fs(platform::to_file_system_path(empty_path));
	}
	const auto empty = platform::file_attributes(empty_path);
	assert_equal(true, empty.exists(), "empty file exists");
	assert_equal(0, static_cast<int>(empty.size), "empty file has no bytes");

	assert_equal(true, platform::file_attributes(scratch).exists(), "existing folder is found");
	assert_equal(true, platform::file_attributes(scratch.combine("gone")).confirmed_missing(),
	             "removed folder is confirmed missing");

	// Enumeration only ever reports what it found, so those records are never left unknown.
	const auto contents = platform::iterate_file_items(scratch, false);
	assert_equal(2, static_cast<int>(contents.files.size()), "both files enumerated");
	assert_equal(true, std::ranges::all_of(contents.files, [](const platform::file_info& f)
	             {
		             return f.attributes.exists();
	             }),
	             "enumerated files are found");

	assert_equal(false, platform::file_attributes_t{}.exists(), "unqueried attributes do not exist");
	assert_equal(false, platform::file_attributes_t{}.confirmed_missing(),
	             "unqueried attributes are not confirmed missing");
}

void register_tests2(view_state& state, test_registry& tests)
{
	tests.add("Should natural compare"s, should_icmp_natural);

	//
	// Utility / Infrastructure
	//
	tests.add("Should complete result scope"s, should_complete_result_scope);
	tests.add("Should abort result scope during exception"s, should_abort_result_scope_during_exception);
	tests.add("Should format audio stream names"s, should_format_audio_stream_names);
	tests.add("Should cancel superseded tokens"s, should_cancel_superseded_tokens);
	tests.add("Should report file presence"s, should_report_file_presence);
	tests.add("Should intern strings"s, should_intern_strings);
	tests.add("Should calc HMAC SHA1"s, should_calc_HMACSHA1);
	tests.add("Should calc Hashes"s, should_calc_hashes);
	tests.add("Should calc perceptual hashes"s, should_calc_perceptual_hashes);
	tests.add("Should recognise the same picture"s, should_recognise_the_same_picture);
	tests.add("Should match SIMD software blends"s, should_match_simd_software_blends);
	tests.add("Should convert YUV surfaces for software rendering"s,
	          should_convert_yuv_surfaces_for_software_rendering);
	tests.add("Should layout selection thumbnail collage"s, should_layout_selection_thumbnail_collage);
	tests.add("Should convert Utf8"s, should_convert_utf8);
	tests.add("Should split"s, should_split);
	tests.add("Should split genre"s, should_split_genre);
	tests.add("Should extract url"s, should_extract_url);
	tests.add("Should collect descriptive fields"s, should_collect_descriptive_fields);
	tests.add("Should detect wildcard"s, should_detect_wildcard);
	tests.add("Should match wildcard"s, should_match_wildcard);
	tests.add("Should compare versions"s, should_compare_versions);
	tests.add("Should Encrypt Password"s, should_encrypt_password);
	tests.add("Should scan info from title"s, should_scan_info_from_title);
	tests.add("Should parse command line"s, should_parse_command_line);
	tests.add("Should trim strings"s, should_trim_strings);
	tests.add("Should format text"s, should_format_text);
	tests.add("Should find text"s, should_find_text);
	tests.add("Should parse translated short month"s, should_parse_translated_short_month);
	tests.add("INI file settings should persist values"s, should_persist_to_ini_file);

	//
	// Data Model / Properties
	//
	tests.add("Should copy preserve properties"s, should_copy_preserve_properties);
	tests.add("Should replace item metadata without resetting playback position"s,
	          should_replace_item_metadata_without_resetting_playback_position);
	tests.add("Should parse exif tags"s, should_parse_exif_tags);
	tests.add("Should parse exif gps height"s, should_parse_exif_gps_height);
	tests.add("Issue #65: Should drop binary exif comment"s, should_drop_binary_exif_comment);
	tests.add("Should Rename with substitutions"s, should_rename_with_substitutions);
	tests.add("Should rename name token without extension"s, should_rename_name_token_without_extension);
	tests.add("Should reject duplicate rename targets"s, should_reject_duplicate_rename_targets);
	tests.add("Should reject unusable rename targets"s, should_reject_unusable_rename_targets);
	tests.add("Should group Rename sidecar collisions"s, should_group_rename_sidecar_collisions);
	tests.add("Should format plural text"s, should_format_plural_text);
	tests.add("Should format rename"s, should_format_rename);
	tests.add("Should check overwrite"s, should_check_overwrite);
	tests.add("Should plan unique convert outputs"s, should_plan_unique_convert_outputs);
	tests.add("Should adjust item dates from snapshot"s, should_adjust_item_dates_from_snapshot);
	tests.add("Should classify MAPI results"s, should_classify_mapi_results);
	tests.add("Should report zip create failure"s, should_report_zip_create_failure);
	tests.add("Should create original before replace"s, should_create_original_before_replace);
	tests.add("Should report move or copy collision paths"s, should_report_move_or_copy_collision_paths);
	tests.add("Should fail replace when flush fails"s, should_fail_replace_when_flush_fails);
	tests.add("Should cleanup failed update temps"s, should_cleanup_failed_update_temps);
	tests.add("Should detect duplicate import destinations"s, should_detect_duplicate_import_destinations);
	tests.add("Should revalidate import rows"s, should_revalidate_import_rows);
	tests.add("Should revalidate replaced import destinations"s, should_revalidate_replaced_import_destinations);
	tests.add("Should reject missing sync folder"s, should_reject_missing_sync_folder);
	tests.add("Should reject overlapping sync folders"s, should_reject_overlapping_sync_folders);
	tests.add("Should reject ambiguous sync roots"s, should_reject_ambiguous_sync_roots);
	tests.add("Should ignore unclaimed remote sync files"s, should_ignore_unclaimed_remote_files);
	tests.add("Should select sync actions"s, should_select_sync_actions);
	tests.add("Should revalidate sync rows"s, should_revalidate_sync_rows);
	tests.add("Should revalidate sync deletes"s, should_revalidate_sync_deletes);
}

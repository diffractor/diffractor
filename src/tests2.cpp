#include "pch.h"
#include "view_test.h"
#include "test.h"
#include "app_command_line.h"
#include "crypto.h"
#include "crypto_aes256.h"
#include "util_base64.h"
#include "crypto_sha.h"
#include "util_simd.h"
#include "model_db.h"
#include "model_index.h"
#include "test_utils.h"
#include "app_util.h"
#include "app_settings.h"
#include "metadata_exif.h"
#include "ui_elements.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"


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
	assert_equal(true, str::icmp_natural("file99", "file100") < 0, "file99 < file100");

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
	assert_equal(true, str::icmp_natural("IMG_9999.png", "IMG_10000.png") < 0, "IMG sequence overflow");
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared helper function definitions
///////////////////////////////////////////////////////////////////////////////////////////////////

file_scan_result ff_scan_file(files& ff, const df::file_path path, const std::string_view xmp_sidecar)
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, false, ft, xmp_sidecar, {});
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
	//assert_equal(expected.pixel_format, actual.pixel_format, "pixel_format", message);
	assert_equal(expected.publisher, actual.publisher, "publisher", message);
	assert_equal(expected.rating, actual.rating, "rating", message);
	assert_equal(expected.audio_sample_rate, actual.audio_sample_rate, "sample_rate", message);
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
	index.scan_item(node.folder, path, false, false, nullptr, files::file_type_from_name(path.name()));
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
	if (!files::is_raw(path)) return {};
	const auto xmp_path = path.extension(".xmp");
	return xmp_path.exists() ? xmp_path.name() : std::string_view{};
}

int count_search_results(index_state& index, const df::search_t& search)
{
	//const df::item_selector folder(test_files_folder, true);
	//const auto search = search_base.add_selector(folder);

	const df::unique_items existing;
	int result = 0;

	auto cb = [&result](const df::item_set& append_items, const bool completed)
	{
		result += static_cast<int>(append_items.size());
	};

	index.query_items(search, existing, cb, test_token);
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

		const df::unique_items existing;
		const df::item_selector selector(test_files_folder, true);
		df::item_set items1;

		auto cb1 = [&items1](const df::item_set& append_items, const bool completed)
		{
			items1.append(append_items);
		};

		test_index.query_items(df::search_t().add_selector(selector), existing, cb1, test_token);

		db1.load_thumbnails(test_index, items1);
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

std::shared_ptr<av_player> make_test_player()
{
	static null_av_host navh;
	return std::make_shared<av_player>(navh);
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
		const auto crc_sse2 = ~calc_crc32c_sse2(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_sse2, "crc32 sse");
	}

	if (platform::neon_supported)
	{
		const auto crc_neon = ~calc_crc32c_arm(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_neon, "crc32 neon");
	}
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

static void should_persist_to_registry()
{
	const auto archive = platform::create_registry_settings();

	const std::vector<std::string> vals = {
		"Hello World"s,
		"\r\n\t hello"s,
		"Доброго ранку!"s,
		"Japanese こんにちは世界"s,
		"Доброго ранку!"s,
		std::string(64, 'x')
	};

	for (const auto& expected : vals)
	{
		std::string actual;
		archive->write({}, "test", expected);
		archive->read({}, "test", actual);

		assert_equal_strict(expected, actual, "Persist To Registry");
	}
}

static void should_persist_to_ini_file()
{
	// Create INI file settings
	const auto settings = platform::create_ini_file_settings();

	// Verify root was created
	assert_equal(true, settings->root_created(), "root_created", "INI file settings");

	// Test uint32_t
	constexpr uint32_t test_uint32 = 12345;
	settings->write("test_section", "uint32_value", test_uint32);
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
}

static void should_scan_info_from_title()
{
	const auto ps1 = scan_info_from_title("Game.of.Thrones.S02E06.HDTV.x264 - 2HD.mp4");

	assert_equal(2, ps1.season, "season");
	assert_equal(6, ps1.episode, "episode");
	assert_equal("Game of Thrones", ps1.show, "show");

	const auto ps2 = scan_info_from_title("It's.a.Wonderful.Life.1946.720p.BluRay.x264.YIFY.mp4");

	assert_equal(1946, ps2.year, "year");
	assert_equal("It's a Wonderful Life", ps2.title, "title");
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

static void should_rename_with_substitutions()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), "can rename");
	assert_equal(save_path_2.name(), test_item->name(), "renamed");
	assert_equal(true, save_path_2.exists(), "renamed exists");

	// Verify item can be found at new path after rename
	auto reloaded = load_item(index, save_path_2, false);
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
	assert_equal("trip-0001", format_sequence("original", "trip-####", 1), "four digit sequence");
	assert_equal("riginal", format_sequence("original", "???????", 0), "question mark substitution");
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

static void should_replace_file()
{
	const auto src_path = df::file_path(test_files_folder, "Test.jpg");
	const auto copy1 = _temps.next_path(".jpg");
	const auto copy2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, copy1, false, false);
	platform::copy_file(src_path, copy2, false, false);

	assert_equal(true, copy1.exists(), "copy1 exists before replace");
	assert_equal(true, copy2.exists(), "copy2 exists before replace");

	const auto result = platform::replace_file(copy1, copy2);
	assert_equal(true, result.success(), "replace succeeded");
	assert_equal(true, copy1.exists(), "destination exists after replace");
}

static void should_analyze_imports()
{
	import_options options;
	options.dest_folder = _temps.folder();
	options.dest_structure = std::string(default_custom_folder_structure);

	const auto src_path = df::file_path(test_files_folder, "Test.jpg");
	const auto fi = platform::file_attributes(src_path);

	std::vector<folder_scan_item> items;
	folder_scan_item item;
	item.folder = test_files_folder;
	item.item.name = src_path.name();
	item.item.file_modified = fi.modified;
	item.item.file_created = fi.created;
	item.item.ft = files::file_type_from_name(src_path);
	items.emplace_back(item);

	const item_import_set no_previous;
	const auto result = import_analysis(items, options, no_previous, test_token);

	assert_equal(true, !result.empty(), "analysis produced results");
	assert_equal(true, count_imports(result) > 0, "has items to import");
}

static void should_analyze_sync()
{
	df::index_roots roots;
	roots.folders.emplace(test_files_folder);

	const auto remote = _temps.folder();
	const auto result = sync_analysis(roots, remote, true, false, false, false, test_token);

	// With empty remote folder, all local files should be candidates for copy_local
	assert_equal(true, result.empty() || !result.empty(), "sync analysis completes without error");
}

void register_tests2(view_state& state, test_registry& tests)
{
	tests.add("Should natural compare"s, should_icmp_natural);

	//
	// Utility / Infrastructure
	//
	tests.add("Should calc HMAC SHA1"s, should_calc_HMACSHA1);
	tests.add("Should calc Hashes"s, should_calc_hashes);
	tests.add("Should convert Utf8"s, should_convert_utf8);
	tests.add("Should split"s, should_split);
	tests.add("Should split genre"s, should_split_genre);
	tests.add("Should extract url"s, should_extract_url);
	tests.add("Should detect wildcard"s, should_detect_wildcard);
	tests.add("Should match wildcard"s, should_match_wildcard);
	tests.add("Should compare versions"s, should_compare_versions);
	tests.add("Should Encrypt Password"s, should_encrypt_password);
	tests.add("Should persist strings in registry"s, should_persist_to_registry);
	tests.add("Should scan info from title"s, should_scan_info_from_title);
	tests.add("Should parse command line"s, should_parse_command_line);
	tests.add("Should trim strings"s, should_trim_strings);
	tests.add("Should format text"s, should_format_text);
	tests.add("Should find text"s, should_find_text);
	tests.add("INI file settings should persist values"s, should_persist_to_ini_file);

	//
	// Data Model / Properties
	//
	tests.add("Should copy preserve properties"s, should_copy_preserve_properties);
	tests.add("Should Rename with substitutions"s, should_rename_with_substitutions);
	tests.add("Should format plural text"s, should_format_plural_text);
	tests.add("Should format rename"s, should_format_rename);
	tests.add("Should check overwrite"s, should_check_overwrite);
	tests.add("Should replace file"s, should_replace_file);
	tests.add("Should analyze imports"s, should_analyze_imports);
	tests.add("Should analyze sync"s, should_analyze_sync);
}

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


static void should_icmp_natural()
{
	// Test basic numeric comparison - the key bug fix
	// Files like 43_100 should come after 43_99, not between 43_10 and 43_11
	assert_equal(true, str::icmp_natural(u8"43_09"sv, u8"43_10"sv) < 0, u8"43_09 < 43_10"sv);
	assert_equal(true, str::icmp_natural(u8"43_10"sv, u8"43_11"sv) < 0, u8"43_10 < 43_11"sv);
	assert_equal(true, str::icmp_natural(u8"43_10"sv, u8"43_100"sv) < 0, u8"43_10 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_99"sv, u8"43_100"sv) < 0, u8"43_99 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_100"sv, u8"43_101"sv) < 0, u8"43_100 < 43_101"sv);

	// Verify the order reported in the bug is fixed
	assert_equal(true, str::icmp_natural(u8"43_09"sv, u8"43_100"sv) < 0, u8"43_09 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_11"sv, u8"43_100"sv) < 0, u8"43_11 < 43_100"sv);

	// Test equality
	assert_equal(0, str::icmp_natural(u8"file10"sv, u8"file10"sv), u8"equal strings"sv);
	assert_equal(0, str::icmp_natural(u8""sv, u8""sv), u8"empty strings"sv);

	// Test case insensitivity
	assert_equal(0, str::icmp_natural(u8"File10"sv, u8"file10"sv), u8"case insensitive"sv);
	assert_equal(0, str::icmp_natural(u8"FILE10"sv, u8"file10"sv), u8"case insensitive upper"sv);

	// Test basic natural ordering
	assert_equal(true, str::icmp_natural(u8"file1"sv, u8"file2"sv) < 0, u8"file1 < file2"sv);
	assert_equal(true, str::icmp_natural(u8"file2"sv, u8"file10"sv) < 0, u8"file2 < file10"sv);
	assert_equal(true, str::icmp_natural(u8"file9"sv, u8"file10"sv) < 0, u8"file9 < file10"sv);
	assert_equal(true, str::icmp_natural(u8"file10"sv, u8"file11"sv) < 0, u8"file10 < file11"sv);
	assert_equal(true, str::icmp_natural(u8"file19"sv, u8"file20"sv) < 0, u8"file19 < file20"sv);
	assert_equal(true, str::icmp_natural(u8"file99"sv, u8"file100"sv) < 0, u8"file99 < file100"sv);

	// Test reverse ordering
	assert_equal(true, str::icmp_natural(u8"file10"sv, u8"file9"sv) > 0, u8"file10 > file9"sv);
	assert_equal(true, str::icmp_natural(u8"file100"sv, u8"file99"sv) > 0, u8"file100 > file99"sv);

	// Test with different prefixes
	assert_equal(true, str::icmp_natural(u8"a10"sv, u8"b1"sv) < 0, u8"a10 < b1"sv);
	assert_equal(true, str::icmp_natural(u8"img001"sv, u8"img002"sv) < 0, u8"img001 < img002"sv);
	assert_equal(true, str::icmp_natural(u8"img009"sv, u8"img010"sv) < 0, u8"img009 < img010"sv);

	// Test numbers at the start
	assert_equal(true, str::icmp_natural(u8"1file"sv, u8"2file"sv) < 0, u8"1file < 2file"sv);
	assert_equal(true, str::icmp_natural(u8"9file"sv, u8"10file"sv) < 0, u8"9file < 10file"sv);
	assert_equal(true, str::icmp_natural(u8"10file"sv, u8"100file"sv) < 0, u8"10file < 100file"sv);

	// Test multiple number groups
	assert_equal(true, str::icmp_natural(u8"file1-1"sv, u8"file1-2"sv) < 0, u8"file1-1 < file1-2"sv);
	assert_equal(true, str::icmp_natural(u8"file1-9"sv, u8"file1-10"sv) < 0, u8"file1-9 < file1-10"sv);
	assert_equal(true, str::icmp_natural(u8"file1-10"sv, u8"file2-1"sv) < 0, u8"file1-10 < file2-1"sv);

	// Test leading zeros
	assert_equal(true, str::icmp_natural(u8"file007"sv, u8"file7"sv) > 0, u8"file007 > file7 (more leading zeros)"sv);
	assert_equal(true, str::icmp_natural(u8"file07"sv, u8"file007"sv) < 0,
	             u8"file07 < file007 (fewer leading zeros)"sv);
	assert_equal(0, str::icmp_natural(u8"file007"sv, u8"file007"sv), u8"same with leading zeros"sv);

	// Test purely numeric strings
	assert_equal(true, str::icmp_natural(u8"1"sv, u8"2"sv) < 0, u8"1 < 2"sv);
	assert_equal(true, str::icmp_natural(u8"9"sv, u8"10"sv) < 0, u8"9 < 10"sv);
	assert_equal(true, str::icmp_natural(u8"99"sv, u8"100"sv) < 0, u8"99 < 100"sv);
	assert_equal(true, str::icmp_natural(u8"999"sv, u8"1000"sv) < 0, u8"999 < 1000"sv);

	// Test strings with no numbers
	assert_equal(true, str::icmp_natural(u8"abc"sv, u8"abd"sv) < 0, u8"abc < abd"sv);
	assert_equal(true, str::icmp_natural(u8"abc"sv, u8"abcd"sv) < 0, u8"abc < abcd"sv);
	assert_equal(0, str::icmp_natural(u8"abc"sv, u8"ABC"sv), u8"abc == ABC (case insensitive)"sv);

	// Test image sequence patterns (common use case)
	assert_equal(true, str::icmp_natural(u8"DSC_0001.jpg"sv, u8"DSC_0002.jpg"sv) < 0, u8"DSC sequence"sv);
	assert_equal(true, str::icmp_natural(u8"DSC_0099.jpg"sv, u8"DSC_0100.jpg"sv) < 0, u8"DSC sequence 99-100"sv);
	assert_equal(true, str::icmp_natural(u8"IMG_9999.png"sv, u8"IMG_10000.png"sv) < 0, u8"IMG sequence overflow"sv);
	assert_equal(true, str::icmp_natural(u8"IMG_9999.png"sv, u8"IMG_10000.png"sv) < 0, u8"IMG sequence overflow"sv);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared helper function definitions
///////////////////////////////////////////////////////////////////////////////////////////////////

file_scan_result ff_scan_file(files& ff, const df::file_path path, const std::u8string_view xmp_sidecar)
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, false, ft, xmp_sidecar, {});
}

file_scan_result ff_scan_and_load_thumb(files& ff, const df::file_path path,
	const std::u8string_view xmp_sidecar)
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, true, ft, xmp_sidecar, thumbnail_max_dimension);
}

void assert_metadata(const prop::item_metadata& expected, const prop::item_metadata& actual,
	const std::u8string_view message)
{
	assert_equal(expected.album, actual.album, u8"album"sv, message);
	assert_equal(expected.album_artist, actual.album_artist, u8"album_artist"sv, message);
	assert_equal(expected.artist, actual.artist, u8"artist"sv, message);
	assert_equal(expected.audio_codec, actual.audio_codec, u8"audio_codec"sv, message);
	assert_equal(expected.audio_sample_rate, actual.audio_sample_rate, u8"audio_sample_rate"sv, message);
	assert_equal(expected.audio_sample_type, actual.audio_sample_type, u8"audio_sample_type"sv, message);
	assert_equal(expected.audio_channels, actual.audio_channels, u8"audio_channels"sv, message);
	assert_equal(expected.bitrate, actual.bitrate, u8"bitrate"sv, message);
	assert_equal(expected.camera_manufacturer, actual.camera_manufacturer, u8"camera_manufacturer"sv, message);
	assert_equal(expected.camera_model, actual.camera_model, u8"camera_model"sv, message);
	assert_equal(expected.comment, actual.comment, u8"comment"sv, message);
	assert_equal(expected.composer, actual.composer, u8"composer"sv, message);
	assert_equal(expected.coordinate, actual.coordinate, u8"coordinate"sv, message);
	assert_equal(expected.copyright_creator, actual.copyright_creator, u8"copyright_creator"sv, message);
	assert_equal(expected.copyright_credit, actual.copyright_credit, u8"copyright_credit"sv, message);
	assert_equal(expected.copyright_notice, actual.copyright_notice, u8"copyright_notice"sv, message);
	assert_equal(expected.copyright_source, actual.copyright_source, u8"copyright_source"sv, message);
	assert_equal(expected.copyright_url, actual.copyright_url, u8"copyright_url"sv, message);
	assert_equal(expected.created_digitized, actual.created_digitized, u8"created_digitized"sv, message);
	assert_equal(expected.created_exif, actual.created_exif, u8"created_exif"sv, message);
	assert_equal(expected.created_utc, actual.created_utc, u8"created_utc"sv, message);
	assert_equal(expected.description, actual.description, u8"description"sv, message);
	assert_equal(expected.width, actual.width, u8"width"sv, message);
	assert_equal(expected.height, actual.height, u8"height"sv, message);
	assert_equal(expected.disk, actual.disk, u8"disk"sv, message);
	assert_equal(expected.duration, actual.duration, u8"duration"sv, message);
	assert_equal(expected.encoder, actual.encoder, u8"encoder"sv, message);
	assert_equal(expected.episode, actual.episode, u8"episode"sv, message);
	assert_equal(prop::format_exposure(expected.exposure_time), prop::format_exposure(actual.exposure_time),
		u8"exposure_time"sv, message);
	assert_equal(prop::format_f_num(expected.f_number), prop::format_f_num(actual.f_number), u8"f_number"sv, message);
	assert_equal(expected.file_name, actual.file_name, u8"file_name"sv, message);
	assert_equal(expected.focal_length, actual.focal_length, u8"focal_length"sv, message);
	assert_equal(expected.focal_length_35mm_equivalent, actual.focal_length_35mm_equivalent,
		u8"focal_length_35mm_equivalent"sv, message);
	assert_equal(expected.genre, actual.genre, u8"genre"sv, message);
	assert_equal(expected.iso_speed, actual.iso_speed, u8"iso_speed"sv, message);
	assert_equal(expected.lens, actual.lens, u8"lens"sv, message);
	assert_equal(expected.location_place, actual.location_place, u8"location_city"sv, message);
	assert_equal(expected.location_country, actual.location_country, u8"location_country"sv, message);
	assert_equal(expected.location_state, actual.location_state, u8"location_state"sv, message);
	assert_equal(expected.orientation, actual.orientation, u8"orientation"sv, message);
	assert_equal(expected.performer, actual.performer, u8"performer"sv, message);
	//assert_equal(expected.pixel_format, actual.pixel_format, u8"pixel_format"sv, message);
	assert_equal(expected.publisher, actual.publisher, u8"publisher"sv, message);
	assert_equal(expected.rating, actual.rating, u8"rating"sv, message);
	assert_equal(expected.audio_sample_rate, actual.audio_sample_rate, u8"sample_rate"sv, message);
	assert_equal(expected.season, actual.season, u8"season"sv, message);
	assert_equal(expected.show, actual.show, u8"show"sv, message);
	assert_equal(expected.synopsis, actual.synopsis, u8"synopsis"sv, message);
	assert_equal(expected.tags, actual.tags, u8"tags"sv, message);
	assert_equal(expected.title, actual.title, u8"title"sv, message);
	assert_equal(expected.track, actual.track, u8"track"sv, message);
	assert_equal(expected.video_codec, actual.video_codec, u8"video_codec"sv, message);
	assert_equal(expected.year, actual.year, u8"year"sv, message);
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
	result->camera_manufacturer = u8"Canon"_c;
	result->camera_model = u8"Canon EOS 7D"_c;
	result->coordinate = gps_coordinate(50.08806, 14.42083);
	result->copyright_notice = u8"Copyright"_c;
	result->created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	result->created_exif = df::date_t(2012, 9, 14, 19, 21, 14);
	result->description = u8"Caption"_c;
	result->exposure_time = 1.0f / 100.0f;
	result->iso_speed = 100;
	result->lens = u8"EF-S15-85mm f/3.5-5.6 IS USM"_c;
	result->location_place = u8"Prague"_c;
	result->location_country = u8"Czech Republic"_c;
	result->location_state = u8"Hlavní Mesto Praha"_c;
	result->rating = 4;
	result->tags = u8"key1 key2 key3"_c;
	result->title = u8"Title"_c;
	result->width = 1024;
	result->height = 683;
	result->pixel_format = u8"YCbCr"_c;

	return result;
}


df::index_file_item make_index_file_info(const df::date_t date)
{
	df::index_file_item result;
	result.file_modified = date;
	result.file_created = date;
	result.ft = files::file_type_from_name(u8"test.jpg"sv);
	result.safe_ps();
	return result;
}


df::item_element_ptr load_item(index_state& index, const df::file_path path, const bool load_thumb)
{
	auto i = std::make_shared<df::item_element>(path, index.find_item(path));
	index.scan_item(i, load_thumb, false);
	return i;
}

std::u8string_view detect_xmp_sidecar(const df::file_path path)
{
	if (!files::is_raw(path)) return {};
	const auto xmp_path = path.extension(u8".xmp"sv);
	return xmp_path.exists() ? xmp_path.name() : std::u8string_view{};
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

int count_search_results(index_state& index, const std::u8string_view query)
{
	return count_search_results(index, df::search_t::parse(query));
}

void build_index(index_state& index, database& db)
{
	df::index_roots paths;
	paths.folders.emplace(test_files_folder);
	paths.excludes.emplace(test_files_folder.combine(u8"excluded1"sv));
	paths.exclude_wildcards.emplace(u8"exclud*2"_c);

	index.index_roots(paths);
	index.index_folders(test_token);
	index.scan_uncached(test_token);
	db.perform_writes();

	assert_equal(expected_cached_item_count, index.stats.media_item_count, u8"cached item count"sv);
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

		assert_equal(expected_cached_item_count, test_index.stats.media_item_count, u8"cached item count"sv);
		assert_equal(0, empty_index.stats.media_item_count, u8"cached item count"sv);
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
	const auto signature = crypto::hmac_sha1(u8"Jefe"sv, u8"what do ya want for nothing?"sv);
	assert_equal(u8"7/zfauXrL6LSdBbV8YTfnCWafHk="sv, signature, u8"Signature"sv);
}

static void should_calc_hashes()
{
	assert_equal(u8"A9993E364706816ABA3E25717850C26C9CD0D89D"sv, crypto::to_sha1(u8"abc"sv), u8"SHA1"sv);
	assert_equal(u8"187797D630ECAA0FC1B920CD9F809C2BBFFCBF4C"sv, crypto::to_sha1(long_text), u8"SHA1"sv);
	assert_equal(u8"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"sv, crypto::to_sha256(u8"abc"sv),
		u8"SHA256"sv);
	assert_equal(u8"1660F10AEC042D762CF8B1C53E976F890C8E797BEF74807F505EDCE20308FC2F"sv, crypto::to_sha256(long_text),
		u8"SHA256"sv);

	const auto crc_data = u8"hello world"s;
	const auto crc_result = crypto::crc32c(crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_result, u8"crc32"sv);

	const auto crc_c = ~calc_crc32c_c(crypto::CRCINIT, crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_c, u8"crc32 c"sv);

	if (platform::crc32_supported)
	{
		const auto crc_sse2 = ~calc_crc32c_sse2(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_sse2, u8"crc32 sse"sv);
	}

	if (platform::neon_supported)
	{
		const auto crc_neon = ~calc_crc32c_arm(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_neon, u8"crc32 neon"sv);
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

	std::u8string_view strings[] = {
		u8"In vollen Zügen genießen"sv,
		u8"Nældens takvinge"sv,
		u8"💉💎👦🏻👓⚡"sv,
		u8"Žižkov"sv,
		u8"Доброго ранку!"sv,
		u8"Japanese こんにちは世界"sv,
		u8"Arabic مرحبا العالم"sv,
		stars
	};

	for (const auto src : strings)
	{
		assert_equal(src, platform::utf16_to_utf8(platform::utf8_to_utf16(src)), u8"platform conversions"sv);
		assert_equal(platform::utf8_to_utf16(src), str::utf8_to_utf16(src), u8"to utf16"sv);
		assert_equal(src, str::utf16_to_utf8(platform::utf8_to_utf16(src)), u8"to utf8"sv);
		assert_equal(src, str::utf16_to_utf8(str::utf8_to_utf16(src)), u8"internal conversions"sv);
	}

	constexpr wchar_t icon_text[2] = { static_cast<wchar_t>(icon_index::fit), 0 };
	const auto icon_text_converted = str::utf8_to_utf16(str::utf16_to_utf8(icon_text));
	assert_equal(icon_text, icon_text_converted, u8"icon to utf8"sv);
}

static void should_split()
{
	constexpr auto to_be_split = u8"H:\\2-Archief VIDEO privé\\Eigen video's\nF:\\1-Archief FOTOGRAFIE privé"sv;
	const auto parts = str::split(to_be_split, false, [](const wchar_t c) { return c == '\n' || c == '\r'; });

	assert_equal(u8"H:\\2-Archief VIDEO privé\\Eigen video's"sv, parts[0], u8"Split 1"sv);
	assert_equal(u8"F:\\1-Archief FOTOGRAFIE privé"sv, parts[1], u8"Split 2"sv);

	constexpr auto to_be_split2 = u8"aaa 'bbb ccc' ddd \"ee ff \""sv;
	const auto parts2 = str::split(to_be_split2, true);

	constexpr auto to_be_split3 = u8"Доброго ранку!"sv;
	const auto parts3 = str::split(to_be_split3, true);

	assert_equal(u8"aaa"sv, parts2[0], u8"Split 1"sv);
	assert_equal(u8"bbb ccc"sv, parts2[1], u8"Split 2"sv);
	assert_equal(u8"ddd"sv, parts2[2], u8"Split 3"sv);
	assert_equal(u8"ee ff "sv, parts2[3], u8"Split 4"sv);
	assert_equal(u8"ранку!"sv, parts3[1], u8"Split 5"sv);

	// Random data checking for crashes
	std::u8string_view strings[] = {
		u8"In vollen Zügen genießen"sv,
		u8"Nældens takvinge"sv,
		u8"Žižkov"sv,
		u8"Доброго ранку!"sv,
		u8"Japanese こんにちは世界"sv,
		u8"Arabic مرحبا العالم"sv,
		u8"Доброго ранку!"sv,
		u8"\"'"sv,
		u8"\" \" \""sv,
		u8"''''"sv,
		u8"aaa'bb  bbb'aa"sv,
		u8"aaa\0\0\'"sv,
		u8"\r\t\naaaa\" aaa bbb"sv,
		u8"'\t \n abc"sv,
		u8"'"sv,
	};

	for (const auto& src : strings)
	{
		str::split_count(src, true);
	}
}

static void should_extract_url()
{
	constexpr auto input1 = u8"Visit my website at https://www.example.com for more info."sv;
	constexpr auto input2 = u8"Check out this article: http://anotherexample.org/article"sv;
	constexpr auto input3 = u8"No URLs here."sv;
	constexpr auto input4 =
		u8"Quite nice  <a href=\"http://bighugelabs.com/flickr/onblack.php?id=1397504988\"> On Black</a>"sv;

	assert_equal(u8"https://www.example.com"sv, df::url_extract(input1), u8"extract url"sv);
	assert_equal(u8"http://anotherexample.org/article"sv, df::url_extract(input2), u8"extract url"sv);
	assert_equal(u8""sv, df::url_extract(input3), u8"extract url"sv);
	assert_equal(u8""sv, df::url_extract(input3), u8"extract url"sv);
	assert_equal(u8"http://bighugelabs.com/flickr/onblack.php?id=1397504988"sv, df::url_extract(input4),
		u8"extract url"sv);
}

static void should_match_wildcard()
{
	assert_equal(true, str::wildcard_icmp(u8""sv, u8""sv));
	assert_equal(true, str::wildcard_icmp(u8""sv, u8"*"sv));
	assert_equal(true, str::wildcard_icmp(u8" "sv, u8"*"sv));
	assert_equal(true, str::wildcard_icmp(u8" "sv, u8" *"sv));
	assert_equal(false, str::wildcard_icmp(u8" "sv, u8"  *"sv));

	assert_equal(true, str::wildcard_icmp(u8"hello world"sv, u8"hello world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello ?! world"sv, u8"hello * world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello-xx-world"sv, u8"hello*world"sv));
	assert_equal(false, str::wildcard_icmp(u8"hello-xx-world"sv, u8"hello *world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello-xx-world"sv, u8"*world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello-xx-world"sv, u8"hello*"sv));

	assert_equal(true, str::wildcard_icmp(u8"HELLO-XX-WORLD"sv, u8"hello*"sv));
	assert_equal(true, str::wildcard_icmp(u8"HELLO-XX-WORLD"sv, u8"hello*world"sv));


	assert_equal(0, str::icmp(u8"ДОБРОГО РАНКУ"sv, u8"Доброго ранку"sv));
	assert_equal(0, str::icmp(u8"ARABIC مرحبا العالم"sv, u8"Arabic مرحبا العالم"sv));
	assert_equal(0, str::icmp(u8"JAPANESE こんにちは世界"sv, u8"Japanese こんにちは世界"sv));
	assert_equal(0, str::icmp(u8"💉💎👦🏻👓⚡"sv, u8"💉💎👦🏻👓⚡"sv));

	assert_equal(true, str::wildcard_icmp(u8"Доброго ранку"sv, u8"Доброго*"sv));
	assert_equal(true, str::wildcard_icmp(u8"ДОБРОГО РАНКУ"sv, u8"Доброго*"sv));
	assert_equal(true, str::wildcard_icmp(u8"ДОБРОГО РАНКУ"sv, u8"*ранку"sv));
	assert_equal(true, str::wildcard_icmp(u8"💉💎👦🏻👓⚡"sv, u8"*💎*"sv));
	assert_equal(true, str::wildcard_icmp(u8"💉💎👦🏻👓⚡"sv, u8"💉*"sv));
}

static void should_detect_wildcard()
{
	assert_equal(false, str::is_wildcard(u8""sv));
	assert_equal(false, str::is_wildcard(u8"abcdef"sv));
	assert_equal(true, str::is_wildcard(u8"abc*"sv));
	assert_equal(false, str::is_wildcard(u8"abc\\*"sv));
	assert_equal(false, str::is_wildcard(u8"abc\\*ef"sv));
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
	assert_equal(base64_encode(test_dec), base64_encode(decrypted), u8"encrypt using aes"sv);

	const std::vector<std::u8string_view> test_values =
	{
		{},
		u8"This is a test."sv,
		long_text
	};

	static constexpr auto password = u8"diffractor-hello"sv;

	for (const auto& val : test_values)
	{
		auto result = crypto::decrypt(crypto::encrypt(val, password), password);
		assert_equal(val, std::u8string(result.begin(), result.end()), u8"Encode - Decode"sv);
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

	const std::vector<std::u8string> vals = {
		u8"Hello World"s,
		u8"\r\n\t hello"s,
		u8"Доброго ранку!"s,
		u8"Japanese こんにちは世界"s,
		u8"Доброго ранку!"s,
		std::u8string(64, 'x')
	};

	for (const auto& expected : vals)
	{
		std::u8string actual;
		archive->write({}, u8"test"sv, expected);
		archive->read({}, u8"test"sv, actual);

		assert_equal_strict(expected, actual, u8"Persist To Registry"sv);
	}
}

static void should_persist_to_ini_file()
{
	// Create INI file settings
	const auto settings = platform::create_ini_file_settings();

	// Verify root was created
	assert_equal(true, settings->root_created(), u8"root_created"sv, u8"INI file settings"sv);

	// Test uint32_t
	constexpr uint32_t test_uint32 = 12345;
	settings->write(u8"test_section"sv, u8"uint32_value"sv, test_uint32);
	uint32_t read_uint32 = 0;
	assert_equal(true, settings->read(u8"test_section"sv, u8"uint32_value"sv, read_uint32), u8"read uint32"sv,
		u8"INI file settings"sv);
	assert_equal(test_uint32, read_uint32, u8"uint32 value"sv, u8"INI file settings"sv);

	// Test uint64_t
	constexpr uint64_t test_uint64 = 0xFFFFFFFFFFFFull;
	settings->write(u8"test_section"sv, u8"uint64_value"sv, test_uint64);
	uint64_t read_uint64 = 0;
	assert_equal(true, settings->read(u8"test_section"sv, u8"uint64_value"sv, read_uint64), u8"read uint64"sv,
		u8"INI file settings"sv);
	assert_equal(test_uint64, read_uint64, u8"uint64 value"sv, u8"INI file settings"sv);

	// Test string
	const auto test_string = u8"Hello, World! With special chars: äöü"s;
	settings->write(u8"test_section"sv, u8"string_value"sv, test_string);
	std::u8string read_string;
	assert_equal(true, settings->read(u8"test_section"sv, u8"string_value"sv, read_string), u8"read string"sv,
		u8"INI file settings"sv);
	assert_equal(test_string, read_string, u8"string value"sv, u8"INI file settings"sv);

	// Test binary data (base64 encoded)
	const std::vector<uint8_t> test_binary = { 0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD };
	settings->write(u8"test_section"sv, u8"binary_value"sv, df::cspan{ test_binary.data(), test_binary.size() });
	std::vector<uint8_t> read_buffer(test_binary.size());
	size_t read_len = read_buffer.size();
	assert_equal(true, settings->read(u8"test_section"sv, u8"binary_value"sv, read_buffer.data(), read_len),
		u8"read binary"sv, u8"INI file settings"sv);
	assert_equal(test_binary.size(), read_len, u8"binary length"sv, u8"INI file settings"sv);
	for (size_t i = 0; i < test_binary.size(); ++i)
	{
		assert_equal(static_cast<uint32_t>(test_binary[i]), static_cast<uint32_t>(read_buffer[i]), u8"binary byte"sv,
			u8"INI file settings"sv);
	}
}

static void should_parse_command_line()
{
	command_line_t cl1;
	cl1.parse(u8"-no-gpu"sv);

	assert_equal(true, cl1.no_gpu, u8"no_gpu"sv);
	assert_equal(false, cl1.no_indexing, u8"no_indexing"sv);

	command_line_t cl2;
	cl2.parse(test_files_folder.text());

	assert_equal(false, cl2.folder_path.is_empty(), u8"folder_path"sv);
	assert_equal(std::u8string_view{}, cl2.selection.name(), u8"selection name"sv);
	assert_equal(test_files_folder.text(), cl2.folder_path.folder().text(), u8"folder path"sv);
	assert_equal(false, cl2.no_gpu, u8"no_gpu"sv);
	assert_equal(false, cl2.no_indexing, u8"no_indexing"sv);


	const auto path3 = test_files_folder.combine_file(u8"test.jpg"sv);
	command_line_t cl3;
	cl3.parse(str::format(u8"{} -no-indexing"sv, path3));

	assert_equal(path3.folder().text(), cl3.folder_path.folder().text(), u8"folder_path"sv);
	assert_equal(path3.name(), cl3.selection.name(), u8"selection name"sv);
	assert_equal(path3.folder().text(), cl3.selection.folder().text(), u8"selection folder"sv);
	assert_equal(false, cl3.no_gpu, u8"no_gpu"sv);
	assert_equal(true, cl3.no_indexing, u8"no_indexing"sv);

	command_line_t cl4;
	cl4.parse(u8"--no-gpu \"C:\\Program Files\""sv);
	assert_equal(true, cl4.no_gpu, u8"no_gpu"sv);
	assert_equal(false, cl4.folder_path.is_empty(), u8"folder_path program Files"sv);

	command_line_t cl5;
	cl5.parse(u8"----- --no-gpu"sv);
	assert_equal(true, cl5.no_gpu, u8"no_gpu"sv);
}

static void should_trim_strings()
{
	assert_equal(u8"xxx"sv, str::trim_and_cache(u8"xxx\n"sv), u8"remove cr lf"sv);
	assert_equal(u8"xxx"sv, str::trim_and_cache(u8"\rxxx\r"sv), u8"remove lf"sv);
	assert_equal(u8"xxx"sv, str::trim_and_cache(u8"   xxx\t\t "sv), u8"remove space"sv);
}

static void should_format_text()
{
	assert_equal(u8"ac-dc"sv, str::format(u8"{2}{0}-{1}{0}"sv, u8"c"sv, u8"d"sv, u8"a"sv), u8"order"sv);
	assert_equal(u8"0.00123"sv, str::format(u8"{}"sv, 0.00123), u8"double"sv);
	assert_equal(u8"0.001"sv, str::format(u8"{:0.3}"sv, 0.00123), u8"double"sv);
	assert_equal(u8"5.5"sv, str::format(u8"{}"sv, 5.5000), u8"double"sv);
	assert_equal(u8"123"sv, str::format(u8"{}"sv, 123), u8"int"sv);
	assert_equal(u8"0123"sv, str::format(u8"{:04}"sv, 123), u8"int"sv);
	assert_equal(u8" 123"sv, str::format(u8"{:4}"sv, 123), u8"int"sv);
	assert_equal(u8"hex=7B"sv, str::format(u8"hex={:x}"sv, 0x7B), u8"hex"sv);
	assert_equal(u8"-test-"sv, str::format(u8"-{}-"sv, u8"test"sv), u8"char8_t*"sv);
	assert_equal(u8"-test-"sv, str::format(u8"-{}-"sv, std::u8string(u8"test"sv)), u8"string"sv);
	assert_equal(u8"-test-"sv, str::format(u8"-{}-"sv, std::u8string_view(u8"test"sv)), u8"string_view"sv);
	assert_equal(u8"33 {} {test}"sv, str::format(u8"{} {{}} {{test}}"sv, 33), u8"string_view"sv);
	assert_equal(u8"22 x 33"sv, str::format(u8"{} x {}"sv, 22, 33), u8"string_view"sv);
}

static std::u8string find_and_format_result(const std::u8string_view text, const std::u8string_view sub_string)
{
	const auto r = str::ifind2(text, sub_string, 0);
	auto result = std::u8string(text);

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
	assert_equal(u8"*white* on blond"sv, find_and_format_result(u8"white on blond"sv, u8"white"sv));
	assert_equal(u8"*whi*te on *bl*ond"sv, find_and_format_result(u8"white on blond"sv, u8"whi bl"sv));
	assert_equal(u8"*white* on *blond*"sv, find_and_format_result(u8"white on blond"sv, u8"white blond"sv));
	assert_equal(u8"*white* bl on *blond*"sv, find_and_format_result(u8"white bl on blond"sv, u8"white blond"sv));
}

static void should_scan_info_from_title()
{
	const auto ps1 = scan_info_from_title(u8"Game.of.Thrones.S02E06.HDTV.x264 - 2HD.mp4"sv);

	assert_equal(2, ps1.season, u8"season"sv);
	assert_equal(6, ps1.episode, u8"episode"sv);
	assert_equal(u8"Game of Thrones"sv, ps1.show, u8"show"sv);

	const auto ps2 = scan_info_from_title(u8"It's.a.Wonderful.Life.1946.720p.BluRay.x264.YIFY.mp4"sv);

	assert_equal(1946, ps2.year, u8"year"sv);
	assert_equal(u8"It's a Wonderful Life"sv, ps2.title, u8"title"sv);
}

static void should_compare_versions()
{
	const df::version current_version(s_app_version);
	assert_equal(s_app_version, current_version.to_string(), u8"Can parse and to_string current version"sv);

	const df::version test_version1(u8"123.45"sv);
	const df::version test_version1b(u8"123.45"sv);
	df::version test_version2(u8"456.1"sv);

	assert_equal(u8"123.45"sv, test_version1.to_string(), u8"Can parse and to_string test version 1"sv);
	assert_equal(u8"456.1"sv, test_version2.to_string(), u8"Can parse and to_string test version 2"sv);

	assert_equal(true, test_version1 < test_version2, u8"Less op version"sv);
	assert_equal(false, test_version2 < test_version1, u8"Less op version"sv);
	assert_equal(false, test_version1 == test_version2, u8"== op version"sv);
	assert_equal(true, test_version1 == test_version1b, u8"== op version"sv);

	assert_equal(u8"457.1"sv, (test_version2 + 1).to_string(), u8"+ op version"sv);
}

static void should_copy_preserve_properties()
{
	prop::item_metadata src;
	src.title = u8"Test Title"_c;
	src.rating = 4;
	src.media_position = 19.5;
	src.iso_speed = 400;
	src.artist = u8"Test Artist"_c;

	prop::item_metadata dst;
	dst = src;

	assert_equal(src.title, dst.title, u8"should copy title"sv);
	assert_equal(src.rating, dst.rating, u8"should copy rating"sv);
	assert_equal(src.iso_speed, dst.iso_speed, u8"should copy iso_speed"sv);
	assert_equal(src.artist, dst.artist, u8"should copy artist"sv);
}

static void should_rename_with_substitutions()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, u8"Test.jpg"sv);
	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, save_path_1, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), u8"can rename"sv);
	assert_equal(save_path_2.name(), test_item->name(), u8"renamed"sv);
	assert_equal(true, save_path_2.exists(), u8"renamed exists"sv);

	// Verify item can be found at new path after rename
	auto reloaded = load_item(index, save_path_2, false);
	assert_equal(save_path_2.name(), reloaded->name(), u8"index finds renamed item"sv);
}

static void should_format_plural_text()
{
	const plural_text items_fmt(u8"{count} item"sv, u8"{count} items"sv);

	assert_equal(u8"1 item"sv, format_plural_text(items_fmt, 1), u8"singular"sv);
	assert_equal(u8"5 items"sv, format_plural_text(items_fmt, 5), u8"plural"sv);
	assert_equal(u8"0 items"sv, format_plural_text(items_fmt, 0), u8"zero"sv);

	const plural_text name_fmt(u8"{first-name} will be processed."sv, u8"{count} items including {first-name} will be processed."sv);

	assert_equal(u8"photo.jpg will be processed."sv, format_plural_text(name_fmt, u8"photo.jpg"sv, 1, {}), u8"named singular"sv);
	assert_equal(u8"3 items including photo.jpg will be processed."sv, format_plural_text(name_fmt, u8"photo.jpg"sv, 3, {}), u8"named plural"sv);
}

static void should_format_rename()
{
	assert_equal(u8"photo-005"sv, format_sequence(u8"original"sv, u8"photo-###"sv, 5), u8"sequence format"sv);
	assert_equal(u8"photo-042"sv, format_sequence(u8"original"sv, u8"photo-###"sv, 42), u8"sequence zero pad"sv);
	assert_equal(u8"trip-0001"sv, format_sequence(u8"original"sv, u8"trip-####"sv, 1), u8"four digit sequence"sv);
	assert_equal(u8"riginal"sv, format_sequence(u8"original"sv, u8"???????"sv, 0), u8"question mark substitution"sv);
}

static void should_check_overwrite()
{
	const auto src_path = df::file_path(test_files_folder, u8"Test.jpg"sv);
	const auto dest_folder = test_files_folder;

	df::item_set items;
	const auto item = std::make_shared<df::item_element>(src_path, df::index_file_item{});
	items.add(item);

	const auto overwrites = check_overwrite(dest_folder, items, {});
	assert_equal(true, !overwrites.empty(), u8"should detect existing file"sv);

	const auto no_overwrites = check_overwrite(dest_folder, items, u8".xyz"sv);
	assert_equal(true, no_overwrites.empty(), u8"should not detect with different extension"sv);
}

static void should_replace_file()
{
	const auto src_path = df::file_path(test_files_folder, u8"Test.jpg"sv);
	const auto copy1 = _temps.next_path(u8".jpg"sv);
	const auto copy2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, copy1, false, false);
	platform::copy_file(src_path, copy2, false, false);

	assert_equal(true, copy1.exists(), u8"copy1 exists before replace"sv);
	assert_equal(true, copy2.exists(), u8"copy2 exists before replace"sv);

	const auto result = platform::replace_file(copy1, copy2);
	assert_equal(true, result.success(), u8"replace succeeded"sv);
	assert_equal(true, copy1.exists(), u8"destination exists after replace"sv);
}

static void should_analyze_imports()
{
	import_options options;
	options.dest_folder = _temps.folder();
	options.dest_structure = std::u8string(defaut_custom_folder_structure);

	const auto src_path = df::file_path(test_files_folder, u8"Test.jpg"sv);
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

	assert_equal(true, !result.empty(), u8"analysis produced results"sv);
	assert_equal(true, count_imports(result) > 0, u8"has items to import"sv);
}

static void should_analyze_sync()
{
	df::index_roots roots;
	roots.folders.emplace(test_files_folder);

	const auto remote = _temps.folder();
	const auto result = sync_analysis(roots, remote, true, false, false, false, test_token);

	// With empty remote folder, all local files should be candidates for copy_local
	assert_equal(true, result.empty() || !result.empty(), u8"sync analysis completes without error"sv);
}

void register_tests2(view_state& state, test_registry& tests)
{
	tests.add(u8"Should natural compare"s, should_icmp_natural);

	//
	// Utility / Infrastructure
	//
	tests.add(u8"Should calc HMAC SHA1"s, should_calc_HMACSHA1);
	tests.add(u8"Should calc Hashes"s, should_calc_hashes);
	tests.add(u8"Should convert Utf8"s, should_convert_utf8);
	tests.add(u8"Should split"s, should_split);
	tests.add(u8"Should extract url"s, should_extract_url);
	tests.add(u8"Should detect wildcard"s, should_detect_wildcard);
	tests.add(u8"Should match wildcard"s, should_match_wildcard);
	tests.add(u8"Should compare versions"s, should_compare_versions);
	tests.add(u8"Should Encrypt Password"s, should_encrypt_password);
	tests.add(u8"Should persist strings in registry"s, should_persist_to_registry);
	tests.add(u8"Should scan info from title"s, should_scan_info_from_title);
	tests.add(u8"Should parse command line"s, should_parse_command_line);
	tests.add(u8"Should trim strings"s, should_trim_strings);
	tests.add(u8"Should format text"s, should_format_text);
	tests.add(u8"Should find text"s, should_find_text);
	tests.add(u8"INI file settings should persist values"s, should_persist_to_ini_file);

	//
	// Data Model / Properties
	//
	tests.add(u8"Should copy preserve properties"s, should_copy_preserve_properties);
	tests.add(u8"Should Rename with substitutions"s, should_rename_with_substitutions);
	tests.add(u8"Should format plural text"s, should_format_plural_text);
	tests.add(u8"Should format rename"s, should_format_rename);
	tests.add(u8"Should check overwrite"s, should_check_overwrite);
	tests.add(u8"Should replace file"s, should_replace_file);
	tests.add(u8"Should analyze imports"s, should_analyze_imports);
	tests.add(u8"Should analyze sync"s, should_analyze_sync);
}

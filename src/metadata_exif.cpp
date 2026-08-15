// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: EXIF metadata extraction and writing. Parses camera settings, dates, GPS
// coordinates, and other EXIF tags from JPEG and other image formats.

#include "pch.h"
#include "metadata_exif.h"
#include "files.h"
#include "model_location.h"

#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-tag.h>
#include <libexif/exif-content.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-format.h>
#include <libexif/exif-byte-order.h>

#include <utility>

static constexpr uint32_t bytes_per_format[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};


//// Tags/Constants from http://www.exiv2.org/tags.html
enum exif_tag
{
	//	EXIF_TAG_INTEROPERABILITY_INDEX = 0x0001,
	//	EXIF_TAG_INTEROPERABILITY_VERSION = 0x0002,
	//	EXIF_TAG_IMAGE_WIDTH = 0x0100,
	//	EXIF_TAG_IMAGE_LENGTH = 0x0101,
	//	EXIF_TAG_BITS_PER_SAMPLE = 0x0102,
	//	EXIF_TAG_COMPRESSION = 0x0103,
	//	EXIF_TAG_PHOTOMETRIC_INTERPRETATION = 0x0106,
	//	EXIF_TAG_FILL_ORDER = 0x010a,
	//	EXIF_TAG_DOCUMENT_NAME = 0x010d,
	//	EXIF_TAG_IMAGE_DESCRIPTION = 0x010e,
	//	EXIF_TAG_MAKE = 0x010f,
	//	EXIF_TAG_MODEL = 0x0110,
	//	EXIF_TAG_STRIP_OFFSETS = 0x0111,
	//	EXIF_TAG_ORIENTATION = 0x0112,
	//	EXIF_TAG_SAMPLES_PER_PIXEL = 0x0115,
	//	EXIF_TAG_ROWS_PER_STRIP = 0x0116,
	//	EXIF_TAG_STRIP_BYTE_COUNTS = 0x0117,
	//	EXIF_TAG_X_RESOLUTION = 0x011a,
	//	EXIF_TAG_Y_RESOLUTION = 0x011b,
	//	EXIF_TAG_PLANAR_CONFIGURATION = 0x011c,
	//	EXIF_TAG_RESOLUTION_UNIT = 0x0128,
	//	EXIF_TAG_TRANSFER_FUNCTION = 0x012d,
	//	EXIF_TAG_SOFTWARE = 0x0131,
	//	EXIF_TAG_DATE_TIME = 0x0132,
	//	EXIF_TAG_ARTIST = 0x013b,
	//	EXIF_TAG_WHITE_POINT = 0x013e,
	//	EXIF_TAG_PRIMARY_CHROMATICITIES = 0x013f,
	//	EXIF_TAG_TRANSFER_RANGE = 0x0156,
	//	EXIF_TAG_JPEG_PROC = 0x0200,
	//	EXIF_TAG_JPEG_INTERCHANGE_FORMAT = 0x0201,
	//	EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH = 0x0202,
	//	EXIF_TAG_YCBCR_COEFFICIENTS = 0x0211,
	//	EXIF_TAG_YCBCR_SUB_SAMPLING = 0x0212,
	//	EXIF_TAG_YCBCR_POSITIONING = 0x0213,
	//	EXIF_TAG_REFERENCE_BLACK_WHITE = 0x0214,
	//	EXIF_TAG_RELATED_IMAGE_FILE_FORMAT = 0x1000,
	//	EXIF_TAG_RELATED_IMAGE_WIDTH = 0x1001,
	//	EXIF_TAG_RELATED_IMAGE_LENGTH = 0x1002,
	EXIF_TAG_IMAGE_RATING = 0x4746,
	EXIF_TAG_IMAGE_RATING_PERCENT = 0x4749,
	//	EXIF_TAG_CFA_REPEAT_PATTERN_DIM = 0x828d,
	//	EXIF_TAG_CFA_PATTERN = 0x828e,
	//	EXIF_TAG_BATTERY_LEVEL = 0x828f,
	//	EXIF_TAG_COPYRIGHT = 0x8298,
	//	EXIF_TAG_EXPOSURE_TIME = 0x829a,
	//	EXIF_TAG_FNUMBER = 0x829d,
	//	EXIF_TAG_IPTC_NAA = 0x83bb,
	//	EXIF_TAG_EXIF_IFD_POINTER = 0x8769,
	//	EXIF_TAG_INTER_COLOR_PROFILE = 0x8773,
	//	EXIF_TAG_EXPOSURE_PROGRAM = 0x8822,
	//	EXIF_TAG_SPECTRAL_SENSITIVITY = 0x8824,
	//	EXIF_TAG_GPS_INFO_IFD_POINTER = 0x8825,
	//	EXIF_TAG_ISO_SPEED_RATINGS = 0x8827,
	//	EXIF_TAG_OECF = 0x8828,
	//	EXIF_TAG_EXIF_VERSION = 0x9000,
	//	EXIF_TAG_DATE_TIME_ORIGINAL = 0x9003,
	//	EXIF_TAG_DATE_TIME_DIGITIZED = 0x9004,
	//	EXIF_TAG_COMPONENTS_CONFIGURATION = 0x9101,
	//	EXIF_TAG_COMPRESSED_BITS_PER_PIXEL = 0x9102,
	//	EXIF_TAG_SHUTTER_SPEED_VALUE = 0x9201,
	//	EXIF_TAG_APERTURE_VALUE = 0x9202,
	//	EXIF_TAG_BRIGHTNESS_VALUE = 0x9203,
	//	EXIF_TAG_EXPOSURE_BIAS_VALUE = 0x9204,
	//	EXIF_TAG_MAX_APERTURE_VALUE = 0x9205,
	//	EXIF_TAG_SUBJECT_DISTANCE = 0x9206,
	//	EXIF_TAG_METERING_MODE = 0x9207,
	//	EXIF_TAG_LIGHT_SOURCE = 0x9208,
	//	EXIF_TAG_FLASH = 0x9209,
	//	EXIF_TAG_FOCAL_LENGTH = 0x920a,
	//	EXIF_TAG_SUBJECT_AREA = 0x9214,
	//	EXIF_TAG_MAKER_NOTE = 0x927c,
	//	EXIF_TAG_USER_COMMENT = 0x9286,
	EXIF_TAG_USER_COMMENT_XP = 0x9C9C,
	//	EXIF_TAG_SUB_SEC_TIME = 0x9290,
	//	EXIF_TAG_SUB_SEC_TIME_ORIGINAL = 0x9291,
	//	EXIF_TAG_SUB_SEC_TIME_DIGITIZED = 0x9292,
	//	EXIF_TAG_FLASH_PIX_VERSION = 0xa000,
	//	EXIF_TAG_COLOR_SPACE = 0xa001,
	//	EXIF_TAG_PIXEL_X_DIMENSION = 0xa002,
	//	EXIF_TAG_PIXEL_Y_DIMENSION = 0xa003,
	//	EXIF_TAG_RELATED_SOUND_FILE = 0xa004,
	//	EXIF_TAG_INTEROPERABILITY_IFD_POINTER = 0xa005,
	//	EXIF_TAG_FLASH_ENERGY = 0xa20b,
	//	EXIF_TAG_SPATIAL_FREQUENCY_RESPONSE = 0xa20c,
	//	EXIF_TAG_FOCAL_PLANE_X_RESOLUTION = 0xa20e,
	//	EXIF_TAG_FOCAL_PLANE_Y_RESOLUTION = 0xa20f,
	//	EXIF_TAG_FOCAL_PLANE_RESOLUTION_UNIT = 0xa210,
	//	EXIF_TAG_SUBJECT_LOCATION = 0xa214,
	//	EXIF_TAG_EXPOSURE_INDEX = 0xa215,
	//	EXIF_TAG_SENSING_METHOD = 0xa217,
	//	EXIF_TAG_FILE_SOURCE = 0xa300,
	//	EXIF_TAG_SCENE_TYPE = 0xa301,
	//	EXIF_TAG_NEW_CFA_PATTERN = 0xa302,
	//	EXIF_TAG_CUSTO_renderED = 0xa401,
	//	EXIF_TAG_EXPOSURE_MODE = 0xa402,
	//	EXIF_TAG_WHITE_BALANCE = 0xa403,
	//	EXIF_TAG_DIGITAL_ZOOM_RATIO = 0xa404,
	//	EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM = 0xa405,
	//	EXIF_TAG_SCENE_CAPTURE_TYPE = 0xa406,
	//	EXIF_TAG_GAIN_CONTROL = 0xa407,
	//	EXIF_TAG_CONTRAST = 0xa408,
	//	EXIF_TAG_SATURATION = 0xa409,
	//	EXIF_TAG_SHARPNESS = 0xa40a,
	//	EXIF_TAG_DEVICE_SETTING_DESCRIPTION = 0xa40b,
	//	EXIF_TAG_SUBJECT_DISTANCE_RANGE = 0xa40c,
	//	EXIF_TAG_IMAGE_UNIQUE_ID = 0xa420,
	//	EXIF_TAG_LENS_MODEL = 0xa434,
	//
	//	TAG_XMP = 700,
};

enum cannon_tag
{
	MNOTE_CANON_TAG_UNKNOWN_0 = 0x0,
	MNOTE_CANON_TAG_SETTINGS_1 = 0x1,
	MNOTE_CANON_TAG_FOCAL_LENGTH = 0x2,
	MNOTE_CANON_TAG_UNKNOWN_3 = 0x3,
	MNOTE_CANON_TAG_SETTINGS_2 = 0x4,
	MNOTE_CANON_TAG_PANORAMA = 0x5,
	MNOTE_CANON_TAG_IMAGE_TYPE = 0x6,
	MNOTE_CANON_TAG_FIRMWARE = 0x7,
	MNOTE_CANON_TAG_IMAGE_NUMBER = 0x8,
	MNOTE_CANON_TAG_OWNER = 0x9,
	MNOTE_CANON_TAG_UNKNOWN_10 = 0xa,
	MNOTE_CANON_TAG_SERIAL_NUMBER = 0xc,
	MNOTE_CANON_TAG_CAMERA_INFO = 0xd,
	MNOTE_CANON_TAG_CUSTOM_FUNCS = 0xf,
	MNOTE_CANON_TAG_MODEL_ID = 0x10,
	MNOTE_CANON_TAG_COLOR_INFORMATION = 0xa0,
	MNOTE_CANON_TAG_LENS = 0x0095
};

//enum
//{
//	EXIF_TAG_GPS_VERSION_ID = 0x0000,
//	EXIF_TAG_GPS_LATITUDE_REF = 0x0001,
//	//  INTEROPERABILITY_INDEX   
//	EXIF_TAG_GPS_LATITUDE = 0x0002,
//	//  INTEROPERABILITY_VERSION 
//	EXIF_TAG_GPS_LONGITUDE_REF = 0x0003,
//	EXIF_TAG_GPS_LONGITUDE = 0x0004,
//	EXIF_TAG_GPS_ALTITUDE_REF = 0x0005,
//	EXIF_TAG_GPS_ALTITUDE = 0x0006,
//	EXIF_TAG_GPS_TIME_STAMP = 0x0007,
//	EXIF_TAG_GPS_SATELLITES = 0x0008,
//	EXIF_TAG_GPS_STATUS = 0x0009,
//	EXIF_TAG_GPS_MEASURE_MODE = 0x000a,
//	EXIF_TAG_GPS_DOP = 0x000b,
//	EXIF_TAG_GPS_SPEED_REF = 0x000c,
//	EXIF_TAG_GPS_SPEED = 0x000d,
//	EXIF_TAG_GPS_TRACK_REF = 0x000e,
//	EXIF_TAG_GPS_TRACK = 0x000f,
//	EXIF_TAG_GPS_IMG_DIRECTION_REF = 0x0010,
//	EXIF_TAG_GPS_IMG_DIRECTION = 0x0011,
//	EXIF_TAG_GPS_MAP_DATUM = 0x0012,
//	EXIF_TAG_GPS_DEST_LATITUDE_REF = 0x0013,
//	EXIF_TAG_GPS_DEST_LATITUDE = 0x0014,
//	EXIF_TAG_GPS_DEST_LONGITUDE_REF = 0x0015,
//	EXIF_TAG_GPS_DEST_LONGITUDE = 0x0016,
//	EXIF_TAG_GPS_DEST_BEARING_REF = 0x0017,
//	EXIF_TAG_GPS_DEST_BEARING = 0x0018,
//	EXIF_TAG_GPS_DEST_DISTANCE_REF = 0x0019,
//	EXIF_TAG_GPS_DEST_DISTANCE = 0x001a,
//	EXIF_TAG_GPS_PROCESSING_METHOD = 0x001b,
//	EXIF_TAG_GPS_AREA_INFORMATION = 0x001c,
//	EXIF_TAG_GPS_DATE_STAMP = 0x001d,
//	EXIF_TAG_GPS_DIFFERENTIAL = 0x001e
//}
//GpsTag;

df_assert_pod(metadata_exif::urational32_t);
df_assert_pod(metadata_exif::srational32_t);

enum maker_note_type
{
	EXIF_DATA_TYPE_MAKER_NOTE_NONE,
	EXIF_DATA_TYPE_MAKER_NOTE_CANON,
	EXIF_DATA_TYPE_MAKER_NOTE_OLYMPUS,
	EXIF_DATA_TYPE_MAKER_NOTE_PENTAX,
	EXIF_DATA_TYPE_MAKER_NOTE_NIKON,
	EXIF_DATA_TYPE_MAKER_NOTE_CASIO,
	EXIF_DATA_TYPE_MAKER_NOTE_FUJI
};


enum class tag_type
{
	exif,
	gps,
	canon
};

static bool is_junk(const uint8_t* p, const uint32_t s)
{
	static constexpr uint8_t junk_marker[] = {0x12, 0xf8, 0x0f, 0x3b,};
	return s >= 4 && memcmp(p, junk_marker, 4) == 0;
}

// Vendors (e.g. Samsung SM-G900F) pack binary blobs into text tags such as UserComment,
// sometimes behind a valid "ASCII\0\0\0" character code. Control characters other than
// tab, newline and return prove the payload is not readable text.
static bool is_binary_text(const std::string_view text)
{
	for (const auto c : text)
	{
		const auto u = static_cast<uint8_t>(c);
		if (u < 0x20 && u != '\t' && u != '\n' && u != '\r') return true;
		if (u == 0x7f) return true;
	}

	return false;
}

static str::cached cache_text(const std::string_view text)
{
	// Padding must go before the scan; trailing nuls and spaces are normal in EXIF records.
	const auto trimmed = str::trim(text);
	if (trimmed.empty() || is_binary_text(trimmed)) return {};
	return str::strip_and_cache(trimmed);
}

// Samsung ISP writes a "JKJK" scene/exposure block map plus "FAFA" focus records into text tags.
// Naming the block is more use in the verbose listing than an empty or truncated value.
static std::string_view vendor_blob_name(const uint8_t* p, const uint32_t len)
{
	if (!p) return {};
	const std::string_view sv{std::bit_cast<const char*>(p), len};
	if (sv.find("JKJK") != std::string_view::npos) return "Samsung camera debug data";
	return {};
}

class exif_data_buffer
{
	const uint8_t* const _data;
	const size_t _size;
	const bool _is_intel;

public:
	exif_data_buffer(const df::cspan cs, const bool isIntel) : _data(cs.data), _size(cs.size), _is_intel(isIntel)
	{
	}

	bool is_overflow(const size_t i, const size_t size) const
	{
		// Guard against additive overflow on untrusted offsets.
		if (size > _size) return true;
		return i > _size - size;
	}

	const uint8_t* data() const
	{
		return _data;
	}

	size_t size() const
	{
		return _size;
	}

	const uint8_t* data(const uint32_t i, const uint32_t size) const
	{
		if (is_overflow(i, size)) return nullptr;
		return _data + i;
	}

	bool is_intel() const
	{
		return _is_intel;
	}

	str::cached cached_string(const uint32_t i, uint32_t len, bool probablyUnicode) const
	{
		if (!is_overflow(i, len))
		{
			const auto* p = _data + i;

			if (len > 7 && memcmp(p, "UNICODE", 7) == 0)
			{
				if (len > 9)
				{
					constexpr size_t offset = 8;
					p = p + offset;
					const auto length = len - offset;
					// Exif spells this UTF-16, so the code unit is two bytes on every platform.
					// wchar_t is only two on Windows.
					const auto char_length = length / sizeof(char16_t);

					if (_is_intel)
					{
						// p is a file offset, so it carries no char16_t alignment of its own.
						std::u16string aligned(char_length, u'\0');
						std::memcpy(aligned.data(), p, char_length * sizeof(char16_t));
						return str::strip_and_cache(aligned);
					}

					const auto buffer = df::unique_alloc<uint8_t>(length + 2);
					auto* const dst = buffer.get();
					size_t pos = 0;


					// length is not forced even, so stopping at pos < length could read one byte
					// past the tag payload on a malformed record.
					while (pos + 1 < length)
					{
						dst[pos] = p[pos + 1];
						dst[pos + 1] = p[pos];
						pos += 2;
					}

					std::u16string aligned(char_length, u'\0');
					std::memcpy(aligned.data(), dst, char_length * sizeof(char16_t));
					return str::strip_and_cache(aligned);
				}
			}
			else if (len > 5 && memcmp(p, "ASCII", 5) == 0)
			{
				if (len > 8)
				{
					p = p + 8;
					// The remaining length is what is left after the 8 byte type prefix; passing the
					// original len let the junk marker compare read past the payload.
					return is_junk(p, len - 8)
						       ? str::cached{}
						       : cache_text({std::bit_cast<const char*>(p), len - 8});
				}
			}
			else if (is_junk(p, len))
			{
				return {};
			}
			else if (str::is_utf16(p, len))
			{
				const auto char_length = len / 2;
				std::u16string aligned(char_length, u'\0');
				std::memcpy(aligned.data(), p, char_length * sizeof(char16_t));
				return str::strip_and_cache(aligned);
			}
			else if (str::is_utf8(std::bit_cast<const char*>(p), len))
			{
				return cache_text({std::bit_cast<const char*>(p), len});
			}
			else
			{
				return "?"_c;
			}
		}

		return {};
	}

	// Every offset here comes from the file, so no read may assume the address is aligned.
	uint16_t get_uint16(const uint32_t i) const
	{
		if (_is_intel)
		{
			uint16_t n;
			std::memcpy(&n, _data + i, sizeof(n));
			return n;
		}
		return df::byteswap16(_data + i);
	}

	uint32_t get_uint32(const uint32_t i) const
	{
		if (_is_intel)
		{
			uint32_t n;
			std::memcpy(&n, _data + i, sizeof(n));
			return n;
		}
		return df::byteswap32(_data + i);
	}

	int16_t get_int16(const uint32_t i) const
	{
		if (_is_intel)
		{
			int16_t n;
			std::memcpy(&n, _data + i, sizeof(n));
			return n;
		}
		return static_cast<int16_t>(df::byteswap16(_data + i));
	}

	metadata_exif::srational32_t get_srational(const uint32_t i) const
	{
		return metadata_exif::srational32_t(get_uint32(i), get_uint32(i + 4));
	}

	metadata_exif::urational32_t get_urational(const uint32_t i) const
	{
		return metadata_exif::urational32_t(get_uint32(i), get_uint32(i + 4));
	}
};

struct exif_dir_entry
{
	tag_type _tag_type;
	exif_data_buffer& _data;
	uint32_t _offset;
	int _tag;
	exif_format _format;
	uint32_t _components;

	exif_dir_entry() = delete;

	exif_dir_entry(const tag_type tt, exif_data_buffer& data, const uint32_t offset) :
		_tag_type(tt),
		_data(data),
		_offset(offset),
		_tag(_data.get_uint16(offset)),
		_format(static_cast<exif_format>(_data.get_uint16(offset + 2))),
		_components(_data.get_uint32(offset + 4))
	{
	}

	bool is_valid() const
	{
		return _format < NUM_FORMATS && _components > 0 && !_data.is_overflow(data_offset(), size());
	}

	uint32_t size() const
	{
		if (_format >= NUM_FORMATS) return 0;
		return _components * bytes_per_format[_format];
	}

	const uint8_t* data(const uint32_t size) const
	{
		return _data.data(get_uint32(), size);
	}

	uint32_t data_offset() const
	{
		const auto s = size();
		if (s <= 4) return _offset + 8u;
		return _data.get_uint32(_offset + 8);
	}

	int get_int16(const uint32_t i) const
	{
		const auto o = data_offset() + i * 2;
		if (_data.is_overflow(o, 2u)) return 0;
		return _data.get_int16(o);
	}

	uint32_t get_uint16(const uint32_t i) const
	{
		const auto o = data_offset() + i * 2;
		if (_data.is_overflow(o, 2)) return 0;
		return _data.get_uint16(o);
	}

	uint32_t get_uint32(const uint32_t i) const
	{
		const auto o = data_offset() + i * 4;
		if (_data.is_overflow(o, 4)) return 0;
		return _data.get_uint32(o);
	}

	int32_t get_int16() const
	{
		return _data.get_int16(_offset + 8);
	}

	uint32_t get_uint16() const
	{
		return _data.get_uint16(_offset + 8);
	}

	uint32_t get_uint32() const
	{
		return _data.get_uint32(_offset + 8);
	}

	str::cached get_cached_string(const bool probably_unicode) const
	{
		if (FMT_STRING != _format && FMT_UNDEFINED != _format)
			return "?"_c;

		return _data.cached_string(data_offset(), size(), probably_unicode);
	}

	metadata_exif::srational32_t get_srational(const uint32_t i = 0) const
	{
		constexpr uint32_t len = 2 * sizeof(int);
		const auto offset = data_offset() + i * len;
		if (_data.is_overflow(offset, len)) return {};
		return _data.get_srational(offset);
	}

	metadata_exif::urational32_t get_urational(const uint32_t i = 0) const
	{
		constexpr uint32_t len = 2 * sizeof(int);
		const auto offset = data_offset() + i * len;
		if (_data.is_overflow(offset, len)) return {};
		return _data.get_urational(offset);
	}

	std::string_view text() const
	{
		const auto len = size();
		const auto* const sz = std::bit_cast<const char*>(_data.data(data_offset(), len));
		return sz ? std::string_view{sz, len} : std::string_view{};
	}

	uint8_t get_uint8() const
	{
		const auto t = text();
		return t.empty() ? 0u : static_cast<uint8_t>(t.front());
	}

	bool is_intel() const
	{
		return _data.is_intel();
	}
};


class exif_parser
{
	exif_data_buffer& _data;
	std::function<void(exif_dir_entry&)> _handler;

	uint32_t _dir_offsets_size = 0;
	uint32_t _dir_offsets[32] = {};

	uint32_t _thumbnail_offset = 0;
	uint32_t _thumbnail_length = 0;

	str::cached _make = {};

public:
	exif_parser(exif_data_buffer& d, std::function<void(exif_dir_entry&)> h) : _data(d), _handler(std::move(h))
	{
	}

	void parse()
	{
		if (_data.get_uint16(2) == 0x002a)
		{
			// IFD 0
			const auto ifd0_offset = _data.get_uint32(4);
			parse_dir(ifd0_offset, tag_type::exif);

			// IFD 1
			if (!_data.is_overflow(ifd0_offset, 12))
			{
				const auto entryCount = _data.get_uint16(ifd0_offset);

				const auto ifd1_entry = ifd0_offset + 2 + 12 * entryCount;

				if (!_data.is_overflow(ifd1_entry, 12))
				{
					const auto ifd1_offset = _data.get_uint32(ifd1_entry);

					if (ifd1_offset)
					{
						parse_dir(ifd1_offset, tag_type::exif);
					}
				}
			}
		}
	}

	void parse_dir(const uint32_t offset, const tag_type tagType)
	{
		if (!_data.is_overflow(offset, 2))
		{
			for (auto i = 0u; i < _dir_offsets_size; i++)
			{
				if (_dir_offsets[i] == offset)
					return;
			}

			// Bounds check: a crafted file with many distinct IFD offsets must not
			// overflow the fixed-size visited-offset array.
			if (_dir_offsets_size >= std::size(_dir_offsets))
				return;

			_dir_offsets[_dir_offsets_size++] = offset;
			const auto entry_count = _data.get_uint16(offset);

			for (auto i = 0u; i < entry_count; ++i)
			{
				const auto pos = offset + 2 + 12 * i;

				if (!_data.is_overflow(pos, 12))
				{
					exif_dir_entry tag(tagType, _data, pos);

					if (tag.is_valid())
					{
						process_tag(tag);
					}
					else
					{
					}
				}
			}
		}
	}

private:
	maker_note_type identify_maker_note(const exif_dir_entry& entry) const
	{
		const auto size = entry.size();
		const auto* const data = entry.data(4);

		if (data)
		{
			// Olympus & Nikon & Sanyo 
			if (size >= 8 &&
				(!memcmp(data, "OLYMP", 6) ||
					!memcmp(data, "OLYMPUS", 8) ||
					!memcmp(data, "SANYO", 6) ||
					!memcmp(data, "EPSON", 6) ||
					!memcmp(data, "Nikon", 6)))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_OLYMPUS;
			}

			if (is_empty(_make))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_NONE;
			}

			// Canon 
			if (icmp(_make, "Canon") == 0)
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_CANON;
			}

			// Pentax & some variant of Nikon 
			if (size >= 2 && data[0] == 0x00 && data[1] == 0x1b)
			{
				if (icmp(_make, "Nikon") == 0)
				{
					return EXIF_DATA_TYPE_MAKER_NOTE_NIKON;
				}
				return EXIF_DATA_TYPE_MAKER_NOTE_PENTAX;
			}
			if (size >= 8 && !memcmp(data, "AOC", 4))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_PENTAX;
			}
			if (size >= 8 && !memcmp(data, "QVC", 4))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_CASIO;
			}
			if (size >= 12 && !memcmp(data, "FUJIFILM", 8))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_FUJI;
			}
		}

		return EXIF_DATA_TYPE_MAKER_NOTE_NONE;
	}

	void process_tag(exif_dir_entry& entry)
	{
		switch (entry._tag)
		{
		case EXIF_TAG_EXIF_IFD_POINTER:
			// IFD EXIF
			parse_dir(entry.get_uint32(), tag_type::exif);
			break;

		case EXIF_TAG_GPS_INFO_IFD_POINTER:
			// IFD GPS
			parse_dir(entry.get_uint32(), tag_type::gps);
			break;

		case EXIF_TAG_INTEROPERABILITY_IFD_POINTER:
			// IFD INTEROPERABILITY	
			parse_dir(entry.get_uint32(), tag_type::exif);
			break;

		case EXIF_TAG_MAKER_NOTE:
			// IFD INTEROPERABILITY					
			switch (identify_maker_note(entry))
			{
			case EXIF_DATA_TYPE_MAKER_NOTE_CANON:
				parse_dir(entry.get_uint32(), tag_type::canon);
				break;
			}
			break;

		case EXIF_TAG_JPEG_INTERCHANGE_FORMAT:
			_thumbnail_offset = entry.get_uint32();
			break;

		case EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH:
			_thumbnail_length = entry.get_uint32();
			break;

		default:

			if (entry._tag == EXIF_TAG_MAKE)
			{
				_make = entry.get_cached_string(false);
			}

			_handler(entry);
			break;
		}
	}
};

static void exif_enumerate(const std::function<void(exif_dir_entry&)>& h, const df::cspan data)
{
	if (data > 16)
	{
		// We should be past the header
		df::assert_true(!is_exif_signature(data));

		uint16_t ended;
		std::memcpy(&ended, data.data, sizeof(ended));

		if (ended == 0x4949)
		{
			exif_data_buffer buffer(data, true);
			exif_parser(buffer, h).parse();
		}
		else if (ended == 0x4D4D)
		{
			exif_data_buffer buffer(data, false);
			exif_parser(buffer, h).parse();
		}
	}
}

class canon_lenses
{
	df::hash_map<int, const char*> _choices = {
		{1, "Canon EF 50mm f/1.8"},
		{2, "Canon EF 28mm f/2.8"},
		{3, "Canon EF 135mm f/2.8 Soft"},
		{4, "Canon EF 35-105mm f/3.5-4.5"}, // 0
		{4, "Sigma UC Zoom 35-135mm f/4-5.6"}, // 1
		{5, "Canon EF 35-70mm f/3.5-4.5"},
		{6, "Canon EF 28-70mm f/3.5-4.5"}, // 0
		{6, "Sigma 18-50mm f/3.5-5.6 DC"}, // 1
		{6, "Sigma 18-125mm f/3.5-5.6 DC IF ASP"}, // 2
		{6, "Tokina AF193-2 19-35mm f/3.5-4.5"}, // 3
		{6, "Sigma 28-80mm f/3.5-5.6 II Macro"}, // 4
		{7, "Canon EF 100-300mm f/5.6L"},
		{8, "Canon EF 100-300mm f/5.6"}, // 0
		{8, "Sigma 70-300mm f/4-5.6 [APO] DG Macro"}, // 1
		{8, "Tokina AT-X 242 AF 24-200mm f/3.5-5.6"}, // 2
		{9, "Canon EF 70-210mm f/4"}, // 0
		{9, "Sigma 55-200mm f/4-5.6 DC"}, // 1
		{10, "Canon EF 50mm f/2.5 Macro"}, // 0
		{10, "Sigma 50mm f/2.8 EX"}, // 1
		{10, "Sigma 28mm f/1.8"}, // 2
		{10, "Sigma 105mm f/2.8 Macro EX"}, // 3
		{10, "Sigma 70mm f/2.8 EX DG Macro EF"}, // 4
		{11, "Canon EF 35mm f/2"},
		{13, "Canon EF 15mm f/2.8 Fisheye"},
		{14, "Canon EF 50-200mm f/3.5-4.5L"},
		{15, "Canon EF 50-200mm f/3.5-4.5"},
		{16, "Canon EF 35-135mm f/3.5-4.5"},
		{17, "Canon EF 35-70mm f/3.5-4.5A"},
		{18, "Canon EF 28-70mm f/3.5-4.5"},
		{20, "Canon EF 100-200mm f/4.5A"},
		{21, "Canon EF 80-200mm f/2.8L"},
		{22, "Canon EF 20-35mm f/2.8L"}, // 0
		{22, "Tokina AT-X 280 AF PRO 28-80mm f/2.8 Aspherical"}, // 1
		{23, "Canon EF 35-105mm f/3.5-4.5"},
		{24, "Canon EF 35-80mm f/4-5.6 Power Zoom"},
		{25, "Canon EF 35-80mm f/4-5.6 Power Zoom"},
		{26, "Canon EF 100mm f/2.8 Macro"}, // 0
		{26, "Cosina 100mm f/3.5 Macro AF"}, // 1
		{26, "Tamron SP AF 90mm f/2.8 Di Macro"}, // 2
		{26, "Tamron SP AF 180mm f/3.5 Di Macro"}, // 3
		{26, "Carl Zeiss Planar T* 50mm f/1.4"}, // 4
		{27, "Canon EF 35-80mm f/4-5.6"},
		{28, "Canon EF 80-200mm f/4.5-5.6"}, // 0
		{28, "Tamron SP AF 28-105mm f/2.8 LD Aspherical IF"}, // 1
		{28, "Tamron SP AF 28-75mm f/2.8 XR Di LD Aspherical [IF] Macro"}, // 2
		{28, "Tamron AF 70-300mm f/4.5-5.6 Di LD 1:2 Macro Zoom"}, // 3
		{28, "Tamron AF Aspherical 28-200mm f/3.8-5.6"}, // 4
		{29, "Canon EF 50mm f/1.8 MkII"},
		{30, "Canon EF 35-105mm f/4.5-5.6"},
		{31, "Canon EF 75-300mm f/4-5.6"}, // 0
		{31, "Tamron SP AF 300mm f/2.8 LD IF"}, // 1
		{32, "Canon EF 24mm f/2.8"}, // 0
		{32, "Sigma 15mm f/2.8 EX Fisheye"}, // 1
		{33, "Voigtlander or Carl Zeiss Lens"}, // 0
		{33, "Voigtlander Ultron 40mm f/2 SLII Aspherical"}, // 1
		{33, "Carl Zeiss Distagon 15mm T* f/2.8 ZE"}, // 2
		{33, "Carl Zeiss Distagon 18mm T* f/3.5 ZE"}, // 3
		{33, "Carl Zeiss Distagon 21mm T* f/2.8 ZE"}, // 4
		{33, "Carl Zeiss Distagon 28mm T* f/2 ZE"}, // 5
		{33, "Carl Zeiss Distagon 35mm T* f/2 ZE"}, // 6
		{33, "Carl Zeiss Planar 50mm T* f/1.4 ZE"}, // 7
		{35, "Canon EF 35-80mm f/4-5.6"},
		{36, "Canon EF 38-76mm f/4.5-5.6"},
		{37, "Canon EF 35-80mm f/4-5.6"}, // 0
		{37, "Tamron 70-200mm f/2.8 Di LD IF Macro"}, // 1
		{37, "Tamron AF 28-300mm f/3.5-6.3 XR Di VC LD Aspherical [IF] Macro Model A20"}, // 2
		{37, "Tamron SP AF 17-50mm f/2.8 XR Di II VC LD Aspherical [IF] "}, // 3
		{37, "Tamron AF 18-270mm f/3.5-6.3 Di II VC LD Aspherical [IF] Macro"}, // 4
		{38, "Canon EF 80-200mm f/4.5-5.6"},
		{39, "Canon EF 75-300mm f/4-5.6"},
		{40, "Canon EF 28-80mm f/3.5-5.6"},
		{41, "Canon EF 28-90mm f/4-5.6"},
		{42, "Canon EF 28-200mm f/3.5-5.6"}, // 0
		{42, "Tamron AF 28-300mm f/3.5-6.3 XR Di VC LD Aspherical [IF] Macro Model A20"}, // 1
		{43, "Canon EF 28-105mm f/4-5.6"},
		{44, "Canon EF 90-300mm f/4.5-5.6"},
		{45, "Canon EF-S 18-55mm f/3.5-5.6"},
		{46, "Canon EF 28-90mm f/4-5.6"},
		{48, "Canon EF-S 18-55mm f/3.5-5.6 IS"},
		{49, "Canon EF-S 55-250mm f/4-5.6 IS"},
		{50, "Canon EF-S 18-200mm f/3.5-5.6 IS"},
		{51, "Canon EF-S 18-135mm f/3.5-5.6 IS"},
		{52, "Canon EF-S 18-55mm f/3.5-5.6 IS II"},
		{53, "Canon EF-S 18-55mm f/3.5-5.6 III"},
		{54, "Canon EF-S 55-250mm f/4-5.6 IS II"},
		{94, "Canon TS-E 17mm f/4L"},
		{95, "Canon TS-E 24.0mm f/3.5 L II"},
		{124, "Canon MP-E 65mm f/2.8 1-5x Macro Photo"},
		{125, "Canon TS-E 24mm f/3.5L"},
		{126, "Canon TS-E 45mm f/2.8"},
		{127, "Canon TS-E 90mm f/2.8"},
		{129, "Canon EF 300mm f/2.8L"},
		{130, "Canon EF 50mm f/1.0L"},
		{131, "Canon EF 28-80mm f/2.8-4L"}, // 0
		{131, "Sigma 8mm f/3.5 EX DG Circular Fisheye"}, // 1
		{131, "Sigma 17-35mm f/2.8-4 EX DG Aspherical HSM"}, // 2
		{131, "Sigma 17-70mm f/2.8-4.5 DC Macro"}, // 3
		{131, "Sigma APO 50-150mm f/2.8 EX DC HSM"}, // 4
		{131, "Sigma APO 120-300mm f/2.8 EX DG HSM"}, // 5
		{131, "Sigma 4.5mm F2.8 EX DC HSM Circular Fisheye"}, // 6
		{131, "Sigma 70-200mm f/2.8 APO EX HSM"}, // 7
		{132, "Canon EF 1200mm f/5.6L"},
		{134, "Canon EF 600mm f/4L IS"},
		{135, "Canon EF 200mm f/1.8L"},
		{136, "Canon EF 300mm f/2.8L"},
		{137, "Canon EF 85mm f/1.2L"}, // 0
		{137, "Sigma 18-50mm f/2.8-4.5 DC OS HSM"}, // 1
		{137, "Sigma 50-200mm f/4-5.6 DC OS HSM"}, // 2
		{137, "Sigma 18-250mm f/3.5-6.3 DC OS HSM"}, // 3
		{137, "Sigma 24-70mm f/2.8 IF EX DG HSM"}, // 4
		{137, "Sigma 18-125mm f/3.8-5.6 DC OS HSM"}, // 5
		{137, "Sigma 17-70mm f/2.8-4 DC Macro OS HSM"}, // 6
		{137, "Sigma 17-50mm f/2.8 OS HSM"}, // 7
		{137, "Sigma 18-200mm f/3.5-6.3 II DC OS HSM"}, // 8
		{137, "Tamron AF 18-270mm f/3.5-6.3 Di II VC PZD"}, // 9
		{137, "Sigma 8-16mm f/4.5-5.6 DC HSM"}, // 10
		{137, "Tamron SP 17-50mm f/2.8 XR Di II VC"}, // 11
		{137, "Tamron SP 60mm f/2 Macro Di II"}, // 12
		{137, "Sigma 10-20mm f/3.5 EX DC HSM"}, // 13
		{137, "Tamron SP 24-70mm f/2.8 Di VC USD"}, // 14
		{137, "Sigma 18-35mm f/1.8 DC HSM"}, // 15
		{137, "Sigma 12-24mm f/4.5-5.6 DG HSM II"}, // 16
		{138, "Canon EF 28-80mm f/2.8-4L"},
		{139, "Canon EF 400mm f/2.8L"},
		{140, "Canon EF 500mm f/4.5L"},
		{141, "Canon EF 500mm f/4.5L"},
		{142, "Canon EF 300mm f/2.8L IS"},
		{143, "Canon EF 500mm f/4L IS"},
		{144, "Canon EF 35-135mm f/4-5.6 USM"},
		{145, "Canon EF 100-300mm f/4.5-5.6 USM"},
		{146, "Canon EF 70-210mm f/3.5-4.5 USM"},
		{147, "Canon EF 35-135mm f/4-5.6 USM"},
		{148, "Canon EF 28-80mm f/3.5-5.6 USM"},
		{149, "Canon EF 100mm f/2 USM"},
		{150, "Canon EF 14mm f/2.8L"}, // 0
		{150, "Sigma 20mm EX f/1.8"}, // 1
		{150, "Sigma 30mm f/1.4 DC HSM"}, // 2
		{150, "Sigma 24mm f/1.8 DG Macro EX"}, // 3
		{151, "Canon EF 200mm f/2.8L"},
		{152, "Canon EF 300mm f/4L"}, // 0
		{152, "Sigma 12-24mm f/4.5-5.6 EX DG ASPHERICAL HSM"}, // 1
		{152, "Sigma 14mm f/2.8 EX Aspherical HSM"}, // 2
		{152, "Sigma 10-20mm f/4-5.6"}, // 3
		{152, "Sigma 100-300mm f/4"}, // 4
		{153, "Canon EF 35-350mm f/3.5-5.6L"}, // 0
		{153, "Sigma 50-500mm f/4-6.3 APO HSM EX"}, // 1
		{153, "Tamron AF 28-300mm f/3.5-6.3 XR LD Aspherical [IF] Macro"}, // 2
		{153, "Tamron AF 18-200mm f/3.5-6.3 XR Di II LD Aspherical [IF] Macro Model A14"}, // 3
		{153, "Tamron 18-250mm f/3.5-6.3 Di II LD Aspherical [IF] Macro"}, // 4
		{154, "Canon EF 20mm f/2.8 USM"},
		{155, "Canon EF 85mm f/1.8 USM"},
		{156, "Canon EF 28-105mm f/3.5-4.5 USM"}, // 0
		{156, "Tamron SP AF 70-300mm f/4-5.6 Di VC USD"}, // 1
		{160, "Canon EF 20-35mm f/3.5-4.5 USM"}, // 0
		{160, "Tamron AF 19-35mm f/3.5-4.5"}, // 1
		{160, "Tokina AT-X 124 AF 12-24mm f/4 DX"}, // 2
		{160, "Tokina AT-X 107 AF DX Fish-eye 10-17mm f/3.5-4.5"}, // 3
		{160, "Tokina AT-X 116 PRO DX AF 11-16mm f/2.8"}, // 4
		{161, "Canon EF 28-70mm f/2.8L"}, // 0
		{161, "Sigma 24-70mm EX f/2.8"}, // 1
		{161, "Sigma 28-70mm f/2.8 EX"}, // 2
		{161, "Tamron AF 17-50mm f/2.8 Di-II LD Aspherical"}, // 3
		{161, "Tamron 90mm f/2.8"}, // 4
		{161, "Sigma 24-60mm f/2.8 EX DG"}, // 5
		{162, "Canon EF 200mm f/2.8L"},
		{163, "Canon EF 300mm f/4L"},
		{164, "Canon EF 400mm f/5.6L"},
		{165, "Canon EF 70-200mm f/2.8 L"},
		{166, "Canon EF 70-200mm f/2.8 L + 1.4x"},
		{167, "Canon EF 70-200mm f/2.8 L + 2x"},
		{168, "Canon EF 28mm f/1.8 USM"},
		{169, "Canon EF 17-35mm f/2.8L"}, // 0
		{169, "Sigma 18-200mm f/3.5-6.3 DC OS"}, // 1
		{169, "Sigma 15-30mm f/3.5-4.5 EX DG Aspherical"}, // 2
		{169, "Sigma 18-50mm f/2.8 Macro"}, // 3
		{169, "Sigma 50mm f/1.4 EX DG HSM"}, // 4
		{169, "Sigma 85mm f/1.4 EX DG HSM"}, // 5
		{169, "Sigma 30mm f/1.4 EX DC HSM"}, // 6
		{169, "Sigma 35mm f/1.4 DG HSM"}, // 7
		{170, "Canon EF 200mm f/2.8L II"},
		{171, "Canon EF 300mm f/4L"},
		{172, "Canon EF 400mm f/5.6L"},
		{173, "Canon EF 180mm Macro f/3.5L"}, // 0
		{173, "Sigma 180mm EX HSM Macro f/3.5"}, // 1
		{173, "Sigma APO Macro 150mm f/3.5 EX DG IF HSM"}, // 2
		{174, "Canon EF 135mm f/2L"}, // 0
		{174, "Sigma 70-200mm f/2.8 EX DG APO OS HSM"}, // 1
		{174, "Sigma 50-500mm f/4.5-6.3 APO DG OS HSM"}, // 2
		{174, "Sigma 150-500mm f/5-6.3 APO DG OS HSM"}, // 3
		{175, "Canon EF 400mm f/2.8L"},
		{176, "Canon EF 24-85mm f/3.5-4.5 USM"},
		{177, "Canon EF 300mm f/4L IS"},
		{178, "Canon EF 28-135mm f/3.5-5.6 IS"},
		{179, "Canon EF 24mm f/1.4L"},
		{180, "Canon EF 35mm f/1.4L"},
		{181, "Canon EF 100-400mm f/4.5-5.6L IS + 1.4x"},
		{182, "Canon EF 100-400mm f/4.5-5.6L IS + 2x"},
		{183, "Canon EF 100-400mm f/4.5-5.6L IS"}, // 0
		{183, "Sigma 150mm f/2.8 EX DG OS HSM APO Macro"}, // 1
		{183, "Sigma 105mm f/2.8 EX DG OS HSM Macro"}, // 2
		{184, "Canon EF 400mm f/2.8L + 2x"},
		{185, "Canon EF 600mm f/4L IS"},
		{186, "Canon EF 70-200mm f/4L"},
		{187, "Canon EF 70-200mm f/4L + 1.4x"},
		{188, "Canon EF 70-200mm f/4L + 2x"},
		{189, "Canon EF 70-200mm f/4L + 2.8x"},
		{190, "Canon EF 100mm f/2.8 Macro"},
		{191, "Canon EF 400mm f/4 DO IS"},
		{193, "Canon EF 35-80mm f/4-5.6 USM"},
		{194, "Canon EF 80-200mm f/4.5-5.6 USM"},
		{195, "Canon EF 35-105mm f/4.5-5.6 USM"},
		{196, "Canon EF 75-300mm f/4-5.6 USM"},
		{197, "Canon EF 75-300mm f/4-5.6 IS USM"},
		{198, "Canon EF 50mm f/1.4 USM"}, // 0
		{198, "Zeiss Otus 55mm f/1.4 ZE"}, // 1
		{199, "Canon EF 28-80mm f/3.5-5.6 USM"},
		{200, "Canon EF 75-300mm f/4-5.6 USM"},
		{201, "Canon EF 28-80mm f/3.5-5.6 USM"},
		{202, "Canon EF 28-80mm f/3.5-5.6 USM IV"},
		{208, "Canon EF 22-55mm f/4-5.6 USM"},
		{209, "Canon EF 55-200mm f/4.5-5.6"},
		{210, "Canon EF 28-90mm f/4-5.6 USM"},
		{211, "Canon EF 28-200mm f/3.5-5.6 USM"},
		{212, "Canon EF 28-105mm f/4-5.6 USM"},
		{213, "Canon EF 90-300mm f/4.5-5.6 USM"},
		{214, "Canon EF-S 18-55mm f/3.5-5.6 USM"},
		{215, "Canon EF 55-200mm f/4.5-5.6 II USM"},
		{224, "Canon EF 70-200mm f/2.8L IS"},
		{225, "Canon EF 70-200mm f/2.8L IS + 1.4x"},
		{226, "Canon EF 70-200mm f/2.8L IS + 2x"},
		{227, "Canon EF 70-200mm f/2.8L IS + 2.8x"},
		{228, "Canon EF 28-105mm f/3.5-4.5 USM"},
		{229, "Canon EF 16-35mm f/2.8L"},
		{230, "Canon EF 24-70mm f/2.8L"},
		{231, "Canon EF 17-40mm f/4L"},
		{232, "Canon EF 70-300mm f/4.5-5.6 DO IS USM"},
		{233, "Canon EF 28-300mm f/3.5-5.6L IS"},
		{234, "Canon EF-S 17-85mm f4-5.6 IS USM"},
		{235, "Canon EF-S 10-22mm f/3.5-4.5 USM"},
		{236, "Canon EF-S 60mm f/2.8 Macro USM"},
		{237, "Canon EF 24-105mm f/4L IS"},
		{238, "Canon EF 70-300mm f/4-5.6 IS USM"},
		{239, "Canon EF 85mm f/1.2L II"},
		{240, "Canon EF-S 17-55mm f/2.8 IS USM"},
		{241, "Canon EF 50mm f/1.2L"},
		{242, "Canon EF 70-200mm f/4L IS"},
		{243, "Canon EF 70-200mm f/4L IS + 1.4x"},
		{244, "Canon EF 70-200mm f/4L IS + 2x"},
		{245, "Canon EF 70-200mm f/4L IS + 2.8x"},
		{246, "Canon EF 16-35mm f/2.8L II"},
		{247, "Canon EF 14mm f/2.8L II USM"},
		{248, "Canon EF 200mm f/2L IS"},
		{249, "Canon EF 800mm f/5.6L IS"},
		{250, "Canon EF 24 f/1.4L II"},
		{251, "Canon EF 70-200mm f/2.8L IS II USM"},
		{252, "Canon EF 70-200mm f/2.8L IS II USM + 1.4x"},
		{253, "Canon EF 70-200mm f/2.8L IS II USM + 2x"},
		{254, "Canon EF 100mm f/2.8L Macro IS USM"},
		{488, "Canon EF-S 15-85mm f/3.5-5.6 IS USM"},
		{489, "Canon EF 70-300mm f/4-5.6L IS USM"},
		{490, "Canon EF 8-15mm f/4L USM"},
		{491, "Canon EF 300mm f/2.8L IS II USM"},
		{492, "Canon EF 400mm f/2.8L IS II USM"},
		{493, "Canon EF 24-105mm f/4L IS USM"},
		{494, "Canon EF 600mm f/4.0L IS II USM"},
		{495, "Canon EF 24-70mm f/2.8L II USM"},
		{496, "Canon EF 200-400mm f/4L IS USM"},
		{502, "Canon EF 28mm f/2.8 IS USM"},
		{503, "Canon EF 24mm f/2.8 IS USM"},
		{504, "Canon EF 24-70mm f/4L IS USM"},
		{505, "Canon EF 35mm f/2 IS USM"},
		{4142, "Canon EF-S 18-135mm f/3.5-5.6 IS STM"},
		{4143, "Canon EF-M 18-55mm f/3.5-5.6 IS STM"},
		{4144, "Canon EF 40mm f/2.8 STM"},
		{4145, "Canon EF-M 22mm f/2 STM"},
		{4146, "Canon EF-S 18-55mm f/3.5-5.6 IS STM"},
		{4147, "Canon EF-M 11-22mm f/4-5.6 IS STM"}
	};

public:
	canon_lenses() = default;

	str::cached Lookup(const int id)
	{
		const auto found = _choices.find(id);
		str::cached result = {};

		if (found != _choices.end())
		{
			result = str::cache(found->second);
		}

		return result;
	}
};

exif_gps_coordinate_builder::exif_gps_coordinate_builder() : _south(NorthSouth::North),
                                                             _west(EastWest::East),
                                                             _latitude(invalid_coordinate),
                                                             _longitude(invalid_coordinate)
{
}

bool exif_gps_coordinate_builder::is_valid() const
{
	const auto alat = fabs(_latitude);
	const auto alon = fabs(_longitude);

	return alat < invalid_coordinate &&
		alon < invalid_coordinate &&
		alat > 0.0 &&
		alon > 0.0;
}

gps_coordinate exif_gps_coordinate_builder::build() const
{
	// std::fabs, not abs: these are doubles, and the C abs takes and returns int, so a coordinate
	// would arrive here as whole degrees with the minutes and seconds truncated away.
	auto lat = std::fabs(_latitude);
	auto lng = std::fabs(_longitude);

	if (_south == NorthSouth::South)
	{
		lat = 0.0 - lat;
	}

	if (_west == EastWest::West)
	{
		lng = 0.0 - lng;
	}

	const gps_coordinate coords(lat, lng);

	if (!coords.is_valid())
	{
		return {};
	}

	return coords;
}

class exif_camera_settings_processor
{
	prop::item_metadata& _metadata;
	exif_gps_coordinate_builder _gps_coordinate;
	bool _created_date_set;
	bool _created_date_is_original = false;
	bool _below_sea_level = false;
	float _gps_speed = 0.0f;
	float _speed_to_kmh = 1.0f;

public:
	explicit exif_camera_settings_processor(prop::item_metadata& pd) : _metadata(pd), _created_date_set(false)
	{
	}

	~exif_camera_settings_processor()
	{
		if (_gps_coordinate.is_valid())
		{
			_metadata.coordinate = _gps_coordinate.build();
		}

		if (_below_sea_level) _metadata.altitude = -_metadata.altitude;
		_metadata.gps_speed = _gps_speed * _speed_to_kmh;
	}

	void tag(const exif_dir_entry& entry)
	{
		switch (entry._tag_type)
		{
		case tag_type::exif:
			exif_tag(entry);
			break;
		case tag_type::gps:
			gps_tag(entry);
			break;
		case tag_type::canon:
			canon_tag(entry);
			break;
		}
	}

	void canon_tag(const exif_dir_entry& entry) const
	{
		static canon_lenses lenses;

		switch (entry._tag)
		{
		case MNOTE_CANON_TAG_SETTINGS_1:
			if (entry._components > 27 && entry._components < 1000) // Often corrupt by windows
			{
				auto flash = entry.get_int16(4);
				const auto lens = entry.get_int16(22);
				const auto high_focal_len = entry.get_int16(23);
				const auto low_focal_len = entry.get_int16(24);
				const auto focal_units = entry.get_int16(25);
				auto max_aperture = entry.get_int16(26);
				auto min_aperture = entry.get_int16(27);

				str::cached lens_text = {};

				if (lens > 0)
				{
					lens_text = lenses.Lookup(lens);
				}

				if (lens_text.is_empty() && (low_focal_len >= 0 || high_focal_len >= 0) && focal_units > 0)
				{
					const auto low = low_focal_len / static_cast<double>(focal_units);
					const auto high = high_focal_len / static_cast<double>(focal_units);

					if (std::fabs(low - high) < 0.1 || low < 0.1)
					{
						lens_text = str::cache(std::format("{:.1}mm", high));
					}
					else if (low >= 0.1 && high >= 0.1)
					{
						lens_text = str::cache(std::format("{:.1}f-{:.1}mm", low, high));
					}
				}

				if (!is_empty(lens_text))
				{
					_metadata.lens = lens_text;
				}
			}
			break;

		case MNOTE_CANON_TAG_LENS:
			{
				const auto text = entry.text();

				if (!str::is_empty(text))
				{
					_metadata.lens = str::strip_and_cache(text);
				}
			}
			break;
		}
	}

	static int safe_rating(int r)
	{
		if (r < 0)
		{
			return -1;
		}

		if (r > 5)
		{
			r = df::round_up(r, 20);
		}

		return std::clamp(r, 0, 5);
	}

	void exif_tag(const exif_dir_entry& entry)
	{
		switch (entry._tag)
		{
		case EXIF_TAG_ORIENTATION:
			_metadata.orientation = static_cast<ui::orientation>(entry.get_uint16());
			break;

		case EXIF_TAG_APERTURE_VALUE:
			if (df::is_zero(_metadata.f_number))
			{
				_metadata.f_number = static_cast<float>(prop::aperture_to_fstop(entry.get_urational().to_real()));
			}
			break;

		case EXIF_TAG_FNUMBER:
			_metadata.f_number = static_cast<float>(entry.get_urational().to_real());
			break;

		case EXIF_TAG_MAX_APERTURE_VALUE:
			// APEX aperture; only used as a fallback when no F-number is present.
			if (df::is_zero(_metadata.f_number))
			{
				_metadata.f_number = static_cast<float>(prop::aperture_to_fstop(entry.get_urational().to_real()));
			}
			break;

		case EXIF_TAG_EXPOSURE_TIME:
			_metadata.exposure_time = static_cast<float>(entry.get_srational().to_real());
			break;

		case EXIF_TAG_SHUTTER_SPEED_VALUE:
			// APEX shutter speed (exposure_time = 2^-value); fallback when no exposure time is present.
			if (df::is_zero(_metadata.exposure_time))
			{
				_metadata.exposure_time = static_cast<float>(std::pow(2.0, -entry.get_srational().to_real()));
			}
			break;

		case EXIF_TAG_ISO_SPEED_RATINGS:
			_metadata.iso_speed = entry.get_uint16();
			break;

		case EXIF_TAG_FOCAL_LENGTH:
			_metadata.focal_length = static_cast<float>(entry.get_urational().to_real());
			break;

		case EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM:
			_metadata.focal_length_35mm_equivalent = static_cast<int>(entry.get_uint16());
			break;

		case EXIF_TAG_MAKE:
			_metadata.camera_manufacturer = entry.get_cached_string(false);
			break;

		case EXIF_TAG_MODEL:
			_metadata.camera_model = entry.get_cached_string(false);
			break;

		case EXIF_TAG_SOFTWARE:
			if (is_empty(_metadata.encoder)) _metadata.encoder = entry.get_cached_string(false);
			break;

		case EXIF_TAG_ARTIST:
			if (is_empty(_metadata.artist)) _metadata.artist = entry.get_cached_string(false);
			break;

		case EXIF_TAG_LENS_MODEL:
			_metadata.lens = entry.get_cached_string(false);
			break;

		case EXIF_TAG_IMAGE_UNIQUE_ID:
			// This is not unique for most cameras
			// _metadata.unique_id = entry.get_cached_string(false);
			break;

		case EXIF_TAG_COPYRIGHT:
			_metadata.copyright_notice = entry.get_cached_string(false);
			break;

		case EXIF_TAG_IMAGE_DESCRIPTION:
			_metadata.description = entry.get_cached_string(false);
			break;

		case EXIF_TAG_USER_COMMENT:
			_metadata.comment = entry.get_cached_string(false);
			break;

		case EXIF_TAG_USER_COMMENT_XP:
			if (is_empty(_metadata.comment)) _metadata.comment = entry.get_cached_string(true);
			break;

		case EXIF_TAG_XP_TITLE:
			if (is_empty(_metadata.title)) _metadata.title = entry.get_cached_string(true);
			break;

		case EXIF_TAG_XP_AUTHOR:
			if (is_empty(_metadata.artist)) _metadata.artist = entry.get_cached_string(true);
			break;

		case EXIF_TAG_XP_KEYWORDS:
			if (is_empty(_metadata.tags)) _metadata.tags = entry.get_cached_string(true);
			break;

		case EXIF_TAG_XP_SUBJECT:
			if (is_empty(_metadata.description)) _metadata.description = entry.get_cached_string(true);
			break;

		case EXIF_TAG_IMAGE_RATING:
			_metadata.rating = safe_rating(entry.get_int16());
			break;

		case EXIF_TAG_IMAGE_RATING_PERCENT:
			_metadata.rating = safe_rating(df::round_up(entry.get_int16(), 20));
			break;

		case EXIF_TAG_DATE_TIME:
			{
				df::date_t ft;

				// DateTime (0x0132) is the file/container change date and lives in IFD0, which is
				// walked before the Exif SubIFD holding DateTimeOriginal (0x9003). Take it only as a
				// provisional value so the authoritative capture time can still override it (#184).
				if (!_created_date_set &&
					ft.parse_exif_date(entry.text()) &&
					ft.is_valid())
				{
					_metadata.created_exif = ft;
					_created_date_set = true;
				}
			}
			break;
		case EXIF_TAG_DATE_TIME_ORIGINAL:
			{
				df::date_t ft;

				// DateTimeOriginal is the authoritative capture time and always wins over a value
				// provisionally taken from DateTime, regardless of IFD enumeration order (#184).
				if (!_created_date_is_original &&
					ft.parse_exif_date(entry.text()) &&
					ft.is_valid())
				{
					_metadata.created_exif = ft;
					_created_date_set = true;
					_created_date_is_original = true;
				}
			}
			break;
		case EXIF_TAG_DATE_TIME_DIGITIZED:
			{
				df::date_t ft;

				if (ft.parse_exif_date(entry.text()) &&
					ft.is_valid())
				{
					_metadata.created_digitized = ft;
					_created_date_set = true;
				}
				break;
			}
		}
	}

	void gps_tag(const exif_dir_entry& entry)
	{
		switch (entry._tag)
		{
		case EXIF_TAG_GPS_LATITUDE:
			{
				const auto degrees = entry.get_urational(0);
				const auto minutes = entry.get_urational(1);
				const auto seconds = entry.get_urational(2);
				const auto latitude = gps_coordinate::dms_to_decimal(degrees.to_real(), minutes.to_real(),
				                                                     seconds.to_real());

				_gps_coordinate.latitude(latitude);
			}
			break;

		case EXIF_TAG_GPS_LATITUDE_REF:
			// 'N' or 'S'
			if (first_char_is(entry.text(), 'S'))
			{
				_gps_coordinate.latitude_north_south(exif_gps_coordinate_builder::NorthSouth::South);
			}
			else
			{
				_gps_coordinate.latitude_north_south(exif_gps_coordinate_builder::NorthSouth::North);
			}
			break;

		case EXIF_TAG_GPS_LONGITUDE:
			{
				const auto degrees = entry.get_urational(0);
				const auto minutes = entry.get_urational(1);
				const auto seconds = entry.get_urational(2);
				const auto longitude = gps_coordinate::
					dms_to_decimal(degrees.to_real(), minutes.to_real(), seconds.to_real());

				_gps_coordinate.longitude(longitude);
			}
			break;

		case EXIF_TAG_GPS_LONGITUDE_REF:
			// 'E' or 'W'
			if (first_char_is(entry.text(), 'W'))
			{
				_gps_coordinate.longitude_east_west(exif_gps_coordinate_builder::EastWest::West);
			}
			else
			{
				_gps_coordinate.longitude_east_west(exif_gps_coordinate_builder::EastWest::East);
			}
			break;

		case EXIF_TAG_GPS_ALTITUDE:
			_metadata.altitude = static_cast<float>(entry.get_urational().to_real());
			break;

		case EXIF_TAG_GPS_ALTITUDE_REF:
			// A byte, not text: 1 means below sea level. The sign is applied after parsing
			// because the reference may appear either side of the value.
			_below_sea_level = entry.get_uint8() == 1;
			break;

		case EXIF_TAG_GPS_SPEED:
			_gps_speed = static_cast<float>(entry.get_urational().to_real());
			break;

		case EXIF_TAG_GPS_SPEED_REF:
			// 'K' kilometres per hour (the default), 'M' miles per hour, 'N' knots.
			if (first_char_is(entry.text(), 'M')) _speed_to_kmh = 1.609344f;
			else if (first_char_is(entry.text(), 'N')) _speed_to_kmh = 1.852f;
			else _speed_to_kmh = 1.0f;
			break;
		}
	}
};


void metadata_exif::parse(prop::item_metadata& pd, const df::cspan cs)
{
	if (!cs.empty())
	{
		exif_camera_settings_processor processor(pd);
		exif_enumerate([&processor](const exif_dir_entry& e) { processor.tag(e); }, cs);
	}
}

// PixelXDimension and friends are SHORT or LONG. Writing two bytes into a LONG leaves the old
// high half behind on Intel order, and overwrites the high half on Motorola order.

static void update_tag(ExifData* ed, const int ifd, const ExifTag tag, const int value)
{
	const ExifEntry* ee = exif_content_get_entry(ed->ifd[ifd], tag);
	if (nullptr == ee)
		return;

	const ExifByteOrder o = exif_data_get_byte_order(ed);

	switch (ee->format)
	{
	case EXIF_FORMAT_SHORT:
		exif_set_short(ee->data, o, value);
		break;
	case EXIF_FORMAT_LONG:
		exif_set_long(ee->data, o, value);
		break;
	case EXIF_FORMAT_SLONG:
		exif_set_slong(ee->data, o, value);
		break;
	default:
		break;
	}
}

struct exif_free
{
	void operator()(ExifData* x) const { exif_data_unref(x); }
};

static std::string_view exif_ifd_title(const ExifIfd ifd)
{
	switch (ifd)
	{
	case EXIF_IFD_0: return "Main image (IFD0)";
	case EXIF_IFD_1: return "Thumbnail (IFD1)";
	case EXIF_IFD_EXIF: return "Exif";
	case EXIF_IFD_GPS: return "GPS";
	case EXIF_IFD_INTEROPERABILITY: return "Interoperability";
	default: return "Other";
	}
}

metadata_kv_list metadata_exif::to_info(const df::cspan data)
{
	metadata_kv_list result;
	std::unique_ptr<ExifData, exif_free> ed;

	if (is_exif_signature(data))
	{
		ed = std::unique_ptr<ExifData, exif_free>(
			exif_data_new_from_data(data.data, static_cast<unsigned int>(data.size)));
	}
	else
	{
		std::vector<uint8_t> with_sig;
		with_sig.reserve(data.size + exif_signature.size());
		with_sig.assign(exif_signature.begin(), exif_signature.end());
		with_sig.insert(with_sig.end(), data.data, data.data + data.size);
		ed = std::unique_ptr<ExifData, exif_free>(
			exif_data_new_from_data(with_sig.data(), static_cast<unsigned int>(with_sig.size())));
	}

	if (ed)
	{
		constexpr auto buffer_size = df::sixty_four_k;
		const auto buffer_alloc = df::unique_alloc<char>(buffer_size);
		auto* const buffer = buffer_alloc.get();

		// Exif is a set of image file directories, and which directory a tag lives in is part of what
		// the block contains, so each is listed as its own section rather than flattened together.
		for (auto ifd_index = 0; ifd_index < EXIF_IFD_COUNT; ++ifd_index)
		{
			const auto* const content = ed->ifd[ifd_index];

			if (!content || !content->count) continue;

			const auto ifd = static_cast<ExifIfd>(ifd_index);
			const auto title = exif_ifd_title(ifd);

			auto& section = result.emplace_back(std::format("{} ({})", title, content->count),
			                                    std::string{});
			section.container = true;
			section.id = std::format("exif.ifd.{}", ifd_index);
			// The Exif directory holds the capture settings most readers came for, so it opens
			// however many tags the camera wrote.
			section.open_by_default = ifd == EXIF_IFD_EXIF;

			for (auto j = 0u; j < content->count; j++)
			{
				auto* const e = content->entries[j];
				buffer[0] = 0;
				const auto* const text = exif_entry_get_value(e, buffer, buffer_size);
				const auto len = text ? strnlen(text, buffer_size) : 0;
				const std::string_view rendered{text ? text : "", len};
				const auto vendor = vendor_blob_name(e->data, e->size);
				// An empty render over a payload of real size means libexif stopped at a nul inside
				// a binary blob, so report the size rather than a blank value.
				const auto readable = vendor.empty() && (!rendered.empty() || e->size <= 4) &&
					!is_junk(std::bit_cast<const uint8_t*>(rendered.data()), static_cast<uint32_t>(len)) &&
					!is_binary_text(rendered);

				auto& row = result.emplace_back(
					str::cache(exif_tag_get_name_in_ifd(e->tag, ifd)),
					readable
						? std::string(str::utf8_cast(rendered))
						: std::format("{}, {} bytes", vendor.empty() ? std::string_view{"binary"} : vendor,
						              e->size));

				row.depth = 1;
				row.shape = std::format("{} x{}, 0x{:04x}", exif_format_get_name(e->format),
				                        e->components, static_cast<unsigned>(e->tag));
				row.id = std::format("exif.{}.{:04x}", ifd_index, static_cast<unsigned>(e->tag));

				// The bytes stay reachable whether or not libexif could render them; the hex listing
				// is built only when the row is opened.
				if (e->data && e->size > 0)
				{
					const auto kept = std::min<size_t>(e->size, str::max_hex_dump_bytes);
					row.detail = metadata_binary_detail{std::vector<uint8_t>(e->data, e->data + kept)};
				}
			}
		}

		if (ed->data && ed->size > 0)
		{
			auto& row = result.emplace_back("Embedded thumbnail"_c, std::format("{} bytes", ed->size));
			row.id = "exif.thumbnail";
			row.shape = "binary"_c;
			const auto kept = std::min<size_t>(ed->size, str::max_hex_dump_bytes);
			row.detail = metadata_binary_detail{std::vector<uint8_t>(ed->data, ed->data + kept)};
		}
	}

	return result;
}

df::blob metadata_exif::fix_dims(const df::span cs, const int image_width, const int image_height)
{
	df::blob result;

	df::assert_true(is_exif_signature(cs));
	const std::unique_ptr<ExifData, exif_free> ed(exif_data_new_from_data(cs.data, static_cast<unsigned int>(cs.size)));

	if (ed)
	{
		update_tag(ed.get(), EXIF_IFD_0, EXIF_TAG_ORIENTATION, 1);
		update_tag(ed.get(), EXIF_IFD_1, EXIF_TAG_ORIENTATION, 1);
		update_tag(ed.get(), EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION, image_width);
		update_tag(ed.get(), EXIF_IFD_EXIF, EXIF_TAG_PIXEL_Y_DIMENSION, image_height);
		update_tag(ed.get(), EXIF_IFD_INTEROPERABILITY, EXIF_TAG_RELATED_IMAGE_WIDTH, image_width);
		update_tag(ed.get(), EXIF_IFD_INTEROPERABILITY, EXIF_TAG_RELATED_IMAGE_LENGTH, image_height);

		uint8_t* data = nullptr;
		uint32_t size = 0;
		exif_data_save_data(ed.get(), &data, &size);

		if (data)
		{
			result.assign(data, data + size);
			free(data);
		}
	}
	return result;
}

#define FILE_BYTE_ORDER EXIF_BYTE_ORDER_INTEL

// Exif requires an 8 byte character code ahead of UserComment text.
static constexpr std::string_view ascii_comment_prefix{"ASCII\0\0\0", 8};

// Allocates a zeroed entry of exactly size bytes and hands it to the IFD, returning it so the
// caller can fill entry->data. Returns null if the entry could not be attached.
static ExifEntry* create_tag(const ExifData* exif, const ExifIfd ifd, const ExifTag tag, const ExifFormat format,
                             const uint32_t components, const uint32_t size)
{
	auto* const content = exif->ifd[ifd];

	if (!content || size == 0)
	{
		return nullptr;
	}

	// exif_data_fix() may already have created the tag, and libexif refuses a duplicate.
	if (auto* const existing = exif_content_get_entry(content, tag))
	{
		exif_content_remove_entry(content, existing);
	}

	auto* const mem = exif_mem_new_default();

	if (!mem)
	{
		return nullptr;
	}

	auto* const entry = exif_entry_new_mem(mem);
	auto* const buf = entry ? static_cast<uint8_t*>(exif_mem_alloc(mem, size)) : nullptr;

	if (buf)
	{
		memset(buf, 0, size);
		entry->data = buf;
		entry->size = size;
		entry->tag = tag;
		entry->components = components;
		entry->format = format;

		exif_content_add_entry(content, entry);
	}

	const auto attached = entry && entry->parent;

	if (entry) exif_entry_unref(entry);
	exif_mem_unref(mem);

	return attached ? entry : nullptr;
}

static void add_ascii(ExifData* exif, const ExifIfd ifd, const ExifTag tag, const std::string_view val)
{
	const auto len = static_cast<uint32_t>(val.size() + 1);
	const auto* const entry = create_tag(exif, ifd, tag, EXIF_FORMAT_ASCII, len, len);

	if (entry)
	{
		memcpy(entry->data, val.data(), val.size());
	}
}

static void add_short(ExifData* exif, const ExifIfd ifd, const ExifTag tag, const uint16_t val)
{
	const auto* const entry = create_tag(exif, ifd, tag, EXIF_FORMAT_SHORT, 1, exif_format_get_size(EXIF_FORMAT_SHORT));

	if (entry)
	{
		exif_set_short(entry->data, FILE_BYTE_ORDER, val);
	}
}

static void add_rational(ExifData* exif, const ExifIfd ifd, const ExifTag tag, const uint32_t numerator,
                         const uint32_t denominator)
{
	const auto* const entry = create_tag(exif, ifd, tag, EXIF_FORMAT_RATIONAL, 1,
	                                     exif_format_get_size(EXIF_FORMAT_RATIONAL));

	if (entry)
	{
		exif_set_rational(entry->data, FILE_BYTE_ORDER, ExifRational{numerator, denominator});
	}
}

// Exif rationals are 32 bit, so scale by a fixed denominator and reduce rather than search for
// the closest fraction.
static void add_real(ExifData* exif, const ExifIfd ifd, const ExifTag tag, const double val)
{
	constexpr uint32_t scale = 10000u;

	if (val <= 0.0 || val > static_cast<double>(std::numeric_limits<uint32_t>::max()) / scale)
	{
		return;
	}

	auto numerator = static_cast<uint32_t>(std::llround(val * scale));
	auto denominator = scale;
	const auto divisor = std::gcd(numerator, denominator);

	if (divisor > 1)
	{
		numerator /= divisor;
		denominator /= divisor;
	}

	add_rational(exif, ifd, tag, numerator, denominator);
}

// Sub-second exposures are conventionally recorded as 1/n.
static void add_exposure_time(ExifData* exif, const double seconds)
{
	if (seconds <= 0.0) return;

	if (seconds < 1.0)
	{
		const auto denominator = std::llround(1.0 / seconds);

		if (denominator > 0 && denominator <= std::numeric_limits<uint32_t>::max())
		{
			add_rational(exif, EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME, 1u, static_cast<uint32_t>(denominator));
			return;
		}
	}

	add_real(exif, EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME, seconds);
}

static void add_date(ExifData* exif, const ExifIfd ifd, const ExifTag tag, const df::date_t date)
{
	const auto d = date.date();
	add_ascii(exif, ifd, tag,
	          std::format("{:04}:{:02}:{:02} {:02}:{:02}:{:02}", d.year, d.month, d.day, d.hour, d.minute, d.second));
}

static void add_user_comment(ExifData* exif, const std::string_view val)
{
	const auto len = static_cast<uint32_t>(ascii_comment_prefix.size() + val.size());
	const auto* const entry = create_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_USER_COMMENT, EXIF_FORMAT_UNDEFINED, len, len);

	if (entry)
	{
		memcpy(entry->data, ascii_comment_prefix.data(), ascii_comment_prefix.size());
		memcpy(entry->data + ascii_comment_prefix.size(), val.data(), val.size());
	}
}

df::blob metadata_exif::make_exif(const prop::item_metadata_ptr& md)
{
	df::blob result;
	ExifData* exif = exif_data_new();

	if (exif)
	{
		//  Set the image options 
		exif_data_set_option(exif, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
		exif_data_set_data_type(exif, EXIF_DATA_TYPE_COMPRESSED);
		exif_data_set_byte_order(exif, FILE_BYTE_ORDER);

		//  Create the mandatory EXIF fields with default data 
		exif_data_fix(exif);

		if (md->orientation != ui::orientation::none)
			add_short(exif, EXIF_IFD_0, EXIF_TAG_ORIENTATION, static_cast<uint16_t>(md->orientation));
		if (!prop::is_null(md->f_number)) add_real(exif, EXIF_IFD_EXIF, EXIF_TAG_FNUMBER, md->f_number);
		if (!prop::is_null(md->exposure_time)) add_exposure_time(exif, md->exposure_time);
		if (!prop::is_null(md->iso_speed))
			add_short(exif, EXIF_IFD_EXIF, EXIF_TAG_ISO_SPEED_RATINGS, md->iso_speed);
		if (!prop::is_null(md->focal_length)) add_real(exif, EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH, md->focal_length);
		if (!prop::is_null(md->focal_length_35mm_equivalent))
			add_short(exif, EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM, md->focal_length_35mm_equivalent);
		if (!prop::is_null(md->camera_manufacturer))
			add_ascii(exif, EXIF_IFD_0, EXIF_TAG_MAKE, md->camera_manufacturer.sv());
		if (!prop::is_null(md->camera_model)) add_ascii(exif, EXIF_IFD_0, EXIF_TAG_MODEL, md->camera_model.sv());
		if (!prop::is_null(md->artist)) add_ascii(exif, EXIF_IFD_0, EXIF_TAG_ARTIST, md->artist.sv());
		if (!prop::is_null(md->lens)) add_ascii(exif, EXIF_IFD_EXIF, EXIF_TAG_LENS_MODEL, md->lens.sv());
		if (!prop::is_null(md->copyright_notice))
			add_ascii(exif, EXIF_IFD_0, EXIF_TAG_COPYRIGHT, md->copyright_notice.sv());
		if (!prop::is_null(md->description))
			add_ascii(exif, EXIF_IFD_0, EXIF_TAG_IMAGE_DESCRIPTION, md->description.sv());
		if (!prop::is_null(md->comment)) add_user_comment(exif, md->comment.sv());

		const auto created = md->created();

		if (created.is_valid())
		{
			add_date(exif, EXIF_IFD_0, EXIF_TAG_DATE_TIME, created);
			add_date(exif, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL, created);
		}

		if (md->created_digitized.is_valid())
		{
			add_date(exif, EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_DIGITIZED, md->created_digitized);
		}

		////  All these tags are created with default values by exif_data_fix() 
		////  Change the data to the correct values for this image. 
		//entry = init_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION);
		//exif_set_long(entry->data, FILE_BYTE_ORDER, image_jpg_x);

		////  Create a EXIF_TAG_USER_COMMENT tag. This one must be handled
		// * differently because that tag isn't automatically created and
		// * allocated by exif_data_fix(), nor can it be created using
		// * exif_entry_initialize() so it must be explicitly allocated here.
		// 
		//entry = create_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_USER_COMMENT,
		//	234             sizeof(ASCII_COMMENT) + sizeof(FILE_COMMENT) - 2);
		////  Write the special header needed for a comment tag 
		//memcpy(entry->data, ASCII_COMMENT, sizeof(ASCII_COMMENT) - 1);
		////  Write the actual comment text, without the trailing NUL character 
		//memcpy(entry->data + 8, FILE_COMMENT, sizeof(FILE_COMMENT) - 1);
		////  create_tag() happens to set the format and components correctly for
		//// * EXIF_TAG_USER_COMMENT, so there is nothing more to do. 

		// //  Create a EXIF_TAG_SUBJECT_AREA tag 

		uint8_t* exif_data = nullptr;
		unsigned int exif_data_len = 0;
		exif_data_save_data(exif, &exif_data, &exif_data_len);

		if (exif_data)
		{
			result.assign(exif_data, exif_data + exif_data_len);
			free(exif_data);
		}
		exif_data_unref(exif);
	}

	return result;
}

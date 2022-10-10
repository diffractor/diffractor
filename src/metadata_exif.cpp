// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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

static_assert(std::is_trivially_copyable_v<metadata_exif::urational32_t>);
static_assert(std::is_trivially_copyable_v<metadata_exif::srational32_t>);

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

static bool is_junk(const uint8_t* p, uint32_t s)
{
	static constexpr uint8_t junk_marker[] = {0x12, 0xf8, 0x0f, 0x3b,};
	return s >= 4 && memcmp(p, junk_marker, 4) == 0;
}

class exif_data_buffer
{
private:
	const uint8_t* const _data;
	const size_t _size;
	const bool _is_intel;

public:
	exif_data_buffer(df::cspan cs, bool isIntel) : _data(cs.data), _size(cs.size), _is_intel(isIntel)
	{
	}

	bool is_overflow(uint32_t i, size_t size) const
	{
		return (i < 0) || (i + size) > _size;
	}

	const uint8_t* data() const
	{
		return _data;
	}

	size_t dize() const
	{
		return _size;
	}

	const uint8_t* data(uint32_t i, uint32_t size) const
	{
		if (is_overflow(i, size)) return nullptr;
		return _data + i;
	}

	bool is_intel() const
	{
		return _is_intel;
	}

	str::cached cached_string(uint32_t i, uint32_t len, bool probablyUnicode) const
	{
		if (!is_overflow(i, len))
		{
			const auto* p = _data + i;

			if (len > 7 && memcmp(p, u8"UNICODE", 7) == 0)
			{
				if (len > 9)
				{
					const size_t offset = 8;
					p = p + offset;
					const auto length = len - offset;
					const auto char_length = length / sizeof(wchar_t);

					if (_is_intel)
					{
						return str::strip_and_cache({std::bit_cast<const wchar_t*>(p), char_length});
					}

					const auto buffer = df::unique_alloc<uint8_t>(length + 2);
					auto* const dst = buffer.get();
					size_t pos = 0;


					while (pos < length)
					{
						dst[pos] = p[pos + 1];
						dst[pos + 1] = p[pos];
						pos += 2;
					}

					return str::strip_and_cache({std::bit_cast<const wchar_t*>(dst), char_length});
				}
			}
			else if (len > 5 && memcmp(p, u8"ASCII", 5) == 0)
			{
				if (len > 8)
				{
					p = p + 8;
					return is_junk(p, len)
						       ? str::cached{}
						       : str::strip_and_cache({std::bit_cast<const char8_t*>(p), len - 8});
				}
			}
			else if (is_junk(p, len))
			{
				return {};
			}
			else if (str::is_utf16(p, len))
			{
				return str::strip_and_cache({std::bit_cast<const wchar_t*>(p), len / 2});
			}
			else if (str::is_utf8(std::bit_cast<const char8_t*>(p), len))
			{
				return str::strip_and_cache({std::bit_cast<const char8_t*>(p), len});
			}
			else
			{
				return u8"?"_c;
			}
		}

		return {};
	}

	uint16_t get_uint16(uint32_t i) const
	{
		if (_is_intel)
		{
			return *std::bit_cast<const uint16_t*>(_data + i);
		}
		return df::byteswap16(_data + i);
	}

	uint32_t get_uint32(uint32_t i) const
	{
		if (_is_intel)
		{
			return *std::bit_cast<const uint32_t*>(_data + i);
		}
		return df::byteswap32(_data + i);
	}

	int16_t get_int16(uint32_t i) const
	{
		if (_is_intel)
		{
			return *std::bit_cast<const int16_t*>(_data + i);
		}
		return static_cast<int16_t>(df::byteswap16(_data + i));
	}

	int32_t get_int32(uint32_t i) const
	{
		if (_is_intel)
		{
			return *std::bit_cast<const int32_t*>(_data + i);
		}
		return static_cast<int32_t>(df::byteswap32(_data + i));
	}

	metadata_exif::srational32_t get_srational(uint32_t i) const
	{
		return metadata_exif::srational32_t(get_uint32(i), get_uint32(i + 4));
	}

	metadata_exif::urational32_t get_urational(uint32_t i) const
	{
		return metadata_exif::urational32_t(get_int32(i), get_int32(i + 4));
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

	exif_dir_entry(tag_type tt, exif_data_buffer& data, const uint32_t offset) :
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
		const auto o = data_offset() + (i * 2);
		if (_data.is_overflow(o, 2u)) return 0;
		return _data.get_int16(o);
	}

	uint32_t get_uint16(const uint32_t i) const
	{
		const auto o = data_offset() + (i * 2);
		if (_data.is_overflow(o, 2)) return 0;
		return _data.get_uint16(o);
	}

	uint32_t get_uint32(const uint32_t i) const
	{
		const auto o = data_offset() + (i * 2);
		if (_data.is_overflow(o, 2)) return 0;
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
			return u8"?"_c;

		return _data.cached_string(data_offset(), size(), probably_unicode);
	}

	metadata_exif::srational32_t get_srational(const uint32_t i = 0) const
	{
		const auto len = 2 * sizeof(int);
		const auto offset = data_offset() + (i * len);
		if (_data.is_overflow(offset, len)) return {};
		return _data.get_srational(offset);
	}

	metadata_exif::urational32_t get_urational(const uint32_t i = 0) const
	{
		const auto len = 2 * sizeof(int);
		const auto offset = data_offset() + (i * len);
		if (_data.is_overflow(offset, len)) return {};
		return _data.get_urational(offset);
	}

	std::u8string_view text() const
	{
		const auto len = size();
		const auto* const sz = std::bit_cast<const char8_t*>(_data.data(data_offset(), len));
		return sz ? std::u8string_view{sz, len} : std::u8string_view{};
	}

	bool is_intel() const
	{
		return _data.is_intel();
	}
};


class exif_parser
{
private:
	exif_data_buffer& _data;
	std::function<void(exif_dir_entry&)> _handler;

	uint32_t _dir_offsets_size = 0;
	uint32_t _dir_offsets[32];

	uint32_t _thumbnail_offset = 0;
	uint32_t _thumbnail_length = 0;

	str::cached _make;

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

					// entryCount = _data.UInt16(offset);
					//offset = _data.UInt32(offset + 2 + 12 * entryCount);

					//if (offset)
					//{
					//ParseDir<Exif::Tag>(offset);
					//}
				}
			}
		}
	}

	void parse_dir(uint32_t offset, const tag_type tagType)
	{
		if (!_data.is_overflow(offset, 2))
		{
			for (auto i = 0u; i < _dir_offsets_size; i++)
			{
				if (_dir_offsets[i] == offset)
					return;
			}

			_dir_offsets[_dir_offsets_size++] = offset;
			const auto entry_count = _data.get_uint16(offset);

			for (auto i = 0u; i < entry_count; ++i)
			{
				const auto pos = offset + 2 + (12 * i);

				if (!_data.is_overflow(pos, 12))
				{
					exif_dir_entry tag(tagType, _data, pos);

					if (tag.is_valid())
					{
						process_tag(tag);
					}
					else
					{
						//df::log(__FUNCTION__, u8"Invalid EXIF Tag: u8" << tag._tag;
					}
				}
			}
		}
	}

private:
	maker_note_type identify_maker_note(exif_dir_entry& entry) const
	{
		const auto size = entry.size();
		const auto* const data = entry.data(4);

		if (data)
		{
			// Olympus & Nikon & Sanyo 
			if ((size >= 8) &&
				(!memcmp(data, u8"OLYMP", 6) ||
					!memcmp(data, u8"OLYMPUS", 8) ||
					!memcmp(data, u8"SANYO", 6) ||
					!memcmp(data, u8"EPSON", 6) ||
					!memcmp(data, u8"Nikon", 6)))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_OLYMPUS;
			}

			if (is_empty(_make))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_NONE;
			}

			// Canon 
			if (icmp(_make, u8"Canon") == 0)
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_CANON;
			}

			// Pentax & some variant of Nikon 
			if ((size >= 2) && (data[0] == 0x00) && (data[1] == 0x1b))
			{
				if (icmp(_make, u8"Nikon") == 0)
				{
					return EXIF_DATA_TYPE_MAKER_NOTE_NIKON;
				}
				return EXIF_DATA_TYPE_MAKER_NOTE_PENTAX;
			}
			if ((size >= 8) && !memcmp(data, u8"AOC", 4))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_PENTAX;
			}
			if ((size >= 8) && !memcmp(data, u8"QVC", 4))
			{
				return EXIF_DATA_TYPE_MAKER_NOTE_CASIO;
			}
			if ((size >= 12) && !memcmp(data, u8"FUJIFILM", 8))
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

static void exif_enumerate(const std::function<void(exif_dir_entry&)>& h, df::cspan data)
{
	if (data > 16)
	{
		df::assert_true(!is_exif_signature(data));

		const auto ended = *std::bit_cast<const uint16_t*>(data.data);

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
private:
	df::hash_map<int, const char8_t*> _choices = {
		{1, u8"Canon EF 50mm f/1.8"},
		{2, u8"Canon EF 28mm f/2.8"},
		{3, u8"Canon EF 135mm f/2.8 Soft"},
		{4, u8"Canon EF 35-105mm f/3.5-4.5"}, // 0
		{4, u8"Sigma UC Zoom 35-135mm f/4-5.6"}, // 1
		{5, u8"Canon EF 35-70mm f/3.5-4.5"},
		{6, u8"Canon EF 28-70mm f/3.5-4.5"}, // 0
		{6, u8"Sigma 18-50mm f/3.5-5.6 DC"}, // 1
		{6, u8"Sigma 18-125mm f/3.5-5.6 DC IF ASP"}, // 2
		{6, u8"Tokina AF193-2 19-35mm f/3.5-4.5"}, // 3
		{6, u8"Sigma 28-80mm f/3.5-5.6 II Macro"}, // 4
		{7, u8"Canon EF 100-300mm f/5.6L"},
		{8, u8"Canon EF 100-300mm f/5.6"}, // 0
		{8, u8"Sigma 70-300mm f/4-5.6 [APO] DG Macro"}, // 1
		{8, u8"Tokina AT-X 242 AF 24-200mm f/3.5-5.6"}, // 2
		{9, u8"Canon EF 70-210mm f/4"}, // 0
		{9, u8"Sigma 55-200mm f/4-5.6 DC"}, // 1
		{10, u8"Canon EF 50mm f/2.5 Macro"}, // 0
		{10, u8"Sigma 50mm f/2.8 EX"}, // 1
		{10, u8"Sigma 28mm f/1.8"}, // 2
		{10, u8"Sigma 105mm f/2.8 Macro EX"}, // 3
		{10, u8"Sigma 70mm f/2.8 EX DG Macro EF"}, // 4
		{11, u8"Canon EF 35mm f/2"},
		{13, u8"Canon EF 15mm f/2.8 Fisheye"},
		{14, u8"Canon EF 50-200mm f/3.5-4.5L"},
		{15, u8"Canon EF 50-200mm f/3.5-4.5"},
		{16, u8"Canon EF 35-135mm f/3.5-4.5"},
		{17, u8"Canon EF 35-70mm f/3.5-4.5A"},
		{18, u8"Canon EF 28-70mm f/3.5-4.5"},
		{20, u8"Canon EF 100-200mm f/4.5A"},
		{21, u8"Canon EF 80-200mm f/2.8L"},
		{22, u8"Canon EF 20-35mm f/2.8L"}, // 0
		{22, u8"Tokina AT-X 280 AF PRO 28-80mm f/2.8 Aspherical"}, // 1
		{23, u8"Canon EF 35-105mm f/3.5-4.5"},
		{24, u8"Canon EF 35-80mm f/4-5.6 Power Zoom"},
		{25, u8"Canon EF 35-80mm f/4-5.6 Power Zoom"},
		{26, u8"Canon EF 100mm f/2.8 Macro"}, // 0
		{26, u8"Cosina 100mm f/3.5 Macro AF"}, // 1
		{26, u8"Tamron SP AF 90mm f/2.8 Di Macro"}, // 2
		{26, u8"Tamron SP AF 180mm f/3.5 Di Macro"}, // 3
		{26, u8"Carl Zeiss Planar T* 50mm f/1.4"}, // 4
		{27, u8"Canon EF 35-80mm f/4-5.6"},
		{28, u8"Canon EF 80-200mm f/4.5-5.6"}, // 0
		{28, u8"Tamron SP AF 28-105mm f/2.8 LD Aspherical IF"}, // 1
		{28, u8"Tamron SP AF 28-75mm f/2.8 XR Di LD Aspherical [IF] Macro"}, // 2
		{28, u8"Tamron AF 70-300mm f/4.5-5.6 Di LD 1:2 Macro Zoom"}, // 3
		{28, u8"Tamron AF Aspherical 28-200mm f/3.8-5.6"}, // 4
		{29, u8"Canon EF 50mm f/1.8 MkII"},
		{30, u8"Canon EF 35-105mm f/4.5-5.6"},
		{31, u8"Canon EF 75-300mm f/4-5.6"}, // 0
		{31, u8"Tamron SP AF 300mm f/2.8 LD IF"}, // 1
		{32, u8"Canon EF 24mm f/2.8"}, // 0
		{32, u8"Sigma 15mm f/2.8 EX Fisheye"}, // 1
		{33, u8"Voigtlander or Carl Zeiss Lens"}, // 0
		{33, u8"Voigtlander Ultron 40mm f/2 SLII Aspherical"}, // 1
		{33, u8"Carl Zeiss Distagon 15mm T* f/2.8 ZE"}, // 2
		{33, u8"Carl Zeiss Distagon 18mm T* f/3.5 ZE"}, // 3
		{33, u8"Carl Zeiss Distagon 21mm T* f/2.8 ZE"}, // 4
		{33, u8"Carl Zeiss Distagon 28mm T* f/2 ZE"}, // 5
		{33, u8"Carl Zeiss Distagon 35mm T* f/2 ZE"}, // 6
		{33, u8"Carl Zeiss Planar 50mm T* f/1.4 ZE"}, // 7
		{35, u8"Canon EF 35-80mm f/4-5.6"},
		{36, u8"Canon EF 38-76mm f/4.5-5.6"},
		{37, u8"Canon EF 35-80mm f/4-5.6"}, // 0
		{37, u8"Tamron 70-200mm f/2.8 Di LD IF Macro"}, // 1
		{37, u8"Tamron AF 28-300mm f/3.5-6.3 XR Di VC LD Aspherical [IF] Macro Model A20"}, // 2
		{37, u8"Tamron SP AF 17-50mm f/2.8 XR Di II VC LD Aspherical [IF] u8"}, // 3
		{37, u8"Tamron AF 18-270mm f/3.5-6.3 Di II VC LD Aspherical [IF] Macro"}, // 4
		{38, u8"Canon EF 80-200mm f/4.5-5.6"},
		{39, u8"Canon EF 75-300mm f/4-5.6"},
		{40, u8"Canon EF 28-80mm f/3.5-5.6"},
		{41, u8"Canon EF 28-90mm f/4-5.6"},
		{42, u8"Canon EF 28-200mm f/3.5-5.6"}, // 0
		{42, u8"Tamron AF 28-300mm f/3.5-6.3 XR Di VC LD Aspherical [IF] Macro Model A20"}, // 1
		{43, u8"Canon EF 28-105mm f/4-5.6"},
		{44, u8"Canon EF 90-300mm f/4.5-5.6"},
		{45, u8"Canon EF-S 18-55mm f/3.5-5.6"},
		{46, u8"Canon EF 28-90mm f/4-5.6"},
		{48, u8"Canon EF-S 18-55mm f/3.5-5.6 IS"},
		{49, u8"Canon EF-S 55-250mm f/4-5.6 IS"},
		{50, u8"Canon EF-S 18-200mm f/3.5-5.6 IS"},
		{51, u8"Canon EF-S 18-135mm f/3.5-5.6 IS"},
		{52, u8"Canon EF-S 18-55mm f/3.5-5.6 IS II"},
		{53, u8"Canon EF-S 18-55mm f/3.5-5.6 III"},
		{54, u8"Canon EF-S 55-250mm f/4-5.6 IS II"},
		{94, u8"Canon TS-E 17mm f/4L"},
		{95, u8"Canon TS-E 24.0mm f/3.5 L II"},
		{124, u8"Canon MP-E 65mm f/2.8 1-5x Macro Photo"},
		{125, u8"Canon TS-E 24mm f/3.5L"},
		{126, u8"Canon TS-E 45mm f/2.8"},
		{127, u8"Canon TS-E 90mm f/2.8"},
		{129, u8"Canon EF 300mm f/2.8L"},
		{130, u8"Canon EF 50mm f/1.0L"},
		{131, u8"Canon EF 28-80mm f/2.8-4L"}, // 0
		{131, u8"Sigma 8mm f/3.5 EX DG Circular Fisheye"}, // 1
		{131, u8"Sigma 17-35mm f/2.8-4 EX DG Aspherical HSM"}, // 2
		{131, u8"Sigma 17-70mm f/2.8-4.5 DC Macro"}, // 3
		{131, u8"Sigma APO 50-150mm f/2.8 EX DC HSM"}, // 4
		{131, u8"Sigma APO 120-300mm f/2.8 EX DG HSM"}, // 5
		{131, u8"Sigma 4.5mm F2.8 EX DC HSM Circular Fisheye"}, // 6
		{131, u8"Sigma 70-200mm f/2.8 APO EX HSM"}, // 7
		{132, u8"Canon EF 1200mm f/5.6L"},
		{134, u8"Canon EF 600mm f/4L IS"},
		{135, u8"Canon EF 200mm f/1.8L"},
		{136, u8"Canon EF 300mm f/2.8L"},
		{137, u8"Canon EF 85mm f/1.2L"}, // 0
		{137, u8"Sigma 18-50mm f/2.8-4.5 DC OS HSM"}, // 1
		{137, u8"Sigma 50-200mm f/4-5.6 DC OS HSM"}, // 2
		{137, u8"Sigma 18-250mm f/3.5-6.3 DC OS HSM"}, // 3
		{137, u8"Sigma 24-70mm f/2.8 IF EX DG HSM"}, // 4
		{137, u8"Sigma 18-125mm f/3.8-5.6 DC OS HSM"}, // 5
		{137, u8"Sigma 17-70mm f/2.8-4 DC Macro OS HSM"}, // 6
		{137, u8"Sigma 17-50mm f/2.8 OS HSM"}, // 7
		{137, u8"Sigma 18-200mm f/3.5-6.3 II DC OS HSM"}, // 8
		{137, u8"Tamron AF 18-270mm f/3.5-6.3 Di II VC PZD"}, // 9
		{137, u8"Sigma 8-16mm f/4.5-5.6 DC HSM"}, // 10
		{137, u8"Tamron SP 17-50mm f/2.8 XR Di II VC"}, // 11
		{137, u8"Tamron SP 60mm f/2 Macro Di II"}, // 12
		{137, u8"Sigma 10-20mm f/3.5 EX DC HSM"}, // 13
		{137, u8"Tamron SP 24-70mm f/2.8 Di VC USD"}, // 14
		{137, u8"Sigma 18-35mm f/1.8 DC HSM"}, // 15
		{137, u8"Sigma 12-24mm f/4.5-5.6 DG HSM II"}, // 16
		{138, u8"Canon EF 28-80mm f/2.8-4L"},
		{139, u8"Canon EF 400mm f/2.8L"},
		{140, u8"Canon EF 500mm f/4.5L"},
		{141, u8"Canon EF 500mm f/4.5L"},
		{142, u8"Canon EF 300mm f/2.8L IS"},
		{143, u8"Canon EF 500mm f/4L IS"},
		{144, u8"Canon EF 35-135mm f/4-5.6 USM"},
		{145, u8"Canon EF 100-300mm f/4.5-5.6 USM"},
		{146, u8"Canon EF 70-210mm f/3.5-4.5 USM"},
		{147, u8"Canon EF 35-135mm f/4-5.6 USM"},
		{148, u8"Canon EF 28-80mm f/3.5-5.6 USM"},
		{149, u8"Canon EF 100mm f/2 USM"},
		{150, u8"Canon EF 14mm f/2.8L"}, // 0
		{150, u8"Sigma 20mm EX f/1.8"}, // 1
		{150, u8"Sigma 30mm f/1.4 DC HSM"}, // 2
		{150, u8"Sigma 24mm f/1.8 DG Macro EX"}, // 3
		{151, u8"Canon EF 200mm f/2.8L"},
		{152, u8"Canon EF 300mm f/4L IS"}, // 0
		{152, u8"Sigma 12-24mm f/4.5-5.6 EX DG ASPHERICAL HSM"}, // 1
		{152, u8"Sigma 14mm f/2.8 EX Aspherical HSM"}, // 2
		{152, u8"Sigma 10-20mm f/4-5.6"}, // 3
		{152, u8"Sigma 100-300mm f/4"}, // 4
		{153, u8"Canon EF 35-350mm f/3.5-5.6L"}, // 0
		{153, u8"Sigma 50-500mm f/4-6.3 APO HSM EX"}, // 1
		{153, u8"Tamron AF 28-300mm f/3.5-6.3 XR LD Aspherical [IF] Macro"}, // 2
		{153, u8"Tamron AF 18-200mm f/3.5-6.3 XR Di II LD Aspherical [IF] Macro Model A14"}, // 3
		{153, u8"Tamron 18-250mm f/3.5-6.3 Di II LD Aspherical [IF] Macro"}, // 4
		{154, u8"Canon EF 20mm f/2.8 USM"},
		{155, u8"Canon EF 85mm f/1.8 USM"},
		{156, u8"Canon EF 28-105mm f/3.5-4.5 USM"}, // 0
		{156, u8"Tamron SP AF 70-300mm f/4-5.6 Di VC USD"}, // 1
		{160, u8"Canon EF 20-35mm f/3.5-4.5 USM"}, // 0
		{160, u8"Tamron AF 19-35mm f/3.5-4.5"}, // 1
		{160, u8"Tokina AT-X 124 AF 12-24mm f/4 DX"}, // 2
		{160, u8"Tokina AT-X 107 AF DX Fish-eye 10-17mm f/3.5-4.5"}, // 3
		{160, u8"Tokina AT-X 116 PRO DX AF 11-16mm f/2.8"}, // 4
		{161, u8"Canon EF 28-70mm f/2.8L"}, // 0
		{161, u8"Sigma 24-70mm EX f/2.8"}, // 1
		{161, u8"Sigma 28-70mm f/2.8 EX"}, // 2
		{161, u8"Tamron AF 17-50mm f/2.8 Di-II LD Aspherical"}, // 3
		{161, u8"Tamron 90mm f/2.8"}, // 4
		{161, u8"Sigma 24-60mm f/2.8 EX DG"}, // 5
		{162, u8"Canon EF 200mm f/2.8L"},
		{163, u8"Canon EF 300mm f/4L"},
		{164, u8"Canon EF 400mm f/5.6L"},
		{165, u8"Canon EF 70-200mm f/2.8 L"},
		{166, u8"Canon EF 70-200mm f/2.8 L + 1.4x"},
		{167, u8"Canon EF 70-200mm f/2.8 L + 2x"},
		{168, u8"Canon EF 28mm f/1.8 USM"},
		{169, u8"Canon EF 17-35mm f/2.8L"}, // 0
		{169, u8"Sigma 18-200mm f/3.5-6.3 DC OS"}, // 1
		{169, u8"Sigma 15-30mm f/3.5-4.5 EX DG Aspherical"}, // 2
		{169, u8"Sigma 18-50mm f/2.8 Macro"}, // 3
		{169, u8"Sigma 50mm f/1.4 EX DG HSM"}, // 4
		{169, u8"Sigma 85mm f/1.4 EX DG HSM"}, // 5
		{169, u8"Sigma 30mm f/1.4 EX DC HSM"}, // 6
		{169, u8"Sigma 35mm f/1.4 DG HSM"}, // 7
		{170, u8"Canon EF 200mm f/2.8L II"},
		{171, u8"Canon EF 300mm f/4L"},
		{172, u8"Canon EF 400mm f/5.6L"},
		{173, u8"Canon EF 180mm Macro f/3.5L"}, // 0
		{173, u8"Sigma 180mm EX HSM Macro f/3.5"}, // 1
		{173, u8"Sigma APO Macro 150mm f/3.5 EX DG IF HSM"}, // 2
		{174, u8"Canon EF 135mm f/2L"}, // 0
		{174, u8"Sigma 70-200mm f/2.8 EX DG APO OS HSM"}, // 1
		{174, u8"Sigma 50-500mm f/4.5-6.3 APO DG OS HSM"}, // 2
		{174, u8"Sigma 150-500mm f/5-6.3 APO DG OS HSM"}, // 3
		{175, u8"Canon EF 400mm f/2.8L"},
		{176, u8"Canon EF 24-85mm f/3.5-4.5 USM"},
		{177, u8"Canon EF 300mm f/4L IS"},
		{178, u8"Canon EF 28-135mm f/3.5-5.6 IS"},
		{179, u8"Canon EF 24mm f/1.4L"},
		{180, u8"Canon EF 35mm f/1.4L"},
		{181, u8"Canon EF 100-400mm f/4.5-5.6L IS + 1.4x"},
		{182, u8"Canon EF 100-400mm f/4.5-5.6L IS + 2x"},
		{183, u8"Canon EF 100-400mm f/4.5-5.6L IS"}, // 0
		{183, u8"Sigma 150mm f/2.8 EX DG OS HSM APO Macro"}, // 1
		{183, u8"Sigma 105mm f/2.8 EX DG OS HSM Macro"}, // 2
		{184, u8"Canon EF 400mm f/2.8L + 2x"},
		{185, u8"Canon EF 600mm f/4L IS"},
		{186, u8"Canon EF 70-200mm f/4L"},
		{187, u8"Canon EF 70-200mm f/4L + 1.4x"},
		{188, u8"Canon EF 70-200mm f/4L + 2x"},
		{189, u8"Canon EF 70-200mm f/4L + 2.8x"},
		{190, u8"Canon EF 100mm f/2.8 Macro"},
		{191, u8"Canon EF 400mm f/4 DO IS"},
		{193, u8"Canon EF 35-80mm f/4-5.6 USM"},
		{194, u8"Canon EF 80-200mm f/4.5-5.6 USM"},
		{195, u8"Canon EF 35-105mm f/4.5-5.6 USM"},
		{196, u8"Canon EF 75-300mm f/4-5.6 USM"},
		{197, u8"Canon EF 75-300mm f/4-5.6 IS USM"},
		{198, u8"Canon EF 50mm f/1.4 USM"}, // 0
		{198, u8"Zeiss Otus 55mm f/1.4 ZE"}, // 1
		{199, u8"Canon EF 28-80mm f/3.5-5.6 USM"},
		{200, u8"Canon EF 75-300mm f/4-5.6 USM"},
		{201, u8"Canon EF 28-80mm f/3.5-5.6 USM"},
		{202, u8"Canon EF 28-80mm f/3.5-5.6 USM IV"},
		{208, u8"Canon EF 22-55mm f/4-5.6 USM"},
		{209, u8"Canon EF 55-200mm f/4.5-5.6"},
		{210, u8"Canon EF 28-90mm f/4-5.6 USM"},
		{211, u8"Canon EF 28-200mm f/3.5-5.6 USM"},
		{212, u8"Canon EF 28-105mm f/4-5.6 USM"},
		{213, u8"Canon EF 90-300mm f/4.5-5.6 USM"},
		{214, u8"Canon EF-S 18-55mm f/3.5-5.6 USM"},
		{215, u8"Canon EF 55-200mm f/4.5-5.6 II USM"},
		{224, u8"Canon EF 70-200mm f/2.8L IS"},
		{225, u8"Canon EF 70-200mm f/2.8L IS + 1.4x"},
		{226, u8"Canon EF 70-200mm f/2.8L IS + 2x"},
		{227, u8"Canon EF 70-200mm f/2.8L IS + 2.8x"},
		{228, u8"Canon EF 28-105mm f/3.5-4.5 USM"},
		{229, u8"Canon EF 16-35mm f/2.8L"},
		{230, u8"Canon EF 24-70mm f/2.8L"},
		{231, u8"Canon EF 17-40mm f/4L"},
		{232, u8"Canon EF 70-300mm f/4.5-5.6 DO IS USM"},
		{233, u8"Canon EF 28-300mm f/3.5-5.6L IS"},
		{234, u8"Canon EF-S 17-85mm f4-5.6 IS USM"},
		{235, u8"Canon EF-S 10-22mm f/3.5-4.5 USM"},
		{236, u8"Canon EF-S 60mm f/2.8 Macro USM"},
		{237, u8"Canon EF 24-105mm f/4L IS"},
		{238, u8"Canon EF 70-300mm f/4-5.6 IS USM"},
		{239, u8"Canon EF 85mm f/1.2L II"},
		{240, u8"Canon EF-S 17-55mm f/2.8 IS USM"},
		{241, u8"Canon EF 50mm f/1.2L"},
		{242, u8"Canon EF 70-200mm f/4L IS"},
		{243, u8"Canon EF 70-200mm f/4L IS + 1.4x"},
		{244, u8"Canon EF 70-200mm f/4L IS + 2x"},
		{245, u8"Canon EF 70-200mm f/4L IS + 2.8x"},
		{246, u8"Canon EF 16-35mm f/2.8L II"},
		{247, u8"Canon EF 14mm f/2.8L II USM"},
		{248, u8"Canon EF 200mm f/2L IS"},
		{249, u8"Canon EF 800mm f/5.6L IS"},
		{250, u8"Canon EF 24 f/1.4L II"},
		{251, u8"Canon EF 70-200mm f/2.8L IS II USM"},
		{252, u8"Canon EF 70-200mm f/2.8L IS II USM + 1.4x"},
		{253, u8"Canon EF 70-200mm f/2.8L IS II USM + 2x"},
		{254, u8"Canon EF 100mm f/2.8L Macro IS USM"},
		{488, u8"Canon EF-S 15-85mm f/3.5-5.6 IS USM"},
		{489, u8"Canon EF 70-300mm f/4-5.6L IS USM"},
		{490, u8"Canon EF 8-15mm f/4L USM"},
		{491, u8"Canon EF 300mm f/2.8L IS II USM"},
		{492, u8"Canon EF 400mm f/2.8L IS II USM"},
		{493, u8"Canon EF 24-105mm f/4L IS USM"},
		{494, u8"Canon EF 600mm f/4.0L IS II USM"},
		{495, u8"Canon EF 24-70mm f/2.8L II USM"},
		{496, u8"Canon EF 200-400mm f/4L IS USM"},
		{502, u8"Canon EF 28mm f/2.8 IS USM"},
		{503, u8"Canon EF 24mm f/2.8 IS USM"},
		{504, u8"Canon EF 24-70mm f/4L IS USM"},
		{505, u8"Canon EF 35mm f/2 IS USM"},
		{4142, u8"Canon EF-S 18-135mm f/3.5-5.6 IS STM"},
		{4143, u8"Canon EF-M 18-55mm f/3.5-5.6 IS STM"},
		{4144, u8"Canon EF 40mm f/2.8 STM"},
		{4145, u8"Canon EF-M 22mm f/2 STM"},
		{4146, u8"Canon EF-S 18-55mm f/3.5-5.6 IS STM"},
		{4147, u8"Canon EF-M 11-22mm f/4-5.6 IS STM"}
	};

public:
	canon_lenses()
	{
	}

	str::cached Lookup(int id)
	{
		const auto found = _choices.find(id);
		str::cached result;

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
	auto lat = abs(_latitude);
	auto lng = abs(_longitude);

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
private:
	prop::item_metadata& _metadata;
	exif_gps_coordinate_builder _gps_coordinate;
	bool _created_date_set;

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
	}

	void tag(exif_dir_entry& entry)
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

	void canon_tag(exif_dir_entry& entry) const
	{
		static canon_lenses lenses;
		//df::log(__FUNCTION__, u8"Canon tag %x (%d)\n", entry.tag, entry.Size());

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

				str::cached lens_text;

				if (lens > 0)
				{
					lens_text = lenses.Lookup(lens);
				}

				if (lens_text.is_empty() && (low_focal_len >= 0 || high_focal_len >= 0) && focal_units > 0)
				{
					const auto low = low_focal_len / static_cast<double>(focal_units);
					const auto high = high_focal_len / static_cast<double>(focal_units);

					if (abs(low - high) < 0.1 || low < 0.1)
					{
						lens_text = str::cache(str::format(u8"{:.1}mm", high));
					}
					else if (low >= 0.1 && high >= 0.1)
					{
						lens_text = str::cache(str::format(u8"{:.1}f-{:.1}mm", low, high));
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

	int safe_rating(int r)
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

	void exif_tag(exif_dir_entry& entry)
	{
		//df::log(__FUNCTION__, u8"Exif tag %x (%d)\n", entry.tag, entry.Size());

		switch (entry._tag)
		{
		case EXIF_TAG_ORIENTATION:
			_metadata.orientation = static_cast<ui::orientation>(entry.get_uint16());
			break;

		case EXIF_TAG_APERTURE_VALUE:
			if (df::is_zero(_metadata.f_number))
			{
				_metadata.f_number = prop::aperture_to_fstop(entry.get_urational().to_real());
			}
			break;

		case EXIF_TAG_FNUMBER:
			_metadata.f_number = entry.get_urational().to_real();
			break;

		//case Exif::EXIF_TAG_SHUTTER_SPEED_VALUE: 
		case EXIF_TAG_EXPOSURE_TIME:
			_metadata.exposure_time = entry.get_srational().to_real();
			break;

		case EXIF_TAG_ISO_SPEED_RATINGS:
			_metadata.iso_speed = entry.get_uint16();
			break;

		case EXIF_TAG_FOCAL_LENGTH:
			_metadata.focal_length = entry.get_urational().to_real();
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

		case EXIF_TAG_IMAGE_RATING:
			_metadata.rating = safe_rating(entry.get_int16());
			break;

		case EXIF_TAG_IMAGE_RATING_PERCENT:
			_metadata.rating = safe_rating(df::round_up(entry.get_int16(), 20));
			break;

		case EXIF_TAG_DATE_TIME:
		case EXIF_TAG_DATE_TIME_ORIGINAL:
			{
				df::date_t ft;

				if (!_created_date_set &&
					ft.parse_exif_date(entry.text()) &&
					ft.is_valid())
				{
					_metadata.created_exif = ft;
					_created_date_set = true;
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

	void gps_tag(exif_dir_entry& entry)
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
		}
	}
};


void metadata_exif::parse(prop::item_metadata& pd, df::cspan cs)
{
	if (!cs.empty())
	{
		exif_camera_settings_processor processor(pd);
		exif_enumerate([&processor](exif_dir_entry& e) { processor.tag(e); }, cs);
	}
}

static void set_uint16(exif_dir_entry& e, df::span cs, uint16_t value)
{
	const auto offset = e.data_offset();

	if (e.is_intel())
	{
		cs.data[offset] = static_cast<uint8_t>(value);
		cs.data[offset + 1] = static_cast<uint8_t>(value >> 8);
	}
	else
	{
		cs.data[offset] = static_cast<uint8_t>(value >> 8);
		cs.data[offset + 1] = static_cast<uint8_t>(value);
	}
}

void metadata_exif::fix_exif_dimensions(df::span data, const sizei dimensions)
{
	if (data > 16)
	{
		df::assert_true(!is_exif_signature(data));

		const auto h = [dimensions, data](exif_dir_entry& e)
		{
			if (e._tag_type == tag_type::exif)
			{
				switch (e._tag)
				{
				case EXIF_TAG_PIXEL_X_DIMENSION:
					set_uint16(e, data, dimensions.cx);
					break;

				case EXIF_TAG_PIXEL_Y_DIMENSION:
					set_uint16(e, data, dimensions.cy);
					break;

				case EXIF_TAG_ORIENTATION:
					set_uint16(e, data, static_cast<int>(ui::orientation::top_left));
					break;
				}
			}
		};

		exif_enumerate(h, data);
	}
}

void metadata_exif::fix_exif_rating(df::span data, int rating)
{
	if (data > 16)
	{
		df::assert_true(!is_exif_signature(data));

		const auto h = [rating, data](exif_dir_entry& e)
		{
			if (e._tag_type == tag_type::exif)
			{
				switch (e._tag)
				{
				case EXIF_TAG_IMAGE_RATING:
					set_uint16(e, data, rating);
					break;

				case EXIF_TAG_IMAGE_RATING_PERCENT:
					set_uint16(e, data, rating * 20);
					break;
				}
			}
		};

		exif_enumerate(h, data);
	}
}


static long get_int(ExifData* ed, ExifEntry* ee)
{
	const ExifByteOrder o = exif_data_get_byte_order(ed);
	long value;

	switch (ee->format)
	{
	case EXIF_FORMAT_SHORT:
		value = exif_get_short(ee->data, o);
		break;
	case EXIF_FORMAT_LONG:
		value = exif_get_long(ee->data, o);
		break;
	case EXIF_FORMAT_SLONG:
		value = exif_get_slong(ee->data, o);
		break;
	default:
		{
			const auto* const message = "Invalid Exif byte order";
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}
	}
	return value;
}

static void update_tag(ExifData* ed, int ifd, ExifTag tag, int value)
{
	ExifEntry* ee;

	ee = exif_content_get_entry(ed->ifd[ifd], tag);
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
	void operator()(ExifData* x) { exif_data_unref(x); }
};

metadata_kv_list metadata_exif::to_info(df::cspan data)
{
	metadata_kv_list result;
	std::unique_ptr<ExifData, exif_free> ed;

	if (is_exif_signature(data))
	{
		ed = std::unique_ptr<ExifData, exif_free>(exif_data_new_from_data(data.data, data.size));
	}
	else
	{
		std::vector<uint8_t> with_sig;
		with_sig.reserve(data.size + exif_signature.size());
		with_sig.assign(exif_signature.begin(), exif_signature.end());
		with_sig.insert(with_sig.end(), data.data, data.data + data.size);
		ed = std::unique_ptr<ExifData, exif_free>(exif_data_new_from_data(with_sig.data(), with_sig.size()));
	}

	if (ed)
	{
		char buffer[df::sixty_four_k];

		for (const auto& i : ed->ifd)
		{
			if (i && i->count)
			{
				const auto* const content = i;

				for (auto j = 0u; j < content->count; j++)
				{
					const auto buffer_size = df::sixty_four_k;
					auto* const e = content->entries[j];
					const auto* const text = exif_entry_get_value(e, buffer, buffer_size);

					if (!is_junk(std::bit_cast<const uint8_t*>(text), 4))
					{
						result.emplace_back(str::cache(exif_tag_get_name_in_ifd(e->tag, exif_entry_get_ifd(e))),
						                    std::u8string(str::utf8_cast(text)));
					}
				}
			}
		}
	}

	return result;
}

df::blob metadata_exif::fix_dims(const df::span cs, const int image_width, const int image_height)
{
	df::blob result;

	df::assert_true(is_exif_signature(cs));
	const std::unique_ptr<ExifData, exif_free> ed(exif_data_new_from_data(cs.data, cs.size));

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
		result.assign(data, data + size);
		free(data);
	}
	return result;
}


static ExifEntry* create_tag(const ExifData* exif, const ExifIfd ifd, const ExifTag tag, const size_t len)
{
	//  Create a memory allocator to manage this ExifEntry 
	ExifMem* mem = exif_mem_new_default();
	df::assert_true(mem != nullptr); //  catch an out of memory condition 

	//  Create a new ExifEntry using our allocator 
	auto* const entry = exif_entry_new_mem(mem);
	df::assert_true(entry != nullptr);

	//  Allocate memory to use for holding the tag data 
	auto* const buf = static_cast<uint8_t*>(exif_mem_alloc(mem, len));
	df::assert_true(buf != nullptr);

	//  Fill in the entry 
	entry->data = buf;
	entry->size = len;
	entry->tag = tag;
	entry->components = len;
	entry->format = EXIF_FORMAT_UNDEFINED;

	//  Attach the ExifEntry to an IFD 
	exif_content_add_entry(exif->ifd[ifd], entry);

	//  The ExifMem and ExifEntry are now owned elsewhere 
	exif_mem_unref(mem);
	exif_entry_unref(entry);

	return entry;
}

//  Get an existing tag, or create one if it doesn't exist 
static ExifEntry* init_tag(ExifData* exif, ExifIfd ifd, ExifTag tag)
{
	ExifEntry* entry;
	//  Return an existing tag if one exists 
	if (!((entry = exif_content_get_entry(exif->ifd[ifd], tag))))
	{
		//  Allocate a new entry 
		entry = exif_entry_new();
		df::assert_true(entry != nullptr); //  catch an out of memory condition 
		entry->tag = tag; //  tag must be set before calling
		//	 exif_content_add_entry 

		//  Attach the ExifEntry to an IFD 
		exif_content_add_entry(exif->ifd[ifd], entry);

		//  Allocate memory for the entry and fill with default data 
		exif_entry_initialize(entry, tag);

		//  Ownership of the ExifEntry has now been passed to the IFD.
		// One must be very careful in accessing a structure after
		// unref'ing it; in this case, we know u8"entry" won't be freed
		// because the reference count was bumped when it was added to
		// the IFD.

		exif_entry_unref(entry);
	}
	return entry;
}

#define FILE_BYTE_ORDER EXIF_BYTE_ORDER_INTEL
#define ASCII_COMMENT u8"ASCII\0\0\0"

void add_tag(ExifData* exif, ExifTag tag, const str::cached val)
{
	auto* const mem = exif_mem_new_default();

	if (mem)
	{
		auto* const entry = exif_entry_new_mem(mem);

		if (entry)
		{
			const auto len = val.size() + 1;
			const auto ifd = EXIF_IFD_EXIF;
			auto* const buf = static_cast<uint8_t*>(exif_mem_alloc(mem, len));

			if (buf)
			{
				entry->data = buf;
				entry->size = len;
				entry->tag = tag;
				entry->components = len;
				entry->format = EXIF_FORMAT_UNDEFINED;

				memcpy(entry->data, val.sz(), len);

				exif_content_add_entry(exif->ifd[ifd], entry);
			}
			exif_entry_unref(entry);
		}

		exif_mem_unref(mem);
	}
}

void add_tag(ExifData* exif, ExifTag tag, const float val)
{
	/*const auto mem = exif_mem_new_default();

	if (mem)
	{
		const auto entry = exif_entry_new_mem(mem);

		if (entry)
		{
			update_tag(exif, EXIF_IFD_0, EXIF_TAG_ORIENTATION, 1);
			
			const auto len = val.size() + 1;
			const auto ifd = EXIF_IFD_EXIF;
			const auto buf = static_cast<uint8_t*>(exif_mem_alloc(mem, len));

			if (buf)
			{
				entry->data = buf;
				entry->size = len;
				entry->tag = tag;
				entry->components = len;
				entry->format = EXIF_FORMAT_UNDEFINED;

				entry = create_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_SUBJECT_AREA, 4 * exif_format_get_size(EXIF_FORMAT_SHORT));
				entry->format = EXIF_FORMAT_SHORT;
				entry->components = 4;
				exif_set_rational(uint8_t* b, ExifByteOrder order,
					ExifRational value);

				memcpy(entry->data, val.sz(), len);

				exif_content_add_entry(exif->ifd[ifd], entry);
			}
			exif_entry_unref(entry);
		}

		exif_mem_unref(mem);
	}*/
}

void add_tag(ExifData* exif, ExifTag tag, const int val)
{
	/*const auto mem = exif_mem_new_default();

	if (mem)
	{
		const auto entry = exif_entry_new_mem(mem);

		if (entry)
		{
			update_tag(exif, EXIF_IFD_0, EXIF_TAG_ORIENTATION, 1);

			const auto len = val.size() + 1;
			const auto ifd = EXIF_IFD_EXIF;
			const auto buf = static_cast<uint8_t*>(exif_mem_alloc(mem, len));

			if (buf)
			{
				entry->data = buf;
				entry->size = len;
				entry->tag = tag;
				entry->components = len;
				entry->format = EXIF_FORMAT_UNDEFINED;

				entry = create_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_SUBJECT_AREA, 4 * exif_format_get_size(EXIF_FORMAT_SHORT));
				entry->format = EXIF_FORMAT_SHORT;
				entry->components = 4;
				exif_set_rational(uint8_t* b, ExifByteOrder order,
					ExifRational value);

				memcpy(entry->data, val.sz(), len);

				exif_content_add_entry(exif->ifd[ifd], entry);
			}
			exif_entry_unref(entry);
		}

		exif_mem_unref(mem);
	}*/
}

df::blob metadata_exif::make_exif(const prop::item_metadata_ptr& md)
{
	df::blob result;

	ExifEntry* entry;
	ExifData* exif = exif_data_new();

	if (exif)
	{
		//  Set the image options 
		exif_data_set_option(exif, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
		exif_data_set_data_type(exif, EXIF_DATA_TYPE_COMPRESSED);
		exif_data_set_byte_order(exif, FILE_BYTE_ORDER);

		//  Create the mandatory EXIF fields with default data 
		exif_data_fix(exif);

		//if (md->orientation != ui::orientation::none) add_tag(exif, EXIF_TAG_ORIENTATION, md->orientation);
		if (!prop::is_null(md->f_number)) add_tag(exif, EXIF_TAG_FNUMBER, md->f_number);
		if (!prop::is_null(md->exposure_time)) add_tag(exif, EXIF_TAG_EXPOSURE_TIME, md->exposure_time);
		if (!prop::is_null(md->iso_speed)) add_tag(exif, EXIF_TAG_ISO_SPEED_RATINGS, md->iso_speed);
		if (!prop::is_null(md->focal_length)) add_tag(exif, EXIF_TAG_FOCAL_LENGTH, md->focal_length);
		if (!prop::is_null(md->focal_length_35mm_equivalent)) add_tag(exif, EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM, md->focal_length_35mm_equivalent);
		if (!prop::is_null(md->camera_manufacturer)) add_tag(exif, EXIF_TAG_MAKE, md->camera_manufacturer);
		if (!prop::is_null(md->camera_model)) add_tag(exif, EXIF_TAG_MODEL, md->camera_model);
		if (!prop::is_null(md->lens)) add_tag(exif, EXIF_TAG_LENS_MODEL, md->lens);
		if (!prop::is_null(md->copyright_notice)) add_tag(exif, EXIF_TAG_COPYRIGHT, md->copyright_notice);
		if (!prop::is_null(md->description)) add_tag(exif, EXIF_TAG_IMAGE_DESCRIPTION, md->description);
		if (!prop::is_null(md->comment)) add_tag(exif, EXIF_TAG_USER_COMMENT, md->comment);
		//if (!prop::is_null(md->rating)) add_tag(exif, EXIF_TAG_IMAGE_RATING, md->rating);
		//if (!prop::is_null(md->created_exif)) add_tag(exif, EXIF_TAG_DATE_TIME, md->created_exif);
		//if (!prop::is_null(md->created_digitized)) add_tag(exif, EXIF_TAG_DATE_TIME_DIGITIZED, md->created_digitized);

		////  All these tags are created with default values by exif_data_fix() 
		////  Change the data to the correct values for this image. 
		//entry = init_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_X_DIMENSION);
		//exif_set_long(entry->data, FILE_BYTE_ORDER, image_jpg_x);

		//entry = init_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_PIXEL_Y_DIMENSION);
		//exif_set_long(entry->data, FILE_BYTE_ORDER, image_jpg_y);

		//entry = init_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_COLOR_SPACE);
		//exif_set_short(entry->data, FILE_BYTE_ORDER, 1);

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
		//entry = create_tag(exif, EXIF_IFD_EXIF, EXIF_TAG_SUBJECT_AREA, 4 * exif_format_get_size(EXIF_FORMAT_SHORT));
		//entry->format = EXIF_FORMAT_SHORT;
		//entry->components = 4;
		//exif_set_short(entry->data, FILE_BYTE_ORDER, image_jpg_x / 2);
		//exif_set_short(entry->data + 2, FILE_BYTE_ORDER, image_jpg_y / 2);
		//exif_set_short(entry->data + 4, FILE_BYTE_ORDER, image_jpg_x);
		//exif_set_short(entry->data + 6, FILE_BYTE_ORDER, image_jpg_y);

		/*if (!prop::is_null(md->album)) row.write(prop::album.id, md->album);
		if (!prop::is_null(md->album_artist)) row.write(prop::album_artist.id, md->album_artist);
		if (!prop::is_null(md->artist)) row.write(prop::artist.id, md->artist);
		if (!prop::is_null(md->audio_codec)) row.write(prop::audio_codec.id, md->audio_codec);
		if (!prop::is_null(md->bitrate)) row.write(prop::bitrate.id, md->bitrate);
		if (!prop::is_null(md->camera_manufacturer)) row.write(prop::camera_manufacturer.id, md->camera_manufacturer);
		if (!prop::is_null(md->camera_model)) row.write(prop::camera_model.id, md->camera_model);
		if (!prop::is_null(md->location_place)) row.write(prop::location_place.id, md->location_place);
		if (!prop::is_null(md->comment)) row.write(prop::comment.id, md->comment);
		if (!prop::is_null(md->copyright_creator)) row.write(prop::copyright_creator.id, md->copyright_creator);
		if (!prop::is_null(md->copyright_credit)) row.write(prop::copyright_credit.id, md->copyright_credit);
		if (!prop::is_null(md->copyright_notice)) row.write(prop::copyright_notice.id, md->copyright_notice);
		if (!prop::is_null(md->copyright_source)) row.write(prop::copyright_source.id, md->copyright_source);
		if (!prop::is_null(md->copyright_url)) row.write(prop::copyright_url.id, md->copyright_url);
		if (!prop::is_null(md->location_country)) row.write(prop::location_country.id, md->location_country);
		if (!prop::is_null(md->description)) row.write(prop::description.id, md->description);
		if (!prop::is_null(md->file_name)) row.write(prop::file_name.id, md->file_name);
		if (!prop::is_null(md->raw_file_name)) row.write(prop::raw_file_name.id, md->file_name);
		if (!prop::is_null(md->genre)) row.write(prop::genre.id, md->genre);
		if (!prop::is_null(md->lens)) row.write(prop::lens.id, md->lens);
		if (!prop::is_null(md->pixel_format)) row.write(prop::pixel_format.id, md->pixel_format);
		if (!prop::is_null(md->show)) row.write(prop::show.id, md->show);
		if (!prop::is_null(md->location_state)) row.write(prop::location_state.id, md->location_state);
		if (!prop::is_null(md->synopsis)) row.write(prop::synopsis.id, md->synopsis);
		if (!prop::is_null(md->composer)) row.write(prop::composer.id, md->composer);
		if (!prop::is_null(md->encoder)) row.write(prop::encoder.id, md->encoder);
		if (!prop::is_null(md->publisher)) row.write(prop::publisher.id, md->publisher);
		if (!prop::is_null(md->performer)) row.write(prop::performer.id, md->performer);
		if (!prop::is_null(md->title)) row.write(prop::title.id, md->title);
		if (!prop::is_null(md->tags)) row.write(prop::tag.id, md->tags);
		if (!prop::is_null(md->video_codec)) row.write(prop::video_codec.id, md->video_codec);
		if (!prop::is_null(md->game)) row.write(prop::game.id, md->game);
		if (!prop::is_null(md->system)) row.write(prop::system.id, md->system);
		if (!prop::is_null(md->label)) row.write(prop::label.id, md->label);
		if (!prop::is_null(md->doc_id)) row.write(prop::doc_id.id, md->doc_id);

		if (!prop::is_null(md->width) || !prop::is_null(md->height)) row.write(prop::dimensions.id, df::xy32::make(md->width, md->height));
		if (!prop::is_null(md->iso_speed)) row.write(prop::iso_speed.id, md->iso_speed);
		if (!prop::is_null(md->focal_length)) row.write(prop::focal_length.id, md->focal_length);
		if (!prop::is_null(md->focal_length_35mm_equivalent)) row.write(prop::focal_length_35mm_equivalent.id, md->focal_length_35mm_equivalent);
		if (!prop::is_null(md->rating)) row.write(prop::rating.id, md->rating);
		if (!prop::is_null(md->audio_sample_rate)) row.write(prop::audio_sample_rate.id, md->audio_sample_rate);
		if (!prop::is_null(md->audio_sample_type)) row.write(prop::audio_sample_type.id, md->audio_sample_type);
		if (!prop::is_null(md->season)) row.write(prop::season.id, md->season);
		if (!prop::is_null(md->track)) row.write(prop::track_num.id, md->track);
		if (!prop::is_null(md->disk)) row.write(prop::disk_num.id, md->disk);
		if (!prop::is_null(md->duration)) row.write(prop::duration.id, md->duration);
		if (!prop::is_null(md->episode)) row.write(prop::episode.id, md->episode);
		if (!prop::is_null(md->exposure_time)) row.write(prop::exposure_time.id, md->exposure_time);
		if (!prop::is_null(md->f_number)) row.write(prop::f_number.id, md->f_number);

		if (!prop::is_null(md->created_exif)) row.write(prop::created_exif.id, md->created_exif.to_int64());
		if (!prop::is_null(md->created_digitized)) row.write(prop::created_digitized.id, md->created_digitized.to_int64());*/

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

// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "metadata_icc.h"


enum icc_types
{
	TYPE_XYZ = 0x58595A20,
	// 'XYZ '
	TYPE_PARA = 0,
	// 'para'
	TYPE_RGB = 1,
	// 'RGB '
	TAG_WTPT = 0x77747074,
	// 'wtpt'
	TAG_R_XYZ = 0x7258595A,
	// 'rXYZ'
	TAG_R_TRC = 0x72545243,
	// 'rTRC'
	TAG_G_XYZ = 0x6758595A,
	// 'gXYZ'
	TAG_G_TRC = 0x67545243,
	// 'gTRC'
	TAG_B_XYZ = 0x6258595A,
	// 'bXYZ'
	TAG_B_TRC = 0x62545243,
	// 'bTRC'
};


class icc_profile
{
public:
	struct DateTime
	{
		uint16_t year_;
		uint16_t month_;
		uint16_t day_;
		uint16_t hour_;
		uint16_t min_;
		uint16_t sec_;
	};

	struct XYZ
	{
		double x_;
		double y_;
		double z_;
	};

	struct Tag
	{
		uint32_t type_;
		std::vector<uint8_t> data_;

		Tag()
		{
		}

		Tag(const uint32_t type, std::vector<uint8_t> data) : type_(type), data_(std::move(data))
		{
		}
	};

	uint32_t profileSize_;
	uint32_t cmmType_;
	uint32_t profileVersion_;
	uint32_t profileClass_;
	uint32_t colorSpace_;
	uint32_t connectionSpace_;
	DateTime dtime_;
	uint32_t acsp_;
	uint32_t platform_;
	uint32_t flags_;
	uint32_t deviceManufacture_;
	uint32_t deviceModel_;
	uint64_t deviceAttrib_;
	uint32_t intent_;
	XYZ connectionIllum_;
	uint32_t creator_;

	std::map<uint32_t, Tag> tags_;

	std::u8string dump4(const uint32_t u)
	{
		return str::print(u8"%c%c%c%c", (u >> 24) & 0xFF, (u >> 16) & 0xFF, (u >> 8) & 0xFF, u & 0xFF);
	}

	metadata_kv_list dump()
	{
		metadata_kv_list result;

		result.emplace_back(u8"Size"_c, str::print(u8"%x", profileSize_));
		result.emplace_back(u8"CMM Type"_c, dump4(cmmType_));
		result.emplace_back(u8"Version"_c,
		                    str::print(u8"%x (version %d)", profileVersion_, (profileVersion_ >> 24) & 0xFF));
		result.emplace_back(u8"Class"_c, dump4(profileClass_));
		result.emplace_back(u8"Color Space Data"_c, dump4(colorSpace_));
		result.emplace_back(u8"Connection Space"_c, dump4(connectionSpace_));
		result.emplace_back(u8"Date Time"_c,
		                    str::print(u8"%d-%d-%d,%d:%d,%d", dtime_.year_, dtime_.month_, dtime_.day_, dtime_.hour_,
		                               dtime_.min_, dtime_.sec_));
		result.emplace_back(u8"File Signature"_c, dump4(acsp_));
		result.emplace_back(u8"Primary Platform"_c, dump4(platform_));
		result.emplace_back(u8"CMM Flags"_c, str::print(u8"%x", flags_));
		result.emplace_back(u8"Device Manufacturer"_c, dump4(deviceManufacture_));
		result.emplace_back(u8"Device Model"_c, dump4(deviceModel_));
		result.emplace_back(u8"Device Attributes"_c, str::print(u8"%I64x", deviceAttrib_));
		result.emplace_back(u8"Rendering Intent"_c, str::print(u8"%d", intent_));
		result.emplace_back(u8"Connection Space Illuminant"_c,
		                    str::print(u8"(%f,%f,%f)", connectionIllum_.x_, connectionIllum_.y_, connectionIllum_.z_));
		result.emplace_back(u8"Creator"_c, dump4(creator_));

		for (const auto& t : tags_)
		{
			auto name = str::print(u8"%c%c%c%c:%c%c%c%c", (t.first >> 24) & 0xFF, (t.first >> 16) & 0xFF,
			                       (t.first >> 8) & 0xFF,
			                       t.first & 0xFF, (t.second.type_ >> 24) & 0xFF, (t.second.type_ >> 16) & 0xFF,
			                       (t.second.type_ >> 8) & 0xFF, t.second.type_ & 0xFF);

			auto max_text_len = 16_z;
			auto hex = str::to_hex(t.second.data_.data(), std::min(t.second.data_.size(), max_text_len));
			if (t.second.data_.size() > max_text_len) hex += u8"...";
			result.emplace_back(str::cache(name), hex);
		}

		return result;
	}
};

class icc_stream
{
public:
	const uint8_t* buffer_;
	size_t size_;
	size_t index_;

	icc_stream(const df::cspan data, const size_t index = 0)
		: buffer_(data.data), size_(data.size), index_(index)
	{
	}

	virtual ~icc_stream()
	{
		buffer_ = nullptr;
		size_ = 0;
	}

	bool eof() const { return index_ >= size_; }

	void skip(const size_t bytes)
	{
		index_ += bytes;
		if (index_ > size_)
			index_ = size_ - 1;
	}

	const uint32_t seek(const size_t index)
	{
		const size_t current = index_;
		index_ = index;
		if (index_ > size_)
			index_ = size_ - 1;
		return current;
	}

	const uint8_t uint8()
	{
		if (index_ >= size_)
			return 0; // EOF
		const uint8_t ret = buffer_[index_];
		index_++;
		return ret;
	}

	const int8_t int8() { return static_cast<int8_t>(uint8()); }

	// ICC profile uses big endian only.
	const uint16_t uint16() { return (uint8() << 8) | (uint8()); }
	const int16_t int16() { return (int8() << 8) | (uint8()); }
	const uint32_t uint32() { return (uint16() << 16) | (uint16()); }
	const int32_t int32() { return (int16() << 16) | (uint16()); }
	const uint64_t uint64() { return (static_cast<uint64_t>(uint32()) << 32) | (uint32()); }

	const double s15Fixed16() { return static_cast<double>(int32()) / 0x10000; }
	const double u16Fixed16() { return static_cast<double>(uint32()) / 0x10000; }
	const double u8Fixed8() { return static_cast<double>(uint16()) / 0x100; }

	std::vector<uint8_t> array(const size_t s)
	{
		std::vector<uint8_t> ret(s);
		std::copy(buffer_ + index_, buffer_ + index_ + s, ret.begin());
		return ret;
	}

	const icc_profile::DateTime dateTime()
	{
		icc_profile::DateTime dt;
		dt.year_ = uint16();
		dt.month_ = uint16();
		dt.day_ = uint16();
		dt.hour_ = uint16();
		dt.min_ = uint16();
		dt.sec_ = uint16();
		return dt;
	}

	const icc_profile::XYZ xyz()
	{
		icc_profile::XYZ xyz;
		xyz.x_ = s15Fixed16();
		xyz.y_ = s15Fixed16();
		xyz.z_ = s15Fixed16();
		return xyz;
	}
};

bool load_from_mem(icc_profile& p, df::cspan data)
{
	icc_stream stream(data);
	p.profileSize_ = stream.uint32();
	p.cmmType_ = stream.uint32();
	p.profileVersion_ = stream.uint32();
	p.profileClass_ = stream.uint32();
	p.colorSpace_ = stream.uint32();
	p.connectionSpace_ = stream.uint32();
	p.dtime_ = stream.dateTime();
	p.acsp_ = stream.uint32(); // must be acsp
	p.platform_ = stream.uint32();
	p.flags_ = stream.uint32();
	p.deviceManufacture_ = stream.uint32();
	p.deviceModel_ = stream.uint32();
	p.deviceAttrib_ = stream.uint64();
	p.intent_ = stream.uint32();
	p.connectionIllum_ = stream.xyz();
	p.creator_ = stream.uint32();
	stream.seek(128); // header

	if (p.acsp_ != 0x61637370)
	{
		// not valid Signature 'acsp'
		return false;
	}
	if (stream.eof())
		return false;

	const uint32_t tagCount = stream.uint32();
	for (uint32_t u = 0; u < tagCount; u++)
	{
		uint32_t sig = stream.uint32();
		const uint32_t offs = stream.uint32();
		const uint32_t size = stream.uint32();
		const size_t current = stream.seek(offs);
		p.tags_[sig] = icc_profile::Tag(stream.uint32(), stream.array(size));
		stream.seek(current);
	}

	return true;
}

metadata_kv_list metadata_icc::to_info(df::cspan data)
{
	icc_profile p;

	if (load_from_mem(p, data))
	{
		return p.dump();
	}

	return {};
}

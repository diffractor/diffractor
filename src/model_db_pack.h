// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once


class metadata_packer
{
public:
	std::vector<uint8_t> _data;

public:
	void reset_to_header()
	{
		_data.clear();
		_data.reserve(256);
		_data.push_back(0xff); // marker
		_data.push_back(0x01); // version
	}

	metadata_packer()
	{
		reset_to_header();
	}

	df::cspan cdata() const
	{
		return _data;
	}

	size_t size() const
	{
		return _data.size();
	}

	void write_prop_id(const uint16_t id)
	{
		_data.push_back(id & 0xff);
		_data.push_back((id >> 8) & 0xff);
	}

	void write_len(const size_t val_len)
	{
		if (val_len < 0xfe)
		{
			_data.push_back(val_len & 0xff);
		}
		else if (val_len < 0xffff)
		{
			_data.push_back(0xff);
			_data.push_back(val_len & 0xff);
			_data.push_back((val_len >> 8) & 0xff);
		}
		else
		{
			_data.push_back(0xfe);
			_data.push_back(val_len & 0xff);
			_data.push_back((val_len >> 8) & 0xff);
			_data.push_back((val_len >> 16) & 0xff);
			_data.push_back((val_len >> 24) & 0xff);
		}
	}

	template <typename T>
	void write(const uint16_t id, const T& v)
	{
		constexpr auto val_len = sizeof(v);

		write_prop_id(id);
		write_len(val_len);

		const auto existing_len = _data.size();
		_data.resize(existing_len + val_len);
		*std::bit_cast<T*>(_data.data() + existing_len) = v;
	}

	void write(const uint16_t id, const str::cached v)
	{
		const auto val_len = v.size();

		if (val_len > 0 && val_len < df::one_meg)
		{
			write_prop_id(id);
			write_len(val_len);

			const auto existing_len = _data.size();
			_data.resize(existing_len + val_len);
			const auto* const src = std::bit_cast<const uint8_t*>(v.sz());
			std::copy(src, src + val_len, _data.begin() + existing_len);
		}
	}

	void pack(const prop::item_metadata_ptr& md);
};


class metadata_unpacker
{
	const df::cspan _data;
	size_t _pos = 2;
	uint32_t _version = 0;

public:
	metadata_unpacker(const df::cspan data) : _data(data)
	{
		if (_data.size >= 2 && _data.data[0] == 0xFF)
		{
			_version = _data.data[1];
		}
	}

	size_t remaining() const
	{
		return (_pos >= _data.size) ? 0 : _data.size - _pos;
	}

	prop::key_ref read_type()
	{
		if (_version != 1 || remaining() < 2)
		{
			return prop::null;
		}

		auto id = static_cast<uint16_t>(_data.data[_pos++]);
		id |= static_cast<uint16_t>(_data.data[_pos++] << 8);
		return prop::from_id(id);
	}

	const size_t read_len()
	{
		size_t result = _data.data[_pos++];

		if (result == 0xFF)
		{
			result = static_cast<size_t>(_data.data[_pos++]);
			result |= static_cast<size_t>(_data.data[_pos++]) << 8;
		}
		else if (result == 0xFE)
		{
			result = static_cast<size_t>(_data.data[_pos++]);
			result |= static_cast<size_t>(_data.data[_pos++]) << 8;
			result |= static_cast<size_t>(_data.data[_pos++]) << 16;
			result |= static_cast<size_t>(_data.data[_pos++]) << 24;
		}

		return result;
	}

	template <typename T>
	void read_val(T& v)
	{
		const auto ser_len = read_len();
		df::assert_true(sizeof(v) == ser_len);

		if (sizeof(v) == ser_len)
		{
			v = *std::bit_cast<const T*>(_data.data + _pos);
		}

		_pos += ser_len;
	}

	void read_val(str::cached& v)
	{
		const auto ser_len = read_len();
		v = str::cache(std::u8string_view{ std::bit_cast<const char8_t*>(_data.data + _pos), ser_len });
		_pos += ser_len;
	}

	void unpack(const prop::item_metadata_ptr& md);
};

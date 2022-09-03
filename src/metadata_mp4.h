// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "util.h"

namespace mp4_file
{
	class atom_t;

#define A9(x) ((x) | 0xa9000000u)

	static constexpr uint32_t ALBUM = A9('alb');
	static constexpr uint32_t ALBUMARTIST = 'aART';
	static constexpr uint32_t ARTIST = A9('ART');
	static constexpr uint32_t ARTISTID = 'atID';
	static constexpr uint32_t CATEGORY = 'catg';
	static constexpr uint32_t COMMENTS = A9('cmt');
	static constexpr uint32_t COMPILATION = 'cpil';
	static constexpr uint32_t COMPOSER = A9('wrt');
	static constexpr uint32_t COMPOSERID = 'cmID';
	static constexpr uint32_t CONTENTID = 'cnID';
	static constexpr uint32_t CONTENTRATING = 'rtng';
	static constexpr uint32_t COPYRIGHT = 'cprt';
	static constexpr uint32_t DATE = 'date';
	static constexpr uint32_t DESCRIPTION = 'desc';
	static constexpr uint32_t DISK = 'disk';
	static constexpr uint32_t ENCODEDBY = A9('enc');
	static constexpr uint32_t ENCODINGTOOL = A9('too');
	static constexpr uint32_t GAPLESS = 'pgap';
	static constexpr uint32_t GENRE = A9('gen');
	static constexpr uint32_t GENREID = 'geID';
	static constexpr uint32_t GENRETYPE = 'gnre';
	static constexpr uint32_t GROUPING = A9('grp');
	static constexpr uint32_t HDVIDEO = 'hdvd';
	static constexpr uint32_t ITUNESACCOUNT = 'apID';
	static constexpr uint32_t ITUNESACCOUNTTYPE = 'akID';
	static constexpr uint32_t ITUNESCOUNTRY = 'sfID';
	static constexpr uint32_t KEYWORDS = 'keyw';
	static constexpr uint32_t LONGDESCRIPTION = 'ldes';
	static constexpr uint32_t LYRICS = A9('lyr');
	static constexpr uint32_t MEDIATYPE = 'stik';
	static constexpr uint32_t NAME = A9('nam');
	static constexpr uint32_t PLAYLISTID = 'plID';
	static constexpr uint32_t PODCAST = 'pcst';
	static constexpr uint32_t PURCHASEDATE = 'purd';
	static constexpr uint32_t RELEASEDATE = A9('day');
	static constexpr uint32_t SORTALBUM = 'soal';
	static constexpr uint32_t SORTALBUMARTIST = 'soaa';
	static constexpr uint32_t SORTARTIST = 'soar';
	static constexpr uint32_t SORTCOMPOSER = 'soco';
	static constexpr uint32_t SORTNAME = 'sonm';
	static constexpr uint32_t SORTTVSHOW = 'sosn';
	static constexpr uint32_t TEMPO = 'tmpo';
	static constexpr uint32_t TRACK = 'trkn';
	static constexpr uint32_t TVEPISODE = 'tves';
	static constexpr uint32_t TVEPISODEID = 'tven';
	static constexpr uint32_t TVNETWORK = 'tvnn';
	static constexpr uint32_t TVSEASON = 'tvsn';
	static constexpr uint32_t TVSHOW = 'tvsh';
	static constexpr uint32_t XID = 'xid ';
	static constexpr uint32_t XYZ = A9('xyz');
	static constexpr uint32_t RATING = 'rate';
	static constexpr uint32_t COVER_ART = 'covr';

	static constexpr uint32_t XMP_PACKET = 'uuid';

	static constexpr uint8_t XMP_UUID[16] = {
		0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC
	};

	// MAKE_TAG2(0xa9,'a','u','t'): Artist?
	// MAKE_TAG2(0xa9,'c','p','y'):  Copyright?
	// MAKE_TAG2(0xa9,'i','n','f') Comments?
	// MAKE_TAG2(0xa9,'s','w','r') Encoder software

	enum AtomDataType
	{
		TypeImplicit = 0,
		// for use with tags for which no type needs to be indicated because only one type is allowed
		TypeUTF8 = 1,
		// without any count or null terminator
		TypeUTF16 = 2,
		// also known as UTF-16BE
		TypeSJIS = 3,
		// deprecated unless it is needed for special Japanese characters
		TypeHTML = 6,
		// the HTML file header specifies which HTML version
		TypeXML = 7,
		// the XML header must identify the DTD or schemas
		TypeUUID = 8,
		// also known as GUID; stored as 16 bytes in binary (valid as an ID)
		TypeISRC = 9,
		// stored as UTF-8 text (valid as an ID)
		TypeMI3P = 10,
		// stored as UTF-8 text (valid as an ID)
		TypeGIF = 12,
		// (deprecated) a GIF image
		TypeJPEG = 13,
		// a JPEG image
		TypePNG = 14,
		// a PNG image
		TypeURL = 15,
		// absolute, in UTF-8 characters
		TypeDuration = 16,
		// in milliseconds, 32-bit integer
		TypeDateTime = 17,
		// in UTC, counting seconds since midnight, January 1, 1904; 32 or 64-bits
		TypeGenred = 18,
		// a list of enumerated values
		TypeInteger = 21,
		// a signed big-endian integer with length one of { 1,2,3,4,8 } bytes
		TypeRIAAPA = 24,
		// RIAA parental advisory; { -1=no, 1=yes, 0=unspecified }, 8-bit ingteger
		TypeUPC = 25,
		// Universal Product Code, in text UTF-8 format (valid as an ID)
		TypeBMP = 27,
		// Windows image image
		TypeUndefined = 255 // undefined
	};

	using atom_ptr = std::shared_ptr<atom_t>;
	using atom_list = std::vector<atom_ptr>;
	//typedef std::vector<std::vector<uint8_t>> bit_vectors;

	class atom_t : public df::no_copy, public std::enable_shared_from_this<atom_t>
	{
	private:
		int64_t offset{0};
		int64_t length{0};
		uint32_t name{0};
		atom_list children;

	public:
		atom_t() = default;
		~atom_t() = default;

		static bool is_container(uint32_t name)
		{
			static const std::array<uint32_t, 11> containers = {
				'moov', 'udta', 'mdia', 'meta', 'ilst', 'stbl', 'minf', 'moof', 'traf', 'trak', 'stsd'
			};
			return std::ranges::find(containers, name) != containers.end();
		}

		void load(df::file& file, int64_t start, int64_t limit)
		{
			offset = start;

			uint8_t buffer[32] = {0};
			auto headerLength = 8;

			if (!file.seek_from_begin(offset) || !file.read(buffer, 8))
			{
				throw app_exception("MP4: Couldn't read 8 bytes of data for atom header");
			}

			length = df::byteswap32(buffer);

			if (length == 0 && (offset + 4) < (limit - 8))
			{
				// Padding?
				if (!file.seek_from_begin(offset + 4) || !file.read(buffer, 8))
				{
					throw app_exception("MP4: failed to read after padding");
				}

				offset = offset + 4;
				length = df::byteswap32(buffer);
			}

			name = df::byteswap32(buffer + 4);

			if (length == 1 && (offset + 8) < (limit - 8))
			{
				if (!file.read(buffer, 8))
				{
					throw app_exception("MP4: failed to read 64 bit offset");
				}

				length = df::byteswap64(buffer);
				headerLength += 8;
			}

			if (length < 8 || (offset + length) > limit)
			{
				throw app_exception("MP4: Invalid atom size");
			}

			if (is_container(name))
			{
				if (name == 'meta')
				{
					// Detect the hdlr
					if (length < 32 || !file.seek_from_begin(offset + 8) || !file.read(buffer, 24))
					{
						throw app_exception("MP4: short meta block");
					}

					if (df::byteswap32(buffer + 8) == 'hdlr')
					{
						headerLength += 4;
					}
					else if (df::byteswap32(buffer + 4) == 'hdlr')
					{
					}
					else
					{
						throw app_exception("MP4: cannon detect hdlr after meta block");
					}
				}
				else if (name == 'stsd')
				{
					headerLength += 8;
				}

				auto childOffset = offset + headerLength;

				while (childOffset + 8 <= offset + length)
				{
					auto child = std::make_shared<atom_t>();
					children.emplace_back(child);
					child->load(file, childOffset, limit);
					childOffset = child->offset + child->length;
				}
			}
		}

		void trace_structure(std::u8string& s, std::u8string path = {})
		{
			const uint32_t buffer[2] = {df::byteswap32(name), 0};
			path += std::bit_cast<const char8_t*>(static_cast<const uint32_t*>(buffer));

			if (children.empty())
			{
				s += path;
				s += u8" - ";
				s += str::to_string(offset);
				s += u8" - ";
				s += str::to_string(length);
				s += u8"\n";
			}
			else
			{
				path += u8".";

				for (const auto& atom : children)
				{
					atom->trace_structure(s, path);
				}
			}
		}

		atom_ptr find_xmp(const df::file& file)
		{
			for (const auto& atom : children)
			{
				if (atom->name == XMP_PACKET)
				{
					file.seek_from_begin(atom->offset + 8);

					uint8_t uuid[16];
					memset(uuid, 0, 16);

					if (file.read(uuid, 16) && std::memcmp(uuid, XMP_UUID, 16) == 0)
						return atom;
				}
			}

			// todo could be moov\udta\meta\XMP_ 

			return nullptr;
		}

		atom_ptr find(uint32_t name1, uint32_t name2 = 0, uint32_t name3 = 0, uint32_t name4 = 0, uint32_t name5 = 0)
		{
			if (name1 == 0)
			{
				return shared_from_this();
			}

			for (const auto& atom : children)
			{
				if (atom->name == name1)
				{
					return atom->find(name2, name3, name4, name5);
				}
			}

			return nullptr;
		}

		atom_list find_all(uint32_t name, bool recursive = false)
		{
			atom_list result;

			for (const auto& atom : children)
			{
				if (atom->name == name)
				{
					result.emplace_back(atom);
				}
				if (recursive)
				{
					auto more = atom->find_all(name, recursive);
					result.insert(result.cend(), more.cbegin(), more.cend());
				}
			}
			return result;
		}

		bool path(atom_list& path, uint32_t name1, uint32_t name2 = 0, uint32_t name3 = 0)
		{
			path.emplace_back(shared_from_this());

			if (name1 == 0)
			{
				return true;
			}
			for (const auto& atom : children)
			{
				if (atom->name == name1)
				{
					return atom->path(path, name2, name3);
				}
			}
			return false;
		}

		atom_list path(uint32_t name1, uint32_t name2 = 0, uint32_t name3 = 0, uint32_t name4 = 0)
		{
			atom_list path;

			for (const auto& atom : children)
			{
				if (atom->name == name1)
				{
					if (!atom->path(path, name2, name3, name4))
					{
						path.clear();
					}

					return path;
				}
			}
			return path;
		}

		void updateOffsets(int64_t start, int64_t delta)
		{
			for (const auto& atom : children)
			{
				atom->updateOffsets(start, delta);
			}

			if (offset > start)
			{
				offset += delta;
			}
		}

		friend class index_t;
	};


	class index_t : public df::no_copy
	{
		df::file& _file;
		atom_ptr _root;
		uint32_t _brand;

		const static uint32_t brand_isom = 0x69736F6Du;
		const static uint32_t brand_mp41 = 0x6D703431u;
		const static uint32_t brand_mp42 = 0x6D703432u;
		const static uint32_t brand_f4v = 0x66347620u;
		const static uint32_t brand_qt = 0x71742020u;
		const static uint32_t brand_3gp4 = 0x33677034u;
		const static uint32_t brand_3gp5 = 0x33677035u;

	public:
		index_t(df::file& f) : _file(f), _root(std::make_shared<atom_t>()), _brand(0)
		{
		}

		bool load()
		{
			try
			{
				const auto limit = _file.file_size();
				auto offset = 0llu;

				while (offset + 8 <= limit)
				{
					auto atom = std::make_shared<atom_t>();
					_root->children.emplace_back(atom);
					atom->load(_file, offset, limit);

					offset = atom->offset + atom->length;
				}

				if (offset != limit)
				{
					throw app_exception("MP4: failed to read to the end");
				}

#ifdef _DEBUG
				//std::u8string dump;
				//_root->trace_structure(dump);
				//df::log(__FUNCTION__, "MP4 Dump: " << dump;
#endif

				const auto found = _root->find('ftyp');

				if (found && found->length < 1024)
				{
					_file.seek_from_begin(found->offset);

					auto* const ftype = static_cast<uint8_t*>(alloca(static_cast<int>(found->length)));

					if (_file.read(ftype, found->length))
					{
						auto length = df::byteswap32(ftype);
						auto name = df::byteswap32(ftype + 4);
						auto major_brand = df::byteswap32(ftype + 8);
						auto minor_version = df::byteswap32(ftype + 16);

						for (auto offset = 16; offset < found->length; offset += 4)
						{
							const auto comp_brand = df::byteswap32(ftype + offset);

							switch (comp_brand)
							{
							case brand_isom:
							case brand_mp41:
							case brand_mp42:
							case brand_f4v:
							case brand_qt:
							case brand_3gp4:
							case brand_3gp5:
								_brand = comp_brand;
								break;
							}
						}
					}
				}
			}
			catch (std::exception& e)
			{
				std::u8string dump;
				_root->trace_structure(dump);
				df::log(__FUNCTION__, str::format(u8"MP4 Dump: {} [{}]", dump, str::utf8_cast(e.what())));
				df::assert_true(false);
			}

			return is_valid();
		}

		bool is_valid() const
		{
			return _brand == brand_mp41 ||
				_brand == brand_mp42 ||
				_brand == brand_f4v ||
				_brand == brand_qt ||
				_brand == brand_isom ||
				_brand == brand_3gp4 ||
				_brand == brand_3gp5;
		}

		static void update_atom_header(uint8_t* header, uint32_t name, uint32_t length)
		{
			store_be32(header + 0, length);
			store_be32(header + 4, name);
		}

		static int round_to_k(int n)
		{
			return ((n + 1023) & ~1023);
		}

		static void pad_atom(std::vector<uint8_t>& data, int64_t padding_size = -1)
		{
			const auto existingSize = data.size();

			if (padding_size == -1)
			{
				padding_size = round_to_k(data.size()) - existingSize;
			}

			df::assert_true(padding_size > 0);

			data.resize(static_cast<int>(existingSize + padding_size + 8), 1);
			update_atom_header(data.data() + existingSize, 'free', static_cast<int>(padding_size + 8));
		}

		static void store_be32(uint8_t* data, uint32_t v)
		{
			*std::bit_cast<uint32_t*>(data) = df::byteswap32(v);
		}

		static void store_be64(uint8_t* data, uint64_t v)
		{
			*std::bit_cast<uint64_t*>(data) = df::byteswap64(v);
		}

		void update_parents(atom_list& path, int64_t delta, int ignore = 0) const
		{
			uint8_t buffer[8] = {0};

			for (auto i = 0u; i < path.size() - ignore; i++)
			{
				_file.seek_from_begin(path[i]->offset);

				if (!_file.read(buffer, 8))
				{
					df::assert_true(false);
					return;
				}

				const auto length = df::byteswap32(buffer);
				auto name = df::byteswap32(buffer + 4);

				// 64-bit
				if (length == 1)
				{
					_file.read(buffer, 8);
					const auto length64 = df::byteswap64(buffer);

					_file.seek_from_begin(path[i]->offset + 8);

					auto buffer = df::byteswap64(length64 + delta);
					_file.write(&buffer, 8);
				}
				// 32-bit
				else
				{
					if (!df::is_int32(length + delta))
					{
						df::assert_true(false);
						return; // overflow ?
					}

					_file.seek_from_begin(path[i]->offset);

					uint32_t buffer = df::byteswap32(static_cast<uint32_t>(length + delta));
					_file.write(&buffer, 4);
				}
			}
		}

		void update_offsets(int64_t offset, int64_t delta) const
		{
			_root->updateOffsets(offset, delta);

			const auto moov = _root->find('moov');

			if (moov)
			{
				const auto stco = moov->find_all('stco', true);

				for (const auto& atom : stco)
				{
					_file.seek_from_begin(atom->offset + 12);

					const auto read_size = atom->length - 12;
					auto data = _file.read(read_size);
					auto count = df::byteswap32(data.data());
					auto pos = 4;
					auto has_changes = false;

					df::assert_true((count * 4 + 4) <= read_size);

					while (count--)
					{
						const auto off = df::byteswap32(data.data() + pos);

						if (off > offset)
						{
							store_be32(data.data() + pos, static_cast<uint32_t>(off + delta));
							has_changes = true;
						}

						pos += 4;
					}

					if (has_changes)
					{
						_file.seek_from_begin(atom->offset + 12);
						_file.write(data.data(), data.size());
					}
				}

				const auto co64 = moov->find_all('co64', true);

				for (const auto& atom : co64)
				{
					_file.seek_from_begin(atom->offset + 12);

					const auto read_size = atom->length - 12;
					auto data = _file.read(read_size);
					auto count = df::byteswap32(data.data());
					auto pos = 4;
					auto has_changes = false;

					df::assert_true((count * 8 + 4) <= read_size);

					while (count--)
					{
						const auto off = df::byteswap64(data.data() + pos);

						if (static_cast<int64_t>(off) > offset)
						{
							store_be64(data.data() + pos, off + delta);
							has_changes = true;
						}

						pos += 8;
					}

					if (has_changes)
					{
						_file.seek_from_begin(atom->offset + 12);
						_file.write(data.data(), data.size());
					}
				}
			}

			const auto moof = _root->find('moof');

			if (moof)
			{
				const auto tfhd = moof->find_all('tfhd', true);

				for (const auto& atom : tfhd)
				{
					_file.seek_from_begin(atom->offset + 9);

					auto data = _file.read(atom->length - 9);
					const uint8_t raw_flags[4] = {0, data[0], data[1], data[2]};
					const auto flags = df::byteswap32(raw_flags);

					if (flags & 1)
					{
						const auto off = df::byteswap64(data.data() + 7);

						if (static_cast<int64_t>(off) > offset)
						{
							store_be64(data.data() + 7, off + delta);

							_file.seek_from_begin(atom->offset + 9);
							_file.write(data.data(), data.size());
						}
					}
				}
			}
		}

		/*AtomPtr PathToAtom(AtomList& path)
		{
		}*/

		void save_new(atom_list& path, std::vector<uint8_t>& data)
		{
			const auto parent = path.empty() ? _root : path.back();
			const auto offset = parent->offset + 8;
			_file.insert(data.data(), data.size(), offset, 0);

			update_parents(path, data.size());
			update_offsets(offset, data.size());
		}

		void save_existing(atom_list& path, std::vector<uint8_t>& data) const
		{
			const auto existing = path.back();
			auto existing_offset = existing->offset;
			auto existing_length = existing->length;

			if (path.size() > 1)
			{
				const auto parent = path[path.size() - 2];
				const auto index = std::find(parent->children.cbegin(), parent->children.cend(), existing);

				// check if there is a free atom before and possibly use it as padding
				if (index != parent->children.cbegin())
				{
					auto prevIndex = index;
					--prevIndex;
					const auto prev = *prevIndex;

					if (prev->name == 'free')
					{
						existing_offset = prev->offset;
						existing_length += prev->length;
					}
				}

				// check if there is a free atom after and possibly use it as padding
				auto nextIndex = index;
				++nextIndex;

				if (nextIndex != parent->children.cend())
				{
					const auto next = *nextIndex;

					if (next->name == 'free')
					{
						existing_length += next->length;
					}
				}
			}

			auto delta = static_cast<int64_t>(data.size()) - existing_length;

			if (delta > 0 || (delta < 0 && delta > -8))
			{
				pad_atom(data);
				delta = data.size() - existing_length;
			}
			else if (delta < 0)
			{
				pad_atom(data, -delta - 8);
				delta = 0;
			}

			_file.insert(data.data(), data.size(), existing_offset, existing_length);

			if (delta)
			{
				update_parents(path, delta, 1);
				update_offsets(existing_offset, delta);
			}
		}

		static std::u8string text(const uint8_t* buffer, int len)
		{
			const auto length = std::min(len, static_cast<int>(df::byteswap32(buffer)));
			auto name = df::byteswap32(buffer + 4);
			auto data_length = df::byteswap32(buffer + 8);
			const auto data_name = df::byteswap32(buffer + 12);

			if (data_name == 'data')
			{
				return std::u8string(buffer + 24, buffer + length);
			}
			return std::u8string(buffer + 8, buffer + length);
		}

		std::u8string metadata(uint32_t name) const
		{
			const auto found = _root->find('moov', 'udta', 'meta', 'ilst', name);

			if (found && found->length > 24 && found->length < df::sixty_four_k)
			{
				auto* const buffer = static_cast<uint8_t*>(alloca(static_cast<int>(found->length)));

				if (_file.seek_from_begin(found->offset) && _file.read(buffer, found->length) && found->length < df::
					one_meg)
				{
					df::assert_true(name == df::byteswap32(buffer + 4));
					df::assert_true(found->length == df::byteswap32(buffer));

					return text(buffer, static_cast<int>(found->length));
				}
			}

			return u8"";
		}

		static void update_text_header(uint8_t* header, uint32_t name, enum AtomDataType type, uint32_t len)
		{
			store_be32(header + 0, len);
			store_be32(header + 4, name);
			store_be32(header + 8, len - 8);
			store_be32(header + 12, 'data');
			store_be32(header + 16, type);
			store_be32(header + 20, 0);
		}

		static void modify_data(std::vector<uint8_t>& buffer, int pos, enum AtomDataType type, const void* val,
		                        uint32_t val_len)
		{
			const auto* const data = buffer.data() + pos;
			const auto existing_length = df::byteswap32(data);
			const auto name = df::byteswap32(data + 4u);

			const uint64_t total_length = 8u + 16u + val_len;
			const int64_t delta = static_cast<int64_t>(total_length) - existing_length;

			if (delta > 0)
			{
				// add padding
				buffer.insert(buffer.cbegin() + pos + existing_length, static_cast<uint32_t>(delta), 0);
			}
			else if (delta < 0)
			{
				// remove
				const auto end = buffer.cbegin() + pos + existing_length;
				buffer.erase(end + static_cast<uint32_t>(delta), end);
			}

			auto* const dest = buffer.data() + pos;
			update_text_header(dest, name, type, static_cast<uint32_t>(total_length));
			memcpy(dest + 8 + 16, val, val_len);
		}

		static void modify_text(std::vector<uint8_t>& buffer, int pos, const char8_t* sz)
		{
			modify_data(buffer, pos, TypeUTF8, sz, str::len(sz));
		}

		static void append_data(std::vector<uint8_t>& buffer, const uint32_t name, enum AtomDataType type,
		                        const void* val, int valLen)
		{
			uint8_t header[8 + 16];
			update_text_header(header, name, type, 8 + 16 + valLen);
			const auto* const v = static_cast<const uint8_t*>(val);

			buffer.insert(buffer.cend(), header, header + 8 + 16);
			buffer.insert(buffer.cend(), v, v + valLen);
		}

		static void append_text(std::vector<uint8_t>& buffer, const uint32_t name, const char8_t* v)
		{
			if (!str::is_empty(v))
			{
				append_data(buffer, name, TypeUTF8, v, str::len(v));
			}
		}

		static void update_xmp_header(std::u8string& xmp)
		{
			uint8_t xmpheader[8 + 16];
			store_be32(xmpheader, xmp.size() + 8 + 16);
			store_be32(xmpheader + 4, XMP_PACKET);
			memcpy_s(xmpheader + 8, 16, XMP_UUID, 16);
			xmp.insert(xmp.cbegin(), xmpheader, xmpheader + 8 + 16);
		}

		/*void modify_multiple(std::vector<uint8_t>& meta_ilst, uint32_t pos, const prop::item_metadata& additions,
		                     const prop::item_metadata& removals, prop::key_ref type) const
		{
			const auto data = meta_ilst.data() + pos;
			const auto length = be_uint32(data);

			if (!additions.is_null(type) || !removals.is_null(type))
			{
				const auto add = additions.read_string_vector(type);
				const auto remove = removals.read_string_vector(type);

				tag_set words(split(text(data, length), str::is_comma));
				words.remove(tag_set(remove));
				words.add(tag_set(add));
				words.make_unique();
				words.fix_invalid_entries();

				modify_text(meta_ilst, pos, words.to_string(", ", false).c_str());
			}
		}*/

		//bool modify(const metadata_edits& edits)
		//{
		//	char8_t sz[32];

		//	const auto additions = edits.additions();
		//	const auto removals = edits.removals();

		//	std::vector<uint8_t> meta_ilst;

		//	const auto found_meta = _root->find('moov', 'udta', 'meta', 'ilst');

		//	df::sorted_set<const prop::key *> already_updated;
		//	df::sorted_map<uint32_t, prop::key_ref> property_mapping;

		//	property_mapping[NAME] = prop::title;
		//	property_mapping[COMMENTS] = prop::comment;
		//	property_mapping[DESCRIPTION] = prop::description;
		//	property_mapping[COPYRIGHT] = prop::copyright;
		//	property_mapping[KEYWORDS] = prop::tag;
		//	property_mapping[ARTIST] = prop::artist;
		//	property_mapping[RATING] = prop::rating;
		//	property_mapping[ARTIST] = prop::artist;
		//	property_mapping[ALBUMARTIST] = prop::album_artist;
		//	property_mapping[ALBUM] = prop::album;
		//	property_mapping[TVSHOW] = prop::show;
		//	property_mapping[TVSEASON] = prop::season;
		//	property_mapping[TVEPISODE] = prop::episode;
		//	property_mapping[DISK] = prop::disk_num;
		//	property_mapping[TRACK] = prop::track_num;
		//	property_mapping[RELEASEDATE] = prop::year;

		//	if (found_meta && _file.is_valid_read(found_meta->offset, found_meta->length))
		//	{
		//		_file.seek_from_begin(found_meta->offset);
		//		meta_ilst = _file.read(found_meta->length);

		//		if (be_uint32(meta_ilst.data() + 4) != 'ilst')
		//		{
		//			df::assert_true(false);
		//			return false;
		//		}

		//		auto pos = 8u;

		//		while (pos < meta_ilst.size())
		//		{
		//			const auto data = meta_ilst.data() + pos;
		//			const auto length = be_uint32(data);
		//			auto name = be_uint32(data + 4);

		//			if (length < 16)
		//			{
		//				df::assert_true(false);
		//				return false;
		//			}

		//			auto found = property_mapping.find(name);

		//			if (found != property_mapping.cend())
		//			{
		//				auto type = found->second;

		//				if (!additions.is_null(type) || !removals.is_null(type))
		//				{
		//					if (type == prop::tag)
		//					{
		//						modify_multiple(meta_ilst, pos, additions, removals, type);
		//					}
		//					else if (type == prop::rating)
		//					{
		//						const auto i = std::clamp(additions.read_int(type) * 20, 0, 100);
		//						_itoa_s(i, sz, 10);
		//						modify_text(meta_ilst, pos, sz);
		//					}
		//					else if (type == prop::year)
		//					{
		//						_itoa_s(additions.read_int(type), sz, 10);
		//						modify_text(meta_ilst, pos, sz);
		//					}
		//					else if (type->data_type == prop::data_type::int_pair)
		//					{
		//						const auto xy = additions.read_pair(type);
		//						uint16_t data[4] = {0u, _byteswap_ushort(xy.x), _byteswap_ushort(xy.y), 0u};
		//						modify_data(meta_ilst, pos, TypeImplicit, data, 8);
		//					}
		//					else if (type->data_type == prop::data_type::int32)
		//					{
		//						auto data = _byteswap_ulong(additions.read_int(type));
		//						modify_data(meta_ilst, pos, TypeInteger, &data, 4);
		//					}
		//					else if (type->data_type == prop::data_type::string)
		//					{
		//						modify_text(meta_ilst, pos, additions.read_string(type));
		//					}

		//					already_updated.emplace(type);
		//				}
		//			}

		//			const auto updated_length = be_uint32(meta_ilst.data() + pos);
		//			pos += updated_length;
		//		}
		//	}
		//	else
		//	{
		//		// Add new
		//		meta_ilst.resize(8);
		//	}

		//	for (auto& mapping : property_mapping)
		//	{
		//		auto type = mapping.second;

		//		if (!additions.is_null(type) && already_updated.find(type) == already_updated.cend())
		//		{
		//			if (type == prop::tag)
		//			{
		//				append_text(meta_ilst, mapping.first,
		//				            str::combine(additions.read_string_vector(prop::tag), ", ").c_str());
		//			}
		//			else if (type == prop::rating)
		//			{
		//				const auto i = std::clamp(additions.read_int(type) * 20, 0, 100);
		//				_itoa_s(i, sz, 10);
		//				append_text(meta_ilst, mapping.first, sz);
		//			}
		//			else if (type == prop::year)
		//			{
		//				_itoa_s(additions.read_int(type), sz, 10);
		//				append_text(meta_ilst, mapping.first, sz);
		//			}
		//			else if (type->data_type == prop::data_type::int_pair)
		//			{
		//				const auto xy = additions.read_pair(type);
		//				uint16_t data[4] = {0u, _byteswap_ushort(xy.x), _byteswap_ushort(xy.y), 0u};
		//				append_data(meta_ilst, mapping.first, TypeImplicit, data, 8);
		//			}
		//			else if (type->data_type == prop::data_type::int32)
		//			{
		//				auto data = _byteswap_ulong(additions.read_int(type));
		//				append_data(meta_ilst, mapping.first, TypeInteger, &data, 4);
		//			}
		//			else if (type->data_type == prop::data_type::string)
		//			{
		//				append_text(meta_ilst, mapping.first, additions.read_string(type));
		//			}
		//		}
		//	}

		//	store_be32(meta_ilst.data(), meta_ilst.size());
		//	store_be32(meta_ilst.data() + 4, 'ilst');

		//	auto path_meta = _root->path('moov', 'udta', 'meta', 'ilst');

		//	if (path_meta.size() == 4)
		//	{
		//		save_existing(path_meta, meta_ilst);
		//	}
		//	else
		//	{
		//		uint8_t raw_data[45] = {
		//			0x00, 0x00, 0x02, 0xEC, 0x6D, 0x65, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
		//			0x00, 0x00, 0x00, 0x21, 0x68, 0x64, 0x6C, 0x72, 0x00, 0x00, 0x00, 0x00,
		//			0x00, 0x00, 0x00, 0x00, 0x6D, 0x64, 0x69, 0x72, 0x61, 0x70, 0x70, 0x6C,
		//			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		//		};

		//		meta_ilst.insert(meta_ilst.cbegin(), raw_data, raw_data + countof(raw_data));
		//		update_atom_header(meta_ilst.data(), 'meta', meta_ilst.size());

		//		auto path = _root->path('moov', 'udta');

		//		if (path.size() != 2)
		//		{
		//			meta_ilst.insert(meta_ilst.cbegin(), 8, 0);
		//			update_atom_header(meta_ilst.data(), 'udta', meta_ilst.size());
		//			path = _root->path('moov');
		//		}

		//		save_new(path, meta_ilst);
		//	}

		//	///////////////////////////////////// XMP

		//	std::u8string xmp;
		//	const auto xmp_atom = _root->find_xmp(_file);

		//	// todo: remove moov\udta\meta\XMP_ 

		//	if (xmp_atom)
		//	{
		//		_file.seek_from_begin(xmp_atom->offset + 24);

		//		const auto xmp_len = xmp_atom->length - 24;

		//		if (xmp_len < df::one_meg)
		//		{
		//			xmp.resize(static_cast<int>(xmp_len));
		//			if (!_file.read(reinterpret_cast<uint8_t*>(&xmp[0]), xmp_len))
		//			{
		//				df::assert_true(false);
		//				return false;
		//			}

		//			metadata_xmp::update(xmp, edits);
		//			update_xmp_header(xmp);

		//			const auto existing_offset = xmp_atom->offset;
		//			const auto existing_length = xmp_atom->length;
		//			const auto delta = xmp.size() - existing_length;

		//			_file.emplace(reinterpret_cast<const uint8_t*>(xmp.data()), xmp.size(), existing_offset,
		//			             existing_length);

		//			if (delta)
		//			{
		//				update_offsets(existing_offset, delta);
		//			}
		//		}
		//	}
		//	else
		//	{
		//		// Append
		//		metadata_xmp::update(xmp, edits);
		//		update_xmp_header(xmp);

		//		_file.seek_from_begin(_file.length());
		//		_file.write(reinterpret_cast<const uint8_t*>(xmp.data()), xmp.size());
		//	}

		//	return true;
		//}
	};
};

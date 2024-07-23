// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "files.h"
#include "app_text.h"
#include "model_search.h"
#include "ui_elements.h"

class group_title_control;
class sort_items_element;
class view_state;
class index_state;

enum class group_by
{
	file_type,
	shuffle,
	size,
	extension,
	location,
	rating_label,
	date_created,
	date_modified,
	camera,
	resolution,
	album_show,
	presence,
	folder
};

enum class sort_by
{
	def,
	name,
	size,
	date_modified,
};

enum class item_presence
{
	this_in = 1,
	similar_in = 2,
	newer_in = 3,
	older_in = 4,
	not_in = 5,
	unknown = 100
};

inline double calc_thumb_scale(const sized dimensions, const sized limit, const bool zoom = true)
{
	const double sx = limit.Width / dimensions.Width;
	const double sy = limit.Height / dimensions.Height;

	return zoom ? std::max(sx, sy) : std::min(sx, sy);
}

inline double calc_thumb_scale(const sizei dimensions, const sizei limit, const bool zoom = true)
{
	const double sx = limit.cx / static_cast<double>(dimensions.cx);
	const double sy = limit.cy / static_cast<double>(dimensions.cy);

	return zoom ? std::max(sx, sy) : std::min(sx, sy);
}


std::u8string format_invalid_name_message(std::u8string_view name);
std::u8string_view item_presence_text(item_presence v, bool long_text);
void parse_more_folders(df::index_roots& result, const std::u8string_view& more_folders);

namespace df
{
	struct index_file_item;
	class file_group_histogram;
	struct index_folder_item;
	class item_element;
	class item_group;
	class item_set;

	struct item_row_draw_info;

	using item_element_ptr = std::shared_ptr<item_element>;
	using const_item_element_ptr = std::shared_ptr<const item_element>;
	using item_summary_ptr = std::shared_ptr<file_group_histogram>;
	using item_group_ptr = std::shared_ptr<item_group>;
	using weak_item_group_ptr = std::weak_ptr<item_group>;

	using item_groups = std::vector<item_group_ptr>;
	using item_elements = std::vector<item_element_ptr>;

	struct item_less;
	struct item_eq;
	struct item_hash;

	using unique_item_elements = hash_map<file_path, item_element_ptr, ihash, ieq>;
	using file_items_by_folder = hash_map<folder_path, item_elements, ihash, ieq>;

	using index_folder_item_ptr = std::shared_ptr<index_folder_item>;
	using index_folder_info_const_ptr = std::shared_ptr<const index_folder_item>;
	using index_item_info_map = hash_map<str::cached, index_file_item, ihash, ieq>;
	using index_item_infos = std::vector<index_file_item>;
	using index_folder_info_map = hash_map<folder_path, index_folder_item_ptr, ihash, ieq>;
	using index_folder_infos = std::vector<index_folder_item_ptr>;

	enum class item_group_display
	{
		icons,
		detail
	};

	enum class item_online_status
	{
		disk,
		offline
	};

	struct item_display_info
	{
		std::u8string info = {};

		str::cached name = {};
		str::cached title = {};
		str::cached folder = {};
		str::cached label = {};

		icon_index icon = icon_index::document;

		int rating = 0;
		int duplicates = 0;
		int sidecars = 0;
		int disk = 0;
		int track = 0;
		int duration = 0;
		int items = 0;

		str::cached bitrate = {};
		str::cached pixel_format = {};
		sizei dimensions = {};
		uint16_t audio_channels = 0;
		uint16_t audio_sample_rate = 0;
		uint16_t audio_sample_type = 0;

		file_size size = {};
		date_t created = {};
		date_t modified = {};

		item_online_status online_status = item_online_status::disk;
		ui::style::font_face title_font = ui::style::font_face::dialog;
		item_presence presence = item_presence::unknown;
	};

	struct duplicate_info
	{
		uint32_t group = 0;
		uint32_t count = 0;
	};

	struct duplicate_info2 : public duplicate_info
	{
		date_t group_modified;
		bool group_has_modifications = false;

		void record(const date_t modified, const uint32_t dup_group)
		{
			++count;

			if (group == 0)
			{
				group = dup_group;
			}

			if (modified.to_seconds() != group_modified.to_seconds())
			{
				group_has_modifications = group_modified.is_valid();

				if (group_modified < modified)
				{
					group_modified = modified;
				}
			}
		}
	};

	enum class index_item_flags : uint32_t
	{
		none = 0,
		is_read_only = 1 << 0,
		is_offline = 1 << 1,
		is_sidecar = 1 << 2,
	};

	constexpr index_item_flags operator|(const index_item_flags a, const index_item_flags b)
	{
		return static_cast<index_item_flags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr index_item_flags operator&(const index_item_flags a, const index_item_flags b)
	{
		return static_cast<index_item_flags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}

	constexpr bool operator&&(const index_item_flags a, const index_item_flags b)
	{
		return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
	}

	constexpr index_item_flags& operator|=(index_item_flags& a, const index_item_flags b)
	{
		a = static_cast<index_item_flags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
		return a;
	}

	constexpr index_item_flags& operator&=(index_item_flags& a, const index_item_flags b)
	{
		a = static_cast<index_item_flags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
		return a;
	}

	constexpr index_item_flags operator~(const index_item_flags a)
	{
		return static_cast<index_item_flags>(~static_cast<uint32_t>(a));
	}

	constexpr void set_index_item_flag(index_item_flags& val, const index_item_flags mask, const bool state)
	{
		if (state)
		{
			val |= mask;
		}
		else
		{
			val &= ~mask;
		}
	}

	struct index_file_item
	{
		index_item_flags flags = index_item_flags::none;

		file_type_ref ft = nullptr;
		str::cached name;
		file_size size;
		date_t file_created;
		date_t file_modified;
		mutable date_t metadata_scanned;
		mutable prop::item_metadata_aptr metadata;		
		mutable duplicate_info duplicates;

		mutable bloom_bits bloom;
		mutable uint32_t crc32c = 0;

		index_file_item() = default;

		index_file_item(const index_file_item& other)
			: flags(other.flags),
			  ft(other.ft),
			  name(other.name),
			  size(other.size),
			  file_created(other.file_created),
			  file_modified(other.file_modified),
			  metadata_scanned(other.metadata_scanned),
			  metadata(other.metadata.load()),
			  duplicates(other.duplicates),
			  bloom(other.bloom),
			  crc32c(other.crc32c)
		{
		}

		index_file_item(index_file_item&& other) noexcept
			: flags(other.flags),
			  ft(other.ft),
			  name(std::move(other.name)),
			  size(std::move(other.size)),
			  file_created(std::move(other.file_created)),
			  file_modified(std::move(other.file_modified)),
			  metadata_scanned(std::move(other.metadata_scanned)),
			  metadata(other.metadata.load()),
			  duplicates(std::move(other.duplicates)),
			  bloom(std::move(other.bloom)),
			  crc32c(other.crc32c)
		{
			other.metadata.store(nullptr);
		}

		index_file_item& operator=(const index_file_item& other)
		{
			if (this == &other)
				return *this;
			flags = other.flags;
			ft = other.ft;
			name = other.name;
			size = other.size;
			file_created = other.file_created;
			file_modified = other.file_modified;
			metadata_scanned = other.metadata_scanned;
			metadata.store(other.metadata.load());
			bloom = other.bloom;
			duplicates = other.duplicates;
			crc32c = other.crc32c;
			return *this;
		}

		index_file_item& operator=(index_file_item&& other) noexcept
		{
			if (this == &other)
				return *this;
			flags = other.flags;
			ft = other.ft;
			name = std::move(other.name);
			size = std::move(other.size);
			file_created = std::move(other.file_created);
			file_modified = std::move(other.file_modified);
			metadata_scanned = std::move(other.metadata_scanned);
			metadata.store(other.metadata.load());
			other.metadata.store(nullptr);
			bloom = std::move(other.bloom);
			duplicates = std::move(other.duplicates);
			crc32c = other.crc32c;
			return *this;
		}

		date_t created() const
		{
			date_t d;
			const auto md = metadata.load();

			if (md)
			{
				d = md->created();
			}

			return d.is_valid() ? d : file_created;
		}

		item_online_status calc_online_status() const
		{
			return (flags && index_item_flags::is_offline) ? item_online_status::offline : item_online_status::disk;
		}

		void update_duplicates(const index_folder_item_ptr& f, duplicate_info dup_info) const;
		void calc_bloom_bits() const;

		prop::item_metadata_ptr safe_ps() const
		{
			auto result = metadata.load();

			if (!result)
			{
				// slight safety around the not returning an invalid result;
				result = std::make_shared<prop::item_metadata>();
				metadata.store(result);
			}

			return result;
		}

		str::cached xmp() const
		{
			const auto md = metadata.load();
			return md ? md->xmp : str::cached{};
		}

		bool operator<(const str::cached other) const
		{
			return icmp(name, other) < 0;
		}

		bool operator<(const std::u8string_view other) const
		{
			return icmp(name, other) < 0;
		}

		bool operator<(const index_file_item& other) const
		{
			return icmp(name, other.name) < 0;
		}

		bool operator==(const str::cached other) const
		{
			return icmp(name, other) == 0;
		}

		bool operator==(const std::u8string_view other) const
		{
			return icmp(name, other) == 0;
		}

		bool operator==(const index_file_item& other) const
		{
			return icmp(name, other.name) == 0;
		}
	};

	struct index_folder_item
	{
		const index_item_infos files;
		index_folder_infos folders;

		bool is_in_collection = false;
		bool is_read_only = false;
		bool is_excluded = false;

		str::cached volume = {};
		str::cached name = {};
		date_t created = {};
		date_t modified = {};

		bloom_bits bloom_filter;

		index_folder_item() = default;
		index_folder_item(const index_folder_item&) noexcept = delete;
		index_folder_item& operator=(const index_folder_item&) = delete;
		index_folder_item(index_folder_item&&) noexcept = delete;
		index_folder_item& operator=(index_folder_item&&) = delete;

		explicit index_folder_item(index_item_infos f, index_folder_infos g = {}) noexcept : files(std::move(f)),
			folders(std::move(g))
		{
			assert_true(files.size() == files.capacity());
			assert_true(folders.size() == folders.capacity());
		}

		void reset_bloom_bits()
		{
			bloom_filter.types = 0;

			for (const auto& f : files)
			{
				bloom_filter |= f.bloom;
			}
		}

		void update_bloom_bits(const index_file_item& file_node)
		{
			bloom_filter |= file_node.bloom;
		}
	};


	struct location_heat_map
	{
		static constexpr uint32_t map_width = 256;
		static constexpr uint32_t map_height = 128;

		std::array<uint32_t, map_width * map_height> coordinates{};

		static pointi calc_map_loc(const gps_coordinate coord)
		{
			const auto x = round((coord.longitude() / 180.0 * (map_width / 2.0)) + (map_width / 2.0));
			const auto y = round((coord.latitude() / 90.0 * (map_height / 2.0)) + (map_height / 2.0));
			const auto xx = std::clamp(x, 0, static_cast<int>(map_width) - 1);
			const auto yy = (static_cast<int>(map_height) - 1) - std::clamp(y, 0, static_cast<int>(map_height) - 1);
			return {xx, yy};
		}
	};

	struct date_counts
	{
		int modified = 0;
		int created = 0;
	};

	struct date_histogram
	{
		std::array<date_counts, 12 * 10> dates{};
	};

	class file_group_histogram
	{
	public:
		std::array<count_and_size, file_group::max_count> counts{};

		count_and_size total_items() const
		{
			count_and_size result;
			for (auto i = 1; i < file_group::max_count; i++)
			{
				result.size += counts[i].size;
				result.count += counts[i].count;
			}
			return result;
		}

		count_and_size total_folders() const
		{
			return counts[file_group::folder.id];
		}

		icon_index icon() const
		{
			auto largest = 0;

			for (auto i = 0; i < file_group::max_count; i++)
			{
				if (counts[i].count > counts[largest].count)
				{
					largest = i;
				}
			}

			return file_group_from_index(largest)->icon;
		}

		icon_index max_type_icon()
		{
			const auto ft = std::distance(counts.begin(),
			                              std::ranges::max_element(counts, [](auto&& left, auto&& right)
			                              {
				                              return left.count < right.count;
			                              }));
			return file_group_from_index(static_cast<int>(ft))->icon;
		}

		void record(const index_folder_info_const_ptr& folder)
		{
			counts[file_group::folder.id].count += 1;
		}

		void record(const index_file_item& info)
		{
			const auto ii = info.ft->group->id;
			counts[ii].count += 1;
			counts[ii].size += info.size;
		}

		void record(const file_type_ref mt, const file_size& size)
		{
			const auto ii = mt->group->id;
			counts[ii].count += 1;
			counts[ii].size += size;
		}

		void add(const file_group_histogram& other)
		{
			for (auto i = 1; i < file_group::max_count; i++)
			{
				counts[i].size += other.counts[i].size;
				counts[i].count += other.counts[i].count;
			}
		}

		friend bool operator==(const file_group_histogram& lhs, const file_group_histogram& rhs)
		{
			return lhs.counts == rhs.counts;
		}

		friend bool operator!=(const file_group_histogram& lhs, const file_group_histogram& rhs)
		{
			return !(lhs == rhs);
		}
	};

	class item_element final : public std::enable_shared_from_this<item_element>, public view_element
	{
	protected:
		file_path _path = {};
		str::cached _name = {};
		file_type_ref _ft = file_type::other;
		prop::item_metadata_aptr _metadata;		
		index_folder_item_ptr _info;
		ui::const_image_ptr _thumbnail;
		ui::const_image_ptr _cover_art;
		mutable ui::texture_ptr _texture;

		file_size _size = {};
		date_t _thumbnail_timestamp = {};
		date_t _modified = {};
		date_t _created = {};
		date_t _media_created = {};

		sizei _thumbnail_dims = {};
		duplicate_info _duplicates = {};
		search_result _search = {};

		uint32_t _crc32c = 0;
		uint32_t _total_count = 0;
		int _random = 0;

		item_presence _presence = item_presence::unknown;
		ui::orientation _thumbnail_orientation = ui::orientation::top_left;
		item_online_status _online_status = item_online_status::offline;

		bool _is_loading_thumbnail = false;
		bool _failed_loading_thumbnail = false;
		bool _shell_thumbnail_pending = false;
		bool _is_read_only = true;
		bool _is_folder = false;
		bool _db_thumbnail_pending = true;

	public:
		bool alt_background = false;
		bool row_layout_valid = true;

		item_element(const file_path id, const index_file_item& info) noexcept : view_element(view_element_style::can_invoke), _random(rand())
		{
			update(id, info);
		}

		item_element(const folder_path path, index_folder_item_ptr info) noexcept : _path(path), _info(std::move(info))
		{
			_is_read_only = _info->is_read_only;
			_name = path.name();
			_ft = file_type::folder;
			_modified = _info->modified;
			_created = _info->created;
			_is_folder = true;
		}

		void update(file_path path, const index_file_item& info) noexcept;


		item_element(const item_element& other) = delete;
		item_element(item_element&& other) = delete;
		item_element& operator=(const item_element& other) = delete;
		item_element& operator=(item_element&& other) = delete;

		void duplicates(const duplicate_info d)
		{
			_duplicates = d;
		}

		const duplicate_info& duplicates() const
		{
			return _duplicates;
		}

		date_t thumbnail_timestamp() const
		{
			return _thumbnail_timestamp;
		}

		str::cached name() const { return _name; }
		file_type_ref file_type() const { return _ft; }

		std::u8string_view extension() const
		{
			if (is_folder()) return {};
			const std::u8string_view sv = _name;
			const auto ext = find_ext(sv);
			return sv.substr(ext);
		}

		bool is_media() const
		{
			return _ft->is_media();
		}

		bool is_link() const
		{
			return ends(_name, u8".lnk"sv);
		}

		bool has_title() const
		{
			const auto m = _metadata.load();
			return m && !is_empty(m->title);
		}

		str::cached title() const
		{
			const auto m = _metadata.load();
			return !m || is_empty(m->title) ? _name : m->title;
		}

		int rating() const
		{
			const auto md = _metadata.load();;
			return md ? md->rating : 0;
		}

		std::u8string_view label() const
		{
			const auto md = _metadata.load();;
			return md ? md->label : std::u8string_view{};
		}

		bool has_gps() const
		{
			const auto md = _metadata.load();;
			return md && md->has_gps();
		}

		uint32_t crc32c() const
		{
			return _crc32c;
		}

		void crc32c(const uint32_t crc)
		{
			_crc32c = crc;
		}

		item_display_info populate_info() const;

		bool has_thumb() const
		{
			return is_valid(_thumbnail);
		}

		bool has_cover_art() const
		{
			return is_valid(_cover_art);
		}

		const ui::const_image_ptr& thumbnail() const
		{
			return _thumbnail;
		}

		const ui::const_image_ptr& cover_art() const
		{
			return _cover_art;
		}

		bool is_selected() const
		{
			return is_style_bit_set(view_element_style::selected);
		}

		void select(const bool s, const view_host_base_ptr& view, const view_element_ptr& e)
		{
			set_style_bit(view_element_style::selected, s, view, e);
			is_error(s && is_error(), view, e);
		}

		bool is_error() const
		{
			return is_style_bit_set(view_element_style::error);
		}

		void is_error(bool val, const view_host_base_ptr& view, const view_element_ptr& e)
		{
			set_style_bit(view_element_style::error, val, view, e);
		}

		void invert_selection(const view_host_base_ptr& view, const view_element_ptr& e)
		{
			style ^= view_element_style::selected;
			is_error(is_selected() && is_error(), view, e);
		}

		search_result search() const
		{
			return _search;
		}

		void search(const search_result& r)
		{
			_search = r;
		}

		index_folder_info_const_ptr info() const
		{
			return _info;
		}

		void info(index_folder_item_ptr info)
		{
			assert_true(is_folder());
			_info = std::move(info);
			_is_read_only = _info->is_read_only;
		}

		sizei thumbnail_dims() const
		{
			return _thumbnail_dims;
		}

		ui::orientation thumbnail_orientation() const
		{
			return _thumbnail_orientation;
		}

		void thumbnail(ui::const_image_ptr i, ui::const_image_ptr ca, const date_t timestamp = date_t::null)
		{
			_thumbnail = std::move(i);
			_cover_art = std::move(ca);
			_texture.reset();
			_thumbnail_timestamp = timestamp;

			if (_ft != file_type::folder)
			{
				if (is_valid(_cover_art))
				{
					_thumbnail_dims = _cover_art->dimensions();
					_thumbnail_orientation = _cover_art->orientation();
				}
				else if (is_valid(_thumbnail))
				{
					_thumbnail_dims = _thumbnail->dimensions();
					_thumbnail_orientation = _thumbnail->orientation();
				}
			}
		}

		void clear_cached_surface() const
		{
			_texture.reset();
		}

		void calc_folder_summary(cancel_token token);

		void render_bg(ui::draw_context& dc, const item_group& group, pointi element_offset) const;
		void render(ui::draw_context& dc, const item_group& group, pointi element_offset) const;

		sizei measure(ui::measure_context& mc, int width_limit) const override;
		void layout(ui::measure_context& mc, recti bounds_in, ui::control_layouts& positions) override;
		view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
		                                             const std::vector<recti>& excluded_bounds) override;

		platform::file_op_result rename(index_state& index, std::u8string_view name);
		
		std::u8string base_name() const
		{
			if (is_folder()) return std::u8string(_name);
			return std::u8string(path().file_name_without_extension());
		}

		void add_to(item_set& results);
		void add_to(paths& paths);
		void add_to(unique_paths& paths);
		void open(view_state& s, const view_host_base_ptr& view) const;

		search_t containing() const
		{
			if (is_folder()) return search_t().add_selector(_path.folder().parent());
			return search_t().add_selector(_path.folder());
		}

		bool is_read_only() const
		{
			return _is_read_only;
		}

		bool is_folder() const
		{
			return _is_folder;
		}

		bool is_in_collection() const
		{
			return _info && _info->is_in_collection;
		}

		prop::item_metadata_const_ptr metadata() const
		{
			return _metadata.load();
		}

		prop::item_metadata_ptr metadata()
		{
			return _metadata.load();
		}

		file_size file_size() const
		{
			return _size;
		}

		date_t file_created() const
		{
			return _created;
		}

		date_t file_modified() const
		{
			return _modified;
		}

		date_t media_created() const
		{
			return _media_created;
		}

		date_t calc_media_created() const
		{
			const auto md = _metadata.load();

			if (!md)
			{
				return _created.system_to_local();
			}

			auto d = md->created();

			if (!d.is_valid())
			{
				d = _created.system_to_local();
			}

			return d;
		}

		str::cached sidecars() const
		{
			const auto md = _metadata.load();
			return md ? md->sidecars : str::cached{};
		}

		str::cached xmp() const
		{
			const auto md = _metadata.load();
			return md ? md->xmp : str::cached{};
		}

		size_t sidecars_count() const
		{
			return split_count(sidecars(), true);
		}

		double media_position() const
		{
			const auto md = _metadata.load();
			return md ? md->media_position : 0;
		}

		void media_position(const double d)
		{
			const auto md = _metadata.load();;

			if (md)
			{
				md->media_position = d;
			}
		}

		bool should_load_thumbnail() const
		{
			if (is_folder())
				return false;

			if (!_ft->has_trait(file_traits::bitmap) && !_ft->has_trait(file_traits::av))
				return false;

			if (_is_loading_thumbnail || _failed_loading_thumbnail || _shell_thumbnail_pending || _db_thumbnail_pending)
				return false;

			if (is_empty(_thumbnail))
				return true;

			if (_thumbnail_timestamp < _modified)
				return true;

			return false;
		}

		void add_if_thumbnail_load_needed(item_elements& items)
		{
			if (should_load_thumbnail())
			{
				items.emplace_back(shared_from_this());
			}
		}

		bool is_loading_thumbnail() const
		{
			return _is_loading_thumbnail;
		}

		void is_loading_thumbnail(bool v)
		{
			_is_loading_thumbnail = v;
		}

		bool failed_loading_thumbnail() const
		{
			return _failed_loading_thumbnail;
		}

		void failed_loading_thumbnail(bool v)
		{
			_failed_loading_thumbnail = v;
		}

		item_online_status online_status() const
		{
			return _online_status;
		}

		int random() const
		{
			return _random;
		}

		void random(int i)
		{
			_random = i;
		}

		void presence(item_presence presence)
		{
			_presence = presence;
		}

		item_presence presence() const
		{
			return _presence;
		}

		file_path path() const
		{
			return _path;
		}

		folder_path folder() const
		{
			return _path.folder();
		}

		void db_thumb_query_complete()
		{
			_db_thumbnail_pending = false;
		}

		bool db_thumbnail_pending() const
		{
			return _db_thumbnail_pending;
		}
	};

	struct unique_items
	{
		unique_item_elements _items;

		item_element_ptr find(const folder_path path_in) const
		{
			file_path search_path(path_in);
			const auto found = _items.find(search_path);
			return found != _items.end() ? found->second : nullptr;
		}

		item_element_ptr find(const file_path id) const
		{
			const auto found = _items.find(id);
			return found != _items.end() ? found->second : nullptr;
		}
	};

	enum class process_items_type
	{
		photos_only,
		can_save_pixels,
		can_save_metadata,
		local_file,
		local_file_or_folder
	};

	enum class process_result_code
	{
		ok,
		nothing_selected,
		cloud_item,
		read_only,
		not_photo,
		cannot_embed_xmp,
		cannot_save_pixels,
		cannot_edit,
		folder
	};

	struct process_result
	{
		process_result_code code = process_result_code::ok;
		size_t items_count = 0;
		str::cached first_file_name;

		std::u8string to_string() const
		{
			std::u8string result;

			if (code == process_result_code::nothing_selected)
			{
				result = tt.no_items_are_selected;
			}
			else
			{
				result = format_plural_text(tt.cannot_process_fmt, first_file_name, static_cast<int>(items_count), {});
				result += u8" "sv;

				if (code == process_result_code::cloud_item)
				{
					result += tt.not_supported_cloud;
				}
				else if (code == process_result_code::read_only)
				{
					result += tt.not_supported_readonly;
				}
				else if (code == process_result_code::cannot_embed_xmp)
				{
					result += tt.not_supported_readonly_metadata;
				}
				else if (code == process_result_code::not_photo)
				{
					result += tt.not_supported_photo;
				}
				else if (code == process_result_code::cannot_save_pixels)
				{
					result += tt.not_supported_save_format;
				}
				else if (code == process_result_code::cannot_edit)
				{
					result += tt.cannot_edit;
				}
				else if (code == process_result_code::folder)
				{
					result += tt.not_supported_folder;
				}
			}

			return result;
		}

		void record_error(const item_element_ptr& i, process_result_code result_code, bool mark_errors,
		                  const view_host_base_ptr& view)
		{
			if (code == process_result_code::ok || code == result_code)
			{
				code = result_code;
				if (is_empty(first_file_name)) first_file_name = i->name();
				if (mark_errors) i->is_error(true, view, i);
				items_count += 1;
			}
		}

		bool fail() const
		{
			return code != process_result_code::ok;
		}

		bool success() const
		{
			return code == process_result_code::ok;
		}
	};

	class item_set_info
	{
	public:
		int duplicates = 0;
		int sidecars = 0;
		int untagged = 0;
		int unlocated = 0;
		int unrated = 0;
		int uncredited = 0;
		int rejected = 0;
	};

	class item_set
	{
	public:
		item_elements _items;

	public:
		item_set() noexcept = default;

		item_set(item_elements items): _items(std::move(items))
		{
		}

		item_set(const item_set&) = default;
		item_set& operator=(const item_set&) = default;
		item_set(item_set&&) noexcept = default;
		item_set& operator=(item_set&&) noexcept = default;

		void clear()
		{
			_items.clear();
		}

		bool is_empty() const
		{
			return _items.empty();
		}

		bool empty() const
		{
			return _items.empty();
		}

		size_t size() const
		{
			return _items.size();
		}

		friend bool operator==(const item_set& lhs, const item_set& rhs)
		{
			return lhs._items == rhs._items;
		}

		friend bool operator!=(const item_set& lhs, const item_set& rhs)
		{
			return !(lhs == rhs);
		}

		void shuffle()
		{
			for (const auto& i : _items)
			{
				i->random(rand());
			}
		}

		void for_all(const std::function<void(const item_element_ptr&)>& f) const
		{
			for (const auto& i : _items) f(i);
		}

		bool has_errors() const
		{
			for (const auto& i : _items) if (i->is_error()) return true;
			return false;
		}

		void clear_errors(const view_host_base_ptr& view) const
		{
			for (const auto& i : _items) i->is_error(false, view, i);
		}

		void append(const item_set& other)
		{
			_items.insert(_items.end(), other._items.begin(), other._items.end());
		}

		bool single_file_extension() const
		{
			if (has_folders()) return false;
			if (_items.empty()) return false;

			const auto ext = _items[0]->extension();

			for (const auto& i : _items)
			{
				if (str::icmp(ext, i->extension()) != 0)
				{
					return false;
				}
			}

			return true;
		}

		void add(const item_element_ptr& i) { _items.emplace_back(i); }

		file_group_histogram summary() const;
		item_set_info info() const;

		const item_elements& items() const
		{
			return _items;
		}

		paths ids() const
		{
			paths result;
			for (const auto& i : _items)
			{
				if (i->is_folder())
				{
					result.folders.emplace_back(i->folder());
				}
				else
				{
					result.files.emplace_back(i->path());
				}
			}
			return result;
		}

		icon_index common_icon() const
		{
			file_group_histogram type_histogram;
			for (const auto& i : _items) type_histogram.record(i->file_type(), i->file_size());
			return type_histogram.max_type_icon();
		}

		bool has_folders() const
		{
			for (const auto& i : _items)
			{
				if (i->is_folder())
					return true;
			}
			return false;
		}

		file_type_ref group_file_type() const
		{
			file_type_ref ft = file_type::other;

			for (const auto& i : _items)
			{
				if (i->file_type() != ft)
				{
					if (ft == file_type::other)
					{
						ft = i->file_type();
					}
					else
					{
						return file_type::other;
					}
				}
			}

			return ft;
		}

		std::vector<ui::const_image_ptr> thumbs(size_t max = max_thumbnails_to_display,
		                                        const item_element_ptr& skip_this = nullptr) const;
		size_t thumb_count() const;

		process_result can_process(process_items_type file_types, bool mark_errors,
		                           const view_host_base_ptr& view) const;

		item_set selected() const
		{
			item_set result;
			for (const auto& i : _items) if (i->is_selected()) result._items.emplace_back(i);
			return result;
		}

		str::cached first_name() const
		{
			for (const auto& i : _items) return i->name();
			return {};
		}

		item_element_ptr closest_drawable(const pointi loc, const item_element_ptr& ignore) const
		{
			item_element_ptr result;
			double distance = 0;

			for_all([&result, &distance, &ignore, &loc](auto&& i)
			{
				if (i != ignore)
				{
					const auto center = i->bounds.center();
					const auto dx = static_cast<double>(loc.x) - static_cast<double>(center.x);
					const auto dy = static_cast<double>(loc.y) - static_cast<double>(center.y);
					const auto d = (dx * dx) + (dy * dy);

					if (!result || distance > d)
					{
						result = i;
						distance = d;
					}
				}
			});

			return result;
		}

		item_element_ptr find(const std::u8string_view s) const
		{
			for (const auto& i : _items) if (icmp(i->name(), s) == 0 || icmp(i->path().name(), s) == 0) return i;
			return nullptr;
		}

		item_element_ptr find(const file_path id) const
		{
			for (const auto& i : _items) if (i->path() == id) return i;
			return nullptr;
		}

		item_element_ptr find(const folder_path id) const
		{
			file_path search_path(id);
			for (const auto& i : _items) if (i->path() == search_path) return i;
			return nullptr;
		}

		item_element_ptr find(const item_element_ptr& f) const
		{
			for (const auto& i : _items) if (i == f) return i;
			return nullptr;
		}

		item_element_ptr find_file(const item_element_ptr& f) const
		{
			for (const auto& i : _items) if (i == f) return i;
			return nullptr;
		}

		file_size file_size() const
		{
			df::file_size result;
			for (const auto& i : _items) result += i->file_size();
			return result;
		}

		bool contains(const item_element_ptr& ii) const
		{
			for (const auto& i : _items) if (i == ii) return true;
			return false;
		}

		void append_unique(unique_items& results) const
		{
			for (const auto& i : _items) results._items.insert_or_assign(i->path(), i);
		}

		std::vector<folder_path> folder_paths() const
		{
			std::vector<folder_path> result;
			for (const auto& i : _items)
			{
				if (i->is_folder())
				{
					result.emplace_back(i->folder());
				}
			}
			return result;
		}

		std::vector<file_path> file_paths(bool include_sidecars = true) const
		{
			hash_set<file_path, ihash, ieq> result;

			for (const auto& i : _items)
			{
				if (!i->is_folder())
				{
					auto&& path = i->path();
					auto&& folder = i->folder();

					result.emplace(path);

					if (include_sidecars)
					{
						const auto sidecar_parts = split(i->sidecars(), true);

						for (const auto& s : sidecar_parts)
						{
							result.emplace(folder.combine_file(s));
						}
					}
				}
			}

			return {result.begin(), result.end()};
		}
	};

	struct item_draw_info
	{
		static constexpr int _max_width = 500;
		static constexpr int _text_padding = 4;
		static constexpr int _thumb_padding = 2;

		int extent = 0;
		int width = 0;

		double val_max = static_cast<double>(INT64_MIN);
		double val_min = static_cast<double>(INT64_MAX);

		void clear_for_layout()
		{
			width = 0;
			val_max = static_cast<double>(INT64_MIN);
			val_min = static_cast<double>(INT64_MAX);
		}

		void update_extent(ui::draw_context& dc, const std::u8string_view text, const double val)
		{
			const auto max_width = df::round(_max_width * dc.scale_factor);

			extent = std::max(extent, dc.measure_text(text, ui::style::font_face::dialog,
			                                          ui::style::text_style::single_line, max_width).cx);
			val_max = std::max(val_max, val);
			val_min = std::min(val_min, val);
		}

		void update_extent(ui::draw_context& dc, const std::u8string_view text)
		{
			const auto max_width = df::round(_max_width * dc.scale_factor);
			extent = std::max(extent, dc.measure_text(text, ui::style::font_face::dialog,
			                                          ui::style::text_style::single_line, max_width).cx);
		}

		/*recti calc_bg_bounds(const recti row_bounds, const int line_height, const int text_x, const int text_y) const
		{
			recti result;
			result.left = text_x +text_padding;
			result.right = result.left + width;
			result.top = (row_bounds.top + row_bounds.bottom - line_height) / 2 - 1;
			result.bottom = result.top + line_height + 2;
			return result;
		}*/

		recti calc_bounds(const recti row_bounds, const int text_x, const int text_y, const int text_padding) const
		{			
			auto bounds = row_bounds;
			bounds.left = text_x + (text_padding / 2);
			bounds.right = bounds.left + width;
			return bounds;
		}

		void draw(ui::draw_context& rc, const std::u8string_view text, const double val, const recti bounds,
		          const ui::style::font_face text_font, const ui::style::text_style text_style,
		          const ui::color color) const
		{
			ui::color rank_color;

			if (val_min < val_max && setting.highlight_large_items)
			{
				//const auto bg_bounds = calc_bg_bounds(row_bounds, rc.text_line_height(text_font), text_x, text_y);
				const auto importance_alpha = std::min(
					color.a, static_cast<float>(0.7 * (val - val_min) / (val_max - val_min)));
				rank_color = ui::color(ui::style::color::rank_background, importance_alpha);
			}

			draw(rc, text, rank_color, bounds, text_font, text_style, color);
		}

		void draw(ui::draw_context& rc, const std::u8string_view text, const ui::color bg_color, const recti bounds,
		          const ui::style::font_face text_font, const ui::style::text_style text_style,
		          const ui::color color) const
		{
			rc.draw_text(text, bounds, text_font, text_style, color, bg_color);
		}
	};

	struct item_row_draw_info
	{
		item_draw_info icon;
		item_draw_info disk;
		item_draw_info track;
		item_draw_info title;
		item_draw_info flag;
		item_draw_info presence;
		item_draw_info sidecars;
		item_draw_info items;
		item_draw_info info;
		item_draw_info duration;
		item_draw_info file_size;
		item_draw_info bitrate;
		item_draw_info pixel_format;
		item_draw_info dimensions;
		item_draw_info audio_sample_rate;
		item_draw_info created;
		item_draw_info modified;

		int total(const int text_padding) const
		{
			auto result = 0;
			if (icon.width > 0) result += icon.width + text_padding;
			if (disk.width > 0) result += disk.width + text_padding;
			if (track.width > 0) result += track.width + text_padding;
			if (title.width > 0) result += title.width + text_padding;
			if (flag.width > 0) result += flag.width + text_padding;
			if (presence.width > 0) result += presence.width + text_padding;
			if (sidecars.width > 0) result += sidecars.width + text_padding;
			if (items.width > 0) result += items.width + text_padding;
			if (info.width > 0) result += info.width + text_padding;
			if (duration.width > 0) result += duration.width + text_padding;
			if (file_size.width > 0) result += file_size.width + text_padding;
			if (bitrate.width > 0) result += bitrate.width + text_padding;
			if (pixel_format.width > 0) result += pixel_format.width + text_padding;
			if (dimensions.width > 0) result += dimensions.width + text_padding;
			if (audio_sample_rate.width > 0) result += audio_sample_rate.width + text_padding;
			if (created.width > 0) result += created.width + text_padding;
			if (modified.width > 0) result += modified.width + text_padding;
			return result;
		}
	};

	struct group_key
	{
		group_key_type type = group_key_type::grouped_no_value;
		prop::key_ref text1_prop_type = prop::null;
		icon_index icon = icon_index::none;
		file_group_ref group = nullptr;

		int32_t order1 = 0;
		int32_t order2 = 0;
		int32_t order3 = 0;

		str::cached text1 = {};
		str::cached text2 = {};
		str::cached text3 = {};


		group_key() noexcept = default;
		group_key(const group_key&) = default;
		group_key& operator=(const group_key&) = default;
		group_key(group_key&&) noexcept = default;
		group_key& operator=(group_key&&) noexcept = default;

		group_key(const group_key_type t) noexcept : type(t)
		{
		}

		bool operator<(const group_key& other) const
		{
			if (type != other.type) return type < other.type;

			if (order1 != other.order1) return order1 < other.order1;
			if (order2 != other.order2) return order2 < other.order2;

			const auto text_delta1 = icmp(text1, other.text1);
			if (text_delta1 != 0) return text_delta1 < 0;

			if (order3 != other.order3) return order3 < other.order3;

			const auto text_delta2 = icmp(text2, other.text2);
			if (text_delta2 != 0) return text_delta2 < 0;

			return icmp(text3, other.text3) < 0;
		}
	};


	class item_group final : public std::enable_shared_from_this<item_group>, public view_element
	{
	public:
		view_state& _state;
		item_elements _items;
		item_group_display _display = item_group_display::icons;
		mutable item_row_draw_info _row_draw_info;

		int _scroll_tooltip_rating = 0;
		std::vector<std::u8string> _scroll_tooltip_text;

		mutable std::vector<recti> _layout_bounds;
		sort_by _sort_order = sort_by::def;
		bool _reverse_sort = false;
		bool _show_folder = true;
		group_key _key;

		std::u8string scroll_text;
		icon_index icon = icon_index::none;

	public:
		item_group(view_state& s, item_elements items, const item_group_display display, group_key key) noexcept :
			_state(s),
			_items(std::move(items)),
			_display(display),
			_key(std::move(key))
		{
		}

		bool has_child_thumbnail() const
		{
			for (const auto& i : _items)
			{
				if (i->has_thumb())
				{
					return true;
				}
			}

			return false;
		}

		void sort(group_by group_mode, sort_by sort_order, bool group_by_dups);

		const item_row_draw_info& row_widths() const
		{
			return _row_draw_info;
		}

		item_group_display display() const
		{
			return _display;
		}

		void toggle_display();
		void update_row_layout(ui::measure_context& mc) const;
		void update_detail_row_layout(ui::draw_context& dc, const item_element_ptr& i, bool has_related) const;
		void display(item_group_display d);

		void items(item_elements items)
		{
			_items = std::move(items);
		}

		const item_elements& items() const
		{
			return _items;
		}

		void render(ui::draw_context& dc, pointi element_offset) const override;
		sizei measure(ui::measure_context& mc, int width_limit) const override;
		void layout(ui::measure_context& mc, recti bounds_in, ui::control_layouts& positions) override;
		void scroll_tooltip(const ui::const_image_ptr& thumbnail, const view_elements_ptr& elements) const;
		void tooltip(view_hover_element& hover, pointi loc, pointi element_offset) const override;
		view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
		                                             const std::vector<recti>& excluded_bounds) override;

		item_element_ptr drawable_from_layout_location(pointi loc) const;
		void update_scroll_info(group_by gb);


		friend class item_set;
		friend class item_element;
		friend class item_group_header;
		friend class sort_items_element;
	};


	std::shared_ptr<group_title_control> build_group_title(view_state& s, const view_host_base_ptr& view,
	                                                       const item_group_ptr& g);
};

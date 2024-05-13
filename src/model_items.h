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

namespace df
{
	struct index_file_item;
	class file_group_histogram;
	struct index_folder_item;
	class file_item;
	class item_element;
	class folder_item;
	class item_group;
	class item_set;

	struct item_row_draw_info;

	using item_element_ptr = std::shared_ptr<item_element>;
	using const_item_element_ptr = std::shared_ptr<const item_element>;
	using item_summary_ptr = std::shared_ptr<file_group_histogram>;
	using item_group_ptr = std::shared_ptr<item_group>;
	using weak_item_group_ptr = std::weak_ptr<item_group>;

	using item_groups = std::vector<item_group_ptr>;
	using file_items = std::vector<file_item_ptr>;
	using folder_items = std::vector<folder_item_ptr>;
	using item_elements = std::vector<item_element_ptr>;

	struct item_less;
	struct item_eq;
	struct item_hash;

	using unique_folder_items = hash_map<folder_path, folder_item_ptr, ihash, ieq>;
	using unique_file_items = hash_map<file_path, file_item_ptr, ihash, ieq>;
	using file_items_by_folder = hash_map<folder_path, file_items, ihash, ieq>;

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
		std::u8string info;

		str::cached name;
		str::cached title;
		str::cached folder;
		str::cached label;

		icon_index icon = icon_index::document;

		int rating = 0;
		int duplicates = 0;
		int sidecars = 0;
		int disk = 0;
		int track = 0;
		int duration = 0;
		int items = 0;

		str::cached bitrate;
		str::cached pixel_format;
		sizei dimensions;
		uint16_t audio_channels = 0;
		uint16_t audio_sample_rate = 0;
		uint16_t audio_sample_type = 0;

		file_size size;
		date_t created;
		date_t modified;

		item_online_status online_status = item_online_status::disk;
		ui::style::font_size title_font = ui::style::font_size::dialog;
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
		mutable bloom_bits bloom;
		mutable duplicate_info duplicates;
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
			  bloom(other.bloom),
			  duplicates(other.duplicates),
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
			  bloom(std::move(other.bloom)),
			  duplicates(std::move(other.duplicates)),
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

		bool is_indexed = false;
		bool is_read_only = false;
		bool is_excluded = false;

		str::cached volume;
		str::cached name;
		date_t created;
		date_t modified;

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
		static const uint32_t map_width = 256;
		static const uint32_t map_height = 128;

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
		std::array<count_and_size, max_file_type_count> counts{};

		count_and_size total_items() const
		{
			count_and_size result;
			for (auto i = 1; i < max_file_type_count; i++)
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

			for (auto i = 0; i < max_file_type_count; i++)
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
			for (auto i = 1; i < max_file_type_count; i++)
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

	class item_element : public view_element
	{
	protected:
		str::cached _name;
		file_type_ref _ft = file_type::other;
		prop::item_metadata_aptr _metadata;

		bool _is_read_only = true;
		search_result _search;

		file_size _size;
		date_t _thumbnail_timestamp;
		date_t _modified;
		date_t _created;
		date_t _media_created;
		duplicate_info _duplicates;

		ui::const_image_ptr _thumbnail;
		mutable ui::texture_ptr _texture;

		int _random = 0;
		item_presence _presence = item_presence::unknown;

		bool _db_thumbnail_pending = true;
		sizei _thumbnail_dims;
		ui::orientation _thumbnail_orientation = ui::orientation::top_left;


	public:
		bool alt_background = false;
		bool row_layout_valid = true;

		item_element() noexcept : view_element(view_element_style::can_invoke), _random(rand())
		{
		}

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
			if (_ft->has_trait(file_type_traits::folder)) return {};
			const std::u8string_view sv = _name;
			const auto ext = find_ext(sv);
			return sv.substr(ext);
		}

		bool is_media() const
		{
			return _ft->is_media();
		}

		virtual item_display_info populate_info() const = 0;
		virtual bool should_load_thumbnail() const = 0;
		virtual void add_if_thumbnail_load_needed(file_items& items) = 0;

		bool has_thumb() const
		{
			return is_valid(_thumbnail);
		}

		const ui::const_image_ptr& thumbnail() const
		{
			return _thumbnail;
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

		sizei thumbnail_dims() const
		{
			return _thumbnail_dims;
		}

		ui::orientation thumbnail_orientation() const
		{
			return _thumbnail_orientation;
		}

		void thumbnail(ui::const_image_ptr i, const date_t timestamp = date_t::null)
		{
			_thumbnail = std::move(i);
			_texture.reset();
			_thumbnail_timestamp = timestamp;

			if (_thumbnail && !_thumbnail->empty() && _ft != file_type::folder)
			{
				if (_ft != file_type::folder)
				{
					if (_thumbnail_dims.is_empty())
					{
						_thumbnail_dims = _thumbnail->dimensions();
						_thumbnail_orientation = _thumbnail->orientation();
					}
				}
			}
		}

		void clear_cached_surface() const
		{
			_texture.reset();
		}


		void render_bg(ui::draw_context& dc, const item_group& group, pointi element_offset) const;
		void render(ui::draw_context& dc, const item_group& group, pointi element_offset) const;

		sizei measure(ui::measure_context& mc, int width_limit) const override;
		void layout(ui::measure_context& mc, recti bounds_in, ui::control_layouts& positions) override;
		view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
		                                             const std::vector<recti>& excluded_bounds) override;

		virtual platform::file_op_result rename(index_state& index, std::u8string_view name) = 0;
		virtual std::u8string base_name() const = 0;
		virtual void add_to(item_set& results) = 0;
		virtual void add_to(paths& paths) = 0;
		virtual void add_to(unique_paths& paths) = 0;
		virtual void open(view_state& s, const view_host_base_ptr& view) const = 0;
		virtual search_t containing() const = 0;

		bool is_read_only() const
		{
			return _is_read_only;
		}

		prop::item_metadata_const_ptr metadata() const
		{
			return _metadata.load();
		}

		prop::item_metadata_ptr metadata()
		{
			return _metadata.load();
		}

		const file_size& file_size() const
		{
			return _size;
		}

		const date_t file_created() const
		{
			return _created;
		}

		const date_t file_modified() const
		{
			return _modified;
		}

		const date_t media_created() const
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

		virtual bool has_title() const
		{
			return false;
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

	class folder_item final : public std::enable_shared_from_this<folder_item>, public item_element
	{
		folder_path _path;
		uint32_t _total_count = 0;
		index_folder_item_ptr _info;

	public:
		folder_item(const folder_path path, index_folder_item_ptr info) noexcept : _path(path), _info(std::move(info))
		{
			_is_read_only = _info->is_read_only;
			_name = path.name();
			_ft = file_type::folder;
			_modified = _info->modified;
			_created = _info->created;
		}

		folder_path path() const { return _path; }

		void calc_folder_summary(cancel_token token);

		platform::file_op_result rename(index_state& index, std::u8string_view name) override;
		void open(view_state& s, const view_host_base_ptr& view) const override;
		void add_to(item_set& results) override;
		void add_to(paths& results) override;
		void add_to(unique_paths& paths) override;
		item_display_info populate_info() const override;

		search_t containing() const override { return search_t().add_selector(_path.parent()); }

		int compare(const folder_item& other) const
		{
			return _path.compare(other._path);
		}

		std::u8string base_name() const override
		{
			return std::u8string(_name);
		}

		bool is_indexed() const
		{
			return _info->is_indexed;
		}

		index_folder_info_const_ptr info() const
		{
			return _info;
		}

		void info(index_folder_item_ptr info)
		{
			_info = std::move(info);
			_is_read_only = _info->is_read_only;
		}

		bool should_load_thumbnail() const override
		{
			return false;
		}

		void add_if_thumbnail_load_needed(file_items& items) override
		{
		}
	};

	class file_item final : public std::enable_shared_from_this<file_item>, public item_element
	{
	private:
		file_path _path;
		item_online_status _online_status = item_online_status::offline;
		uint32_t _crc32c = 0;

		bool _is_loading_thumbnail = false;
		bool _failed_loading_thumbnail = false;
		bool _shell_thumbnail_pending = false;

	public:
		file_item(const file_path id, const index_file_item& info) noexcept
		{
			update(id, info);
		}

		file_item(const file_item& other) = delete;
		file_item(file_item&& other) = delete;
		file_item& operator=(const file_item& other) = delete;
		file_item& operator=(file_item&& other) = delete;

		void open(view_state& s, const view_host_base_ptr& view) const override;
		platform::file_op_result rename(index_state& index, std::u8string_view name) override;

		std::u8string base_name() const override { return std::u8string(path().file_name_without_extension()); }

		file_path path() const
		{
			return _path;
		}

		folder_path folder() const
		{
			return _path.folder();
		}

		str::cached title() const
		{
			const auto m = _metadata.load();
			return !m || is_empty(m->title) ? _name : m->title;
		}

		bool has_title() const override
		{
			const auto m = _metadata.load();
			return m && !is_empty(m->title);
		}

		bool is_link() const
		{
			return ends(_name, u8".lnk"sv);
		}

		item_online_status online_status() const
		{
			return _online_status;
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

		bool should_load_thumbnail() const override
		{
			if (!_ft->has_trait(file_type_traits::bitmap) && !_ft->has_trait(file_type_traits::av))
				return false;

			if (_is_loading_thumbnail || _failed_loading_thumbnail || _shell_thumbnail_pending || _db_thumbnail_pending)
				return false;

			if (is_empty(_thumbnail))
				return true;

			if (_thumbnail_timestamp < _modified)
				return true;

			return false;
		}

		void add_if_thumbnail_load_needed(file_items& items) override
		{
			if (should_load_thumbnail())
			{
				items.emplace_back(shared_from_this());
			}
		}

		/*int compare(const file_item_ptr& other) const
		{
			return compare(other->_folder, other->_name);
		}

		int compare(const file_item& other) const
		{
			return compare(other._folder, other._name);
		}

		int compare(const folder_path folder, const std::u8string_view name) const
		{
			const auto folder_comp = _folder.compare(folder);
			return folder_comp == 0 ? natural_compare(_name, name) : folder_comp;
		}*/

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

		void update(file_path path, const index_file_item& info) noexcept;
		void add_to(item_set& results) override;
		void add_to(paths& results) override;
		void add_to(unique_paths& paths) override;

		item_display_info populate_info() const override;

		void shell_thumbnail_pending(bool cond)
		{
			_shell_thumbnail_pending = cond;
		}

		bool shell_thumbnail_pending() const
		{
			return _shell_thumbnail_pending;
		}

		search_t containing() const override { return search_t().add_selector(_path.folder()); }

		uint32_t crc32c() const
		{
			return _crc32c;
		}

		void crc32c(const uint32_t crc)
		{
			_crc32c = crc;
		}

		bool has_gps() const
		{
			const auto md = _metadata.load();;
			return md && md->has_gps();
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
	};

	struct unique_items
	{
		unique_folder_items _folders;
		unique_file_items _items;

		folder_item_ptr find(const folder_path path) const
		{
			const auto found = _folders.find(path);
			return found != _folders.end() ? found->second : nullptr;
		}

		file_item_ptr find(const file_path id) const
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
		folder_items _folders;
		file_items _items;

	public:
		item_set() noexcept = default;

		item_set(file_items items): _items(std::move(items))
		{
		}

		item_set(const item_set&) = default;
		item_set& operator=(const item_set&) = default;
		item_set(item_set&&) noexcept = default;
		item_set& operator=(item_set&&) noexcept = default;

		void clear()
		{
			_folders.clear();
			_items.clear();
		}

		bool is_empty() const
		{
			return _folders.empty() && _items.empty();
		}

		bool has_folders() const { return !_folders.empty(); }

		bool empty() const
		{
			return _folders.empty() && _items.empty();
		}

		size_t size() const
		{
			return _folders.size() + _items.size();
		}

		friend bool operator==(const item_set& lhs, const item_set& rhs)
		{
			return lhs._folders == rhs._folders
				&& lhs._items == rhs._items;
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

			for (const auto& i : _folders)
			{
				i->random(rand());
			}
		}

		void for_all(const std::function<void(const item_element_ptr&)>& f) const
		{
			for (const auto& i : _folders) f(i);
			for (const auto& i : _items) f(i);
		}

		bool has_errors() const
		{
			for (const auto& i : _folders) if (i->is_error()) return true;
			for (const auto& i : _items) if (i->is_error()) return true;
			return false;
		}

		void clear_errors(const view_host_base_ptr& view) const
		{
			for (const auto& i : _folders) i->is_error(false, view, i);
			for (const auto& i : _items) i->is_error(false, view, i);
		}

		void append(const item_set& other)
		{
			_folders.insert(_folders.end(), other._folders.begin(), other._folders.end());
			_items.insert(_items.end(), other._items.begin(), other._items.end());
		}


		bool are_all_folders_indexed() const
		{
			for (const auto& i : _folders) if (!i->is_indexed()) return false;
			return true;
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

		void add(const folder_item_ptr& i) { _folders.emplace_back(i); }
		void add(const file_item_ptr& i) { _items.emplace_back(i); }

		file_group_histogram summary() const;
		item_set_info info() const;

		const folder_items& folders() const
		{
			return _folders;
		}

		const file_items& items() const
		{
			return _items;
		}

		paths ids() const
		{
			paths result;
			for (const auto& i : _items) result.files.emplace_back(i->path());
			for (const auto& i : _folders) result.folders.emplace_back(i->path());
			return result;
		}

		icon_index common_icon() const
		{
			file_group_histogram type_histogram;
			for (const auto& i : _items) type_histogram.record(i->file_type(), i->file_size());
			for (const auto& i : _folders) type_histogram.record(i->file_type(), i->file_size());
			return type_histogram.max_type_icon();
		}

		bool only_folders() const
		{
			return _items.empty() && !_folders.empty();
		}

		file_item_ptr first_file_item() const
		{
			for (const auto& i : _items)
			{
				return i;
			}

			return nullptr;
		}

		item_elements files_and_folders() const
		{
			item_elements result;
			for (const auto& f : _folders)
			{
				result.emplace_back(f);
			}
			for (const auto& i : _items)
			{
				result.emplace_back(i);
			}
			return result;
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
			for (const auto& i : _folders) if (i->is_selected()) result._folders.emplace_back(i);
			for (const auto& i : _items) if (i->is_selected()) result._items.emplace_back(i);
			return result;
		}

		str::cached first_name() const
		{
			for (const auto& i : _folders) return i->name();
			for (const auto& i : _items) return i->name();
			return {};
		}

		folder_item_ptr folder_from_location(const pointi loc) const
		{
			for (const auto& i : _folders) if (i->bounds.contains(loc)) return i;
			return nullptr;
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
			for (const auto& i : _folders) if (icmp(i->name(), s) == 0) return i;
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
			for (const auto& i : _folders) if (i->path() == id) return i;
			return nullptr;
		}

		item_element_ptr find(const item_element_ptr& f) const
		{
			for (const auto& i : _folders) if (i == f) return i;
			for (const auto& i : _items) if (i == f) return i;
			return nullptr;
		}

		file_item_ptr find_file(const item_element_ptr& f) const
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
			for (const auto& i : _folders) if (i == ii) return true;
			for (const auto& i : _items) if (i == ii) return true;
			return false;
		}

		void append_unique(unique_items& results) const
		{
			for (const auto& i : _folders) results._folders.insert_or_assign(i->path(), i);
			for (const auto& i : _items) results._items.insert_or_assign(i->path(), i);
		}

		std::vector<folder_path> folder_paths() const
		{
			std::vector<folder_path> result;
			for (const auto& i : _folders)
			{
				result.emplace_back(i->path());
			}
			return result;
		}

		std::vector<file_path> file_paths(bool include_sidecars = true) const
		{
			hash_set<file_path, ihash, ieq> result;

			for (const auto& i : _items)
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

			return {result.begin(), result.end()};
		}
	};

	struct item_draw_info
	{
		static constexpr int max_width = 500;
		static constexpr int text_padding = 4;
		static constexpr int thumb_padding = 2;

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
			extent = std::max(extent, dc.measure_text(text, ui::style::font_size::dialog,
			                                          ui::style::text_style::single_line, max_width).cx);
			val_max = std::max(val_max, val);
			val_min = std::min(val_min, val);
		}

		void update_extent(ui::draw_context& dc, const std::u8string_view text)
		{
			extent = std::max(extent, dc.measure_text(text, ui::style::font_size::dialog,
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

		recti calc_bounds(const recti row_bounds, const int text_x, const int text_y) const
		{
			auto bounds = row_bounds;
			bounds.left = text_x + (text_padding / 2);
			bounds.right = bounds.left + width;
			return bounds;
		}

		void draw(ui::draw_context& rc, const std::u8string_view text, const double val, const recti bounds,
		          const ui::style::font_size text_font, const ui::style::text_style text_style,
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
		          const ui::style::font_size text_font, const ui::style::text_style text_style,
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

		int total() const
		{
			auto result = 0;
			if (icon.width > 0) result += icon.width + item_draw_info::text_padding;
			if (disk.width > 0) result += disk.width + item_draw_info::text_padding;
			if (track.width > 0) result += track.width + item_draw_info::text_padding;
			if (title.width > 0) result += title.width + item_draw_info::text_padding;
			if (flag.width > 0) result += flag.width + item_draw_info::text_padding;
			if (presence.width > 0) result += presence.width + item_draw_info::text_padding;
			if (sidecars.width > 0) result += sidecars.width + item_draw_info::text_padding;
			if (items.width > 0) result += items.width + item_draw_info::text_padding;
			if (info.width > 0) result += info.width + item_draw_info::text_padding;
			if (duration.width > 0) result += duration.width + item_draw_info::text_padding;
			if (file_size.width > 0) result += file_size.width + item_draw_info::text_padding;
			if (bitrate.width > 0) result += bitrate.width + item_draw_info::text_padding;
			if (pixel_format.width > 0) result += pixel_format.width + item_draw_info::text_padding;
			if (dimensions.width > 0) result += dimensions.width + item_draw_info::text_padding;
			if (audio_sample_rate.width > 0) result += audio_sample_rate.width + item_draw_info::text_padding;
			if (created.width > 0) result += created.width + item_draw_info::text_padding;
			if (modified.width > 0) result += modified.width + item_draw_info::text_padding;
			return result;
		}
	};

	enum class group_key_type
	{
		folder = 0,
		grouped_value = 10,
		grouped_no_value = 20,
		other = 30,
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

		str::cached text1;
		str::cached text2;
		str::cached text3;


		df::group_key() noexcept = default;
		df::group_key(const group_key&) = default;
		group_key& operator=(const group_key&) = default;
		df::group_key(group_key&&) noexcept = default;
		group_key& operator=(group_key&&) noexcept = default;

		df::group_key(const group_key_type t) noexcept : type(t)
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
		void scroll_tooltip(const ui::const_image_ptr& thumbnail, view_elements& elements) const;
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

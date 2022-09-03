// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "util.h"
#include "util_simd.h"
#include "model_propery.h"

#include "rapidjson/istreamwrapper.h"

static_assert(std::is_trivially_copyable_v<df::file_size>);
static_assert(std::is_trivially_copyable_v<df::day_range_t>);
static_assert(std::is_trivially_copyable_v<df::day_t>);
static_assert(std::is_trivially_copyable_v<df::date_t>);
static_assert(std::is_trivially_copyable_v<sizei>);
static_assert(std::is_trivially_copyable_v<pointi>);
static_assert(std::is_trivially_copyable_v<recti>);
static_assert(std::is_trivially_copyable_v<sized>);
static_assert(std::is_trivially_copyable_v<pointd>);
static_assert(std::is_trivially_copyable_v<rectd>);
static_assert(std::is_trivially_copyable_v<affined>);
static_assert(std::is_trivially_copyable_v<quadd>);
static_assert(std::is_trivially_copyable_v<df::xy8>);
static_assert(std::is_trivially_copyable_v<df::xy16>);
static_assert(std::is_trivially_copyable_v<df::xy32>);
static_assert(std::is_trivially_copyable_v<df::int_counter>);
static_assert(std::is_trivially_copyable_v<df::file_path>);
static_assert(std::is_trivially_copyable_v<df::folder_path>);
static_assert(std::is_trivially_copyable_v<std::u8string_view>);

static_assert(std::is_trivially_copyable_v<ui::color>);
static_assert(std::is_move_constructible_v<ui::const_image_ptr>);
static_assert(std::is_move_constructible_v<ui::const_surface_ptr>);

static_assert(std::is_move_constructible_v<df::cspan>);
static_assert(std::is_move_constructible_v<df::span>);
static_assert(std::is_move_constructible_v<df::blob>);


std::atomic_bool df::is_closing = false;
std::atomic_int df::file_handles_detached = 0;
std::atomic_int df::jobs_running = 0;
std::atomic_int df::loading_media = 0;
std::atomic_int df::command_active = 0;
std::atomic_int df::dragging_items = 0;
std::atomic_int df::handling_crash = 0;
const char* df::rendering_func = "";

std::u8string df::gpu_desc = u8"unknown";
std::u8string df::gpu_id = u8"unknown";
std::u8string df::d3d_info = u8"unknown";

df::date_t df::start_time;
std::atomic_int df::cancel_token::empty;
df::file_path df::last_loaded_path;
df::file_path df::log_path = known_path(platform::known_folder::running_app_folder).combine_file(u8"diffractor.log");
df::file_path df::previous_log_path = known_path(platform::known_folder::running_app_folder).combine_file(
	u8"diffractor.previous.log");

static std::basic_ofstream<char8_t, std::char_traits<char8_t>> log_file;


void df::log(const std::u8string_view context, const std::u8string_view message)
{
	static platform::mutex s_mutex;
	platform::exclusive_lock ll(s_mutex);
	static auto start_time = platform::tick_count();
	static bool did_try_open = false;

#ifdef _DEBUG

	platform::trace(str::format(u8"{}:{}\n", context, message));

#endif

	if (!did_try_open)
	{
		did_try_open = true;

		if (log_path.exists())
		{
			platform::move_file(log_path, previous_log_path, false);
		}

		log_file.open(platform::to_file_system_path(log_path), std::ios::out | std::ios::trunc);
	}

	if (log_file.is_open())
	{
		const auto time = platform::tick_count() - start_time;
		const auto thread_id = platform::current_thread_id();

		log_file << std::right << std::setfill(u8'0') << std::setw(8) << thread_id
			<< " "
			<< std::setw(8) << time
			<< " "
			<< std::setfill(u8' ')
			<< std::left << std::setw(33) << context
			<< message << std::endl;
	}
}

void df::log(const std::string_view context, const std::u8string_view message)
{
	const auto context8 = str::utf8_cast(context);
	df::log(context8, message);
}

void df::log(const std::string_view context, const std::string_view message)
{
	const auto context8 = str::utf8_cast(context);
	const auto message8 = str::utf8_cast(message);
	df::log(context8, message8);
}

void df::trace(const std::u8string_view message)
{
#ifdef _DEBUG
	platform::trace(str::format(u8"{}\n", message));
#endif
}

void df::trace(const std::string_view message)
{
#ifdef _DEBUG
	const auto message8 = str::utf8_cast(message.data());
	platform::trace(str::format(u8"{}\n", message8));
#endif
}

df::file_path df::close_log()
{
	if (log_file.is_open())
	{
		log_file.close();
	}

	return log_path;
}

// Integer square root by Halleck's method, with Legalize's speedup 
int df::isqrt(int x)
{
	if (x < 1) return 0;

	/* Load the binary constant 01 00 00 ... 00, where the number
	* of zero bits to the right of the single one bit
	* is even, and the one bit is as far left as is consistant
	* with that condition.)
	*/
	long squaredbit = static_cast<uint32_t>(~0L) >> 1 & ~(static_cast<uint32_t>(~0L) >> 2);
	/* This portable load replaces the loop that used to be
	* here, and was donated by  legalize@xmission.com
	*/

	/* Form bits of the answer. */
	long remainder = x;
	long root = 0;
	while (squaredbit > 0)
	{
		if (remainder >= (squaredbit | root))
		{
			remainder -= (squaredbit | root);
			root >>= 1;
			root |= squaredbit;
		}
		else
		{
			root >>= 1;
		}
		squaredbit >>= 2;
	}

	return root;
}

std::u8string df::file_size::str() const
{
	return prop::format_size(*this);
}

df::version::version(const std::u8string_view version)
{
	const auto parts = str::split(version, true, [](wchar_t c) { return c == '.'; });

	if (!parts.empty())
	{
		major = str::to_int(parts[0]);
	}

	if (parts.size() > 1)
	{
		minor = str::to_int(parts[1]);
	}
}

std::u8string df::date_t::to_xmp_date() const
{
	const auto st = date();
	return str::format(u8"{}-{}-{}T{}:{}:{}", st.year, st.month, st.day, st.hour, st.minute, st.second);
}

df::date_t df::date_t::system_to_local() const
{
	return date_t(platform::utc_to_local(_i));
}

df::date_t df::date_t::local_to_system() const
{
	return date_t(platform::local_to_utc(_i));
}

df::date_t df::date_t::from(const std::u8string_view r)
{
	date_t ft;
	ft.parse(r);
	return ft;
}

static bool parse_iso_8601_like(const std::u8string_view r, df::day_t& result)
{
	int yyyy = 0, mm = 0, dd = 0, hh = 0, ss = 0, min = 0;
	const int mmm_len = 4;
	char mmm[mmm_len] = {0};
	double sec = 0.0;
	bool success = false;

	if (!r.empty())
	{
		// Samsung 2005:08:17 11:42:43
		// Canon    THU OCT 26 16:46:04 2006
		//          TUE MAY 08 10:00:1: 2007
		// Fujifilm Mon Mar 3 09:44:56 2008

		char day[4] = {0};
		char month[4] = {0};

		const auto* const source_data = std::bit_cast<const char*>(r.data());
		const auto source_len = r.size();

		if (7 == _snscanf_s(source_data, source_len, "%3s %3s %d %d:%d:%d %d", day, 4, month, 4, &dd, &hh, &mm, &ss,
		                    &yyyy) ||
			7 == _snscanf_s(source_data, source_len, "%3s %3s %d %d:%d:%d: %d", day, 4, month, 4, &dd, &hh, &min, &ss,
			                &yyyy))
		{
			mm = str::month(std::bit_cast<const char8_t*>(static_cast<char*>(month)));
			sec = ss;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%2d-%2d %2d:%2d:%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d:%2d:%2d %2d:%2d:%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%2d-%2dT%2d:%2d:%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d%2d%2dT%2d%2d%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d%2d%2d%2d%2d%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%2d-%2d", // "2006-01-14"
		                    &yyyy,
		                    &mm,
		                    &dd) == 3)
		{
			hh = 0, ss = 0, min = 0;
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%3s-%2d", // "2006-jan-14"
		                    &yyyy,
		                    mmm,
		                    mmm_len,
		                    &dd) == 3)
		{
			mm = str::month(std::bit_cast<const char8_t*>(static_cast<const char*>(mmm)));
			hh = 0, ss = 0, min = 0;
			success = true;
		}
	}

	if (success)
	{
		result.year = yyyy;
		result.month = mm;
		result.day = dd;
		result.hour = hh;
		result.minute = min;
		result.second = static_cast<int>(floor(sec + 0.1));
	}

	return success;
}

static int find_val_above(const int n[3], int v)
{
	if (n[0] > v) return 0;
	if (n[1] > v) return 1;
	if (n[2] > v) return 2;
	return -1;
}

static bool try_parse_date(const std::u8string_view text, df::day_t& result)
{
	const auto parts = str::split(text, true, [](wchar_t c) { return c == '-' || c == '/' || c == '\\' || c == '.'; });

	if (parts.size() == 3)
	{
		const int nums[3] = {str::to_int(parts[0]), str::to_int(parts[1]), str::to_int(parts[2])};
		const int month_texts[3] = {str::month(parts[0]), str::month(parts[1]), str::month(parts[2])};

		const int found_year = find_val_above(nums, 31);
		const int found_month_text = find_val_above(month_texts, 0);
		const int found_day = find_val_above(nums, 0);

		//if (found_year != -1 && found_month_text != -1 && found_day !- )

		// month in the middle
		/*if (month1 != 0 && is_num0 && is_num2)
		{
			auto d = str::to_int(parts[1]);

			if (d >= 1 && d <= 31)
			{
				result.month = month;
				result.day = d;
			}
		}

		return true;*/
	}

	return false;
}

bool df::date_t::parse(const std::u8string_view text)
{
	day_t d = {0};

	if (parse_iso_8601_like(text, d) ||
		try_parse_date(text, d))
	{
		date(d);
		return true;
	}

	return false;
}

bool df::date_t::parse_exif_date(const std::u8string_view r)
{
	// "2006:01:14 15:51:31"
	day_t d = {0};
	return parse_iso_8601_like(r, d) && date(d);
}

bool df::date_t::parse_xml_date(const std::u8string_view r)
{
	// "2006:01:14 15:51:31"
	// "2011-10-03T02:59:13.000000Z"
	// "2019-09-23T18:45:46.309-07:00"
	// "2007-11-09T08:00:00Z"
	// "2006-01-14T15:51:31"
	// "2006-01-14"
	day_t d = {0};
	return parse_iso_8601_like(r, d) && date(d);
}

df::file_size df::file_size::null;


df::file_path df::probe_data_file(const std::u8string_view file_name)
{
	const auto module_folder = known_path(platform::known_folder::running_app_folder);
	const auto app_data_folder = known_path(platform::known_folder::app_data);
	const auto module_path = module_folder.combine_file(file_name);

	if (module_path.exists())
	{
		return module_path;
	}

	return app_data_folder.combine_file(file_name);
}

df::blob df::blob_from_file(const file_path path, size_t max_load)
{
	file f;

	if (f.open_read(path, true))
	{
		const auto file_len = f.file_size();
		auto load_len = file_len;

		if (max_load != 0 && load_len > max_load)
		{
			load_len = max_load;
		}

		if (load_len > max_blob_size)
		{
			const auto message = str::format(u8"Cannot read file into memory ({} bytes)", file_len);
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		return f.read_blob(load_len);
	}

	return {};
}

bool df::blob_save_to_file(const cspan data, const file_path path)
{
	size_t written = 0;
	const auto len = data.size;
	const auto file = open_file(path, platform::file_open_mode::create);

	if (file)
	{
		written = file->write(data.data, len);
	}

	return written == len;
}

df::util::json::json_doc df::util::json::json_from_file(const file_path path)
{
	u8istream ifs(str::utf8_to_utf16(path.str()));
	rapidjson::BasicIStreamWrapper<u8istream> isw(ifs);
	json_doc d;
	d.ParseStream(isw);
	return d;
}

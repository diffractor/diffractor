// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Implements the Movie document -- timeline edits and undo, transition and duration
// arithmetic, the derived output geometry, the OpenTimelineIO project file, and the Windows Live
// Movie Maker reader. Pure: no I/O, no decoding, no UI. docs/movie.md owns the behaviour.

#include "pch.h"
#include "model_movie.h"

#include "files.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// OpenTimelineIO stores times as a value over a rate. A rate of 1000 makes every value whole
// milliseconds, so a project round-trips exactly instead of being quantised onto whatever frame
// rate the movie happened to derive when it was saved.
static constexpr double otio_rate = 1000.0;

static bool is_qualified_path(const std::string_view s)
{
	if constexpr (!df::windows_path_semantics)
	{
		return !s.empty() && df::is_path_sep(s[0]);
	}
	else
	{
		if (s.size() < 3) return false;
		if (df::is_path_sep(s[0]) && df::is_path_sep(s[1])) return true;
		return s[1] == ':' && df::is_path_sep(s[2]);
	}
}

//
// Timing
//

double movie_overlap(const movie_clip& left, const movie_clip& right, const movie_settings& settings)
{
	if (settings.transition != movie_transition::crossfade) return 0;
	if (settings.transition_seconds <= 0) return 0;

	return std::min({settings.transition_seconds, left.duration() / 2, right.duration() / 2});
}

movie_timing calc_movie_timing(const std::vector<movie_clip>& clips, const movie_settings& settings)
{
	movie_timing result;
	result.starts.reserve(clips.size());

	double t = 0;

	for (size_t i = 0; i < clips.size(); ++i)
	{
		result.starts.emplace_back(t);
		t += clips[i].duration();
		if (i + 1 < clips.size()) t -= movie_overlap(clips[i], clips[i + 1], settings);
	}

	result.duration = std::max(0.0, t);
	return result;
}

movie_frame calc_movie_frame(const std::vector<movie_clip>& clips, const movie_settings& settings,
                             const double time)
{
	return calc_movie_frame(clips, settings, calc_movie_timing(clips, settings), time);
}

movie_frame calc_movie_frame(const std::vector<movie_clip>& clips, const movie_settings& settings,
	const movie_timing& timing, const double time)
{
	movie_frame result;
	if (clips.empty()) return result;

	const auto at = std::clamp(time, 0.0, timing.duration);

	const auto contribute = [&](const size_t i, const double weight)
	{
		const auto& clip = clips[i];
		movie_frame_source source;
		source.index = static_cast<int>(i);
		source.weight = weight;
		// A photo has one frame however long it is held, so asking for a position in it is
		// meaningless; the compositor decodes it once and reuses it.
		source.source_time = clip.is_photo
			                     ? 0.0
			                     : clip.start + std::clamp(at - timing.starts[i], 0.0, clip.duration());

		(result.a.index < 0 ? result.a : result.b) = source;
	};

	for (size_t i = 0; i < clips.size() && result.b.index < 0; ++i)
	{
		const auto start = timing.starts[i];
		const auto end = start + clips[i].duration();

		// The last clip includes its end, so the final instant of the movie still draws something.
		const auto inside = at >= start && (at < end || (i + 1 == clips.size() && at <= end));
		if (!inside) continue;

		if (result.a.index < 0)
		{
			contribute(i, 1.0);
			continue;
		}

		const auto overlap = movie_overlap(clips[i - 1], clips[i], settings);

		if (overlap <= 0)
		{
			break;
		}

		const auto progress = std::clamp((at - start) / overlap, 0.0, 1.0);
		result.a.weight = 1.0 - progress;
		contribute(i, progress);
	}

	// The fade is as long as a transition, because one number the user already set is easier to
	// predict than a second one that means nearly the same thing.
	const auto fade = std::clamp(settings.transition_seconds, 0.0, timing.duration / 2);

	if (fade > 0)
	{
		if (settings.fade_in && at < fade) result.fade_to_black = 1.0 - at / fade;
		if (settings.fade_out && at > timing.duration - fade)
		{
			result.fade_to_black = std::max(result.fade_to_black, 1.0 - (timing.duration - at) / fade);
		}
	}

	return result;
}

//
// Output geometry
//

static sizei fit_within(const sizei extent, const int max_cx, const int max_cy)
{
	if (extent.cx <= 0 || extent.cy <= 0) return {};

	const auto scale = std::min({
		1.0,
		static_cast<double>(max_cx) / extent.cx,
		static_cast<double>(max_cy) / extent.cy
	});

	const auto cx = std::max(2, static_cast<int>(std::lround(extent.cx * scale)));
	const auto cy = std::max(2, static_cast<int>(std::lround(extent.cy * scale)));

	// Every encoder in reach refuses an odd dimension, so rounding down is the only safe direction.
	return {cx - (cx & 1), cy - (cy & 1)};
}

movie_output derive_movie_output(const std::vector<movie_clip>& clips)
{
	movie_output result;

	// Videos decide the frame, and only fall back to photos when there is no video at all. A
	// 45-megapixel photo beside a 1080p clip must not produce an 8000-pixel movie.
	const movie_clip* largest = nullptr;
	auto largest_area = 0LL;
	auto have_video = false;
	auto rate = 0.0;

	for (const auto& c : clips)
	{
		if (c.extent.cx <= 0 || c.extent.cy <= 0) continue;
		if (!c.is_photo) rate = std::max(rate, c.frame_rate);

		const auto video_here = !c.is_photo;
		if (have_video && !video_here) continue;

		const auto area = static_cast<int64_t>(c.extent.cx) * c.extent.cy;

		if (video_here && !have_video)
		{
			have_video = true;
			largest = &c;
			largest_area = area;
		}
		else if (area > largest_area)
		{
			largest = &c;
			largest_area = area;
		}
	}

	result.extent = largest ? fit_within(largest->extent, movie_max_width, movie_max_height) : sizei{1920, 1080};
	if (result.extent.cx <= 0 || result.extent.cy <= 0) result.extent = {1920, 1080};

	result.frame_rate = rate > 0
		                    ? std::clamp(static_cast<int>(std::lround(rate)), 1, movie_max_frame_rate)
		                    : movie_default_frame_rate;

	const auto bits = static_cast<int64_t>(result.extent.cx) * result.extent.cy * result.frame_rate *
		movie_bits_per_pixel;
	result.video_bitrate = static_cast<int>(std::clamp(bits, static_cast<double>(movie_min_bitrate),
	                                                   static_cast<double>(movie_max_bitrate)));

	return result;
}

movie_clip make_movie_clip(df::file_path path, const movie_settings& settings)
{
	movie_clip result;
	result.path = path;

	const auto mt = files::file_type_from_name(path);
	result.is_photo = mt && mt->group == file_group::photo;

	if (result.is_photo)
	{
		result.end = settings.photo_seconds;
		result.photo_duration_is_default = true;
	}

	return result;
}

bool movie_output_names_a_clip(const std::vector<movie_clip>& clips, const df::file_path output)
{
	if (output.is_empty()) return false;

	return std::ranges::any_of(clips, [output](const movie_clip& clip)
	{
		return !clip.path.is_empty() && df::compare_path_key(clip.path.pack(), output.pack()) == 0;
	});
}

//
// The project file
//

// Always forward-slashed, whether relative or absolute. A native Windows path would be written
// into the JSON with every separator escaped, which is both unreadable and a needless difference
// between the file this platform writes and the one the other reads.
static std::string to_url(const df::file_path path, const df::folder_path project_folder)
{
	if (!project_folder.is_empty())
	{
		const auto relative = std::filesystem::path(path.pack()).lexically_relative(
			std::filesystem::path(project_folder.text().sv()));
		if (!relative.empty()) return relative.generic_string();
	}

	auto result = path.pack();
	for (auto&& c : result) if (c == '\\') c = '/';
	return result;
}

static int hex_digit(const char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static std::optional<std::string> decode_url_path(const std::string_view url)
{
	std::string result;
	result.reserve(url.size());

	for (size_t i = 0; i < url.size(); ++i)
	{
		if (url[i] != '%')
		{
			result += url[i];
			continue;
		}

		if (i + 2 >= url.size()) return {};
		const auto high = hex_digit(url[i + 1]);
		const auto low = hex_digit(url[i + 2]);
		if (high < 0 || low < 0) return {};
		const auto decoded = static_cast<char>((high << 4) | low);
		if (decoded == '\0') return {};
		result += decoded;
		i += 2;
	}

	return result;
}

static df::file_path from_url(const std::string_view url, const df::folder_path project_folder)
{
	if (url.empty()) return {};

	auto encoded = url;
	auto file_uri = false;

	if (url.size() >= 5 && str::icmp(url.substr(0, 5), "file:") == 0)
	{
		file_uri = true;
		encoded.remove_prefix(5);
	}
	else if (url.find("://") != std::string_view::npos)
	{
		return {};
	}

	auto decoded = decode_url_path(encoded);
	if (!decoded) return {};
	auto native = std::move(*decoded);

	if (file_uri && native.starts_with("//"))
	{
		native.erase(0, 2);
		const auto slash = native.find('/');
		const auto authority = native.substr(0, slash);
		native = slash == std::string::npos ? std::string{} : native.substr(slash);

		if (!authority.empty() && str::icmp(authority, "localhost") != 0)
		{
			native = "//" + authority + native;
		}
	}

	if constexpr (df::windows_path_semantics)
	{
		if (file_uri && native.size() >= 3 && native[0] == '/' && native[2] == ':') native.erase(0, 1);
	}

	if (is_qualified_path(native)) return df::file_path(native);
	if (project_folder.is_empty()) return {};

	for (auto&& c : native) if (c == '/') c = df::preferred_path_sep;
	const auto resolved = (std::filesystem::path(project_folder.text().sv()) / std::filesystem::path(native))
		.lexically_normal();
	return df::file_path(resolved.string());
}

using json_writer = rapidjson::Writer<rapidjson::StringBuffer>;

static void write_rational_time(json_writer& w, const double seconds)
{
	w.StartObject();
	w.Key("OTIO_SCHEMA");
	w.String("RationalTime.1");
	w.Key("rate");
	w.Double(otio_rate);
	w.Key("value");
	w.Double(std::round(seconds * otio_rate));
	w.EndObject();
}

std::string write_otio(const std::vector<movie_clip>& clips, const movie_settings& settings,
                       const df::folder_path project_folder)
{
	rapidjson::StringBuffer buffer;
	json_writer w(buffer);

	w.StartObject();
	w.Key("OTIO_SCHEMA");
	w.String("Timeline.1");
	w.Key("name");
	w.String("Diffractor movie");
	w.Key("global_start_time");
	w.Null();

	w.Key("tracks");
	w.StartObject();
	w.Key("OTIO_SCHEMA");
	w.String("Stack.1");
	w.Key("name");
	w.String("tracks");
	w.Key("children");
	w.StartArray();

	w.StartObject();
	w.Key("OTIO_SCHEMA");
	w.String("Track.1");
	w.Key("name");
	w.String("Video");
	w.Key("kind");
	w.String("Video");
	w.Key("children");
	w.StartArray();

	for (size_t i = 0; i < clips.size(); ++i)
	{
		const auto& c = clips[i];

		if (i > 0)
		{
			const auto overlap = movie_overlap(clips[i - 1], c, settings);

			if (overlap > 0)
			{
				w.StartObject();
				w.Key("OTIO_SCHEMA");
				w.String("Transition.1");
				w.Key("name");
				w.String("Crossfade");
				w.Key("transition_type");
				w.String("SMPTE_Dissolve");
				w.Key("in_offset");
				write_rational_time(w, overlap / 2);
				w.Key("out_offset");
				write_rational_time(w, overlap / 2);
				w.Key("metadata");
				w.StartObject();
				w.EndObject();
				w.EndObject();
			}
		}

		w.StartObject();
		w.Key("OTIO_SCHEMA");
		w.String("Clip.1");
		w.Key("name");
		const auto clip_name = c.path.name().sv();
		w.String(clip_name.data(), static_cast<rapidjson::SizeType>(clip_name.size()));

		w.Key("source_range");
		w.StartObject();
		w.Key("OTIO_SCHEMA");
		w.String("TimeRange.1");
		w.Key("start_time");
		write_rational_time(w, c.start);
		w.Key("duration");
		write_rational_time(w, c.duration());
		w.EndObject();

		w.Key("media_reference");
		w.StartObject();
		w.Key("OTIO_SCHEMA");
		w.String("ExternalReference.1");
		w.Key("target_url");
		w.String(to_url(c.path, project_folder));
		w.Key("available_range");
		w.Null();
		w.Key("metadata");
		w.StartObject();
		w.EndObject();
		w.EndObject();

		// OTIO has no photo, and inventing one would make the file unreadable elsewhere. The flag
		// lives in our own metadata namespace, and a reader without it infers from the extension.
		w.Key("metadata");
		w.StartObject();
		w.Key("diffractor");
		w.StartObject();
		w.Key("photo");
		w.Bool(c.is_photo);
		w.Key("photo_duration_is_default");
		w.Bool(c.photo_duration_is_default);
		w.Key("source_duration");
		w.Double(c.source_duration);
		w.EndObject();
		w.EndObject();

		w.EndObject();
	}

	w.EndArray();
	w.Key("metadata");
	w.StartObject();
	w.EndObject();
	w.EndObject(); // track

	w.EndArray();
	w.Key("metadata");
	w.StartObject();
	w.EndObject();
	w.EndObject(); // stack

	// The movie settings have no OTIO home. Another tool reads the transitions written above and
	// ignores this; Diffractor reads this and rebuilds them exactly.
	w.Key("metadata");
	w.StartObject();
	w.Key("diffractor");
	w.StartObject();
	w.Key("transition");
	w.String(settings.transition == movie_transition::crossfade ? "crossfade" : "cut");
	w.Key("transition_seconds");
	w.Double(settings.transition_seconds);
	w.Key("fade_in");
	w.Bool(settings.fade_in);
	w.Key("fade_out");
	w.Bool(settings.fade_out);
	w.Key("photo_seconds");
	w.Double(settings.photo_seconds);
	w.EndObject();
	w.EndObject();

	w.EndObject();

	return {buffer.GetString(), buffer.GetSize()};
}

using json_value = rapidjson::GenericValue<rapidjson::UTF8<char>>;

static double read_rational_time(const json_value& v)
{
	if (!v.IsObject()) return 0;

	const auto rate = v.FindMember("rate");
	const auto value = v.FindMember("value");
	if (value == v.MemberEnd() || !value->value.IsNumber()) return 0;

	const auto r = rate != v.MemberEnd() && rate->value.IsNumber() ? rate->value.GetDouble() : 0.0;
	if (r <= 0) return 0;

	const auto result = value->value.GetDouble() / r;
	return std::isfinite(result) ? result : 0.0;
}

static bool read_bool(const json_value& v, const char* name, const bool fallback)
{
	if (!v.IsObject()) return fallback;
	const auto found = v.FindMember(name);
	return found != v.MemberEnd() && found->value.IsBool() ? found->value.GetBool() : fallback;
}

static double read_double(const json_value& v, const char* name, const double fallback)
{
	if (!v.IsObject()) return fallback;
	const auto found = v.FindMember(name);
	if (found == v.MemberEnd() || !found->value.IsNumber()) return fallback;
	const auto result = found->value.GetDouble();
	return std::isfinite(result) ? result : fallback;
}

// A project file is untrusted input. The panel cannot express a duration outside its own range, so
// neither may a document: a photo hold of 1e308 seconds reaches the frame count as a number no
// integer can hold, and the conversion that walks it is undefined.
static double read_bounded_double(const json_value& v, const char* name, const double fallback,
                                  const double lowest, const double highest)
{
	return std::clamp(read_double(v, name, fallback), lowest, highest);
}

// The same for each clip's own range. A still has no timeline of its own, so only its hold means
// anything; every clip is held to the longest movie a render will make.
static void bound_clip_range(movie_clip& clip)
{
	const auto duration = std::min(clip.end - clip.start, movie_max_clip_seconds);

	if (clip.is_photo)
	{
		clip.start = 0.0;
		clip.end = std::max(duration, movie_min_photo_seconds);
	}
	else
	{
		clip.end = clip.start + duration;
	}
}

static bool is_schema(const json_value& v, const std::string_view prefix)
{
	if (!v.IsObject()) return false;

	const auto found = v.FindMember("OTIO_SCHEMA");
	if (found == v.MemberEnd() || !found->value.IsString()) return false;
	return str::starts(found->value.GetString(), prefix);
}

movie_load_result read_otio(const std::string_view json, const df::folder_path project_folder)
{
	movie_load_result result;

	rapidjson::Document doc;
	doc.Parse(json.data(), json.size());

	if (doc.HasParseError() || !doc.IsObject()) return result;

	const auto& stack = df::util::json::safe_object(doc, "tracks");
	const auto children = stack.IsObject() ? stack.FindMember("children") : stack.MemberEnd();

	if (children == stack.MemberEnd() || !children->value.IsArray())
	{
		result.status = movie_load_status::empty;
		return result;
	}

	// The first video track, and only that one. A file from another tool routinely carries audio
	// tracks, effects and markers Movie has no answer for; refusing the file over them would make
	// interchange useless, so they are counted and dropped.
	const json_value* track = nullptr;

	for (const auto& child : children->value.GetArray())
	{
		if (!is_schema(child, "Track")) continue;

		const auto kind = df::util::json::safe_string(child, "kind");

		if (!track && (kind.empty() || str::icmp(kind, "Video") == 0))
		{
			track = &child;
		}
		else
		{
			++result.ignored_elements;
		}
	}

	if (!track)
	{
		result.status = movie_load_status::empty;
		return result;
	}

	const auto& metadata = df::util::json::safe_object(doc, "metadata");
	const auto& doc_meta = df::util::json::safe_object(metadata, "diffractor");

	const auto has_diffractor_settings = metadata.IsObject() && metadata.HasMember("diffractor") &&
		metadata["diffractor"].IsObject();
	result.settings.transition = has_diffractor_settings &&
		str::icmp(df::util::json::safe_string(doc_meta, "transition"), "crossfade") == 0
		                             ? movie_transition::crossfade
		                             : movie_transition::cut;
	result.settings.transition_seconds = read_bounded_double(doc_meta, "transition_seconds", 1.0, 0.0,
	                                                        movie_max_transition_seconds);
	result.settings.fade_in = read_bool(doc_meta, "fade_in", false);
	result.settings.fade_out = read_bool(doc_meta, "fade_out", false);
	result.settings.photo_seconds = read_bounded_double(doc_meta, "photo_seconds", 4.0, movie_min_photo_seconds,
	                                                    movie_max_photo_seconds);

	const auto track_children = track->FindMember("children");

	if (track_children == track->MemberEnd() || !track_children->value.IsArray())
	{
		result.status = movie_load_status::empty;
		return result;
	}

	struct foreign_transition
	{
		size_t boundary = 0;
		double length = 0;
	};

	std::vector<foreign_transition> foreign_transitions;
	std::optional<double> pending_transition;
	auto foreign_transition_count = 0;
	auto foreign_transition_positions_valid = true;

	for (const auto& child : track_children->value.GetArray())
	{
		if (is_schema(child, "Transition"))
		{
			if (has_diffractor_settings) continue;
			++foreign_transition_count;

			if (str::icmp(df::util::json::safe_string(child, "transition_type"), "SMPTE_Dissolve") == 0)
			{
				const auto in = child.FindMember("in_offset");
				const auto out = child.FindMember("out_offset");
				const auto in_length = in != child.MemberEnd() ? read_rational_time(in->value) : 0.0;
				const auto out_length = out != child.MemberEnd() ? read_rational_time(out->value) : 0.0;
				const auto length = in_length + out_length;

				if (in_length > 0 && out_length > 0 && std::abs(in_length - out_length) < 0.0001)
				{
					if (result.clips.empty() || pending_transition)
					{
						foreign_transition_positions_valid = false;
					}
					else
					{
						pending_transition = length;
					}
					continue;
				}
			}

			foreign_transition_positions_valid = false;
			continue;
		}

		if (!is_schema(child, "Clip"))
		{
			if (pending_transition) foreign_transition_positions_valid = false;
			++result.ignored_elements;
			continue;
		}

		const auto& reference = df::util::json::safe_object(child, "media_reference");
		const auto url = df::util::json::safe_string(reference, "target_url");
		const auto path = from_url(url, project_folder);

		if (path.is_empty())
		{
			if (pending_transition) foreign_transition_positions_valid = false;
			++result.ignored_elements;
			continue;
		}

		movie_clip clip;
		clip.path = path;

		const auto& range = df::util::json::safe_object(child, "source_range");
		const auto start = range.FindMember("start_time");
		const auto duration = range.FindMember("duration");

		clip.start = start != range.MemberEnd() ? read_rational_time(start->value) : 0.0;
		const auto clip_duration = duration != range.MemberEnd() ? read_rational_time(duration->value) : 0.0;
		clip.end = clip.start + clip_duration;

		if (!std::isfinite(clip.start) || !std::isfinite(clip.end) || clip.start < 0 || clip_duration <= 0)
		{
			if (pending_transition) foreign_transition_positions_valid = false;
			++result.ignored_elements;
			continue;
		}

		const auto& clip_meta = df::util::json::safe_object(df::util::json::safe_object(child, "metadata"),
		                                                   "diffractor");

		const auto mt = files::file_type_from_name(path);
		clip.is_photo = read_bool(clip_meta, "photo", mt && mt->group == file_group::photo);
		clip.photo_duration_is_default = read_bool(clip_meta, "photo_duration_is_default", false);
		bound_clip_range(clip);
		clip.source_duration = read_double(clip_meta, "source_duration", clip.end);

		if (pending_transition)
		{
			foreign_transitions.emplace_back(foreign_transition{result.clips.size(), *pending_transition});
			pending_transition.reset();
		}

		result.clips.emplace_back(clip);
	}

	if (pending_transition) foreign_transition_positions_valid = false;

	if (!has_diffractor_settings && foreign_transition_positions_valid && result.clips.size() > 1 &&
		foreign_transitions.size() == result.clips.size() - 1)
	{
		const auto length = foreign_transitions.front().length;
		const auto uniform = std::all_of(foreign_transitions.begin(), foreign_transitions.end(),
			[length](const foreign_transition& transition)
			{
				return std::abs(transition.length - length) < 0.0001;
			});
		auto complete = true;
		for (size_t i = 0; i < foreign_transitions.size(); ++i)
		{
			const auto boundary = foreign_transitions[i].boundary;
			if (boundary != i + 1 || boundary >= result.clips.size())
			{
				complete = false;
				continue;
			}

			const auto representable = std::min(result.clips[boundary - 1].duration() / 2,
			                                    result.clips[boundary].duration() / 2);
			if (foreign_transitions[i].length > representable + 0.0001) complete = false;
		}

		if (uniform && complete)
		{
			result.settings.transition = movie_transition::crossfade;
			result.settings.transition_seconds = std::clamp(length, 0.0, movie_max_transition_seconds);
		}
		else
		{
			result.ignored_elements += foreign_transition_count;
		}
	}
	else if (!has_diffractor_settings)
	{
		result.ignored_elements += foreign_transition_count;
	}

	result.status = result.clips.empty() ? movie_load_status::empty : movie_load_status::ok;
	return result;
}

//
// Windows Live Movie Maker
//

// A tolerant scraper, not a parser. The .wlmp schema is undocumented and has never been stable
// across the tools that wrote it, so matching on exact element names would break on the next file.
// It reads attributes off any element that looks like a media item or an extent and ignores the
// document's structure entirely. It resolves no entity it was not given inline and follows no
// reference, so there is no external-entity surface here.
namespace
{
	struct xml_element
	{
		std::string_view name;
		std::vector<std::pair<std::string_view, std::string>> attributes;

		const std::string* find(const std::string_view key) const
		{
			for (const auto& [k, v] : attributes)
			{
				if (str::icmp(k, key) == 0) return &v;
			}

			return nullptr;
		}
	};

	std::string decode_entities(const std::string_view text)
	{
		if (text.find('&') == std::string_view::npos) return std::string(text);

		std::string result;
		result.reserve(text.size());

		for (size_t i = 0; i < text.size();)
		{
			if (text[i] != '&')
			{
				result += text[i++];
				continue;
			}

			const auto end = text.find(';', i);

			if (end == std::string_view::npos || end - i > 10)
			{
				result += text[i++];
				continue;
			}

			const auto entity = text.substr(i + 1, end - i - 1);

			if (entity == "amp") result += '&';
			else if (entity == "lt") result += '<';
			else if (entity == "gt") result += '>';
			else if (entity == "quot") result += '"';
			else if (entity == "apos") result += '\'';
			else if (entity.size() > 1 && entity[0] == '#')
			{
				const auto hex = entity[1] == 'x' || entity[1] == 'X';
				const auto digits = entity.substr(hex ? 2 : 1);
				uint32_t code = 0;

				const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), code,
				                                       hex ? 16 : 10);

				if (ec == std::errc{} && ptr == digits.data() + digits.size() && code > 0 && code < 0x110000)
				{
					str::char32_to_utf8(std::back_inserter(result), code);
				}
			}
			else
			{
				result.append(text.substr(i, end - i + 1));
			}

			i = end + 1;
		}

		return result;
	}

	std::vector<xml_element> scan_xml(const std::string_view text)
	{
		std::vector<xml_element> result;
		size_t i = 0;

		while (i < text.size())
		{
			const auto open = text.find('<', i);
			if (open == std::string_view::npos) break;

			i = open + 1;
			if (i >= text.size()) break;

			// Declarations, comments, doctypes and closing tags carry nothing this reads.
			if (text[i] == '?' || text[i] == '!' || text[i] == '/')
			{
				const auto close = text.find('>', i);
				if (close == std::string_view::npos) break;
				i = close + 1;
				continue;
			}

			const auto name_start = i;
			while (i < text.size() && !str::is_white_space(text[i]) && text[i] != '>' && text[i] != '/') ++i;

			xml_element element;
			element.name = text.substr(name_start, i - name_start);

			while (i < text.size() && text[i] != '>')
			{
				while (i < text.size() && (str::is_white_space(text[i]) || text[i] == '/')) ++i;
				if (i >= text.size() || text[i] == '>') break;

				const auto key_start = i;
				while (i < text.size() && text[i] != '=' && text[i] != '>' && !str::is_white_space(text[i])) ++i;
				const auto key = text.substr(key_start, i - key_start);

				while (i < text.size() && str::is_white_space(text[i])) ++i;
				if (i >= text.size() || text[i] != '=') continue;
				++i;
				while (i < text.size() && str::is_white_space(text[i])) ++i;
				if (i >= text.size()) break;

				const auto quote = text[i];
				if (quote != '"' && quote != '\'') continue;

				++i;
				const auto value_start = i;
				while (i < text.size() && text[i] != quote) ++i;
				const auto value = text.substr(value_start, i - value_start);
				if (i < text.size()) ++i;

				if (!key.empty()) element.attributes.emplace_back(key, decode_entities(value));
			}

			if (i < text.size()) ++i;
			if (!element.name.empty()) result.emplace_back(std::move(element));
		}

		return result;
	}

	// from_chars rather than stod: stod reads the C locale's decimal point, and a project file's
	// numbers are always '.' whatever the user's regional settings say. A malformed number means
	// "this attribute is absent", so the caller falls back to the clip's other timings.
	double parse_seconds(const std::string* text)
	{
		if (!text || text->empty()) return -1;

		double value = 0;
		const auto* const first = text->data();
		const auto* const last = first + text->size();
		const auto [ptr, ec] = std::from_chars(first, last, value);

		return ec != std::errc{} || !std::isfinite(value) ? -1 : value;
	}

	bool is_ignored_extent(const std::string_view name)
	{
		static constexpr std::string_view ignored[] = {
			"audio", "title", "caption", "credit", "narration", "music", "text", "bounded", "sound",
			"effect", "animation"
		};

		for (const auto prefix : ignored)
		{
			if (name.size() >= prefix.size() && str::icmp(name.substr(0, prefix.size()), prefix) == 0) return true;
		}

		return false;
	}
}

movie_load_result read_wlmp(const std::string_view xml)
{
	movie_load_result result;

	const auto elements = scan_xml(xml);
	if (elements.empty()) return result;

	df::hash_map<std::string, df::file_path, df::ihash, df::ieq> media;

	struct positioned_clip
	{
		movie_clip clip;
		double position = -1;
	};

	std::vector<positioned_clip> clips;

	for (const auto& element : elements)
	{
		const auto id = element.find("id");
		const auto file = element.find("filePath");
		if (!id || !file || id->empty() || file->empty()) continue;
		if (!is_qualified_path(*file)) continue;

		media.insert_or_assign(*id, df::file_path(*file));
	}

	if (media.empty()) return result;

	for (const auto& element : elements)
	{
		if (is_ignored_extent(element.name))
		{
			++result.ignored_elements;
			continue;
		}

		const auto reference = element.find("mediaItemID");
		if (!reference) continue;

		const auto found = media.find(*reference);

		if (found == media.end())
		{
			++result.ignored_elements;
			continue;
		}

		const auto mt = files::file_type_from_name(found->second);
		const auto photo_by_name = mt && mt->group == file_group::photo;

		movie_clip clip;
		clip.path = found->second;
		clip.is_photo = str::contains(element.name, "image") || str::contains(element.name, "photo") ||
			(!str::contains(element.name, "video") && photo_by_name);
		clip.photo_duration_is_default = false;

		const auto in_time = parse_seconds(element.find("inTime"));
		const auto out_time = parse_seconds(element.find("outTime"));
		const auto duration = parse_seconds(element.find("duration"));

		if (in_time >= 0 && out_time > in_time)
		{
			clip.start = in_time;
			clip.end = out_time;
		}
		else if (duration > 0)
		{
			clip.start = std::max(0.0, in_time);
			clip.end = clip.start + duration;
		}
		else
		{
			++result.ignored_elements;
			continue;
		}

		bound_clip_range(clip);
		clip.source_duration = clip.end;
		clips.emplace_back(positioned_clip{std::move(clip), parse_seconds(element.find("position"))});
	}

	if (std::any_of(clips.begin(), clips.end(), [](const positioned_clip& clip) { return clip.position >= 0; }))
	{
		std::stable_sort(clips.begin(), clips.end(), [](const positioned_clip& left, const positioned_clip& right)
		{
			if (left.position < 0) return false;
			if (right.position < 0) return true;
			return left.position < right.position;
		});
	}

	for (auto&& clip : clips) result.clips.emplace_back(std::move(clip.clip));

	result.status = result.clips.empty() ? movie_load_status::empty : movie_load_status::ok;
	return result;
}

//
// The document
//

void movie_project::push_undo()
{
	_undo.emplace_back(undo_entry{_clips, _settings, _selected, _current, _anchor});
	if (_undo.size() > max_undo) _undo.erase(_undo.begin());
	_modified = true;
	++_revision;
}

void movie_project::select_only(const size_t index)
{
	if (_clips.empty())
	{
		_selected.clear();
		_current = 0;
		_anchor = 0;
		return;
	}

	_current = std::min(index, _clips.size() - 1);
	_anchor = _current;
	_selected.assign(1, _current);
}

void movie_project::current(const size_t i)
{
	select_only(i);
}

bool movie_project::is_selected(const size_t i) const
{
	return std::find(_selected.begin(), _selected.end(), i) != _selected.end();
}

void movie_project::select(const size_t index, const bool extend, const bool toggle)
{
	if (index >= _clips.size()) return;

	if (toggle)
	{
		const auto found = std::find(_selected.begin(), _selected.end(), index);

		if (found != _selected.end())
		{
			// The last selected clip cannot be toggled away: a command that acts on the selection
			// would then have nothing to act on while the strip still shows a focused clip.
			if (_selected.size() > 1) _selected.erase(found);
		}
		else
		{
			_selected.emplace_back(index);
			std::sort(_selected.begin(), _selected.end());
		}

		_anchor = index;
	}
	else if (extend)
	{
		_selected.clear();

		for (auto i = std::min(_anchor, index); i <= std::max(_anchor, index); ++i)
		{
			_selected.emplace_back(i);
		}
	}
	else
	{
		_selected.assign(1, index);
		_anchor = index;
	}

	_current = index;
}

void movie_project::focus(const size_t index)
{
	if (index < _clips.size()) _current = index;
}

void movie_project::select_all()
{
	_selected.clear();
	for (size_t i = 0; i < _clips.size(); ++i) _selected.emplace_back(i);
	if (_current >= _clips.size()) _current = _clips.empty() ? 0 : _clips.size() - 1;
}

const movie_clip* movie_project::current_clip() const
{
	return _current < _clips.size() ? &_clips[_current] : nullptr;
}

bool movie_project::is_ready_to_render() const
{
	return !_clips.empty() && std::all_of(_clips.begin(), _clips.end(), [](const movie_clip& clip)
	{
		return clip.is_probed && !clip.is_missing && clip.duration() > 0 &&
			clip.extent.cx > 0 && clip.extent.cy > 0;
	});
}

void movie_project::append(const movie_clip& clip)
{
	insert(_clips.size(), clip);
}

void movie_project::insert(const size_t at, const movie_clip& clip)
{
	push_undo();
	const auto index = std::min(at, _clips.size());
	_clips.insert(_clips.begin() + index, clip);
	select_only(index);
}

void movie_project::remove(const size_t at)
{
	if (at >= _clips.size()) return;

	push_undo();
	_clips.erase(_clips.begin() + at);
	select_only(_current > at ? _current - 1 : _current);
}

void movie_project::move(const size_t from, const size_t to)
{
	if (from >= _clips.size() || from == to) return;

	push_undo();
	const auto clip = _clips[from];
	_clips.erase(_clips.begin() + from);
	const auto index = std::min(to, _clips.size());
	_clips.insert(_clips.begin() + index, clip);
	select_only(index);
}

void movie_project::move_selection(const size_t to)
{
	if (_selected.empty() || _clips.empty()) return;

	std::vector<movie_clip> moved;
	std::vector<movie_clip> rest;
	size_t insert_at = 0;
	size_t focus_offset = 0;

	// `to` counts positions in the list as it stands, so the destination has to be re-expressed
	// against the clips that will still be there once the selection is lifted out.
	for (size_t i = 0; i < _clips.size(); ++i)
	{
		if (is_selected(i))
		{
			if (i == _current) focus_offset = moved.size();
			moved.emplace_back(_clips[i]);
		}
		else
		{
			if (i < to) ++insert_at;
			rest.emplace_back(_clips[i]);
		}
	}

	if (moved.empty()) return;

	insert_at = std::min(insert_at, rest.size());

	// A block dropped where it already is changes nothing, and must not cost an undo step.
	if (_selected.front() == insert_at && _selected.back() - _selected.front() + 1 == _selected.size()) return;

	push_undo();

	rest.insert(rest.begin() + insert_at, moved.begin(), moved.end());
	_clips = std::move(rest);

	_selected.clear();
	for (size_t i = 0; i < moved.size(); ++i) _selected.emplace_back(insert_at + i);

	_current = insert_at + focus_offset;
	_anchor = _current;
}

void movie_project::remove_selection()
{
	if (_selected.empty() || _clips.empty()) return;

	push_undo();

	const auto first = _selected.front();

	// Erased from the back so the earlier indices stay valid as the list shrinks.
	for (auto it = _selected.rbegin(); it != _selected.rend(); ++it)
	{
		if (*it < _clips.size()) _clips.erase(_clips.begin() + *it);
	}

	select_only(first);
}

void movie_project::replace(const size_t at, const movie_clip& clip)
{
	if (at >= _clips.size()) return;

	push_undo();
	_clips[at] = clip;
}

void movie_project::replace_many(const std::vector<std::pair<size_t, movie_clip>>& replacements)
{
	if (replacements.empty()) return;
	if (std::none_of(replacements.begin(), replacements.end(), [this](const auto& replacement)
	{
		return replacement.first < _clips.size();
	})) return;

	push_undo();

	for (const auto& [at, clip] : replacements)
	{
		if (at < _clips.size()) _clips[at] = clip;
	}
}

void movie_project::replace_quietly(const size_t at, const movie_clip& clip)
{
	if (at >= _clips.size()) return;

	_clips[at] = clip;
	++_revision;
}

void movie_project::settings(const movie_settings& s)
{
	push_undo();
	_settings = s;
	apply_photo_duration();
}

void movie_project::apply_photo_duration()
{
	for (auto&& c : _clips)
	{
		if (!c.is_photo || !c.photo_duration_is_default) continue;
		c.start = 0;
		c.end = _settings.photo_seconds;
	}
}

void movie_project::trim(const size_t at, const double start, const double end)
{
	if (at >= _clips.size()) return;

	push_undo();
	auto& c = _clips[at];

	if (c.is_photo)
	{
		c.start = 0;
		c.end = std::max(0.1, end - start);
		c.photo_duration_is_default = false;
	}
	else
	{
		const auto limit = c.source_duration > 0 ? c.source_duration : end;
		c.start = std::clamp(start, 0.0, limit);
		c.end = std::clamp(end, c.start, limit);
	}
}

void movie_project::reset(std::vector<movie_clip> clips, const movie_settings& s, const df::file_path path)
{
	_clips = std::move(clips);
	_settings = s;
	_path = path;
	_seed.clear();
	_undo.clear();
	_modified = false;
	++_revision;
	select_only(0);
}

void movie_project::mark_saved(const df::file_path path, const uint64_t revision)
{
	_path = path;
	if (_revision == revision) _modified = false;

	// Saving makes it a document. A timeline seeded from a selection and then written to a file is
	// no longer a view of that selection, and leaving the seed behind meant re-entering Movie with
	// a different selection read it as an untouched seed and replaced the file just saved.
	_seed.clear();
}

void movie_project::mark_seeded(std::vector<df::file_path> seed)
{
	_seed = std::move(seed);
	// A seeded timeline has no state before itself, so there is nothing to undo back to and nothing
	// yet for the user to have changed.
	_undo.clear();
	_modified = false;
}

void movie_project::undo()
{
	if (_undo.empty()) return;

	auto entry = std::move(_undo.back());
	_undo.pop_back();

	_clips = std::move(entry.clips);
	_settings = entry.settings;
	_selected = std::move(entry.selected);
	_current = entry.current;
	_anchor = entry.anchor;
	_modified = true;
	++_revision;

	// The stored selection came from a list that may have been longer than this one.
	std::erase_if(_selected, [this](const size_t i) { return i >= _clips.size(); });
	if (_selected.empty() && !_clips.empty()) select_only(std::min(_current, _clips.size() - 1));
	if (_current >= _clips.size()) _current = _clips.empty() ? 0 : _clips.size() - 1;
}

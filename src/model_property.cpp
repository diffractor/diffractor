// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "app_icons.h"
#include "model_location.h"
#include "model_items.h"

static_assert(std::is_move_constructible_v<prop::item_metadata>);

using property_map = df::hash_map<std::u8string_view, prop::key_ref, df::ihash, df::ieq>;
static property_map all_props;


int prop::exp_round(const double d)
{
	const auto n = df::round(d);
	if (n >= 950ull) return df::round64(n, 1000ull) * 1000ull;
	if (n >= 95ull) return df::round64(n, 100ull) * 100ull;
	if (n >= 5ull) return df::round64(n, 10ull) * 10ull;
	return n;
}

df::xy32 df::xy32::null = {0, 0};

df::xy32 df::xy32::parse(const std::u8string_view r)
{
	if (str::is_num(r))
	{
		return make(str::to_int(r), 0);
	}

	int x, y;

	if (_snscanf_s(std::bit_cast<const char*>(r.data()), r.size(), "%d/%d", &x, &y) == 2)
	{
		return make(x, y);
	}

	return null;
}

df::xy8 df::xy8::parse(const std::u8string_view r)
{
	if (str::is_num(r))
	{
		return make(str::to_int(r), 0);
	}

	int x, y;

	if (_snscanf_s(std::bit_cast<const char*>(r.data()), r.size(), "%d/%d", &x, &y) == 2)
	{
		return make(x, y);
	}

	return {0, 0};
}

prop::key::key(const uint16_t id, std::u8string_view sn, const std::u8string_view n, std::u8string_view& tx,
               const icon_index i, const prop::data_type t, const uint32_t f, const uint32_t bit) : id(id), icon(i),
	name(str::cache(n)), text_key(tx), data_type(t), flags(f), bloom_bit(bit)
{
	df::assert_true(!all_props.contains(name));
	all_props[name] = this;
}

std::u8string prop::key::text() const
{
	return tt.translate_text(std::u8string(text_key), u8"propertry"sv);
}

static std::u8string_view prop_name_crc32c = u8"crc32c"sv;

prop::key prop::album('al', u8""sv, u8"album"sv, tt.prop_name_album, icon_index::star, data_type::string,
                      style::groupable | style::sortable | style::auto_complete, bloom_bits::album);
prop::key prop::show('sw', u8""sv, u8"show"sv, tt.prop_name_show, icon_index::star, data_type::string,
                     style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::season('ss', u8""sv, u8"season"sv, tt.prop_name_season, icon_index::star, data_type::int32,
                       style::groupable | style::sortable, 0);
prop::key prop::episode('ep', u8""sv, u8"episode"sv, tt.prop_name_episode, icon_index::star, data_type::int_pair,
                        style::groupable | style::sortable, 0);
prop::key prop::artist('ar', u8""sv, u8"artist"sv, tt.prop_name_artist, icon_index::person, data_type::string,
                       style::groupable | style::sortable | style::auto_complete, bloom_bits::artist);
prop::key prop::album_artist('aa', u8""sv, u8"album.artist"sv, tt.prop_name_albumartist, icon_index::person,
                             data_type::string, style::groupable | style::sortable | style::auto_complete,
                             bloom_bits::artist);
prop::key prop::audio_codec('ac', u8""sv, u8"audio.codec"sv, tt.prop_name_audiocodec, icon_index::audio, data_type::string,
                            style::groupable | style::sortable | style::auto_complete, bloom_bits::codec);
prop::key prop::bitrate('br', u8""sv, u8"bitrate"sv, tt.prop_name_bitrate, icon_index::star, data_type::string,
                        style::fuzzy_search | style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::camera_manufacturer('ca', u8""sv, u8"camera.manufacturer"sv, tt.prop_name_cameramanufacturer,
                                    icon_index::camera, data_type::string,
                                    style::groupable | style::sortable | style::auto_complete, bloom_bits::camera);
prop::key prop::camera_model('cm', u8""sv, u8"camera"sv, tt.prop_name_camera, icon_index::camera, data_type::string,
                             style::groupable | style::sortable | style::auto_complete, bloom_bits::camera);
prop::key prop::audio_channels('ch', u8""sv, u8"audio.channels"sv, tt.prop_name_channels, icon_index::star,
                               data_type::int32, style::sortable, bloom_bits::audio_codec);
prop::key prop::audio_sample_rate('sr', u8""sv, u8"audio.sample.rate"sv, tt.prop_name_samplerate, icon_index::star,
                                  data_type::int32, style::groupable | style::sortable | style::auto_complete,
                                  bloom_bits::audio_codec);
prop::key prop::audio_sample_type('sa', u8""sv, u8"audio.sample.type"sv, tt.prop_name_sampletype, icon_index::star,
                                  data_type::int32, style::groupable | style::sortable | style::auto_complete,
                                  bloom_bits::audio_codec);
prop::key prop::location_place('ci', u8""sv, u8"place"sv, tt.prop_name_place, icon_index::world, data_type::string,
                               style::groupable | style::sortable | style::auto_complete, bloom_bits::location);
prop::key prop::comment('ct', u8""sv, u8"comment"sv, tt.prop_name_comment, icon_index::star, data_type::string2,
                        style::none, bloom_bits::text);
prop::key prop::description('de', u8""sv, u8"description"sv, tt.prop_name_description, icon_index::star, data_type::string2,
                            style::none, bloom_bits::text);
prop::key prop::composer('co', u8""sv, u8"composer"sv, tt.prop_name_composer, icon_index::star, data_type::string,
                         style::sortable, bloom_bits::artist);
prop::key prop::copyright_credit('cd', u8""sv, u8"copyright.credit"sv, tt.prop_name_copyrightcredit, icon_index::copyright,
                                 data_type::string, style::groupable | style::sortable | style::auto_complete,
                                 bloom_bits::credit);
prop::key prop::copyright_source('cs', u8""sv, u8"copyright.source"sv, tt.prop_name_copyrightsource, icon_index::copyright,
                                 data_type::string, style::groupable | style::sortable | style::auto_complete,
                                 bloom_bits::credit);
prop::key prop::copyright_creator('cc', u8""sv, u8"copyright.creator"sv, tt.prop_name_copyrightcreator,
                                  icon_index::copyright, data_type::string,
                                  style::groupable | style::sortable | style::auto_complete, bloom_bits::credit);
prop::key prop::copyright_notice('cp', u8""sv, u8"copyright.notice"sv, tt.prop_name_copyrightnotice, icon_index::copyright,
                                 data_type::string, style::groupable | style::sortable | style::auto_complete,
                                 bloom_bits::credit);
prop::key prop::copyright_url('cw', u8""sv, u8"copyright.url"sv, tt.prop_name_copyrighturl, icon_index::copyright,
                              data_type::string, style::groupable | style::sortable | style::auto_complete,
                              bloom_bits::credit);
prop::key prop::location_country('cn', u8""sv, u8"country"sv, tt.prop_name_country, icon_index::world, data_type::string,
                                 style::groupable | style::sortable | style::auto_complete, bloom_bits::location);
prop::key prop::created_exif('c', u8""sv, u8"created.exif"sv, tt.prop_name_createdexif, icon_index::time, data_type::date,
                             style::groupable | style::sortable | style::auto_complete, bloom_bits::created);
prop::key prop::created_digitized('cz', u8""sv, u8"digitized"sv, tt.prop_name_digitized, icon_index::time, data_type::date,
                                  style::groupable | style::sortable | style::auto_complete, bloom_bits::created);
prop::key prop::created_utc('cu', u8""sv, u8"created"sv, tt.prop_name_created, icon_index::time, data_type::date,
                            style::groupable | style::sortable | style::auto_complete, bloom_bits::created);
prop::key prop::disk_num('dk', u8""sv, u8"disk"sv, tt.prop_name_disk, icon_index::star, data_type::int_pair,
                         style::groupable | style::sortable, 0);
prop::key prop::dimensions('di', u8""sv, u8"dimensions"sv, tt.prop_name_dimensions, icon_index::star, data_type::int_pair,
                           style::groupable | style::sortable, 0);
prop::key prop::duration('du', u8""sv, u8"duration"sv, tt.prop_name_duration, icon_index::time, data_type::int32,
                         style::fuzzy_search | style::groupable | style::sortable | style::auto_complete,
                         bloom_bits::duration);
prop::key prop::encoder('en', u8""sv, u8"encoder"sv, tt.prop_name_encoder, icon_index::star, data_type::string,
                        style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::encoding_tool('es', u8""sv, u8"encoding.tool"sv, tt.prop_name_encodingtool, icon_index::star,
                              data_type::string, style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::exposure_time('et', u8""sv, u8"exposure"sv, tt.prop_name_exposure, icon_index::camera, data_type::float32,
                              style::fuzzy_search | style::groupable | style::sortable | style::auto_complete,
                              bloom_bits::camera_settings);
prop::key prop::f_number('fs', u8""sv, u8"fnumber"sv, tt.prop_name_fnumber, icon_index::star, data_type::float32,
                         style::groupable | style::sortable | style::auto_complete, bloom_bits::camera_settings);
prop::key prop::focal_length('fl', u8""sv, u8"focal.length"sv, tt.prop_name_focallength, icon_index::star,
                             data_type::float32,
                             style::fuzzy_search | style::groupable | style::sortable | style::auto_complete,
                             bloom_bits::camera_settings);
prop::key prop::focal_length_35mm_equivalent('f3', u8""sv, u8"focal.length.35mm.equivalent"sv, tt.prop_name_35mmequivalent,
                                             icon_index::star, data_type::int32, style::none,
                                             bloom_bits::camera_settings);
prop::key prop::pixel_format('pf', u8""sv, u8"pixel.format"sv, tt.prop_name_pixelformat, icon_index::star,
                             data_type::string, style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::genre('gn', u8""sv, u8"genre"sv, tt.prop_name_genre, icon_index::star, data_type::string,
                      style::groupable | style::sortable | style::auto_complete, bloom_bits::genre);
prop::key prop::iso_speed('is', u8""sv, u8"iso"sv, tt.prop_name_iso, icon_index::star, data_type::int32,
                          style::groupable | style::sortable | style::auto_complete, bloom_bits::camera_settings);
prop::key prop::latitude('lx', u8""sv, u8"latitude"sv, tt.prop_name_latitude, icon_index::world, data_type::float32,
                         style::none, bloom_bits::location);
prop::key prop::lens('lm', u8""sv, u8"lens"sv, tt.prop_name_lens, icon_index::camera, data_type::string,
                     style::groupable | style::sortable | style::auto_complete, bloom_bits::camera);
prop::key prop::longitude('ly', u8""sv, u8"longitude"sv, tt.prop_name_longitude, icon_index::world, data_type::float32,
                          style::none, bloom_bits::location);
prop::key prop::media_category('mc', u8""sv, u8"media.category"sv, tt.prop_name_mediacategory, icon_index::star,
                               data_type::int32, style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::megapixels('mp', u8""sv, u8"megapixels"sv, tt.prop_name_megapixels, icon_index::star, data_type::float32,
                           style::fuzzy_search | style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::modified('m', u8""sv, u8"modified"sv, tt.prop_name_modified, icon_index::time, data_type::date,
                         style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::null(0, u8""sv, u8"null"sv, tt.prop_name_null, icon_index::star, data_type::int32, style::none, 0);
prop::key prop::orientation('or', u8""sv, u8"orientation"sv, tt.prop_name_orientation, icon_index::star, data_type::int32,
                            style::none, bloom_bits::camera_settings);
prop::key prop::publisher('pb', u8""sv, u8"publisher"sv, tt.prop_name_publisher, icon_index::star, data_type::string,
                          style::groupable | style::sortable | style::auto_complete, bloom_bits::artist);
prop::key prop::performer('pm', u8""sv, u8"performer"sv, tt.prop_name_performer, icon_index::star, data_type::string,
                          style::groupable | style::sortable | style::auto_complete, bloom_bits::artist);
prop::key prop::rating('rt', u8""sv, u8"rating"sv, tt.prop_name_rating, icon_index::star, data_type::int32,
                       style::groupable | style::sortable | style::auto_complete, bloom_bits::rating_label);
prop::key prop::file_size('s', u8""sv, u8"size"sv, tt.prop_name_size, icon_index::star, data_type::size,
                          style::fuzzy_search | style::groupable | style::sortable | style::auto_complete, 0);
prop::key prop::location_state('st', u8""sv, u8"state"sv, tt.prop_name_state, icon_index::world, data_type::string,
                               style::groupable | style::sortable | style::auto_complete, bloom_bits::location);
prop::key prop::streams('sm', u8""sv, u8"streams"sv, tt.prop_name_streams, icon_index::star, data_type::int32,
                        style::groupable | style::sortable, 0);
prop::key prop::synopsis('sy', u8""sv, u8"synopsis"sv, tt.prop_name_synopsis, icon_index::star, data_type::string,
                         style::none, bloom_bits::text);
prop::key prop::tag('tg', u8""sv, u8"tag"sv, tt.prop_name_tag, icon_index::tag, data_type::string,
                    style::groupable | style::multi_value | style::auto_complete, bloom_bits::tag);
prop::key prop::title('tt', u8""sv, u8"title"sv, tt.prop_name_title, icon_index::star, data_type::string, style::sortable,
                      bloom_bits::text);
prop::key prop::track_num('tr', u8""sv, u8"track"sv, tt.prop_name_track, icon_index::star, data_type::int_pair,
                          style::sortable, 0);
prop::key prop::video_codec('vc', u8""sv, u8"video.codec"sv, tt.prop_name_videocodec, icon_index::video, data_type::string,
                            style::groupable | style::sortable | style::auto_complete, bloom_bits::codec);
prop::key prop::year('yr', u8""sv, u8"year"sv, tt.prop_name_year, icon_index::time, data_type::int32,
                     style::groupable | style::sortable | style::auto_complete, bloom_bits::year);
prop::key prop::unique_id('id', u8""sv, u8"id"sv, tt.prop_name_id, icon_index::star, data_type::string2, style::none, 0);
prop::key prop::file_name('fn', u8""sv, u8"file.name"sv, tt.prop_name_filename, icon_index::star, data_type::string,
                          style::none, 0);
prop::key prop::raw_file_name('rf', u8""sv, u8"raw.file"sv, tt.prop_name_rawfile, icon_index::star, data_type::string,
                              style::none, 0);
prop::key prop::system('se', u8""sv, u8"system"sv, tt.prop_name_system, icon_index::star, data_type::string, style::none,
                       bloom_bits::game);
prop::key prop::game('gm', u8""sv, u8"game"sv, tt.prop_name_game, icon_index::star, data_type::string, style::none,
                     bloom_bits::game);

prop::key prop::crc32c('cr', u8""sv, u8"crc32c"sv, prop_name_crc32c, icon_index::star, data_type::int32, style::none,
                       bloom_bits::hash);

prop::key prop::label('lb', u8""sv, u8"label"sv, tt.prop_name_label, icon_index::flag, data_type::string,
                      style::groupable | style::sortable | style::auto_complete, bloom_bits::rating_label);
prop::key prop::doc_id('ii', u8""sv, u8"document.id"sv, tt.prop_name_doc_id, icon_index::star, data_type::int32,
                       style::none, bloom_bits::doc_id);


static df::hash_map<unsigned short, prop::key_ref> build_properties_by_id()
{
	df::hash_map<unsigned short, prop::key_ref> result;

	for (const auto& i : all_props)
	{
		const auto& prop = i.second;
		df::assert_true(!result.contains(prop->id));
		result[prop->id] = prop;
	}

	return result;
}


static property_map build_properties_by_name()
{
	property_map result;

	for (const auto& i : all_props)
	{
		const auto& prop = i.second;
		result[prop->name] = prop;
	}

	result[u8"albums"] = prop::album;
	result[u8"genres"] = prop::genre;
	result[u8"lenses"] = prop::lens;
	result[u8"aperture"] = prop::f_number;
	result[u8"camera"] = prop::camera_model;
	result[u8"cameraManufacturer"] = prop::camera_manufacturer;
	result[u8"cameraModel"] = prop::camera_model;
	result[u8"cameramake"] = prop::camera_manufacturer;
	result[u8"cameramodel"] = prop::camera_model;
	result[u8"cameras"] = prop::camera_model;
	result[u8"caption"] = prop::description;
	result[u8"city"] = prop::location_place;
	result[u8"changed"] = prop::modified;
	result[u8"copyright"] = prop::copyright_notice;
	result[u8"creator"] = prop::copyright_creator;
	result[u8"source"] = prop::copyright_source;
	result[u8"countries"] = prop::location_country;
	result[u8"country"] = prop::location_country;
	result[u8"created"] = prop::created_utc;
	result[u8"credit"] = prop::copyright_credit;
	result[u8"credits"] = prop::copyright_credit;
	result[u8"comm"] = prop::comment;
	result[u8"desc"] = prop::description;
	result[u8"dimensions"] = prop::dimensions;
	result[u8"exposureTime"] = prop::exposure_time;
	result[u8"exposure"] = prop::exposure_time;
	result[u8"film"] = prop::focal_length_35mm_equivalent;
	result[u8"filmEquivalent"] = prop::focal_length_35mm_equivalent;
	result[u8"fspot"] = prop::f_number;
	result[u8"fnumber"] = prop::f_number;
	result[u8"focalLength"] = prop::focal_length;
	result[u8"focalLength35mmEquivalent"] = prop::focal_length_35mm_equivalent;
	result[u8"iso"] = prop::iso_speed;
	result[u8"isospeed"] = prop::iso_speed;
	result[u8"latitude"] = prop::latitude;
	result[u8"lens"] = prop::lens;
	result[u8"longitude"] = prop::longitude;
	result[u8"make"] = prop::camera_model;
	result[u8"megapixels"] = prop::megapixels;
	result[u8"pixels"] = prop::megapixels;
	result[u8"megapixel"] = prop::megapixels;
	result[u8"model"] = prop::camera_model;
	result[u8"modified"] = prop::modified;
	result[u8"mp"] = prop::megapixels;
	result[u8"orientation"] = prop::orientation;
	result[u8"rating"] = prop::rating;
	result[u8"ratings"] = prop::rating;
	result[u8"size"] = prop::file_size;
	result[u8"filesize"] = prop::file_size;
	result[u8"source"] = prop::copyright_source;
	result[u8"speed"] = prop::iso_speed;
	result[u8"star"] = prop::rating;
	result[u8"stars"] = prop::rating;
	result[u8"state"] = prop::location_state;
	result[u8"programme"] = prop::show;
	result[u8"program"] = prop::show;
	result[u8"tag"] = prop::tag;
	result[u8"tagged"] = prop::tag;
	result[u8"tags"] = prop::tag;
	result[u8"taken"] = prop::created_utc;
	result[u8"timeline"] = prop::created_utc;
	result[u8"updated"] = prop::modified;
	result[u8"when"] = prop::created_utc;
	result[u8"created"] = prop::created_utc;
	result[u8"x"] = prop::latitude;
	result[u8"y"] = prop::longitude;
	result[u8"years"] = prop::year;
	result[u8"year"] = prop::year;

	result[u8"y"] = prop::year;
	result[u8"m"] = prop::modified;
	result[u8"c"] = prop::created_utc;

	return result;
}

std::vector<prop::prop_scope> prop::search_scopes()
{
	const std::unordered_set<const key*> exclusions
	{
		comment,
		description,
		created_exif,
		created_digitized,
		created_utc,
		disk_num,
		dimensions,
		episode,
		exposure_time,
		focal_length_35mm_equivalent,
		pixel_format,
		iso_speed,
		latitude,
		longitude,
		media_category,
		megapixels,
		modified,
		null,
		orientation,
		rating,
		file_size,
		streams,
		track_num,
		unique_id,
		file_name,
		raw_file_name,
		system,
		game,
		crc32c,
		doc_id,
	};

	std::vector<prop_scope> result;

	for (const auto& i : all_props)
	{
		if (!exclusions.contains(i.second))
		{
			prop_scope s;
			s.scope = i.first;
			s.type = i.second;
			result.emplace_back(s);
		}
	}

	std::ranges::sort(result, [](const prop_scope& left, const prop_scope& right)
	{
		return str::icmp(left.scope, right.scope) < 0;
	});

	result.emplace(result.begin(), u8"any"s, null);

	return result;
}


std::vector<prop::prop_scope> prop::key_scopes()
{
	std::vector<prop_scope> result;

	for (const auto& i : all_props)
	{
		prop_scope s;
		s.scope = i.first;
		s.type = i.second;
		result.emplace_back(s);
	}

	std::ranges::sort(result, [](const prop_scope& left, const prop_scope& right)
	{
		return str::icmp(left.scope, right.scope) < 0;
	});

	return result;
}

static const property_map properties_by_name = build_properties_by_name();
static const df::hash_map<uint16_t, prop::key_ref> properties_by_id = build_properties_by_id();

bloom_bits prop::item_metadata::calc_bloom_bits() const
{
	bloom_bits result;

	if (!is_null(album)) result.types |= prop::album.bloom_bit;
	if (!is_null(album_artist)) result.types |= prop::album_artist.bloom_bit;
	if (!is_null(artist)) result.types |= prop::artist.bloom_bit;
	if (!is_null(audio_codec)) result.types |= prop::audio_codec.bloom_bit;
	if (!is_null(audio_sample_type)) result.types |= prop::audio_sample_type.bloom_bit;
	if (!is_null(audio_sample_rate)) result.types |= prop::audio_sample_rate.bloom_bit;
	if (!is_null(bitrate)) result.types |= prop::bitrate.bloom_bit;
	if (!is_null(camera_manufacturer)) result.types |= prop::camera_manufacturer.bloom_bit;
	if (!is_null(camera_model)) result.types |= prop::camera_model.bloom_bit;
	if (!is_null(location_place)) result.types |= prop::location_place.bloom_bit;
	if (!is_null(comment)) result.types |= prop::comment.bloom_bit;
	if (!is_null(copyright_creator)) result.types |= prop::copyright_creator.bloom_bit;
	if (!is_null(copyright_credit)) result.types |= prop::copyright_credit.bloom_bit;
	if (!is_null(copyright_notice)) result.types |= prop::copyright_notice.bloom_bit;
	if (!is_null(copyright_source)) result.types |= prop::copyright_source.bloom_bit;
	if (!is_null(copyright_url)) result.types |= prop::copyright_source.bloom_bit;
	if (!is_null(location_country)) result.types |= prop::location_country.bloom_bit;
	if (!is_null(description)) result.types |= prop::description.bloom_bit;
	if (!is_null(file_name)) result.types |= prop::file_name.bloom_bit;
	if (!is_null(genre)) result.types |= prop::genre.bloom_bit;
	if (!is_null(lens)) result.types |= prop::lens.bloom_bit;
	if (!is_null(pixel_format)) result.types |= prop::pixel_format.bloom_bit;
	if (!is_null(show)) result.types |= prop::show.bloom_bit;
	if (!is_null(game)) result.types |= prop::game.bloom_bit;
	if (!is_null(system)) result.types |= prop::system.bloom_bit;
	if (!is_null(location_state)) result.types |= prop::location_state.bloom_bit;
	if (!is_null(synopsis)) result.types |= prop::synopsis.bloom_bit;
	if (!is_null(composer)) result.types |= prop::composer.bloom_bit;
	if (!is_null(encoder)) result.types |= prop::encoder.bloom_bit;
	if (!is_null(publisher)) result.types |= prop::publisher.bloom_bit;
	if (!is_null(performer)) result.types |= prop::performer.bloom_bit;
	if (!is_null(title)) result.types |= prop::title.bloom_bit;
	if (!is_null(video_codec)) result.types |= prop::video_codec.bloom_bit;
	if (!is_null(width)) result.types |= prop::dimensions.bloom_bit;
	if (!is_null(height)) result.types |= prop::dimensions.bloom_bit;
	if (!is_null(iso_speed)) result.types |= prop::iso_speed.bloom_bit;
	if (!is_null(focal_length)) result.types |= prop::focal_length.bloom_bit;
	if (!is_null(focal_length_35mm_equivalent)) result.types |= prop::focal_length_35mm_equivalent.bloom_bit;
	if (!is_null(rating)) result.types |= prop::rating.bloom_bit;
	if (!is_null(season)) result.types |= prop::season.bloom_bit;
	if (!is_null(track)) result.types |= track_num.bloom_bit;
	if (!is_null(disk)) result.types |= disk_num.bloom_bit;
	if (!is_null(duration)) result.types |= prop::duration.bloom_bit;
	if (!is_null(episode)) result.types |= prop::episode.bloom_bit;
	if (!is_null(exposure_time)) result.types |= prop::exposure_time.bloom_bit;
	if (!is_null(f_number)) result.types |= prop::f_number.bloom_bit;
	if (!is_null(created_exif)) result.types |= prop::created_exif.bloom_bit;
	if (!is_null(created_digitized)) result.types |= prop::created_digitized.bloom_bit;
	if (!is_null(created_utc)) result.types |= prop::created_utc.bloom_bit;
	if (!is_null(year)) result.types |= prop::year.bloom_bit;
	if (orientation != ui::orientation::top_left) result.types |= prop::orientation.bloom_bit;
	if (coordinate.is_valid()) result.types |= latitude.bloom_bit;
	if (!is_null(tags)) result.types |= tag.bloom_bit;
	if (!is_null(label)) result.types |= prop::label.bloom_bit;
	if (!is_null(doc_id)) result.types |= prop::doc_id.bloom_bit;

	return result;
}

std::u8string prop::item_metadata::format(const std::u8string_view name) const
{
	if (icmp(prop::album.name, name) == 0) return album.str();
	if (icmp(prop::album_artist.name, name) == 0) return album_artist.str();
	if (icmp(prop::artist.name, name) == 0) return artist.str();
	if (icmp(prop::audio_codec.name, name) == 0) return audio_codec.str();
	if (icmp(prop::bitrate.name, name) == 0) return bitrate.str();
	if (icmp(prop::camera_manufacturer.name, name) == 0) return camera_manufacturer.str();
	if (icmp(prop::camera_model.name, name) == 0) return camera_model.str();
	if (icmp(prop::comment.name, name) == 0) return comment.str();
	if (icmp(prop::composer.name, name) == 0) return composer.str();
	if (icmp(prop::copyright_creator.name, name) == 0) return copyright_creator.str();
	if (icmp(prop::copyright_credit.name, name) == 0) return copyright_credit.str();
	if (icmp(prop::copyright_notice.name, name) == 0) return copyright_notice.str();
	if (icmp(prop::copyright_source.name, name) == 0) return copyright_source.str();
	if (icmp(prop::copyright_url.name, name) == 0) return copyright_url.str();
	if (icmp(prop::description.name, name) == 0) return description.str();
	if (icmp(prop::encoder.name, name) == 0) return encoder.str();
	if (icmp(prop::file_name.name, name) == 0) return file_name.str();
	if (icmp(prop::genre.name, name) == 0) return genre.str();
	if (icmp(prop::lens.name, name) == 0) return lens.str();
	if (icmp(prop::location_place.name, name) == 0) return location_place.str();
	if (icmp(prop::location_country.name, name) == 0) return location_country.str();
	if (icmp(prop::location_state.name, name) == 0) return location_state.str();
	if (icmp(prop::performer.name, name) == 0) return performer.str();
	if (icmp(prop::pixel_format.name, name) == 0) return pixel_format.str();
	if (icmp(prop::publisher.name, name) == 0) return publisher.str();
	if (icmp(prop::show.name, name) == 0) return show.str();
	if (icmp(prop::synopsis.name, name) == 0) return synopsis.str();
	if (icmp(prop::title.name, name) == 0) return title.str();
	if (icmp(prop::label.name, name) == 0) return label.str();
	if (icmp(prop::video_codec.name, name) == 0) return video_codec.str();
	if (icmp(prop::raw_file_name.name, name) == 0) return raw_file_name.str();


	return {};
}

prop::key_ref prop::from_id(const uint16_t id)
{
	const auto found = properties_by_id.find(id);
	return found != properties_by_id.cend() ? found->second : null;
}

prop::key_ref prop::from_prefix(const std::u8string_view scope)
{
	const auto found = properties_by_name.find(scope);

	if (found != properties_by_name.cend())
	{
		return found->second;
	}

	if (str::icmp(scope, u8"created"sv) == 0 || str::icmp(scope, tt.query_created) == 0)
	{
		return created_utc;
	}

	if (str::icmp(scope, u8"modified"sv) == 0 || str::icmp(scope, tt.query_modified) == 0)
	{
		return modified;
	}

	return null;
}

std::u8string prop::format_exposure(const double d)
{
	if (d != 0.0)
	{
		if (d < 1.0)
		{
			return str::format(u8"1/{}s"sv, df::round(1.0 / d));
		}
		return str::format(u8"{}s"sv, df::round(d));
	}

	return {};
}

double prop::closest_fstop(const double fs)
{
	static double stops[] = {
		0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 2, 2.2, 2.4, 2.5, 2.6, 2.8, 3.2, 3.3, 3.4, 3.5, 3.7,
		32, 4, 4.4, 4.5, 4.8, 5.0, 5.2, 5.6, 6.2, 6.3, 6.7, 7.1, 7.3, 8, 8.7, 9, 9.5, 10, 11, 12, 13, 14, 15, 16, 17,
		18, 19, 20, 21, 22, 27
	};

	const auto count = std::size(stops);
	auto* const end = stops + count;

	auto* const upper = std::lower_bound(stops, end, fs);

	if (upper == end) return stops[count - 1];
	if (upper == stops) return *stops;

	const auto* const lower = upper - 1;

	return fabs(*lower - fs) < fabs(*upper - fs) ? *lower : *upper;
}

std::u8string prop::format_fstop(const double d)
{
	if (d != 0.0)
	{
		const auto dd = closest_fstop(d);

		if (_finite(dd) && dd > 0)
		{
			return str::format(u8"f/{:.1}f"sv, dd);
		}
	}
	else
	{
		return u8"f/0"s;
	}

	return {};
}

std::u8string prop::format_focal_length(const double d, int filmEquivalent)
{
	if (d != 0.0)
	{
		if (filmEquivalent != 0)
		{
			return str::format(u8"{:.1}fmm ({}mm film eq)"sv, d, filmEquivalent);
		}
		return str::format(d < 1 ? u8"{:.1}fmm"sv : u8"{:.0}mm"sv, d);
	}

	return u8"0mm"s;
}

std::u8string prop::format_rating(const int i)
{
	if (i == -1) return std::u8string(tt.command_rate_rejected);
	if (i >= 1 && i <= 5) return str::to_string(i);
	return std::u8string(tt.none);
}

std::u8string prop::format_white_balance(const int i)
{
	return str::format(u8"{}"sv, i);
}

std::u8string prop::format_iso(const int i)
{
	return str::format(u8"ISO{}"sv, i);
}

std::u8string prop::format_gps(const double d)
{
	if (fabs(d - gps_coordinate::invalid_coordinate) > 0.001)
	{
		return str::format(u8"{:.5}"sv, d);
	}

	return {};
}

std::u8string prop::format_gps(const double lat, const double lon)
{
	if (lat != static_cast<double>(gps_coordinate::invalid_coordinate) &&
		lon != static_cast<double>(gps_coordinate::invalid_coordinate))
	{
		return str::format(u8"{.5},{.5}"sv, lat, lon);
	}

	return {};
}

std::u8string prop::format_four_cc(const uint32_t v)
{
	char8_t sz[6];
	auto* p = sz;
	if (v >> 24) *p++ = v >> 24;
	if (v >> 16 & 0xff) *p++ = v >> 16 & 0xff;
	if (v >> 8 & 0xff) *p++ = v >> 8 & 0xff;
	if (v & 0xff) *p++ = v & 0xff;
	*p = 0;
	return sz;
}

std::u8string prop::format_streams(const int v)
{
	return str::format(u8"{}"sv, v);
}

const static uint64_t KB = 1024;
const static uint64_t MB = KB * 1024;
const static uint64_t GB = MB * 1024;
const static uint64_t TB = GB * 1024;


std::u8string prop::format_bit_rate(const int64_t br)
{
	auto units = u8"kbit/s"sv;
	auto div = KB;
	auto i = br;

	if (i >= GB)
	{
		div = GB;
		units = u8"Gbit/s"sv;
	}
	else if (i >= MB)
	{
		div = MB;
		units = u8"Mbit/s"sv;
	}

	if (i < KB)
	{
		i = KB;
	}

	const auto n = static_cast<int>(i / div);
	const auto r = static_cast<int>(((i * 10) / div) % 10);

	return str::format(u8"{}{}{} {}"sv, n, platform::number_dec_sep(), r, units);
}


prop::size_rounded prop::round_size(const uint64_t s)
{
	size_rounded result;

	result.unit = u8"KB"sv;
	result.short_unit = u8"K"sv;
	result.div = KB;

	if (s > (TB / 5))
	{
		result.div = TB;
		result.unit = u8"TB"sv;
		result.short_unit = u8"T"sv;
	}
	else if (s > (GB / 5))
	{
		result.div = GB;
		result.unit = u8"GB"sv;
		result.short_unit = u8"G"sv;
	}
	else if (s > (MB / 5))
	{
		result.div = MB;
		result.unit = u8"MB"sv;
		result.short_unit = u8"M"sv;
	}

	if (s < KB)
	{
		result.div = KB;
	}

	const auto nn = df::round((s * 10.0) / result.div);
	result.n = nn / 10;
	result.dec = (nn % 10);
	result.rounded = df::round(static_cast<double>(s) / result.div);

	return result;
}

std::u8string prop::format_size(const df::file_size& s)
{
	const auto rounded = round_size(s.to_int64());
	return rounded.dec == 0
		       ? str::format(u8"{} {}"sv, rounded.n, rounded.unit)
		       : str::format(u8"{}{}{} {}"sv, rounded.n, platform::number_dec_sep(), rounded.dec, rounded.unit);
}

struct magnitude
{
	uint64_t n;
	uint64_t d;
	char c;
	std::u8string label;

	const bool operator<(const uint64_t other) const
	{
		return n < other;
	}
};

std::vector<magnitude> magnitudes{
	{0llu, 0llu, 0, u8""},
	{1llu, 1llu, 'B', u8"<1K"},
	{10llu, 1llu, 'B', u8"<1K"},
	{100llu, 1llu, 'B', u8"<1K"},
	{1000llu, 1000llu, 'K', u8"1K"},
	{10000llu, 1000llu, 'K', u8"10K"},
	{100000llu, 1000llu, 'K', u8"100K"},
	{1000000llu, 1000000llu, 'M', u8"1M"},
	{10000000llu, 1000000llu, 'M', u8"10M"},
	{100000000llu, 1000000llu, 'M', u8"100M"},
	{1000000000llu, 1000000000llu, 'G', u8"1G"},
	{10000000000llu, 1000000000llu, 'G', u8"10G"},
	{100000000000llu, 1000000000llu, 'G', u8"100G"},
	{1000000000000llu, 1000000000000llu, 'T', u8"1T"},
	{10000000000000llu, 1000000000000llu, 'T', u8"10T"},
	{100000000000000llu, 1000000000000llu, 'T', u8"100T"},
	{1000000000000000llu, 1000000000000000llu, 'P', u8"1P"},
	{10000000000000000llu, 1000000000000000llu, 'P', u8"10P"},
	{100000000000000000llu, 1000000000000000llu, 'P', u8"100P"},
	{1000000000000000000llu, 1000000000000000000llu, 'E', u8"1E"},
	{10000000000000000000llu, 1000000000000000000llu, 'E', u8"10E"},
};

static uint64_t u64_diff(uint64_t a, uint64_t b)
{
	return (a > b) ? a - b : b - a;
}

magnitude find_magnitude(const uint64_t n)
{
	const auto ihi = std::lower_bound(magnitudes.begin(), magnitudes.end(), n);

	if (ihi == magnitudes.begin())
	{
		return magnitudes.front();
	}
	if (ihi != magnitudes.end())
	{
		const auto dif_lo = u64_diff((ihi - 1)->n, n);
		const auto dif_hi = u64_diff(ihi->n, n);

		if (dif_lo < dif_hi)
		{
			return *(ihi - 1);
		}
		return *ihi;
	}
	return magnitudes.back();
}


int64_t prop::size_bucket(const int64_t n)
{
	/*static std::array<int64_t, 12> buckets = {
		KB, KB * 10, KB * 100,
		MB, MB * 10, MB * 100,
		GB, GB * 10, GB * 100,
		TB, TB * 10, TB * 100,
	};*/

	return find_magnitude(n).n;
}

std::u8string prop::format_magnitude(const df::file_size& s)
{
	return find_magnitude(s.to_int64()).label;
}


std::u8string prop::format_audio_sample_rate(const int v)
{
	const auto remainder = (v % 1000) / 100;
	const auto khz = v / 1000;

	if (v > 1000 && (remainder == 0)) return str::format(u8"{}kHz"sv, khz);
	if (v > 1000) return str::format(u8"{}{}{}kHz"sv, khz, platform::number_dec_sep(), remainder);
	if (v > 0) return str::format(u8"{}Hz"sv, v);
	return {};
}

std::u8string prop::format_audio_sample_rate(const uint16_t v)
{
	const auto remainder = (v % 1000) / 100;
	const auto khz = v / 1000;

	if (v > 1000 && (remainder == 0)) return str::format(u8"{}kHz"sv, khz);
	if (v > 1000) return str::format(u8"{}{}{}kHz"sv, khz, platform::number_dec_sep(), remainder);
	if (v > 0) return str::format(u8"{}Hz"sv, v);
	return {};
}

std::u8string prop::format_audio_sample_type(const audio_sample_t v)
{
	switch (v)
	{
	case audio_sample_t::none: return u8"none"s;
	case audio_sample_t::unsigned_8bit: return u8"8bit"s;
	case audio_sample_t::signed_16bit: return u8"16bit"s;
	case audio_sample_t::signed_32bit: return u8"32bit"s;
	case audio_sample_t::signed_64bit: return u8"64bit"s;
	case audio_sample_t::signed_float: return u8"float"s;
	case audio_sample_t::signed_double: return u8"double"s;
	case audio_sample_t::unsigned_planar_8bit: return u8"8bit"s;
	case audio_sample_t::signed_planar_16bit: return u8"16bit"s;
	case audio_sample_t::signed_planar_32bit: return u8"32bit"s;
	case audio_sample_t::signed_planar_64bit: return u8"64bit"s;
	case audio_sample_t::planar_float: return u8"float"s;
	case audio_sample_t::planar_double: return u8"double"s;
	default:
		break;
	}
	return {};
}

std::u8string prop::format_audio_channels(const int v)
{
	switch (v)
	{
	case 1:
		return u8"mono"s;
	case 2:
		return u8"stereo"s;
	case 3:
		return u8"3.0 surround"s;
	case 4:
		return u8"quad"s;
	case 5:
		return u8"5.0 surround"s;
	case 6:
		return u8"5.1 surround"s;
	case 8:
		return u8"7.1 surround"s;
	default:
		return str::format(u8"{} channels"sv, v);
	}

	return {};
}

std::u8string prop::format_f_num(const double d)
{
	return str::format(u8"f/{:.01}"sv, d);
}

std::u8string prop::format_dimensions(const sizei v)
{
	return str::format(u8"{}x{}"sv, v.cx, v.cy);
}

std::u8string prop::format_video_resolution(const sizei vv)
{
	const auto v = (vv.cy > vv.cx) ? sizei{vv.cy, vv.cx} : vv;

	if (v.cx == 7680 && v.cy == 4320)
	{
		return u8"8K"s;
	}
	if (v.cx == 4096)
	{
		return u8"4K"s;
	}
	if (v.cx == 3840 && v.cy == 2160)
	{
		return u8"2160p-UHD"s;
	}
	if (v.cx == 2048)
	{
		return u8"2K"s;
	}
	if (v.cx == 1920 && v.cy == 1200)
	{
		return u8"WUXGA"s;
	}
	if (v.cx == 2560 && v.cy == 1440)
	{
		return u8"1440p"s;
	}
	if (v.cx == 1920 && v.cy == 1080)
	{
		return u8"1080p"s;
	}
	if (v.cx == 1280 && v.cy == 720)
	{
		return u8"720p"s;
	}
	if (v.cx == 1280 && v.cy == 720)
	{
		return u8"720p"s;
	}
	if (v.cx == 854 && v.cy == 480)
	{
		return u8"480p"s;
	}
	if (v.cx == 640 && v.cy == 360)
	{
		return u8"360p"s;
	}
	if (v.cx == 426 && v.cy == 240)
	{
		return u8"240p"s;
	}
	if (v.cx == 640 && v.cy == 360)
	{
		return u8"360p"s;
	}
	if (v.cx == 480 && v.cy == 360)
	{
		return u8"360p"s;
	}
	if (v.cx == 320 && v.cy == 240)
	{
		return u8"240p"s;
	}
	if (v.cx == 320 && v.cy == 240)
	{
		return u8"240p"s;
	}
	if (v.cx == 320 && v.cy == 180)
	{
		return u8"180p"s;
	}
	if (v.cx == 256 && v.cy == 144)
	{
		return u8"144p"s;
	}
	if (v.cx == 256 && v.cy == 144)
	{
		return u8"144p"s;
	}
	if (v.cx == 176 && v.cy == 144)
	{
		return u8"144p"s;
	}
	if (v.cx == 160 && v.cy == 120)
	{
		return u8"120p"s;
	}

	return {};
}

std::u8string_view text_or_default(const std::u8string_view text, const std::u8string_view def)
{
	return str::is_empty(text) ? def : text;
}

std::u8string prop::replace_tokens(const std::u8string_view text, const item_metadata_const_ptr& md)
{
	df::date_t created;

	if (md)
	{
		created = md->created();
	}

	auto substitute = [md, created](u8ostringstream& result, const std::u8string_view token_in)
	{
		if (md)
		{
			const auto token = str::to_lower(token_in);

			if (token == u8"created"sv || token == u8"created.date"sv)
			{
				if (created.is_valid())
				{
					result << str::format(u8"{:04}-{:02}-{:02}"sv, created.year(), created.month(), created.day());
				}
				else
				{
					result << tt.unknown;
				}
			}
			else if (token == u8"year"sv || token == u8"created.year"sv)
			{
				if (md->year)
				{
					result << md->year;
				}
				else if (created.is_valid())
				{
					result << created.year();
				}
				else
				{
					result << tt.unknown;
				}
			}
			else if (token == u8"artist"sv)
				result << text_or_default(
					md->album_artist.is_empty() ? md->artist.sv() : md->album_artist.sv(), tt.unknown);
			else if (token == u8"album"sv) result << text_or_default(md->album, tt.unknown);
			else if (token == u8"show"sv) result << text_or_default(md->show, tt.unknown);
			else if (token == u8"season"sv) result << text_or_default(str::to_string(md->season), tt.unknown);
			else if (token == u8"country"sv) result << text_or_default(md->location_country, tt.unknown);
			else
			{
				result << text_or_default(md->format(token), tt.unknown);
			}
		}
		else
		{
			result << tt.unknown;
		}
	};

	return str::replace_tokens(text, substitute);
}

std::u8string prop::format_pixels(const sizei v, const file_type_ref ft)
{
	if (v.is_empty())
	{
		return std::u8string(tt.resolution_none);
	}

	if (ft->has_trait(file_type_traits::av))
	{
		const auto video_res = format_video_resolution(v);
		if (!video_res.empty()) return video_res;
	}

	if (ft->has_trait(file_type_traits::bitmap))
	{
		if (v.cx <= 128 && v.cy <= 128)
		{
			return std::u8string(tt.pixels_icon);
		}

		const auto mp = ui::calc_mega_pixels(v.cx, v.cy);

		if (mp < 0.5)
		{
			return std::u8string(tt.pixels_small);
		}

		if (mp >= 2.0)
		{
			return str::print(u8"%dmp"sv, df::round(mp));
		}

		return str::print(u8"%1.1fmp"sv, mp);
	}

	return str::format(u8"{}x{}"sv, v.cx, v.cy);
}

std::u8string prop::format_duration(const int n)
{
	return str::format_seconds(n);
}

std::u8string prop::format_date(const df::date_t d)
{
	return d.is_valid() ? platform::format_date(d) : std::u8string{};
}


std::u8string prop::format_polish_date(const df::date_t ft)
{
	if (!ft.is_valid())
	{
		return {};
	}

	const auto st = ft.date();
	return str::format(u8"{}-{}-{}"sv, st.year, st.month, st.day);
}

std::u8string df::xy32::str() const
{
	if (y == 0)
	{
		return str::to_string(x);
	}

	return str::format(u8"{}/{}"sv, x, y);
}

std::u8string df::xy8::str() const
{
	if (y == 0)
	{
		return str::to_string(x);
	}

	return str::format(u8"{}/{}"sv, x, y);
}

// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "model_locations.h"
#include "model_location.h"
#include "model_propery.h"

country_t country_t::null;

static_assert(std::is_trivially_copyable_v<location_t>);
static_assert(std::is_move_constructible_v<country_t>);
static_assert(std::is_move_constructible_v<location_match>);

static constexpr auto countries_file_name = u8"location-countries.txt"sv;
static constexpr auto states_file_name = u8"location-states.txt"sv;
static constexpr auto places_file_name = u8"location-places.txt"sv;

const auto max_location_cols = 32;

// Lookup on google
// https://maps.googleapis.com/maps/api/geocode/json?search=wc1x8jy&sensor=false

struct csv_entry
{
	std::u8string_view _s;

	bool is_empty() const
	{
		return _s.empty();
	}

	str::cached cached_string() const
	{
		if (is_empty()) return {};
		return str::trim_and_cache(_s);
	}

	uint32_t hash() const
	{
		return crypto::fnv1a_i(_s);
	}

	std::u8string_view to_range() const
	{
		return _s;
	}

	int to_int() const
	{
		return str::to_int(_s);
	}

	uint32_t to_code2() const
	{
		return ::to_code2(_s);
	}

	double to_double() const
	{
		return str::to_double(_s);
	}

	float to_float() const
	{
		return static_cast<float>(to_double());
	}
};

void location_t::clear()
{
	id = 0;
	place = {};
	state = {};
	country = {};
	position.clear();
}

std::u8string location_t::str() const
{
	std::u8string result;
	join(result, place, u8", "sv, false);
	join(result, state, u8", "sv, false);
	join(result, country, u8", "sv, false);
	return result;
}

std::u8string gps_coordinate::str() const
{
	return prop::format_gps(_latitude, _longitude);
}

void gps_coordinate::decimal_to_dms(const double coord, uint32_t& deg, uint32_t& min, uint32_t& sec)
{
	const int total_sec = abs(df::round(coord * 3600));
	deg = total_sec / 3600;
	min = (total_sec / 60) % 60;
	sec = total_sec % 60;
}

double gps_coordinate::dms_to_decimal(const int deg, const int min, const int sec)
{
	return deg + (min / 60.0) + (sec / 3600.0);
}

double gps_coordinate::dms_to_decimal(const double deg, const double min, const double sec)
{
	return deg + (min / 60.0) + (sec / 3600.0);
}

split_location_result split_location(const std::u8string_view text)
{
	const auto delims = u8"+-"sv;
	split_location_result result;

	const auto c1 = text.find_first_of(delims);

	if (c1 != std::u8string_view::npos)
	{
		const auto c2 = text.find_first_of(delims, c1 + 1);

		if (c2 != std::u8string_view::npos)
		{
			const auto c3 = text.find_first_of(delims, c2 + 1);

			const auto lat = text.substr(c1, c2 - c1);
			const auto lng = text.substr(c2, c3 != std::u8string_view::npos ? c3 - c2 : std::u8string_view::npos);
			const auto alt = c3 != std::u8string_view::npos ? text.substr(c3) : std::u8string_view{};

			result.x = str::to_double(lat);
			result.y = str::to_double(lng);
			result.z = str::to_double(alt);
			result.success = true;
		}
	}

	return result;
}


location_cache::location_cache() : _locations_path(df::probe_data_file(places_file_name))
{
	static_assert(std::is_trivial_v<kd_coordinates_t>);
	static_assert(std::is_trivial_v<location_id_and_offset>);
	static_assert(std::is_trivial_v<ngram_t>);
	static_assert(std::is_trivial_v<location_ngram_and_offset>);

	static_assert(sizeof(ngram_t) == 4);
	static_assert(sizeof(location_ngram_and_offset) == 8);
	static_assert(sizeof(location_id_and_offset) == 8);
	static_assert(sizeof(kd_coordinates_t) == 16);
}

static void skip_bom(u8istream& file)
{
	const auto bom = file.get() << 16 |
		file.get() << 8 |
		file.get();

	if (bom != 0X00EFBBBF)
	{
		file.seekg(0);
	}
}

static platform::mutex normalize_mutex;
using county_normalize_map = std::unordered_map<str::cached, str::cached, df::ihash, df::ieq>;
_Guarded_by_(normalize_mutex) static county_normalize_map county_abbreviations;
_Guarded_by_(normalize_mutex) static county_normalize_map county_names;


str::cached normalize_county_abbreviation(const str::cached country)
{
	platform::exclusive_lock abbreviation_lock(normalize_mutex);
	const auto found = county_abbreviations.find(country);
	return found != county_abbreviations.end() ? found->second : country;
}

str::cached normalize_county_name(const str::cached country)
{
	platform::exclusive_lock abbreviation_lock(normalize_mutex);
	const auto found = county_names.find(country);
	return found != county_names.end() ? found->second : country;
}

void location_cache::load_countries()
{
	static std::vector<std::pair<uint32_t, gps_coordinate>> centroids{
		{'AW', {12.52088038, -69.98267711}},
		{'AF', {33.83523073, 66.00473366}},
		{'AO', {-12.29336054, 17.53736768}},
		{'AI', {18.2239595, -63.06498927}},
		{'AL', {41.14244989, 20.04983396}},
		{'AX', {60.21488688, 19.95328768}},
		{'AD', {42.54229102, 1.56054378}},
		{'AE', {23.90528188, 54.3001671}},
		{'AR', {-35.3813488, -65.17980692}},
		{'AM', {40.28952569, 44.92993276}},
		{'AS', {-14.30445997, -170.7180258}},
		{'AQ', {-80.50857913, 19.92108951}},
		{'TF', {-49.24895485, 69.22666758}},
		{'AG', {17.2774996, -61.79469343}},
		{'AU', {-25.73288704, 134.4910001}},
		{'AT', {47.58549439, 14.1264761}},
		{'AZ', {40.28827235, 47.54599879}},
		{'BI', {-3.35939666, 29.87512156}},
		{'BE', {50.63981576, 4.64065114}},
		{'BJ', {9.6417597, 2.32785254}},
		{'BF', {12.26953846, -1.75456601}},
		{'BD', {23.86731158, 90.23812743}},
		{'BG', {42.76890318, 25.21552909}},
		{'BH', {26.04205135, 50.54196932}},
		{'CA', {61.36206324, -98.30777028}},
		{'BS', {24.29036702, -76.62843038}},
		{'BA', {44.17450125, 17.76876733}},
		{'BL', {17.89880451, -62.84067779}},
		{'BY', {53.53131377, 28.03209307}},
		{'BZ', {17.20027509, -88.71010486}},
		{'BM', {32.31367802, -64.7545589}},
		{'BO', {-16.70814787, -64.68538645}},
		{'BR', {-10.78777702, -53.09783113}},
		{'BB', {13.18145428, -59.559797}},
		{'BN', {4.51968958, 114.7220304}},
		{'BT', {27.41106589, 90.40188155}},
		{'BW', {-22.18403213, 23.79853368}},
		{'CF', {6.56823297, 20.46826831}},
		{'CH', {46.79785878, 8.20867471}},
		{'CL', {-37.73070989, -71.38256213}},
		{'CN', {36.56176546, 103.8190735}},
		{'CI', {7.6284262, -5.5692157}},
		{'CM', {5.69109849, 12.73964156}},
		{'CD', {-2.87746289, 23.64396107}},
		{'CG', {-0.83787463, 15.21965762}},
		{'CK', {-21.21927288, -159.7872422}},
		{'CO', {3.91383431, -73.08114582}},
		{'KM', {-11.87783444, 43.68253968}},
		{'CV', {15.95523324, -23.9598882}},
		{'CR', {9.97634464, -84.19208768}},
		{'CU', {21.62289528, -79.01605384}},
		{'CW', {12.19551675, -68.97119369}},
		{'KY', {19.42896497, -80.91213321}},
		{'CY', {34.91667211, 33.0060022}},
		{'CZ', {49.73341233, 15.31240163}},
		{'DE', {51.10698181, 10.38578051}},
		{'DJ', {11.74871806, 42.5606754}},
		{'DM', {15.4394702, -61.357726}},
		{'DK', {55.98125296, 10.02800992}},
		{'DO', {18.89433082, -70.50568896}},
		{'DZ', {28.15893849, 2.61732301}},
		{'EC', {-1.42381612, -78.75201922}},
		{'EG', {26.49593311, 29.86190099}},
		{'ER', {15.36186618, 38.84617011}},
		{'ES', {40.24448698, -3.64755047}},
		{'EE', {58.67192972, 25.54248537}},
		{'ET', {8.62278679, 39.60080098}},
		{'FI', {64.49884603, 26.2746656}},
		{'FJ', {-17.42858032, 165.4519543}},
		{'FK', {-51.74483954, -59.35238956}},
		{'FR', {42.17344011, -2.76172945}},
		{'FO', {62.05385403, -6.88095423}},
		{'FM', {7.45246814, 153.2394379}},
		{'GA', {-0.58660025, 11.7886287}},
		{'GB', {54.12387156, -2.86563164}},
		{'GE', {42.16855755, 43.50780252}},
		{'GG', {49.46809761, -2.57239064}},
		{'GH', {7.95345644, -1.21676566}},
		{'GN', {10.43621593, -10.94066612}},
		{'GM', {13.44965244, -15.39601295}},
		{'GW', {12.04744948, -14.94972445}},
		{'GQ', {1.70555135, 10.34137924}},
		{'GR', {39.07469623, 22.95555794}},
		{'GD', {12.11725044, -61.68220189}},
		{'GL', {74.71051289, -41.34191127}},
		{'GT', {15.69403664, -90.36482009}},
		{'GU', {13.44165626, 144.7679102}},
		{'GY', {4.79378034, -58.98202459}},
		{'HK', {22.39827737, 114.1138045}},
		{'HM', {-53.08724656, 73.5205171}},
		{'HN', {14.82688165, -86.6151661}},
		{'HR', {45.08047631, 16.40412899}},
		{'HT', {18.93502563, -72.68527509}},
		{'HU', {47.16277506, 19.39559116}},
		{'ID', {-2.21505456, 117.2401137}},
		{'IM', {54.22418911, -4.53873952}},
		{'IN', {22.88578212, 79.6119761}},
		{'IO', {-7.33059751, 72.44541229}},
		{'IE', {53.1754487, -8.13793569}},
		{'IR', {32.57503292, 54.27407004}},
		{'IQ', {33.03970582, 43.74353149}},
		{'IS', {64.99575386, -18.57396167}},
		{'IL', {31.46110101, 35.00444693}},
		{'IT', {42.79662641, 12.07001339}},
		{'JM', {18.15694878, -77.31482593}},
		{'JE', {49.21837377, -2.12689938}},
		{'JO', {31.24579091, 36.77136104}},
		{'JP', {37.59230135, 138.0308956}},
		{'KZ', {48.15688067, 67.29149357}},
		{'KE', {0.59988022, 37.79593973}},
		{'KG', {41.46221943, 74.54165513}},
		{'KH', {12.72004786, 104.9069433}},
		{'KI', {0.86001503, -45.61110513}},
		{'KN', {17.2645995, -62.68755265}},
		{'KR', {36.38523983, 127.8391609}},
		{'KW', {29.33431262, 47.58700459}},
		{'LA', {18.50217433, 103.7377241}},
		{'LB', {33.92306631, 35.88016072}},
		{'LR', {6.45278492, -9.32207573}},
		{'LY', {27.03094495, 18.00866169}},
		{'LC', {13.89479481, -60.96969923}},
		{'LI', {47.13665835, 9.53574312}},
		{'LK', {7.61266509, 80.70108238}},
		{'LS', {-29.58003188, 28.22723131}},
		{'LT', {55.32610984, 23.88719355}},
		{'LU', {49.76725361, 6.07182201}},
		{'LV', {56.85085163, 24.91235983}},
		{'MO', {22.22311688, 113.5093212}},
		{'MF', {18.08888611, -63.05972851}},
		{'MA', {29.83762955, -8.45615795}},
		{'MC', {43.75274627, 7.40627677}},
		{'MD', {47.19498804, 28.45673372}},
		{'MG', {-19.37189587, 46.70473674}},
		{'MV', {3.7287092, 73.45713004}},
		{'MX', {23.94753724, -102.5234517}},
		{'MH', {7.00376358, 170.3397612}},
		{'MK', {41.59530893, 21.68211346}},
		{'ML', {17.34581581, -3.54269065}},
		{'MT', {35.92149632, 14.40523316}},
		{'MM', {21.18566599, 96.48843321}},
		{'ME', {42.78890259, 19.23883939}},
		{'MN', {46.82681544, 103.0529977}},
		{'MP', {15.82927563, 145.6196965}},
		{'MZ', {-17.27381643, 35.53367543}},
		{'MR', {20.25736706, -10.34779815}},
		{'MS', {16.73941406, -62.18518546}},
		{'MU', {-20.27768704, 57.57120551}},
		{'MW', {-13.21808088, 34.28935599}},
		{'MY', {3.78986846, 109.6976228}},
		{'NA', {-22.13032568, 17.20963567}},
		{'NC', {-21.29991806, 165.6849237}},
		{'NE', {17.41912493, 9.38545882}},
		{'NF', {-29.0514609, 167.9492168}},
		{'NG', {9.59411452, 8.08943895}},
		{'NI', {12.84709429, -85.0305297}},
		{'NU', {-19.04945708, -169.8699468}},
		{'NL', {52.1007899, 5.28144793}},
		{'NO', {68.75015572, 15.34834656}},
		{'NP', {28.24891365, 83.9158264}},
		{'NR', {-0.51912639, 166.9325682}},
		{'NZ', {-41.81113557, 171.4849235}},
		{'OM', {20.60515333, 56.09166155}},
		{'PK', {29.9497515, 69.33957937}},
		{'PA', {8.51750797, -80.11915156}},
		{'PN', {-24.36500535, -128.317042}},
		{'PE', {-9.15280381, -74.38242685}},
		{'PH', {11.77536778, 122.8839325}},
		{'PW', {7.28742784, 134.4080797}},
		{'PG', {-6.46416646, 145.2074475}},
		{'PL', {52.12759564, 19.39012835}},
		{'PR', {18.22813055, -66.47307604}},
		{'KP', {40.15350311, 127.1924797}},
		{'PT', {39.59550671, -8.50104361}},
		{'PY', {-23.22823913, -58.40013703}},
		{'PS', {31.91613893, 35.19628705}},
		{'PF', {-14.72227409, -144.9049439}},
		{'QA', {25.30601188, 51.18479632}},
		{'RO', {45.85243127, 24.97293039}},
		{'RU', {61.98052209, 96.68656112}},
		{'RW', {-1.99033832, 29.91988515}},
		{'EH', {24.22956739, -12.21982755}},
		{'SA', {24.12245841, 44.53686271}},
		{'SD', {15.99035669, 29.94046812}},
		{'SS', {7.30877945, 30.24790002}},
		{'SN', {14.36624173, -14.4734924}},
		{'SG', {1.35876087, 103.8172559}},
		{'GS', {-54.46488248, -36.43318388}},
		{'SH', {-12.40355951, -9.54779416}},
		{'SB', {-8.92178022, 159.6328767}},
		{'SL', {8.56329593, -11.79271247}},
		{'SV', {13.73943744, -88.87164469}},
		{'SM', {43.94186747, 12.45922334}},
		{'SO', {4.75062876, 45.70714487}},
		{'PM', {46.91918789, -56.30319779}},
		{'RS', {44.2215032, 20.78958334}},
		{'ST', {0.44391445, 6.72429658}},
		{'SR', {4.13055413, -55.9123457}},
		{'SK', {48.70547528, 19.47905218}},
		{'SI', {46.11554772, 14.80444238}},
		{'SE', {62.77966519, 16.74558049}},
		{'SZ', {-26.55843045, 31.4819369}},
		{'SX', {18.05081728, -63.05713363}},
		{'SC', {-4.66099094, 55.47603279}},
		{'SY', {35.02547389, 38.50788204}},
		{'TC', {21.83047572, -71.97387881}},
		{'TD', {15.33333758, 18.64492513}},
		{'TG', {8.52531356, 0.96232845}},
		{'TH', {15.11815794, 101.0028813}},
		{'TJ', {38.5304539, 71.01362631}},
		{'TM', {39.11554137, 59.37100021}},
		{'TL', {-8.82889162, 125.8443898}},
		{'TO', {-20.42843174, -174.8098734}},
		{'TT', {10.45733408, -61.26567923}},
		{'TN', {34.11956246, 9.55288359}},
		{'TR', {39.0616029, 35.16895346}},
		{'TW', {23.7539928, 120.9542728}},
		{'TZ', {-6.27565408, 34.81309981}},
		{'UG', {1.27469299, 32.36907971}},
		{'UA', {48.99656673, 31.38326469}},
		{'UY', {-32.79951534, -56.01807053}},
		//{ 'US',	{ 	45.6795472, -112.4616737 } },
		{'US', {39.8333333, -98.585522}},
		{'UZ', {41.75554225, 63.14001528}},
		{'VA', {41.90174985, 12.43387177}},
		{'VC', {13.22472269, -61.20129695}},
		{'VE', {7.12422421, -66.18184123}},
		{'VG', {18.52585755, -64.47146992}},
		{'VI', {17.95500624, -64.80301538}},
		{'VN', {16.6460167, 106.299147}},
		{'VU', {-16.22640909, 167.6864464}},
		{'WF', {-13.88737039, -177.3483483}},
		{'WS', {-13.75324346, -172.1648506}},
		{'YE', {15.90928005, 47.58676189}},
		{'ZA', {-29.00034095, 25.08390093}},
		{'ZM', {-13.45824152, 27.77475946}},
		{'ZW', {-19.00420419, 29.8514412}}
	};

	county_normalize_map abbreviations;
	county_normalize_map names;
	csv_entry entries[max_location_cols];
	u8istream file(platform::to_file_system_path(df::probe_data_file(countries_file_name)), u8istream::binary);

	if (file.is_open())
	{
		skip_bom(file);

		std::u8string line;
		while (std::getline(file, line))
		{
			const auto entry_count = scan_entries(line, entries);

			if (entry_count > 1)
			{
				const auto code = entries[0].to_code2();
				const auto code_text = entries[0].cached_string();
				const auto name = entries[1].cached_string();

				std::vector<str::cached> alt_names;

				for (int i = 0; i < max_country_alt_names; i++)
				{
					auto&& csv_entry = entries[i + 2];

					if (!csv_entry.is_empty())
					{
						const auto alt_name = csv_entry.cached_string();

						alt_names.emplace_back(alt_name);
						abbreviations[alt_name] = code_text;
						names[alt_name] = name;
					}
				}

				abbreviations[name] = code_text;
				names[name] = name;
				names[code_text] = name;

				_countries[code] = country_t(code_text, name, alt_names);
			}
		}

		for (const auto& c : centroids)
		{
			const auto found = _countries.find(c.first);

			if (found != _countries.end())
			{
				found->second._centroid = c.second;
			}
		}
	}

	platform::exclusive_lock abbreviation_lock(normalize_mutex);
	std::swap(county_abbreviations, abbreviations);
	std::swap(county_names, names);
}


void location_cache::load_states()
{
	csv_entry entries[max_location_cols];
	u8istream file(platform::to_file_system_path(df::probe_data_file(states_file_name)), u8istream::binary);

	if (file.is_open())
	{
		skip_bom(file);
		std::u8string line;
		while (std::getline(file, line))
		{
			const auto entry_count = scan_entries(line, entries);
			df::assert_true(entry_count == 2);

			if (entry_count == 2)
			{
				const auto id = entries[0]._s;
				const auto found_sep = id.find('.');

				if (found_sep != std::u8string_view::npos)
				{
					const auto country_code = to_code2(id.substr(0, found_sep));
					const auto found_country = _countries.find(country_code);

					if (found_country != _countries.end())
					{
						const auto state_code = id.substr(found_sep + 1);
						found_country->second.state(state_code, entries[1].cached_string());
					}
				}
			}
		}
	}
}

namespace Cols
{
	enum GeoNamesCols
	{
		id = 0,
		latitude,
		longitude,
		stateCode,
		countryCode,
		population,
		name
	};
};

int location_cache::scan_entries(const std::u8string_view line, csv_entry* entries)
{
	memset(entries, 0, sizeof(csv_entry) * max_location_cols);

	auto col_count = 0;
	auto* col = entries;
	const auto* const col_end = entries + max_location_cols;
	auto i = 0u;
	auto begin = 0u;

	while (i < line.size())
	{
		const auto c = line[i];

		if (c == '\t' && col < col_end)
		{
			col->_s = line.substr(begin, i - begin);
			col++;
			begin = i + 1;
			col_count += 1;
		}

		++i;
	}

	col->_s = line.substr(begin, i - begin);
	col_count += 1;

	return col_count;
}

int location_cache::scan_entries(u8istream& file, std::u8string& line, const std::streamoff offset, csv_entry* entries) const
{
	if (file.is_open())
	{
		file.clear();
		file.seekg(offset, u8istream::beg);

		if (std::getline(file, line))
		{
			return scan_entries(line, entries);
		}
	}

	return 0;
}

location_t location_cache::build_location(u8istream& file, const int offset) const
{
	location_t result;
	std::u8string line;
	csv_entry entries[max_location_cols];
	const auto col_count = scan_entries(file, line, offset, entries);

	if (col_count > 0)
	{
		result = build_location(entries);
	}

	return result;
}

location_t location_cache::build_location(const csv_entry* entries) const
{
	const auto country_code = entries[Cols::countryCode].to_code2();
	const auto country = find_country(country_code);
	const auto state = country.state(entries[Cols::stateCode].to_code2());
	const auto population = entries[Cols::population].to_double();

	const gps_coordinate position(entries[Cols::latitude].to_double(), entries[Cols::longitude].to_double());
	return location_t(entries[Cols::id].to_int(), entries[Cols::name].cached_string(), state, country.name(), position,
		population);
}

void location_cache::load_index()
{
	platform::exclusive_lock lock(_rw);
	const auto expected_number_of_locations = 500000;

	load_countries();
	load_states();

	_locations_by_id.reserve(expected_number_of_locations);
	_locations_by_ngram.reserve(expected_number_of_locations);
	_coords.reserve(expected_number_of_locations);

	csv_entry entries[max_location_cols];

	u8istream file;
	file.open(platform::to_file_system_path(_locations_path), u8istream::binary);

	if (file.is_open())
	{
		skip_bom(file);
		auto pos = file.tellg();

		std::u8string line;
		while (std::getline(file, line))
		{
			if (df::is_closing) return;

			const auto entry_count = scan_entries(line, entries);
			const auto id = static_cast<uint32_t>(entries[Cols::id].to_int());
			const auto country = entries[Cols::countryCode].to_code2();
			const auto offset = static_cast<uint32_t>(pos);
			const auto x = entries[Cols::latitude].to_float();
			const auto y = entries[Cols::longitude].to_float();

			_coords.emplace_back(x, y, offset, country);
			_locations_by_id.emplace_back(id, offset);

			const auto name_entry_count = entry_count - Cols::name;
			const auto* const name_entries = entries + Cols::name;

			for (auto i = 0; i < name_entry_count; i++)
			{
				auto r = name_entries[i].to_range();

				if (!r.empty())
				{
					_locations_by_ngram.emplace_back(r, offset);
				}
			}

			pos = file.tellg();
		}
	}

	std::sort(_locations_by_id.begin(), _locations_by_id.end());
	std::sort(_locations_by_ngram.begin(), _locations_by_ngram.end());

	_locations_by_id.shrink_to_fit();
	_locations_by_ngram.shrink_to_fit();
	_coords.shrink_to_fit();

	_tree.build(_coords);
}

struct location_match_possible
{
	std::u8string line;
	location_match_part city;
	location_match_part state;
	location_match_part country;
	double distance_away{};

	bool operator<(const location_match_possible& other) const
	{
		return distance_away < other.distance_away;
	}
};

static bool find_match(const str::cached text, const std::u8string_view query, str::cached& text_result,
	str::part_t& highlight_result)
{
	const auto found = ifind(text, query);

	if (found != std::u8string_view::npos)
	{
		text_result = text;
		highlight_result = { found, query.length() };
		return true;
	}

	return false;
}

static bool find_match(const std::u8string_view text, const std::u8string_view query, str::cached& text_result,
	str::part_t& highlight_result)
{
	const auto found = str::ifind(text, query);

	if (found != std::u8string_view::npos)
	{
		text_result = str::cache(text);
		highlight_result = { found, query.length() };
		return true;
	}

	return false;
}

static bool find_match(const csv_entry* entry, int entry_count, const std::u8string_view query,
	str::cached& text_result, str::part_t& highlight_result)
{
	for (auto i = 0; i < entry_count; i++)
	{
		if (find_match(entry->to_range(), query, text_result, highlight_result))
		{
			return true;
		}
	}

	return false;
}

static bool find_match(const country_t& country, const std::u8string_view query, str::cached& text_result,
	str::part_t& highlight_result)
{
	if (find_match(country.name(), query, text_result, highlight_result))
	{
		return true;
	}

	if (find_match(country.code(), query, text_result, highlight_result))
	{
		return true;
	}

	for (const auto& an : country.alt_names())
	{
		if (find_match(an, query, text_result, highlight_result))
			return true;
	}

	return false;
}

location_matches location_cache::auto_complete(const std::u8string_view query, const uint32_t max_results,
	const gps_coordinate default_location) const
{
	platform::shared_lock lock(_rw);

	location_matches result;
	std::vector<uint32_t> ngram_matches;
	std::vector<location_match_possible> possible_matches;

	const auto closest = find_closest(default_location.latitude(), default_location.longitude());
	const auto query_lower = str::to_lower(query);
	auto query_parts = str::split(query_lower, true);

	for (auto&& part : query_parts)
	{
		part = str::trim(part);
	}

	if (!query_parts.empty())
	{
		const auto short_query = query_parts.size() == 1 && query_parts[0].size() < 3;
		const auto ngram = ngram_t(query_parts[0]);
		auto found_ngram = std::lower_bound(_locations_by_ngram.begin(), _locations_by_ngram.end(),
			location_ngram_and_offset{ ngram, 0 });

		while (found_ngram != _locations_by_ngram.end() && found_ngram->ngram.is_possible_match(ngram))
		{
			ngram_matches.emplace_back(found_ngram->offset);
			++found_ngram;
		}

		std::ranges::sort(ngram_matches);
		ngram_matches.erase(std::ranges::unique(ngram_matches).begin(), ngram_matches.end());

		u8istream file;
		file.open(platform::to_file_system_path(_locations_path), u8istream::binary);

		if (file.is_open())
		{
			skip_bom(file);
			std::u8string line;

			for (const auto& line_offset : ngram_matches)
			{
				csv_entry entries[max_location_cols];
				const auto entry_count = scan_entries(file, line, line_offset, entries);
				const auto country = find_country(entries[Cols::countryCode].to_code2());
				const auto is_same_country = closest.country == country.code();
				const auto name_col_count = entry_count - Cols::name;

				auto match_count = 0u;
				location_match_possible possible;

				for (const auto& part : query_parts)
				{
					if (!short_query || is_same_country)
					{
						str::cached text_result;
						str::part_t highlight_result;
						bool has_match = false;

						if (is_empty(possible.city.text))
						{
							if (find_match(entries + Cols::name, name_col_count, part, text_result, highlight_result))
							{
								possible.city.text = text_result;
								possible.city.highlights.emplace_back(highlight_result);
								match_count += 1;
								has_match = true;
							}
						}
						else
						{
							if (find_match(possible.city.text, part, text_result, highlight_result))
							{
								possible.city.highlights.emplace_back(highlight_result);
								match_count += 1;
								has_match = true;
							}
						}

						if (!has_match)
						{
							if (is_empty(possible.country.text))
							{
								if (find_match(country, part, text_result, highlight_result))
								{
									possible.country.text = text_result;
									possible.country.highlights.emplace_back(highlight_result);
									match_count += 1;
									has_match = true;
								}
							}
							else
							{
								if (find_match(possible.country.text, part, text_result, highlight_result))
								{
									possible.country.highlights.emplace_back(highlight_result);
									match_count += 1;
									has_match = true;
								}
							}
						}

						if (!has_match)
						{
							if (is_empty(possible.state.text))
							{
								if (find_match(country, part, text_result, highlight_result))
								{
									possible.state.text = text_result;
									possible.state.highlights.emplace_back(highlight_result);
									match_count += 1;
									has_match = true;
								}
							}
							else
							{
								if (find_match(possible.state.text, part, text_result, highlight_result))
								{
									possible.state.highlights.emplace_back(highlight_result);
									match_count += 1;
									has_match = true;
								}
							}
						}

						if (!has_match)
						{
							break;
						}
					}
				}

				if (match_count == query_parts.size())
				{
					gps_coordinate position(entries[Cols::latitude].to_double(), entries[Cols::longitude].to_double());
					possible.distance_away = default_location.magnitude_between_locations(position);
					possible.line = line;
					possible_matches.emplace_back(possible);
				}
			}

			std::sort(possible_matches.begin(), possible_matches.end());

			for (const auto& possible : possible_matches)
			{
				if (result.size() < max_results)
				{
					csv_entry entries[max_location_cols];
					const auto col_count = scan_entries(possible.line, entries);

					location_match lm;

					if (col_count > 0)
					{
						lm.location = build_location(entries);
					}

					lm.city = possible.city;
					lm.state = possible.state;
					lm.country = possible.country;

					if (is_empty(lm.city.text)) lm.city.text = lm.location.place;
					if (is_empty(lm.state.text)) lm.state.text = lm.location.state;
					if (is_empty(lm.country.text)) lm.country.text = lm.location.country;

					lm.distance_away = possible.distance_away;
					result.emplace_back(lm);
				}
			}
		}
	}

	return result;
}

location_t location_cache::find_by_id(const uint32_t id) const
{
	platform::shared_lock lock(_rw);
	location_t result;
	const auto found = std::lower_bound(_locations_by_id.begin(), _locations_by_id.end(),
		location_id_and_offset{ id, 0 });

	if (found != _locations_by_id.end() && found->id == id)
	{
		u8istream file;
		file.open(platform::to_file_system_path(_locations_path), u8istream::binary);

		if (file.is_open())
		{
			skip_bom(file);
			result = build_location(file, found->offset);
		}
	}

	return result;
}

country_loc location_cache::find_country(const double x, const double y) const
{
	platform::shared_lock lock(_rw);
	const kd_coordinates_t xy = { static_cast<float>(x), static_cast<float>(y) };
	const auto closest = _tree.find_closest(_coords, xy);
	const auto found = _countries.find(closest.country);
	return found != _countries.end()
		? country_loc{ found->second.code2(), found->second.name(), found->second.centroid() }
	: country_loc{};
}

location_t location_cache::find_closest(const double x, const double y) const
{
	platform::shared_lock lock(_rw);
	location_t result;
	u8istream file;
	file.open(platform::to_file_system_path(_locations_path), u8istream::binary);

	if (file.is_open())
	{
		skip_bom(file);

		const kd_coordinates_t xy = { static_cast<float>(x), static_cast<float>(y) };
		const auto closest = _tree.find_closest(_coords, xy);
		result = build_location(file, closest.offset);
	}

	return result;
}

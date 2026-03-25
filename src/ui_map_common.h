// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Common map tile types, coordinate math, and rendering engine shared
// by map_view (full view) and map_control (dialog widget). Handles tile
// fetching, caching, GPS math, panning, zooming, and crosshair rendering.

#pragma once

#include "ui.h"

// For M_PI on some compilers, otherwise define it manually.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// A standard tile size in pixels (OpenStreetMap tiles are 256x256)
constexpr int TILE_SIZE = 256;

// Represents a tile coordinate (slippy map tilenames).
struct map_tile_id
{
	int x;
	int y;
	int z; // Zoom level

	bool operator<(const map_tile_id& other) const
	{
		if (z != other.z) return z < other.z;
		if (x != other.x) return x < other.x;
		return y < other.y;
	}

	bool operator==(const map_tile_id& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
};

// Hash specialization for map_tile_id to enable use with std::unordered_set
namespace std
{
	template <>
	struct hash<map_tile_id>
	{
		std::size_t operator()(const map_tile_id& id) const noexcept
		{
			const std::size_t h1 = std::hash<int>{}(id.x);
			const std::size_t h2 = std::hash<int>{}(id.y);
			const std::size_t h3 = std::hash<int>{}(id.z);

			return h1 ^ (h2 << 1) ^ (h3 << 2);
		}
	};
}

// The primary output struct, linking a tile to its drawing position.
struct map_tile
{
	map_tile_id coord;
	pointi screen_pos;
};

// Converts longitude to a tile's X coordinate at a given zoom level.
static double lon_to_tile_x(const double lon, const int zoom)
{
	return (lon + 180.0) / 360.0 * pow(2.0, zoom);
}

// Converts latitude to a tile's Y coordinate at a given zoom level.
static double lat_to_tile_y(const double lat, const int zoom)
{
	const double lat_rad = lat * M_PI / 180.0;
	return (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * pow(2.0, zoom);
}

inline std::vector<map_tile> get_tiles_for_view(const recti& bounds, const pointi scroll_offset, const gps_coordinate& center,
                                         const int zoom)
{
	std::vector<map_tile> tiles_to_draw;

	const double center_tile_x_f = lon_to_tile_x(center.longitude(), zoom);
	const double center_tile_y_f = lat_to_tile_y(center.latitude(), zoom);

	const int center_tile_x = static_cast<int>(floor(center_tile_x_f));
	const int center_tile_y = static_cast<int>(floor(center_tile_y_f));

	const double offset_x = center_tile_x_f - center_tile_x;
	const double offset_y = center_tile_y_f - center_tile_y;

	const int center_tile_screen_x = bounds.width() / 2 - static_cast<int>(offset_x * TILE_SIZE) + scroll_offset.x;
	const int center_tile_screen_y = bounds.height() / 2 - static_cast<int>(offset_y * TILE_SIZE) + scroll_offset.y;

	int start_tile_x = center_tile_x;
	int start_screen_x = center_tile_screen_x;
	while (start_screen_x > bounds.left)
	{
		start_screen_x -= TILE_SIZE;
		start_tile_x--;
	}

	int start_tile_y = center_tile_y;
	int start_screen_y = center_tile_screen_y;
	while (start_screen_y > bounds.top)
	{
		start_screen_y -= TILE_SIZE;
		start_tile_y--;
	}

	for (int y = start_tile_y, screen_y = start_screen_y; screen_y < bounds.bottom; ++y, screen_y += TILE_SIZE)
	{
		for (int x = start_tile_x, screen_x = start_screen_x; screen_x < bounds.right; ++x, screen_x += TILE_SIZE)
		{
			const int max_coord = static_cast<int>(pow(2.0, zoom));
			if (x >= 0 && x < max_coord && y >= 0 && y < max_coord)
			{
				map_tile tile_info;
				tile_info.coord = {x, y, zoom};
				tile_info.screen_pos = {screen_x, screen_y};
				tiles_to_draw.push_back(tile_info);
			}
		}
	}

	return tiles_to_draw;
}

inline std::u8string generate_tile_path(const map_tile_id& coord)
{
	u8ostringstream url;
	url << u8"/" << coord.z << u8"/" << coord.x << u8"/" << coord.y << u8".png";
	return url.str();
}

class map_engine
{
public:
	struct cache_entry
	{
		ui::surface_ptr surface;
		bool in_view = false;
	};

	using cache_entry_ptr = std::shared_ptr<cache_entry>;

private:
	async_strategy& _async;
	std::function<void()> _invalidate;

	int _zoom = 16;
	gps_coordinate _location;
	pointi _scroll_offset = {0, 0};
	pointi _temp_drag_offset = {0, 0};

	std::map<map_tile_id, cache_entry_ptr> _tile_cache;
	std::map<map_tile_id, ui::texture_ptr> _texture_cache;

public:
	map_engine(async_strategy& async, std::function<void()> invalidate)
		: _async(async), _invalidate(std::move(invalidate))
	{
	}

	~map_engine()
	{
		_tile_cache.clear();
		_texture_cache.clear();
	}

	const gps_coordinate& location() const { return _location; }

	void set_location_raw(const gps_coordinate& loc)
	{
		_location = loc;
		_scroll_offset = {};
	}

	void set_location(const gps_coordinate& loc, const recti& bounds)
	{
		if (_location != loc)
		{
			_location = loc;
			_scroll_offset = {};
			_texture_cache.clear();
			fetch_tiles(bounds, _scroll_offset);
			_invalidate();
		}
	}

	gps_coordinate calc_center_gps() const
	{
		const auto total_offset = _scroll_offset + _temp_drag_offset;

		const double center_tile_x_f = lon_to_tile_x(_location.longitude(), _zoom);
		const double center_tile_y_f = lat_to_tile_y(_location.latitude(), _zoom);

		const double tile_offset_x = -static_cast<double>(total_offset.x) / TILE_SIZE;
		const double tile_offset_y = -static_cast<double>(total_offset.y) / TILE_SIZE;

		const double new_center_tile_x = center_tile_x_f + tile_offset_x;
		const double new_center_tile_y = center_tile_y_f + tile_offset_y;

		const double new_longitude = new_center_tile_x / pow(2.0, _zoom) * 360.0 - 180.0;

		const double n = M_PI - 2.0 * M_PI * new_center_tile_y / pow(2.0, _zoom);
		const double new_latitude = 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));

		return gps_coordinate(new_latitude, new_longitude);
	}

	void render(ui::draw_context& dc, const sizei& extent)
	{
		dc.clear(dc.colors.background);

		const auto map_bounds = recti(extent);
		const auto total_offset = _scroll_offset + _temp_drag_offset;
		const auto tiles = get_tiles_for_view(map_bounds, total_offset, _location, _zoom);

		for (const auto& tile : tiles)
		{
			const auto tile_rect = recti(tile.screen_pos, sizei(TILE_SIZE, TILE_SIZE));
			auto found_texture = _texture_cache.find(tile.coord);

			if (found_texture != _texture_cache.end())
			{
				dc.draw_texture(found_texture->second, tile_rect);
			}
			else
			{
				auto found_surface = _tile_cache.find(tile.coord);

				if (found_surface != _tile_cache.end() && found_surface->second->surface)
				{
					auto texture = dc.create_texture();
					texture->update(found_surface->second->surface);
					_texture_cache[tile.coord] = texture;
					dc.draw_texture(texture, tile_rect);
				}
				else
				{
					dc.draw_rect(tile_rect.inflate(-1), ui::color(0.5, 0.5, 0.5, 0.5));
				}
			}
		}

		render_crosshair(dc, extent);
	}

	bool zoom(const int delta, const recti& bounds)
	{
		const int zoom_change = delta > 0 ? 1 : -1;
		const int new_zoom = _zoom + zoom_change;

		if (new_zoom >= 3 && new_zoom <= 18)
		{
			_zoom = new_zoom;
			_texture_cache.clear();
			_scroll_offset = {0, 0};
			fetch_tiles(bounds, _scroll_offset);
			_invalidate();
			return true;
		}

		return false;
	}

	void pan_start()
	{
		_temp_drag_offset = {0, 0};
	}

	gps_coordinate pan(const pointi start_loc, const pointi current_loc, const recti& bounds)
	{
		_temp_drag_offset = current_loc - start_loc;
		fetch_tiles(bounds.inflate(TILE_SIZE), _scroll_offset + _temp_drag_offset);
		_invalidate();
		return calc_center_gps();
	}

	gps_coordinate pan_end(const pointi start_loc, const pointi final_loc, const recti& bounds)
	{
		_scroll_offset = _scroll_offset + _temp_drag_offset;
		_temp_drag_offset = {0, 0};

		const auto gps = calc_center_gps();
		_location = gps;
		_scroll_offset = {};
		_texture_cache.clear();
		fetch_tiles(bounds, _scroll_offset);
		_invalidate();
		return gps;
	}

	void clear_caches()
	{
		_tile_cache.clear();
		_texture_cache.clear();
	}

	void fetch_tiles_for_bounds(const recti& bounds)
	{
		fetch_tiles(bounds, _scroll_offset);
	}

private:
	void render_crosshair(ui::draw_context& dc, const sizei& extent) const
	{
		const int center_x = extent.cx / 2;
		const int center_y = extent.cy / 2;
		const pointi center_point(center_x, center_y);

		constexpr int crosshair_size = 20;
		constexpr int inner_gap = 4;
		constexpr int line_width = 2;
		constexpr int outer_circle_radius = 15;
		constexpr int inner_circle_radius = 3;

		const auto crosshair_color = ui::color(ui::style::color::dialog_selected_background, 0.7);
		const auto outline_color = ui::color(1.0f, 1.0f, 1.0f, 0.5f);

		const recti outer_circle_outline(center_point - pointi(outer_circle_radius + 1, outer_circle_radius + 1),
		                                 sizei((outer_circle_radius + 1) * 2, (outer_circle_radius + 1) * 2));
		dc.draw_rounded_rect(outer_circle_outline, outline_color, outer_circle_radius + 1);

		const recti outer_circle(center_point - pointi(outer_circle_radius, outer_circle_radius),
		                         sizei(outer_circle_radius * 2, outer_circle_radius * 2));
		dc.draw_rounded_rect(outer_circle, crosshair_color, outer_circle_radius);

		const recti left_line_outline(pointi(center_x - crosshair_size, center_y - line_width / 2 - 1),
		                              sizei(crosshair_size - inner_gap, line_width + 2));
		dc.draw_rect(left_line_outline, outline_color);
		const recti left_line(pointi(center_x - crosshair_size, center_y - line_width / 2),
		                      sizei(crosshair_size - inner_gap, line_width));
		dc.draw_rect(left_line, crosshair_color);

		const recti right_line_outline(pointi(center_x + inner_gap, center_y - line_width / 2 - 1),
		                               sizei(crosshair_size - inner_gap, line_width + 2));
		dc.draw_rect(right_line_outline, outline_color);
		const recti right_line(pointi(center_x + inner_gap, center_y - line_width / 2),
		                       sizei(crosshair_size - inner_gap, line_width));
		dc.draw_rect(right_line, crosshair_color);

		const recti top_line_outline(pointi(center_x - line_width / 2 - 1, center_y - crosshair_size),
		                             sizei(line_width + 2, crosshair_size - inner_gap));
		dc.draw_rect(top_line_outline, outline_color);
		const recti top_line(pointi(center_x - line_width / 2, center_y - crosshair_size),
		                     sizei(line_width, crosshair_size - inner_gap));
		dc.draw_rect(top_line, crosshair_color);

		const recti bottom_line_outline(pointi(center_x - line_width / 2 - 1, center_y + inner_gap),
		                                sizei(line_width + 2, crosshair_size - inner_gap));
		dc.draw_rect(bottom_line_outline, outline_color);
		const recti bottom_line(pointi(center_x - line_width / 2, center_y + inner_gap),
		                        sizei(line_width, crosshair_size - inner_gap));
		dc.draw_rect(bottom_line, crosshair_color);

		const recti inner_circle_outline(center_point - pointi(inner_circle_radius + 1, inner_circle_radius + 1),
		                                 sizei((inner_circle_radius + 1) * 2, (inner_circle_radius + 1) * 2));
		dc.draw_rounded_rect(inner_circle_outline, outline_color, inner_circle_radius + 1);

		const recti inner_circle(center_point - pointi(inner_circle_radius, inner_circle_radius),
		                         sizei(inner_circle_radius * 2, inner_circle_radius * 2));
		dc.draw_rounded_rect(inner_circle, crosshair_color, inner_circle_radius);
	}

	void fetch_tile(const cache_entry_ptr& e, const map_tile_id& coord)
	{
		_async.queue_async(async_queue::map_tile, [&async = _async, coord, e, invalidate = _invalidate]
		{
			if (e->in_view && !e->surface)
			{
				platform::web_request req;
				req.path = generate_tile_path(coord);

				static platform::web_host_ptr s_osm_con;
				if (!s_osm_con)
				{
					s_osm_con = platform::connect_to_host(u8"a.tile.openstreetmap.org"sv);
				}

				auto response = send_request(s_osm_con, req);

				if (response.status_code == 200)
				{
					files ff;
					const df::cspan data(std::bit_cast<const uint8_t*>(response.body.data()), response.body.size());
					auto surface = ff.image_to_surface(data);

					async.queue_ui(
						[invalidate]
						{
							invalidate();
						});

					e->surface = std::move(surface);
				}
			}
		});
	}

	void fetch_tiles(const recti& bounds, const pointi scroll_offset)
	{
		const auto tiles = get_tiles_for_view(bounds, scroll_offset, _location, _zoom);
		std::unordered_set<map_tile_id> visible_tiles;
		visible_tiles.reserve(tiles.size());
		for (const auto& tile : tiles)
		{
			visible_tiles.insert(tile.coord);
		}

		for (auto& [coord, entry] : _tile_cache)
		{
			entry->in_view = visible_tiles.contains(coord);
		}

		for (const auto& tile : tiles)
		{
			auto found = _tile_cache.find(tile.coord);

			if (found == _tile_cache.end())
			{
				auto e = std::make_shared<cache_entry>();
				e->in_view = true;
				_tile_cache.emplace(tile.coord, e);
				found = _tile_cache.find(tile.coord);
			}

			if (found != _tile_cache.end() && !found->second->surface)
			{
				fetch_tile(found->second, tile.coord);
			}
		}

		cleanup_cache();
	}

	void cleanup_cache()
	{
		constexpr size_t max_cache_size = 1000;

		if (_tile_cache.size() > max_cache_size)
		{
			auto it = _tile_cache.begin();
			while (it != _tile_cache.end() && _tile_cache.size() > max_cache_size)
			{
				if (!it->second->in_view)
				{
					_texture_cache.erase(it->first);
					it = _tile_cache.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	}
};

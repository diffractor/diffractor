// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2025  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Map view and location display using OpenStreetMap tiles. Handles tile
// fetching, caching, panning, zooming, and GPS coordinate display.

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
namespace std {
	template<>
	struct hash<map_tile_id> {
		std::size_t operator()(const map_tile_id& id) const noexcept {
			// Combine the hash values of x, y, and z
			std::size_t h1 = std::hash<int>{}(id.x);
			std::size_t h2 = std::hash<int>{}(id.y);
			std::size_t h3 = std::hash<int>{}(id.z);
			
			// Use a simple combining formula
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

/**
 * @brief Calculates which tiles are needed to fill a window and where to draw them.
 *
 * @param rect The bounding rectangle of the window/viewport in pixels.
 * @param center The geographical center (lat, lon) of the map view.
 * @param zoom The current zoom level.
 * @return A std::vector of map_tile structs, each identifying a tile and its
 * top-left screen position for drawing.
 */
std::vector<map_tile> get_tiles_for_view(const recti& bounds, const pointi scroll_offset, const gps_coordinate& center,
                                         const int zoom)
{
	std::vector<map_tile> tiles_to_draw;

	// 1. Calculate the central tile's floating-point coordinates.
	const double center_tile_x_f = lon_to_tile_x(center.longitude(), zoom);
	const double center_tile_y_f = lat_to_tile_y(center.latitude(), zoom);

	// 2. Get the integer part for the tile index.
	const int center_tile_x = static_cast<int>(floor(center_tile_x_f));
	const int center_tile_y = static_cast<int>(floor(center_tile_y_f));

	// 3. Get the fractional part, which represents the offset within the tile.
	const double offset_x = center_tile_x_f - center_tile_x;
	const double offset_y = center_tile_y_f - center_tile_y;

	// 4. Calculate the screen position of the center tile's top-left corner.
	// The center of the screen is (rect.width / 2, rect.height / 2).
	// We subtract the pixel offset of the geo-center within its tile.
	// Apply scroll offset to shift the entire map view.
	const int center_tile_screen_x = bounds.width() / 2 - static_cast<int>(offset_x * TILE_SIZE) + scroll_offset.x;
	const int center_tile_screen_y = bounds.height() / 2 - static_cast<int>(offset_y * TILE_SIZE) + scroll_offset.y;

	// 5. Calculate the coordinates of the top-leftmost tile to start drawing from.
	// We repeatedly step back one tile size until we are off the screen to the top-left.
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

	// 6. Iterate through rows and columns, generating the list of required tiles.
	for (int y = start_tile_y, screen_y = start_screen_y; screen_y < bounds.bottom; ++y, screen_y += TILE_SIZE)
	{
		for (int x = start_tile_x, screen_x = start_screen_x; screen_x < bounds.right; ++x, screen_x += TILE_SIZE)
		{
			// Basic validation to ensure tile coordinates are within the valid range for the zoom level.
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

static std::u8string generate_tile_path(const map_tile_id& coord)
{
	u8ostringstream url;
	url << u8"/" << coord.z << u8"/" << coord.x << u8"/" << coord.y << u8".png";
	return url.str();
}

class map_control final : public view_element, public std::enable_shared_from_this<map_control>, public ui::frame_host
{
public:
	bool _hover = false;
	pointi _start_loc;
	ui::frame_ptr _frame;
	sizei _extent;
	async_strategy& _async;

	// zoom 0 to 19
	int _zoom = 16;

	gps_coordinate _location;
	std::u8string _place_id;
	std::function<void(gps_coordinate)> _cb;

	const int cell_width = 18;

	// Map scroll offset for panning
	pointi _scroll_offset = {0, 0};
	// Temporary offset during dragging
	pointi _temp_drag_offset = {0, 0};

	map_control(async_strategy& async, std::function<void(gps_coordinate)> cb) : _async(async), _cb(
		std::move(cb))
	{
	}

	void init(const ui::control_frame_ptr& owner)
	{
		_frame = owner->create_frame(weak_from_this(), {});
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		return {cx, cx};
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_frame);
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		positions.emplace_back(_frame, bounds, is_visible());
	}

	void on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized) override
	{
		_extent = extent;
	}

	gps_coordinate calc_center_gps() const
	{
		// Calculate the center location's GPS coordinates
		// based on existing _location and scroll offset
		const auto total_offset = _scroll_offset + _temp_drag_offset;

		// Convert the current location to tile coordinates
		const double center_tile_x_f = lon_to_tile_x(_location.longitude(), _zoom);
		const double center_tile_y_f = lat_to_tile_y(_location.latitude(), _zoom);

		// Calculate how many tiles we've moved due to scrolling
		// Positive scroll offset means we've moved the map right/down, so the center moved left/up
		const double tile_offset_x = -static_cast<double>(total_offset.x) / TILE_SIZE;
		const double tile_offset_y = -static_cast<double>(total_offset.y) / TILE_SIZE;

		// Calculate the new center tile coordinates
		const double new_center_tile_x = center_tile_x_f + tile_offset_x;
		const double new_center_tile_y = center_tile_y_f + tile_offset_y;

		// Convert tile coordinates back to GPS coordinates
		const double new_longitude = new_center_tile_x / pow(2.0, _zoom) * 360.0 - 180.0;

		const double n = M_PI - 2.0 * M_PI * new_center_tile_y / pow(2.0, _zoom);
		const double new_latitude = 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));

		return gps_coordinate(new_latitude, new_longitude);
	}

	void on_mouse_move(const pointi loc, const bool is_tracking) override
	{
		if (!_hover)
		{
			_hover = true;
			_frame->invalidate();
		}

		if (is_tracking)
		{
			const auto temp_offset = loc - _start_loc;
			_temp_drag_offset = temp_offset;
			fetch_tiles(calc_bounds().inflate(TILE_SIZE), _scroll_offset + temp_offset);
			_frame->invalidate();
			send_location_changed_event(calc_center_gps());
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_start_loc = loc;
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		scroll_map(_temp_drag_offset);
		_temp_drag_offset = {0, 0};
		_start_loc = {0, 0};

		const auto gps = calc_center_gps();
		_location = gps;
		_scroll_offset = {};
		fetch_tiles(calc_bounds(), _scroll_offset);
		send_location_changed_event(gps);
		_frame->invalidate();
	}

	void scroll_map(const pointi delta)
	{
		_scroll_offset = _scroll_offset + delta;
		fetch_tiles(calc_bounds(), _scroll_offset);
	}

	void on_mouse_leave(const pointi loc) override
	{
		if (_hover)
		{
			_hover = false;
			_frame->invalidate();
		}
	}

	void send_location_changed_event(const gps_coordinate& loc) const
	{
		if (_cb)
		{
			_cb(loc);
		}
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
		// Determine zoom direction based on wheel delta
		// Positive delta = zoom in, negative delta = zoom out
		const int zoom_change = delta > 0 ? 1 : -1;
		const int new_zoom = _zoom + zoom_change;

		// Clamp zoom level to valid range (0 to 19)
		if (new_zoom >= 3 && new_zoom <= 18)
		{
			_zoom = new_zoom;

			// Clear texture cache when zoom changes since tiles are different
			_texture_cache.clear();

			// Reset scroll offset to keep the map centered
			_scroll_offset = {0, 0};

			// Fetch new tiles for the updated zoom level
			fetch_tiles(calc_bounds(), _scroll_offset);

			// Invalidate to trigger a repaint
			_frame->invalidate();

			// Mark the event as handled
			was_handled = true;
		}
		else
		{
			// Don't handle if zoom would go out of bounds
			was_handled = false;
		}
	}

	void tick() override
	{
	}

	void activate(bool is_active) override
	{
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	}

	void on_window_paint(ui::draw_context& dc) override
	{
		dc.clear(dc.colors.background);

		const auto total_offset = _scroll_offset + _temp_drag_offset;
		const auto tiles = get_tiles_for_view(calc_bounds(), total_offset, _location, _zoom);

		for (const auto& tile : tiles)
		{
			// Tile positions now already include scroll offset, so use them directly
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

		// Draw crosshair center marker
		const int center_x = bounds.width() / 2;
		const int center_y = bounds.height() / 2;
		const pointi center_point(center_x, center_y);

		// Crosshair parameters
		constexpr int crosshair_size = 20;
		constexpr int inner_gap = 4;
		constexpr int line_width = 2;
		constexpr int outer_circle_radius = 15;
		constexpr int inner_circle_radius = 3;

		const auto crosshair_color = ui::color(ui::style::color::dialog_selected_background, 0.7);
		const auto outline_color = ui::color(1.0f, 1.0f, 1.0f, 0.5f); // White outline for contrast

		// Draw outer circle outline
		const recti outer_circle_outline(center_point - pointi(outer_circle_radius + 1, outer_circle_radius + 1),
		                                 sizei((outer_circle_radius + 1) * 2, (outer_circle_radius + 1) * 2));
		dc.draw_rounded_rect(outer_circle_outline, outline_color, outer_circle_radius + 1);

		// Draw outer circle
		const recti outer_circle(center_point - pointi(outer_circle_radius, outer_circle_radius),
		                         sizei(outer_circle_radius * 2, outer_circle_radius * 2));
		dc.draw_rounded_rect(outer_circle, crosshair_color, outer_circle_radius);

		// Draw horizontal crosshair lines with outline
		// Left line outline
		const recti left_line_outline(pointi(center_x - crosshair_size, center_y - line_width / 2 - 1),
		                              sizei(crosshair_size - inner_gap, line_width + 2));
		dc.draw_rect(left_line_outline, outline_color);

		// Left line
		const recti left_line(pointi(center_x - crosshair_size, center_y - line_width / 2),
		                      sizei(crosshair_size - inner_gap, line_width));
		dc.draw_rect(left_line, crosshair_color);

		// Right line outline
		const recti right_line_outline(pointi(center_x + inner_gap, center_y - line_width / 2 - 1),
		                               sizei(crosshair_size - inner_gap, line_width + 2));
		dc.draw_rect(right_line_outline, outline_color);

		// Right line
		const recti right_line(pointi(center_x + inner_gap, center_y - line_width / 2),
		                       sizei(crosshair_size - inner_gap, line_width));
		dc.draw_rect(right_line, crosshair_color);

		// Draw vertical crosshair lines with outline
		// Top line outline
		const recti top_line_outline(pointi(center_x - line_width / 2 - 1, center_y - crosshair_size),
		                             sizei(line_width + 2, crosshair_size - inner_gap));
		dc.draw_rect(top_line_outline, outline_color);

		// Top line
		const recti top_line(pointi(center_x - line_width / 2, center_y - crosshair_size),
		                     sizei(line_width, crosshair_size - inner_gap));
		dc.draw_rect(top_line, crosshair_color);

		// Bottom line outline
		const recti bottom_line_outline(pointi(center_x - line_width / 2 - 1, center_y + inner_gap),
		                                sizei(line_width + 2, crosshair_size - inner_gap));
		dc.draw_rect(bottom_line_outline, outline_color);

		// Bottom line
		const recti bottom_line(pointi(center_x - line_width / 2, center_y + inner_gap),
		                        sizei(line_width, crosshair_size - inner_gap));
		dc.draw_rect(bottom_line, crosshair_color);

		// Draw inner circle outline
		const recti inner_circle_outline(center_point - pointi(inner_circle_radius + 1, inner_circle_radius + 1),
		                                 sizei((inner_circle_radius + 1) * 2, (inner_circle_radius + 1) * 2));
		dc.draw_rounded_rect(inner_circle_outline, outline_color, inner_circle_radius + 1);

		// Draw inner circle
		const recti inner_circle(center_point - pointi(inner_circle_radius, inner_circle_radius),
		                         sizei(inner_circle_radius * 2, inner_circle_radius * 2));
		dc.draw_rounded_rect(inner_circle, crosshair_color, inner_circle_radius);
	}

	struct cache_entry
	{
		ui::surface_ptr surface;
		bool in_view = false;
	};

	using cache_entry_ptr = std::shared_ptr<cache_entry>;

	// Make this non-static since it's a member variable accessed by member functions
	static std::map<map_tile_id, cache_entry_ptr> _tile_cache;
	std::map<map_tile_id, ui::texture_ptr> _texture_cache;
	platform::web_host_ptr _openstreetmap_con;

	recti calc_bounds() const
	{
		return recti(_extent);
	}

	void fetch_tile(async_strategy& async, const cache_entry_ptr& e, const map_tile_id& coord,
	                std::function<void(ui::surface_ptr)> f)
	{
		async.queue_async(async_queue::map_tile, [&async, coord, f, e, t = shared_from_this()]
		{
			if (e->in_view && !e->surface)
			{
				platform::web_request req;
				req.path = generate_tile_path(coord);

				if (!t->_openstreetmap_con)
				{
					t->_openstreetmap_con = platform::connect_to_host(u8"a.tile.openstreetmap.org"sv);
				}

				auto response = send_request(t->_openstreetmap_con, req);

				if (response.status_code == 200)
				{
					files ff;
					const df::cspan data(std::bit_cast<const uint8_t*>(response.body.data()), response.body.size());
					auto surface = ff.image_to_surface(data);

					async.queue_ui(
						[&async, surface, f]
						{
							f(std::move(surface));
						});
				}
			}
		});
	}

	void cleanup_cache()
	{
		// Remove tiles that are not in view and have been cached for a while
		// This prevents memory bloat when user pans around extensively
		constexpr size_t max_cache_size = 1000; // Keep at most 1000 tiles cached

		if (_tile_cache.size() > max_cache_size)
		{
			// Remove tiles not in view first
			auto it = _tile_cache.begin();
			while (it != _tile_cache.end() && _tile_cache.size() > max_cache_size)
			{
				if (!it->second->in_view)
				{
					// Also remove from texture cache
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

	void fetch_tiles(const recti& bounds, const pointi scroll_offset)
	{
		const auto tiles = get_tiles_for_view(bounds, scroll_offset, _location, _zoom);
		std::unordered_set<map_tile_id> visible_tiles;
		visible_tiles.reserve(tiles.size());
		for (const auto& tile : tiles)
		{
			visible_tiles.insert(tile.coord);
		}

		// First pass: mark all existing cache entries as not in view
		for (auto& [coord, entry] : _tile_cache)
		{
			entry->in_view = visible_tiles.contains(coord);
		}

		// Second pass: mark tiles that are currently in view and queue loading if needed
		for (const auto& tile : tiles)
		{
			auto found = _tile_cache.find(tile.coord);

			if (found == _tile_cache.end())
			{
				auto e = std::make_shared<cache_entry>();
				e->in_view = true; // Mark as in view immediately
				_tile_cache.emplace(tile.coord, e);
			}

			found = _tile_cache.find(tile.coord);

			// Only queue tile fetch if we don't already have the surface
			if (found != _tile_cache.end() && !found->second->surface)
			{
				fetch_tile(_async, found->second, tile.coord,
				           [e = found->second, t = shared_from_this()](ui::surface_ptr surface)
				           {
					           e->surface = std::move(surface);

					           if (e->in_view)
					           {
						           t->_frame->invalidate();
					           }
				           });
			}
		}

		// Clean up cache periodically
		cleanup_cache();
	}

	void set_location_marker(const gps_coordinate loc)
	{
		if (_location != loc)
		{
			_location = loc;
			_scroll_offset = {};
			fetch_tiles(calc_bounds(), _scroll_offset);
			_frame->invalidate();
		}
	}
};

using map_control_ptr = std::shared_ptr<map_control>;

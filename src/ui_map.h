#include <utility>
#include <set>
#include "ui.h"

// For M_PI on some compilers, otherwise define it manually.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// A standard tile size in pixels (OpenStreetMap tiles are 256x256)
const int TILE_SIZE = 256;


// Represents a tile coordinate (slippy map tilenames).
struct map_tile_id {
	int x;
	int y;
	int z; // Zoom level

	bool operator<(const map_tile_id& other) const {
		if (z != other.z) return z < other.z;
		if (x != other.x) return x < other.x;
		return y < other.y;
	}
};

// The primary output struct, linking a tile to its drawing position.
struct map_tile {
	map_tile_id coord;
	pointi screen_pos;
};

// Converts longitude to a tile's X coordinate at a given zoom level.
static double lon_to_tile_x(double lon, int zoom) {
	return (lon + 180.0) / 360.0 * pow(2.0, zoom);
}

// Converts latitude to a tile's Y coordinate at a given zoom level.
static double lat_to_tile_y(double lat, int zoom) {
	double lat_rad = lat * M_PI / 180.0;
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
std::vector<map_tile> get_tiles_for_view(const recti& bounds, const pointi scroll_offset, const gps_coordinate& center, int zoom) {
	std::vector<map_tile> tiles_to_draw;

	// 1. Calculate the central tile's floating-point coordinates.
	double center_tile_x_f = lon_to_tile_x(center.longitude(), zoom);
	double center_tile_y_f = lat_to_tile_y(center.latitude(), zoom);

	// 2. Get the integer part for the tile index.
	int center_tile_x = static_cast<int>(floor(center_tile_x_f));
	int center_tile_y = static_cast<int>(floor(center_tile_y_f));

	// 3. Get the fractional part, which represents the offset within the tile.
	double offset_x = center_tile_x_f - center_tile_x;
	double offset_y = center_tile_y_f - center_tile_y;

	// 4. Calculate the screen position of the center tile's top-left corner.
	// The center of the screen is (rect.width / 2, rect.height / 2).
	// We subtract the pixel offset of the geo-center within its tile.
	// Apply scroll offset to shift the entire map view.
	int center_tile_screen_x = (bounds.width() / 2) - static_cast<int>(offset_x * TILE_SIZE) + scroll_offset.x;
	int center_tile_screen_y = (bounds.height() / 2) - static_cast<int>(offset_y * TILE_SIZE) + scroll_offset.y;

	// 5. Calculate the coordinates of the top-leftmost tile to start drawing from.
	// We repeatedly step back one tile size until we are off the screen to the top-left.
	int start_tile_x = center_tile_x;
	int start_screen_x = center_tile_screen_x;
	while (start_screen_x > bounds.left) {
		start_screen_x -= TILE_SIZE;
		start_tile_x--;
	}

	int start_tile_y = center_tile_y;
	int start_screen_y = center_tile_screen_y;
	while (start_screen_y > bounds.top) {
		start_screen_y -= TILE_SIZE;
		start_tile_y--;
	}

	// 6. Iterate through rows and columns, generating the list of required tiles.
	for (int y = start_tile_y, screen_y = start_screen_y; screen_y < bounds.bottom; ++y, screen_y += TILE_SIZE) {
		for (int x = start_tile_x, screen_x = start_screen_x; screen_x < bounds.right; ++x, screen_x += TILE_SIZE) {

			// Basic validation to ensure tile coordinates are within the valid range for the zoom level.
			int max_coord = static_cast<int>(pow(2.0, zoom));
			if (x >= 0 && x < max_coord && y >= 0 && y < max_coord) {
				map_tile tile_info;
				tile_info.coord = { x, y, zoom };
				tile_info.screen_pos = { screen_x, screen_y };
				tiles_to_draw.push_back(tile_info);
			}
		}
	}

	return tiles_to_draw;
}

static std::u8string generate_tile_path(const map_tile_id& coord) {
	u8ostringstream url;
	url << u8"/" << coord.z << u8"/" << coord.x << u8"/" << coord.y << u8".png";
	return url.str();
}

static void fetch_tile(async_strategy& async, const map_tile_id& coord, std::function<void(ui::surface_ptr)> f)
{
	platform::web_request req;
	req.host = u8"a.tile.openstreetmap.org";
	req.path = generate_tile_path(coord);

	async.queue_async(async_queue::web, [&async, req, f]()
		{
			auto response = send_request(req);

			if (response.status_code == 200)
			{
				files ff;
				df::cspan data(std::bit_cast<const uint8_t*>(response.body.data()), response.body.size());
				auto surface = ff.image_to_surface(data);

				async.queue_ui(
					[&async, surface, f]
					{
						f(std::move(surface));
					});
			}
		});
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
	pointi _scroll_offset = { 0, 0 };
	// Temporary offset during dragging
	pointi _temp_drag_offset = { 0, 0 };

public:
	map_control(async_strategy& async, std::function<void(gps_coordinate)> cb) : _cb(
		std::move(cb)), _async(async)
	{
	}

	void init(const ui::control_frame_ptr& owner)
	{
		_frame = owner->create_frame(weak_from_this(), {});
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		return { cx, cx };
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
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_start_loc = loc;
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		scroll_map(_temp_drag_offset);
		_temp_drag_offset = { 0, 0 };
		_start_loc = { 0, 0 };
		_frame->invalidate();
	}

	void scroll_map(pointi delta)
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

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
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

		for (const auto& tile : tiles) {
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

				if (found_surface != _tile_cache.end() && found_surface->second.surface)
				{
					auto texture = dc.create_texture();
					texture->update(found_surface->second.surface);
					_texture_cache[tile.coord] = texture;
					dc.draw_texture(texture, tile_rect);
				}
				else
				{
					dc.draw_rect(tile_rect.inflate(-1), ui::color(0.5, 0.5, 0.5, 0.5));
				}
			}
		}
	}

	struct cache_entry
	{
		ui::surface_ptr surface;
	};

	static std::map<map_tile_id, cache_entry> _tile_cache;
	std::map<map_tile_id, ui::texture_ptr> _texture_cache;

	recti calc_bounds() const
	{
		return recti(_extent);
	}

	void fetch_tiles(const recti& bounds, pointi scroll_offset)
	{
		const auto tiles = get_tiles_for_view(bounds, scroll_offset, _location, _zoom);

		for (const auto& tile : tiles) {
			if (_tile_cache.find(tile.coord) == _tile_cache.end()) {				
				
				_tile_cache.emplace(tile.coord, cache_entry {});
				
				fetch_tile(_async, tile.coord, [coord = tile.coord, t = shared_from_this()](ui::surface_ptr surface) {					
					t->_tile_cache[coord].surface = std::move(surface);
					t->_frame->invalidate();
				});
			}
		}
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

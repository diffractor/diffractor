// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Location assignment view. Provides a map-based interface for setting
// GPS coordinates on selected items, with search and reverse geocoding.
// The map is rendered directly in the main view area (left panel).

#include "pch.h"
#include "model.h"
#include "model_db.h"
#include "model_locations.h"
#include "ui_controls.h"
#include "ui_dialog.h"
#include "view_locate.h"
#include "view_list.h"
#include "app_command_status.h"


// -------------------------------------------------------------------
// Location autocomplete match for search results
// -------------------------------------------------------------------

class locate_auto_complete_match final : public ui::auto_complete_match,
                                         public std::enable_shared_from_this<locate_auto_complete_match>
{
public:
	location_match match;
	ui::complete_strategy_t& _parent;

	locate_auto_complete_match(ui::complete_strategy_t& parent, location_match m, const int w) :
		auto_complete_match(view_element_style::can_invoke), match(std::move(m)), _parent(parent)
	{
		weight = w;
	}

	std::string edit_text() const override
	{
		return match.location.str();
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg = calc_background_color(dc);

		if (bg.a > 0.0f)
		{
			dc.draw_rect(logical_bounds, bg);
		}

		const auto text_bounds = logical_bounds.inflate(-dc.padding1, 0);
		auto yy = text_bounds.top + dc.padding1;

		const auto city_text = match.city.text.sv();
		const auto state_text = match.state.text.sv();
		const auto country_text = match.country.text.sv();

		if (!city_text.empty())
		{
			dc.draw_text(city_text, text_bounds.offset(0, yy - text_bounds.top), ui::style::font_face::dialog,
			             ui::style::text_style::single_line, dc.colors.foreground, {});
			yy += dc.text_line_height(ui::style::font_face::dialog) + 2;
		}

		std::string sub;
		if (!state_text.empty()) sub += state_text;
		if (!country_text.empty())
		{
			if (!sub.empty()) sub += ", ";
			sub += country_text;
		}

		if (!sub.empty())
		{
			dc.draw_text(sub, text_bounds.offset(0, yy - text_bounds.top), ui::style::font_face::dialog,
			             ui::style::text_style::single_line,
			             ui::color(dc.colors.foreground, 0.6f), {});
		}
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		const auto line_height = mc.text_line_height(ui::style::font_face::dialog);
		return {cx, line_height * 2 + mc.padding2 + 4};
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::click);
		}
	}
};


// -------------------------------------------------------------------
// Location autocomplete strategy using local location database
// -------------------------------------------------------------------

class locate_auto_complete_strategy final : public std::enable_shared_from_this<locate_auto_complete_strategy>,
                                            public ui::complete_strategy_t
{
public:
	view_state& _state;
	ui::control_frame_ptr _parent;
	ui::auto_complete_results _results;
	ui::auto_complete_match_ptr _selected;

	std::function<void(const location_t&)> _changed;

	locate_auto_complete_strategy(view_state& s, ui::control_frame_ptr parent,
	                              std::function<void(const location_t&)> changed)
		: _state(s), _parent(std::move(parent)), _changed(std::move(changed))
	{
		resize_to_show_results = false;
		max_predictions = 15u;
	}

	std::string no_results_message() override
	{
		return std::string(tt.type_to_search);
	}

	void initialise(std::function<void(const ui::auto_complete_results&)> complete) override
	{
		search({}, std::move(complete));
	}

	void search(const std::string& query, std::function<void(const ui::auto_complete_results&)> complete) override
	{
		if (str::is_empty(query))
		{
			_state.queue_ui([complete = std::move(complete)]
			{
				complete({});
			});
			return;
		}

		_state.queue_location(
			[t = shared_from_this(), query, complete = std::move(complete)](const location_cache& locations)
			{
				const auto locally_found = locations.auto_complete(query, t->max_predictions, setting.default_location);

				ui::auto_complete_results found;

				for (const auto& local : locally_found)
				{
					found.emplace_back(
						std::make_shared<locate_auto_complete_match>(*t, local,
						                                             1 + df::round(local.distance_away)));
				}

				std::ranges::stable_sort(found, [](const ui::auto_complete_match_ptr& l,
				                                   const ui::auto_complete_match_ptr& r)
				{
					return l->weight < r->weight;
				});

				t->_state.queue_ui([t, complete, found]
				{
					t->_results = found;
					if (t->_results.size() > t->max_predictions) t->_results.resize(t->max_predictions);
					complete(t->_results);
				});
			});
	}

	void selected(const ui::auto_complete_match_ptr& i, const select_type st) override
	{
		if (i)
		{
			const auto ii = std::dynamic_pointer_cast<locate_auto_complete_match>(i);

			if (ii && _changed)
			{
				_changed(ii->match.location);
			}

			if (_selected)
			{
				_selected->set_style_bit(view_element_style::selected, false);
			}

			_selected = i;

			if (_selected)
			{
				_selected->set_style_bit(view_element_style::selected, true);
			}
		}
	}

	ui::auto_complete_match_ptr selected() const override
	{
		return _selected;
	}
};


// -------------------------------------------------------------------
// locate_view implementation
// -------------------------------------------------------------------

void locate_view::update_location(const location_t& loc)
{
	if (!loc.position.is_valid())
	{
		return;
	}

	_location->id = loc.id;
	_location->place_text = loc.place;
	_location->state_text = loc.state;
	_location->country_text = loc.country;
	_location->latitude = loc.position.latitude();
	_location->longitude = loc.position.longitude();

	// Invalidate any in-flight reverse-geocode requests so they don't overwrite
	// the place/state/country we just set from the user's explicit selection.
	++_geocode_request_id;

	set_map_location(loc.position);
	refresh();
}

void locate_view::activate(const sizei extent)
{
	// Always re-evaluate the initial position when entering the view so that the
	// map reflects the currently selected items rather than a previous session.
	gps_coordinate initial_loc;

	// Try to use GPS from first selected item that has it
	const auto& items = _state.selected_items();
	for (const auto& i : items.items())
	{
		if (i->has_gps())
		{
			const auto md = i->metadata();
			if (md && md->coordinate.is_valid())
			{
				initial_loc = md->coordinate;
				break;
			}
		}
	}

	// Fall back to user's default location or a world-center default
	if (!initial_loc.is_valid())
	{
		initial_loc = setting.default_location;
	}
	if (!initial_loc.is_valid())
	{
		initial_loc = gps_coordinate(48.8566, 2.3522); // Paris
	}

	_engine.set_location_raw(initial_loc);

	// Seed the selected location coordinates from the initial map position so
	// the apply button is enabled immediately and the lat/lon fields show a
	// value before the user pans. Place/state/country are populated by the
	// reverse-geocode triggered below.
	_location->latitude = initial_loc.latitude();
	_location->longitude = initial_loc.longitude();

	map_view::activate(extent);

	// Reverse geocode the initial position so the property fields are populated.
	request_reverse_geocode(initial_loc);
	refresh();
}

void locate_view::deactivate()
{
	_populate_controls = nullptr;
	_status.clear();
	// Reset selected location so the next activation re-evaluates from the
	// newly selected items rather than reusing stale data.
	*_location = selected_location_t{};
	// Invalidate any in-flight reverse-geocode results from this session.
	++_geocode_request_id;
	map_view::deactivate();
}

void locate_view::on_map_panned(const gps_coordinate& new_center)
{
	if (!new_center.is_valid())
	{
		return;
	}

	// Skip work if the centre has not actually moved (zero-distance click).
	if (df::equiv(_location->latitude, new_center.latitude()) &&
		df::equiv(_location->longitude, new_center.longitude()))
	{
		return;
	}

	// Update coordinates immediately so the UI reflects the new position
	_location->latitude = new_center.latitude();
	_location->longitude = new_center.longitude();
	refresh();

	request_reverse_geocode(new_center);
}

void locate_view::request_reverse_geocode(const gps_coordinate& gps)
{
	const auto request_id = ++_geocode_request_id;
	std::weak_ptr<locate_view> weak_self = shared_from_this();
	_state._async.queue_location(
		[&async = _state._async, gps, weak_self, request_id](const location_cache& locations)
		{
			auto place = locations.find_closest(gps.latitude(), gps.longitude());
			place.position = gps;
			async.queue_ui([place, weak_self, request_id]
			{
				if (const auto self = weak_self.lock())
				{
					// Discard stale results from earlier requests.
					if (request_id != self->_geocode_request_id)
					{
						return;
					}

					self->_location->id = place.id;
					self->_location->place_text = place.place;
					self->_location->state_text = place.state;
					self->_location->country_text = place.country;
					self->_location->latitude = place.position.latitude();
					self->_location->longitude = place.position.longitude();
					self->refresh();
				}
			});
		});
}

void locate_view::refresh()
{
	const auto& items = _state.selected_items();
	const auto has_location = can_run();

	int update_count = 0;
	int overwrite_count = 0;

	for (const auto& i : items.items())
	{
		if (has_location)
		{
			if (i->has_gps())
			{
				++overwrite_count;
			}
			++update_count;
		}
	}

	if (has_location)
	{
		_status = std::format("{} {} ({} {})",
		                      update_count, tt.command_locate,
		                      overwrite_count, tt.location_overwrite_gps);
	}
	else
	{
		_status = std::string(tt.location_not_selected);
	}

	_state.invalidate_view(view_invalid::view_layout | view_invalid::status);

	if (_populate_controls)
	{
		_populate_controls();
	}
}

void locate_view::run()
{
	if (!can_run())
		return;

	const auto title = tt.command_locate;
	constexpr auto icon = icon_index::location;

	record_feature_use(features::locate);
	detach_file_handles detach(_state);

	auto dlg = make_dlg(_host->owner());

	metadata_edits edits;
	edits.location_coordinate = gps_coordinate(_location->latitude, _location->longitude);

	// Also write the resolved place/state/country tags when known so IPTC/XMP
	// reflect the chosen location, not just the GPS coordinate.
	if (!_location->place_text.empty())
	{
		edits.location_place = _location->place_text;
	}
	if (!_location->state_text.empty())
	{
		edits.location_state = _location->state_text;
	}
	if (!_location->country_text.empty())
	{
		edits.location_country = _location->country_text;
	}

	// Only record locations that map to a known place id; "0" is meaningless
	// as a recent-location entry.
	if (_location->id != 0)
	{
		_state.recent_locations.add(str::to_string(_location->id));
	}

	const auto results = std::make_shared<command_status>(_state._async, dlg, icon, title,
	                                                      _state.selected_count());
	_state.modify_items(results, icon, title, _state.selected_items().items(), edits, _host);
	results->wait_for_complete();
}


view_controls_host_ptr locate_view::controls(const ui::control_frame_ptr& owner)
{
	auto result = std::make_shared<view_controls_host>(_state);
	auto frame = owner->create_dlg(result, false);

	auto place_edit = std::make_shared<text_element>(std::string{});
	auto state_edit = std::make_shared<text_element>(std::string{});
	auto country_edit = std::make_shared<text_element>(std::string{});
	auto latitude_edit = std::make_shared<text_element>(std::string{});
	auto longitude_edit = std::make_shared<text_element>(std::string{});

	std::weak_ptr<view_controls_host> weak_host = result;
	auto location = _location; // shared_ptr copy for safe capture

	_populate_controls = [location, place_edit, state_edit, country_edit, latitude_edit, longitude_edit, weak_host]()
	{
		place_edit->text(location->place_text);
		state_edit->text(location->state_text);
		country_edit->text(location->country_text);

		const gps_coordinate coord(location->latitude, location->longitude);
		latitude_edit->text(coord.is_valid() ? str::to_string(location->latitude, 5) : std::string{});
		longitude_edit->text(coord.is_valid() ? str::to_string(location->longitude, 5) : std::string{});

		if (const auto host = weak_host.lock())
		{
			host->scroll_controls();
		}
	};

	auto sel_changed = [this](const location_t& loc)
	{
		update_location(loc);
	};

	auto strategy = std::make_shared<locate_auto_complete_strategy>(
		_state, frame, sel_changed);
	const auto search = std::make_shared<ui::search_control>(frame, _location->search_text, strategy);

	const auto props_table = std::make_shared<ui::table_element>(view_element_style::grow);
	props_table->no_shrink_col[0] = true;
	props_table->add(tt.prop_name_place, place_edit);
	props_table->add(tt.prop_name_state, state_edit);
	props_table->add(tt_prep(tt.prop_name_country), country_edit);
	props_table->add(tt.prop_name_latitude, latitude_edit);
	props_table->add(tt.prop_name_longitude, longitude_edit);

	// Build file summary showing what will be updated
	auto file_summary = std::make_shared<ui::group_control>();
	file_summary->add(std::make_shared<ui::title_control>(tt.command_locate));

	const auto& items = _state.selected_items();
	df::item_elements gps_items;

	for (const auto& i : items.items())
	{
		if (i->has_gps())
		{
			gps_items.emplace_back(i);
		}
	}

	file_summary->add(std::make_shared<text_element>(
		format_plural_text(tt.be_updated_fmt, items)));

	if (!gps_items.empty())
	{
		const df::item_set gps_set(gps_items);
		const auto warning = std::make_shared<text_element>(
			format_plural_text(tt.gps_overwrite_count_fmt, gps_set),
			view_element_style::grow | view_element_style::important);
		warning->margin = {10, 10};
		warning->padding = {10, 10};
		warning->update_background_color();
		file_summary->add(warning);
	}

	std::vector<view_element_ptr> controls;
	controls.emplace_back(std::make_shared<text_element>(tt.map_instructions));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<text_element>(tt.type_to_search));
	controls.emplace_back(search);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(props_table);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(file_summary);

	for (const auto& c : controls)
	{
		c->margin.cx = 8;
		c->margin.cy = 4;
	}

	result->_controls = controls;
	result->_frame = result->_dlg = frame;

	// Populate controls with current location data
	_populate_controls();

	return result;
}

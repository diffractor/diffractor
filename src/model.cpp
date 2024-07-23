// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "model.h"
#include "model_index.h"

#include "app_command_line.h"
#include "files.h"
#include "model_items.h"
#include "av_visualizer.h"
#include "av_format.h"
#include "ui_dialog.h"
#include "ui_controls.h"
#include "app_command_status.h"


static const sizei video_preview_size = {256, 256};
fast_fft av_visualizer::fft;
int av_visualizer::xscale[num_bars + 1] = {0};

static bool av_can_use_hw()
{
	return setting.use_d3d11va && !command_line.no_gpu && setting.use_gpu;
}

df::folder_path view_state::save_path() const
{
	if (!_search.has_selector()) return known_path(platform::known_folder::pictures);
	return _search.selectors().front().folder();
}

icon_index view_state::displayed_item_icon() const
{
	return _selected.common_icon();
}

int view_state::displayed_rating() const
{
	for (const auto& i : _selected.items())
	{
		const auto md = i->metadata();

		if (md && md->rating)
		{
			return md->rating;
		}
	}

	return 0;
}

void view_state::toggle_rating(const df::results_ptr& results, const df::item_elements& items, const int r,
                               const view_host_base_ptr& view)
{
	bool add_rating = false;

	if (r != 0)
	{
		for (const auto& i : items)
		{
			const auto md = i->metadata();

			if (md && md->rating != r)
			{
				add_rating = true;
			}
		}
	}

	metadata_edits edits;

	if (add_rating)
	{
		edits.rating = r;
	}
	else
	{
		edits.remove_rating = true;
	}

	modify_items(results, icon_index::star, tt.title_rate, items, edits, view);
}


void view_state::modify_items(const ui::control_frame_ptr& frame, const icon_index icon, const std::u8string_view title,
                              const df::item_elements& items_to_modify, const metadata_edits& edits,
                              const view_host_base_ptr& view)
{
	auto dlg = make_dlg(frame);
	const auto results = std::make_shared<command_status>(_async, dlg, icon, title, selected_count());
	modify_items(results, icon, title, items_to_modify, edits, view);
}


void display_state_t::load_compare_preview(int pos_numerator, int pos_denominator)
{
	if (_is_compare_video && df::file_handles_detached == 0)
	{
		const auto i1 = _item1;
		const auto i2 = _item2;
		const auto st1 = _selected_texture1;
		const auto st2 = _selected_texture2;

		_async.queue_media_preview(
			[t = shared_from_this(), i1, i2, st1, st2, pos_numerator, pos_denominator](media_preview_state& decoder)
			{
				if (decoder.open1(i1->path()) &&
					decoder.open2(i2->path()))
				{
					auto surface1 = std::make_shared<ui::surface>();
					auto surface2 = std::make_shared<ui::surface>();

					if (decoder.decoder1->extract_seek_frame(surface1, {}, pos_numerator, pos_denominator) &&
						decoder.decoder2->extract_seek_frame(surface2, {}, pos_numerator, pos_denominator))
					{
						//surface1.orientation(st1->_item->metadata().orientation);
						//surface2.orientation(st2->_item->metadata().orientation);

						t->_async.queue_ui([t, st1, st2, surface1, surface2]()
						{
							st1->update(surface1);
							st2->update(surface2);
							t->_async.invalidate_view(view_invalid::view_redraw);
						});
					}
				}
			});
	}
}

void display_state_t::load_seek_preview(int pos_numerator, int pos_denominator, std::function<void()> callback)
{
	const auto item = _item1;

	if (player_has_video() && item && item->online_status() == df::item_online_status::disk && df::file_handles_detached
		== 0)
	{
		_async.queue_media_preview(
			[t = shared_from_this(), item, pos_numerator, pos_denominator, callback](media_preview_state& decoder)
			{
				if (decoder.open1(item->path()))
				{
					auto surface = std::make_shared<ui::surface>();

					if (decoder.decoder1->extract_seek_frame(surface, video_preview_size, pos_numerator,
					                                         pos_denominator))
					{
						t->_async.queue_ui([t, surface = std::move(surface), callback]() mutable
						{
							t->_hover_surface = std::move(surface);
							callback();
						});
					}
				}
			});
	}
}

void display_state_t::update_av_session(const std::shared_ptr<av_session>& ses)
{
	if (_session != ses)
	{
		_session = ses;
	}

	if (ses)
	{
		_player_media_info = ses->info();
	}

	_async.invalidate_view(
		view_invalid::view_layout |
		view_invalid::screen_saver |
		view_invalid::media_elements |
		view_invalid::command_state);
}

void display_state_t::calc_pixel_difference() 
{
	auto st1 = _selected_texture1;
	auto st2 = _selected_texture2;

	if (st1 && st2)
	{
		const auto t = shared_from_this();

		_async.queue_media_preview(
			[t, loaded1 = st1->_loaded, loaded2 = st2->_loaded](media_preview_state& decoder)
			{
				t->_pixel_difference = loaded1.calc_pixel_difference(loaded2);
				t->_async.invalidate_view(view_invalid::view_layout | view_invalid::media_elements);
			});
	}
}


void view_state::load_hover_thumb(const df::item_element_ptr& item, double pos_numerator,
                                  double pos_denominator)
{
	if (item &&
		(item->file_type()->group->traits && file_traits::preview_video) &&
		item->online_status() == df::item_online_status::disk &&
		df::file_handles_detached == 0)
	{
		_async.queue_media_preview(
			[this, item, pos_numerator, pos_denominator](media_preview_state& decoder)
			{
				if (decoder.open1(item->path()))
				{
					auto timestamp = platform::now();
					auto surface = std::make_shared<ui::surface>();

					if (decoder.decoder1->extract_thumbnail(surface, video_preview_size, pos_numerator, pos_denominator))
					{
						const auto image = save_png(surface, {});						

						if (is_valid(image))
						{
							const auto cover_art = decoder.decoder1->cover_art();

							queue_ui([this, item, image, cover_art, timestamp]()
							{								
								item->thumbnail(image, cover_art, timestamp);
								invalidate_view(view_invalid::view_redraw);
							});

							queue_async(async_queue::scan_folder, [this, item, image, cover_art, timestamp]()
							{
								item_index.save_thumbnail(item->path(), image, cover_art, timestamp);
							});
						}
					}
				}
			});
	}
}

void view_state::clear_hover_codec() const
{
	_async.queue_media_preview([](media_preview_state& decoder)
	{
		decoder.close();
	});
}

detach_file_handles::detach_file_handles(view_state& s, bool should_close): _state(s)
{
	++df::file_handles_detached;
	_selected = s.selected_items();

	const auto d = s.display_state();

	if (d)
	{
		_i = d->_item1;
		_is_playable = d->can_play_media();
		_is_playing = d->is_playing_media();

		if (d->_session)
		{
			_audio_track = d->_session->video_stream_id();
			_video_track = d->_session->audio_stream_id();
		}

		if (_is_playable)
		{
			platform::thread_event event_wait_player(true, false);
			platform::thread_event event_wait_preview(true, false);

			d->_hover_surface.reset();

			if (d->_session)
			{
				s._player->close(d->_session, [&event_wait_player]()
				{
					event_wait_player.set();
				});
			}

			s._async.queue_media_preview([&event_wait_preview](media_preview_state& decoder)
			{
				decoder.close();
				event_wait_preview.set();
			});

			if (d->_session)
			{
				platform::wait_for({event_wait_player, platform::event_exit}, 10000, false);
			}

			platform::wait_for({event_wait_preview, platform::event_exit}, 10000, false);

			s._async.invalidate_view(view_invalid::view_layout |
				view_invalid::screen_saver |
				view_invalid::app_layout |
				view_invalid::media_elements |
				view_invalid::command_state);
		}

		d->stop_slideshow();
	}

	s.clear_hover_codec();
}

detach_file_handles::~detach_file_handles()
{
	--df::file_handles_detached;

	const auto d = _state.display_state();

	if (d && _i && _i == d->_item1)
	{
		const auto path = _i->path();

		if (path.exists())
		{
			if (_is_playable)
			{
				_state._player->open(_i, _is_playing, _video_track, _audio_track, av_can_use_hw(), true,
				                     [d](std::shared_ptr<av_session> ses)
				                     {
					                     d->update_av_session(ses);
				                     });
			}
		}
		else
		{
			_state.select_nothing({});
			_state.close();
		}
	}

	_state.item_index.queue_scan_modified_items(_state.selected_items());
}

bool media_preview_state::open1(const df::file_path file_path)
{
	const auto new_path = !decoder1 || decoder1->path() != file_path;

	if (new_path)
	{
		const auto new_decoder = std::make_shared<av_format_decoder>();

		if (new_decoder->open(file_path))
		{
			new_decoder->init_streams(-1, -1, false, true, false);
			decoder1 = new_decoder;
		}
		else
		{
			decoder1.reset();
		}
	}

	return decoder1 != nullptr;
}


bool media_preview_state::open2(const df::file_path file_path)
{
	const auto new_path = !decoder2 || decoder2->path() != file_path;

	if (new_path)
	{
		const auto new_decoder = std::make_shared<av_format_decoder>();

		if (new_decoder->open(file_path))
		{
			new_decoder->init_streams(-1, -1, false, true, false);
			decoder2 = new_decoder;
		}
		else
		{
			decoder2.reset();
		}
	}

	return decoder2 != nullptr;
}

df::unique_paths make_unique_paths(df::paths selection)
{
	df::unique_paths result(selection.files.begin(), selection.files.end());
	//for (const auto& f : selection.files) result.emplace(f);
	for (const auto& f : selection.folders) result.emplace(f);
	return result;
}

void view_state::browse_forward(const view_host_base_ptr& view)
{
	history_state::history_entry e;

	if (history.move_history_pos(1, e))
	{
		open(view, e.search, make_unique_paths(e.selected));
	}
}

void view_state::browse_back(const view_host_base_ptr& view)
{
	history_state::history_entry e;

	if (history.move_history_pos(-1, e))
	{
		open(view, e.search, make_unique_paths(e.selected));
	}
}

void view_state::capture_display(std::function<void(file_load_result)> f) const
{
	const auto d = _display;

	if (d)
	{
		if (d->player_has_video())
		{
			_player->capture(d->_session, [f, d = d](file_load_result lr)
			{
				d->_async.queue_ui([f, lr = std::move(lr)]() mutable { f(std::move(lr)); });
			});
		}
		else
		{
			const auto i = d->_item1;

			if (d->_selected_texture1)
			{
				f(d->_selected_texture1->loaded());
			}
			else
			{
				f({});
			}
		}
	}
}

void view_state::view_mode(view_type m)
{
	if (_view_mode != m)
	{
		_view_mode = m;
		_events.view_changed(_view_mode);
	}
}

void view_state::play(const view_host_base_ptr& view)
{
	const auto d = _display;

	if (d)
	{
		if (d->is_playing())
		{
			stop();
		}
		else if (has_display_items())
		{
			record_feature_use(features::slideshow);

			df::item_element_ptr i;

			for (const auto& ii : _selected.items())
			{
				if (ii->is_media())
				{
					i = ii;
				}
			}

			if (i && !i->is_selected())
			{
				select(view, i, false, false, false);
			}
			else if (!i || (_view_mode == view_type::media && d->is_playing()) || !i->is_media())
			{
				const auto next = next_item(true, false);
				const auto mode_can_play = view_mode() == view_type::items || view_mode() == view_type::media;

				if (next && mode_can_play)
				{
					select(view, next, false, false, false);
				}

				i = next;
			}

			if (i)
			{
				_common_display_state._is_playing = true;

				if (i->file_type()->has_trait(file_traits::av) && d->_session)
				{
					setting.auto_play = true;
					_player->play(d->_session);
				}
			}
		}
	}
}

void view_state::stop()
{
	const auto d = _display;
	const auto is_playing_media = d && d->is_playing_media();

	_common_display_state._is_playing = false;

	if (is_playing_media)
	{
		setting.auto_play = false;
	}

	if (d && is_playing_media && d->_session)
	{
		_player->pause(d->_session);
	}
}

void view_state::change_tracks(int video_track, int audio_track)
{
	const auto d = _display;

	if (d && d->_session)
	{
		const auto auto_play = d->_session->is_playing();

		_player->open(d->_item1, auto_play, video_track, audio_track, av_can_use_hw(), true,
		              [d](std::shared_ptr<av_session> ses)
		              {
			              d->update_av_session(ses);
		              });
	}
}

void view_state::change_audio_device(const std::u8string& id)
{
	if (_player)
	{
		_player->audio_device_id(id);
	}
}

void view_state::toggle_group_order()
{
	constexpr group_by options[] = {
		group_by::file_type,
		group_by::size,
		group_by::extension,
		group_by::location,
		group_by::rating_label,
		group_by::date_created,
		group_by::date_modified,
		group_by::camera,
		group_by::resolution,
		group_by::album_show,
		group_by::folder,
		group_by::presence,
	};

	bool found_order = false;

	for (auto i = 0u; i < std::size(options); ++i)
	{
		if (_group_order == options[i])
		{
			group_order(options[(i + 1) % std::size(options)], _sort_order);
			found_order = true;
			break;
		}
	}

	if (!found_order)
	{
		group_order(group_by::file_type, _sort_order);
	}
}

void view_state::group_order(const std::optional<group_by> group, const std::optional<sort_by> order)
{
	auto changed = false;
	const auto is_currently_shuffle = _group_order == group_by::shuffle;

	if (order.has_value() && (_sort_order != order || is_currently_shuffle))
	{
		_sort_order = order.value();

		if (is_currently_shuffle)
		{
			_group_order = group_by::file_type;
		}

		changed = true;
	}

	if (group.has_value() && _group_order != group)
	{
		_group_order = group.value();
		changed = true;
	}

	if (changed)
	{
		if (group == group_by::shuffle)
		{
			_search_items.shuffle();
		}

		_events.invalidate_view(
			view_invalid::view_layout |
			view_invalid::group_layout |
			view_invalid::command_state |
			view_invalid::app_layout);
	}
}

ui::const_image_ptr view_state::first_selected_thumb() const
{
	ui::const_image_ptr result;

	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			if (i->has_thumb() && i->is_selected()) result = i->thumbnail();
		}
	}

	return result;
}

df::unique_items view_state::existing_items() const
{
	df::unique_items results;

	if (_edit_item)
	{
		results._items.insert_or_assign(_edit_item->path(), _edit_item);
	}

	_search_items.append_unique(results);

	return results;
}

void view_state::append_items(const view_host_base_ptr& view, df::item_set items, const df::unique_paths& selection,
                              const bool is_first, const bool is_complete)
{
	df::item_elements select_list;

	if (is_first)
	{
		//_search_items.clear();
		//_item_groups.clear();
		//_selected.clear();
	}

	if (selection.empty())
	{
		for (const auto& i : items.items())
		{
			if (i->is_selected())
			{
				select_list.emplace_back(i);
			}
		}
	}
	else
	{
		for (const auto& i : items.items())
		{
			if (selection.contains(i->path()))
			{
				select_list.emplace_back(i);
			}
		}
	}

	_focus = items.contains(_focus) ? _focus : nullptr;
	_summary_total = items.summary();
	_search_items = std::move(items);

	update_item_groups();
	select(view, select_list, false);
	update_selection();

	const auto d = _display;

	if (d)
	{
		if (d->_selected_texture1)
		{
			d->_selected_texture1->refresh(d->_item1);
		}

		if (d->_selected_texture2)
		{
			d->_selected_texture2->refresh(d->_item2);
		}
	}

	invalidate_view(view_invalid::group_layout);
}

bool view_state::update_selection()
{
	df::item_set selected;

	for (const auto& g : _item_groups)
	{
		for (const auto& i : g->items())
		{
			if (i->is_selected())
			{
				i->add_to(selected);
			}
		}
	}

	const bool changed = _selected != selected;

	if (changed)
	{
		_selected = selected;
		load_display_state();
	}

	return changed;
}

df::item_element_ptr view_state::find_displayed_item_by_name(std::u8string_view file_name) const
{
	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			if (icmp(i->name(), file_name) == 0)
			{
				return i;
			}
		}
	}

	return nullptr;
}


bool view_state::select(const view_host_base_ptr& view, const std::u8string_view file_name, const bool toggle)
{
	const auto i = find_displayed_item_by_name(file_name);

	if (i)
	{
		select(view, i, toggle, false, false);
		return true;
	}

	return false;
}

void view_state::select(const view_host_base_ptr& view, const df::item_element_ptr& selected_item, const bool toggle,
                        const bool extend, bool continue_slideshow)
{
	if (is_item_displayed(selected_item))
	{
		if (extend)
		{
			auto is_before = true;
			auto focus_is_before = true;
			df::item_element_ptr selected_before;
			df::item_element_ptr selected_after;

			for (const auto& b : _item_groups)
			{
				for (const auto& i : b->items())
				{
					if (i == _focus)
					{
						focus_is_before = is_before;
					}

					if (i == selected_item)
					{
						is_before = false;
					}

					if (i->is_selected())
					{
						if (is_before)
						{
							selected_before = i;
						}
						else
						{
							selected_after = i;
						}
					}
				}
			}

			df::item_element_ptr start_item;
			df::item_element_ptr end_item;

			if (selected_before && selected_after)
			{
				if (focus_is_before)
				{
					start_item = selected_before;
					end_item = selected_item;
				}
				else
				{
					start_item = selected_item;
					end_item = selected_after;
				}
			}
			else if (focus_is_before)
			{
				start_item = selected_before;
				end_item = selected_item;
			}
			else if (selected_after)
			{
				start_item = selected_item;
				end_item = selected_after;
			}

			if (start_item && end_item)
			{
				bool is_selecting = false;
				bool is_end = false;

				for (const auto& b : _item_groups)
				{
					for (const auto& i : b->items())
					{
						if (!is_end)
						{
							if (i == start_item)
							{
								is_selecting = true;
							}

							if (is_selecting)
							{
								i->select(true, view, i);
							}

							is_end = i == end_item;
						}
					}
				}
			}
			else
			{
				selected_item->invert_selection(view, selected_item);
			}
		}
		else if (toggle)
		{
			selected_item->invert_selection(view, selected_item);
		}
		else
		{
			for (const auto& b : _item_groups)
			{
				for (const auto& i : b->items())
				{
					if (i == selected_item)
					{
						i->select(true, view, i);
					}
					else if (i->is_selected())
					{
						i->select(i == _pin_item, view, i);
					}
				}
			}
		}

		if (selected_item != _focus)
		{
			const auto previous = _focus;
			_focus = selected_item;
			_events.item_focus_changed(selected_item, previous);
		}

		invalidate_view(view_invalid::selection_list);

		if (!continue_slideshow)
		{
			stop_slideshow();
		}
	}
}

void view_state::select(const view_host_base_ptr& view, const recti selection_bounds, const bool toggle)
{
	df::item_elements selections;

	for (const auto& b : _item_groups)
	{
		if (b->bounds.intersects(selection_bounds))
		{
			for (const auto& i : b->items())
			{
				if (i->bounds.intersects(selection_bounds))
				{
					selections.emplace_back(i);
				}
			}
		}
	}

	if (!selections.empty())
	{
		select(view, selections, toggle);
	}
}

void view_state::unselect(const view_host_base_ptr& view, const df::item_element_ptr& i)
{
	i->select(false, view, i);
	_pin_item.reset();

	const auto f = first_selected();

	if (_focus == i)
	{
		const auto previous = _focus;
		_focus = f.item;
		_events.item_focus_changed(f.item, previous);
	}

	invalidate_view(view_invalid::selection_list);
}

df::item_element_ptr view_state::item_from_location(const pointi loc) const
{
	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			if (i->bounds.contains(loc)) return i;
		}
	}

	return nullptr;
}

group_and_item view_state::selected_item_group() const
{
	if (_focus)
	{
		for (const auto& g : _item_groups)
		{
			for (const auto& i : g->items())
			{
				if (_focus == i)
				{
					return {g, i};
				}
			}
		}
	}

	return first_selected();
}

bool view_state::is_item_displayed(const df::item_element_ptr& first_selection) const
{
	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			if (i == first_selection)
			{
				return true;
			}
		}
	}

	return false;
}

void view_state::select(const view_host_base_ptr& view, const df::item_elements& items_to_select, const bool toggle)
{
	if (toggle)
	{
		const std::unordered_set<df::item_element_ptr> unique_items_to_select(
			items_to_select.begin(), items_to_select.end());

		for (const auto& b : _item_groups)
		{
			for (const auto& i : b->items())
			{
				if (_pin_item == i)
				{
					i->select(true, view, i);
				}
				else if (unique_items_to_select.contains(i))
				{
					i->select(!i->is_selected(), view, i);
				}
				else
				{
					i->select(false, view, i);
				}
			}
		}

		for (const auto& i : items_to_select)
		{
			i->select(true, view, i);
		}
	}
	else
	{
		for (const auto& b : _item_groups)
		{
			for (const auto& i : b->items())
			{
				i->select(_pin_item == i, view, i);
			}
		}

		for (const auto& i : items_to_select)
		{
			i->select(true, view, i);
		}
	}

	df::item_element_ptr new_focus;

	if (!items_to_select.empty())
	{
		auto first_selection = items_to_select.front();

		if (is_item_displayed(first_selection))
		{
			new_focus = std::move(first_selection);
		}
	}

	if (new_focus != _focus)
	{
		const auto previous_focus = _focus;
		_focus = new_focus;
		_events.item_focus_changed(new_focus, previous_focus);
	}

	invalidate_view(view_invalid::selection_list);
}

void view_state::select_all(const view_host_base_ptr& view)
{
	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			i->select(true, view, i);
		}
	}

	invalidate_view(view_invalid::selection_list);
}

void view_state::select_nothing(const view_host_base_ptr& view)
{
	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			i->select(false, view, i);
			i->set_style_bit(view_element_style::highlight, false);
		}
	}

	_focus.reset();
	_pin_item.reset();
	invalidate_view(view_invalid::selection_list);
}

void view_state::select_inverse(const view_host_base_ptr& view)
{
	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			i->invert_selection(view, i);
		}
	}

	invalidate_view(view_invalid::selection_list);
}

void view_state::select_end(const view_host_base_ptr& view, const bool forward, const bool toggle, const bool extend)
{
	select(view, end_item(forward), toggle, extend, false);
	stop_slideshow();
	make_visible(focus_item());
}

void view_state::select_next(const view_host_base_ptr& view, const bool forward, const bool toggle, const bool extend)
{
	select(view, next_item(forward, toggle || extend), toggle, extend, false);
	stop_slideshow();
	make_visible(focus_item());
}

view_state::view_state(state_strategy& ev, async_strategy& ac, index_state& item_index,
                       std::shared_ptr<av_player> player) :
	_events(ev),
	_async(ac),
	item_index(item_index),
	_player(std::move(player)),
	_display(std::make_shared<display_state_t>(ac, _common_display_state))

{
}

view_state::~view_state()
{
	_display.reset();
	_search_items.clear();
	_item_groups.clear();
	_selected.clear();
	_focus = nullptr;
}

bool view_state::enter(const view_host_base_ptr& view)
{
	if (_selected.size() == 1 &&
		_selected.has_folders())
	{
		const auto folder = _selected.items().front();
		folder->open(*this, view);
		return true;
	}

	if (_selected.empty())
	{
		select_next(view, true, false, false);
	}

	if (!_selected.has_folders() &&
		!_selected.items().empty())
	{
		auto all_media = true;

		for (const auto& i : _selected.items())
		{
			all_media &= i->file_type()->is_media();
		}

		if (all_media)
		{
			if (!is_full_screen)
			{
				_events.toggle_full_screen();
			}
			else
			{
				select_next(view, true, false, false);
			}
		}
		else
		{
			invoke(commands::tool_open_with);
		}

		return true;
	}

	return false;
}

class search_element : public text_element_base, public std::enable_shared_from_this<search_element>
{
public:
	view_state& _state;
	const df::search_t _search;
	prop::key_ref _prop_key;

	explicit search_element(view_state& state, std::u8string_view text, df::search_t search,
	                        const prop::key_ref prop_key) noexcept
		: text_element_base(text, view_element_style::has_tooltip | view_element_style::can_invoke), _state(state),
		  _search(std::move(search)), _prop_key(prop_key)
	{
	}

	explicit search_element(view_state& state, std::u8string_view text, df::search_t search) noexcept
		: text_element_base(text, view_element_style::has_tooltip | view_element_style::can_invoke), _state(state),
		  _search(std::move(search)), _prop_key(prop::null)
	{
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void tooltip(view_hover_element& result, const pointi loc, const pointi element_offset) const override
	{
		if (_prop_key != prop::null)
		{
			result.elements->add(make_icon_element(_prop_key->icon, view_element_style::no_break));
			result.elements->add(std::make_shared<text_element>(_prop_key->text(), ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::line_break));
		}

		result.elements->add(std::make_shared<text_element>(_search.text(), ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   view_element_style::line_break));
		result.elements->add(std::make_shared<action_element>(tt.click_to_search_similar));

		result.active_bounds = result.window_bounds = bounds.offset(element_offset);
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.open(event.host, _search, {});
		}
		else if (event.type == view_element_event_type::dpi_changed)
		{
			_tl.reset();
		}
	}
};

static view_element_ptr make_link(view_state& s, std::u8string_view text, df::search_t& search,
                                  const prop::key_ref prop, const df::search_result& current_search)
{
	auto element = std::make_shared<search_element>(s, text, search, prop);
	if (current_search.is_match(prop)) element->set_style_bit(view_element_style::important, true);
	return element;
}

static view_element_ptr make_link(view_state& s, std::u8string_view text, const prop::key_ref prop,
                                  const df::search_result& current_search)
{
	auto element = std::make_shared<search_element>(s, text, df::search_t().with(prop, text), prop);
	if (current_search.is_match(prop)) element->set_style_bit(view_element_style::important, true);
	return element;
}

static uint32_t calc_rank_color()
{
	return ui::average(ui::style::color::rank_background, ui::style::color::view_text);
}

static std::vector<view_element_ptr> format_dims(uint16_t width, uint16_t height, const file_type_ref ft, bool is_rank)
{
	std::vector<view_element_ptr> results;

	if (width > 0 && height > 0)
	{
		const auto dims = sizei{width, height};
		auto element = std::make_shared<text_element>(prop::format_dimensions(dims));
		if (is_rank) element->foreground_color(calc_rank_color());
		results.emplace_back(element);

		if (ft->has_trait(file_traits::av))
		{
			const auto video_res = prop::format_video_resolution(dims);

			if (!video_res.empty())
			{
				results.emplace_back(std::make_shared<text_element>(video_res));
			}
		}
		else
		{
			const auto mp = ui::calc_mega_pixels(dims.cx, dims.cy);

			if (mp > 0.9)
			{
				results.emplace_back(std::make_shared<text_element>(prop::format_pixels(dims, ft)));
			}
		}
	}

	return results;
}

static std::vector<view_element_ptr> create_camera_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                            const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;
	if (!is_empty(md->camera_model))
		results.emplace_back(
			make_link(s, md->camera_model, prop::camera_model, search_result));
	if (!df::is_zero(md->exposure_time))
		results.emplace_back(make_link(s, prop::format_exposure(md->exposure_time),
		                               df::search_t().with(
			                               prop::exposure_time, md->exposure_time),
		                               prop::exposure_time, search_result));
	if (!df::is_zero(md->f_number))
		results.emplace_back(make_link(s, prop::format_f_num(md->f_number),
		                               df::search_t().with(prop::f_number, md->f_number),
		                               prop::f_number, search_result));
	if (md->iso_speed != 0)
		results.emplace_back(make_link(s, prop::format_iso(md->iso_speed),
		                               df::search_t().with(prop::iso_speed, md->iso_speed),
		                               prop::iso_speed, search_result));
	if (!df::is_zero(md->focal_length))
		results.emplace_back(make_link(
			s, prop::format_focal_length(md->focal_length, md->focal_length_35mm_equivalent),
			df::search_t().with(prop::focal_length, md->focal_length), prop::focal_length, search_result));
	if (!is_empty(md->lens)) results.emplace_back(make_link(s, md->lens, prop::lens, search_result));

	return results;
}

static std::vector<view_element_ptr> create_album_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                           const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;

	if (!is_empty(md->album)) results.emplace_back(make_link(s, md->album, prop::album, search_result));
	if (!is_empty(md->show)) results.emplace_back(make_link(s, md->show, prop::show, search_result));

	if (md->year != 0)
	{
		results.emplace_back(make_link(s, str::to_string(md->year), df::search_t().with(prop::year, md->year),
		                               prop::year, search_result));
	}

	if (!is_empty(md->genre)) results.emplace_back(make_link(s, md->genre, prop::genre, search_result));

	return results;
}

static std::vector<view_element_ptr> create_artist_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                            const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;
	df::hash_map<std::u8string_view, prop::key_ref> unique;

	if (!is_empty(md->artist))
	{
		const auto parts = split(md->artist, true, str::is_artist_separator);

		for (const auto& a : parts)
		{
			unique[a] = prop::artist;
		}
	}

	if (!is_empty(md->album_artist))
	{
		const auto parts = split(md->album_artist, true, str::is_artist_separator);

		for (const auto& a : parts)
		{
			unique[a] = prop::album_artist;
		}
	}

	for (const auto& a : unique)
	{
		results.emplace_back(make_link(s, a.first, a.second, search_result));
	}

	return results;
}

static std::vector<view_element_ptr> create_retro_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                           const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;
	if (!is_empty(md->system)) results.emplace_back(make_link(s, md->system, prop::system, search_result));
	if (!is_empty(md->game)) results.emplace_back(make_link(s, md->game, prop::game, search_result));
	return results;
}

static std::vector<view_element_ptr> create_location_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                              const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;
	if (!is_empty(md->location_place))
		results.emplace_back(
			make_link(s, md->location_place, prop::location_place, search_result));
	if (!is_empty(md->location_state))
		results.emplace_back(
			make_link(s, md->location_state, prop::location_state, search_result));
	if (!is_empty(md->location_country))
		results.emplace_back(
			make_link(s, md->location_country, prop::location_country, search_result));
	return results;
}

static std::vector<view_element_ptr> create_copyright_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                               const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;
	if (!is_empty(md->copyright_notice))
		results.emplace_back(
			make_link(s, md->copyright_notice, prop::copyright_notice, search_result));
	if (!is_empty(md->copyright_creator))
		results.emplace_back(
			make_link(s, md->copyright_creator, prop::copyright_creator, search_result));
	if (!is_empty(md->copyright_credit))
		results.emplace_back(
			make_link(s, md->copyright_credit, prop::copyright_credit, search_result));
	if (!is_empty(md->copyright_source))
		results.emplace_back(
			make_link(s, md->copyright_source, prop::copyright_source, search_result));
	if (!is_empty(md->copyright_url))
		results.emplace_back(
			make_link(s, md->copyright_url, prop::copyright_url, search_result));
	return results;
}

static std::vector<view_element_ptr> create_tag_elements(view_state& s, const prop::item_metadata_ptr& md,
                                                         const df::search_result& search_result)
{
	std::vector<view_element_ptr> results;

	const auto tag_parts = split(md->tags, true);

	for (const auto& t : tag_parts)
	{
		auto e = std::make_shared<search_element>(s, t, df::search_t().with(prop::tag, t));
		if (search_result.is_match(prop::tag, t)) e->set_style_bit(view_element_style::important, true);
		results.emplace_back(e);
	}

	return results;
}

static void add_row(const std::shared_ptr<ui::table_element>& result, const std::u8string_view label,
                    std::vector<view_element_ptr> e1, std::vector<view_element_ptr> e2)
{
	if (!e1.empty() || !e2.empty())
	{
		result->add(std::vector<view_element_ptr>{
			std::make_shared<text_element>(label), std::make_shared<view_elements>(e1),
			std::make_shared<view_elements>(e2)
		});
	}
}

static void add_media_elements(view_state& s, const prop::item_metadata_ptr& md, std::vector<view_element_ptr>& video,
                               std::vector<view_element_ptr>& audio, const df::search_result& search_result)
{
	const auto video_codec = md->video_codec;
	const auto pixel_format = md->pixel_format;
	const auto bitrate = md->bitrate.sz();
	const auto audio_codec = md->audio_codec;
	const auto audio_channels = md->audio_channels;
	const auto audio_sample_rate = md->audio_sample_rate;
	const auto audio_sample_type = static_cast<prop::audio_sample_t>(md->audio_sample_type);

	if (!is_empty(video_codec)) video.emplace_back(make_link(s, video_codec, prop::video_codec, search_result));
	if (!is_empty(pixel_format)) video.emplace_back(make_link(s, pixel_format, prop::pixel_format, search_result));
	if (!str::is_empty(bitrate)) video.emplace_back(make_link(s, bitrate, prop::bitrate, search_result));

	if (!prop::is_null(audio_sample_rate))
		audio.emplace_back(make_link(
			s, prop::format_audio_sample_rate(audio_sample_rate),
			df::search_t().with(prop::audio_sample_rate, md->audio_sample_rate), prop::audio_sample_rate,
			search_result));
	if (audio_sample_type != prop::audio_sample_t::none)
		audio.emplace_back(make_link(
			s, format_audio_sample_type(audio_sample_type),
			df::search_t().with(prop::audio_sample_type, md->audio_sample_type), prop::audio_sample_type,
			search_result));
	if (!prop::is_null(audio_channels))
		audio.emplace_back(make_link(s, prop::format_audio_channels(audio_channels),
		                             df::search_t().with(
			                             prop::audio_channels, md->audio_channels),
		                             prop::audio_channels, search_result));
	if (!prop::is_null(audio_codec)) audio.emplace_back(make_link(s, audio_codec, prop::audio_codec, search_result));
}


class title_link_element final : public std::enable_shared_from_this<title_link_element>, public text_element_base
{
private:
	view_state& _state;
	const df::item_element_ptr _item;

public:
	title_link_element(view_state& s, df::item_element_ptr i, std::u8string_view text,
	                   const view_element_style style_in) noexcept : text_element_base(text), _state(s),
	                                                                 _item(std::move(i))
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;
		_font = ui::style::font_face::title;
		_text_style = ui::style::text_style::single_line_center;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			df::related_info r;
			r.load(_item);
			_state.open(event.host, df::search_t().related(r), {{}});
			_state.view_mode(view_type::items);
		}
		else if (event.type == view_element_event_type::dpi_changed)
		{
			_tl.reset();
		}
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);

		if (_tl)
		{
			const auto dups_count = _item->duplicates();
			const auto dups_text = str::to_string(dups_count.count);
			const auto sides_count = _item->sidecars_count();
			const auto sides_text = str::to_string(sides_count);
			const auto text_extent = _tl->measure_text(logical_bounds.width() + 100);
			const auto extent_sides = dc.measure_text(sides_text, ui::style::font_face::dialog,
			                                          ui::style::text_style::single_line_center, logical_bounds.width(),
			                                          logical_bounds.height());
			const auto extent_dups = dc.measure_text(dups_text, ui::style::font_face::dialog,
			                                         ui::style::text_style::single_line_center, 64, 32);

			const auto min_width = std::min(100, text_extent.cx);
			const auto show_sidecars = sides_count > 0 && logical_bounds.width() > min_width;
			const auto show_dups = logical_bounds.width() > min_width;
			const auto bg_alpha = dc.colors.alpha * dc.colors.bg_alpha;
			const auto bg = calc_background_color(dc);

			const auto sides_width = show_sidecars ? extent_sides.cx + dc.padding2 : 0;
			const auto dups_width = show_dups ? extent_dups.cx + dc.padding2 : 0;
			const auto text_width = std::min(text_extent.cx + dc.padding2,
			                                 bounds.width() - sides_width - dups_width);

			if (bg.a > 0.0f)
			{
				auto bg_bounds = logical_bounds;
				bg_bounds.right = bg_bounds.left + text_width + sides_width + dups_width;
				dc.draw_rounded_rect(bg_bounds, bg, dc.padding1);
			}

			const ui::color text_clr(dc.colors.foreground, dc.colors.alpha);

			auto text_bounds = logical_bounds;
			text_bounds.right = text_bounds.left + text_width;
			dc.draw_text(_tl, text_bounds, text_clr, {});

			auto x = text_bounds.right;

			if (show_sidecars)
			{
				const recti bounds_sid(x, logical_bounds.top, x + extent_sides.cx + dc.padding2,
				                       logical_bounds.bottom);
				const auto bg = ui::color(ui::style::color::sidecar_background, bg_alpha);
				dc.draw_text(sides_text, bounds_sid, ui::style::font_face::dialog,
				             ui::style::text_style::single_line_center, text_clr, bg);
				x += extent_sides.cx + dc.padding2;
			}

			if (show_dups)
			{
				const recti bounds_dup(x, logical_bounds.top, x + extent_dups.cx + dc.padding2,
				                       logical_bounds.bottom);
				const auto bg = ui::color(ui::style::color::duplicate_background, bg_alpha);
				dc.draw_text(dups_text, bounds_dup, ui::style::font_face::dialog,
				             ui::style::text_style::single_line_center, text_clr, bg);
			}
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		const auto i = _item;

		if (i)
		{
			hover.elements->add(make_icon_element(icon_index::compare, view_element_style::no_break));
			hover.elements->add(std::make_shared<text_element>(tt.presence_tile,
			                                                  ui::style::font_face::dialog,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break));

			hover.elements->add(std::make_shared<text_element>(item_presence_text(_item->presence(), true),
			                                                  ui::style::font_face::dialog,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break));

			const bool in_dup_group = i->duplicates().group != 0;
			const auto sidecars = i->sidecars();
			const bool has_sidecars = !sidecars.is_empty();

			if (in_dup_group || has_sidecars)
			{
				const auto table = std::make_shared<ui::table_element>(view_element_style::center);
				table->no_shrink_col[1] = true;
				table->no_shrink_col[2] = true;

				table->add(tt.prop_name_filename, tt.prop_name_modified, tt.prop_name_size);

				if (in_dup_group)
				{
					const auto dups = _state.item_index.duplicate_list(i->duplicates().group);

					if (!dups.empty())
					{
						/*hover.elements.add(std::make_shared<text_element>(format_plural_text(tt.dup_count_fmt, i->duplicates().count),
							ui::style::font_size::dialog,
							ui::style::text_style::multiline,
							view_element_style::line_break));*/

						table->add(i->name(), platform::format_date(i->file_modified().system_to_local()),
						           prop::format_size(i->file_size()));

						for (const auto &d : dups)
						{
							if (d.first != i->path())
							{
								table->add(d.second.name,
								           ui::average(ui::style::color::duplicate_background,
								                       ui::style::color::view_text),
								           platform::format_date(d.second.file_modified.system_to_local()),
								           prop::format_size(d.second.size));
							}
						}
					}
				}

				if (has_sidecars)
				{
					const auto sidecar_parts = split(sidecars, true);
					const std::set<std::u8string, df::iless> unique(sidecar_parts.begin(), sidecar_parts.end());

					/*hover.elements.add(std::make_shared<text_element>(format_plural_text(tt.sidecar_count_fmt, unique.size()), ui::style::font_size::dialog,
						ui::style::text_style::multiline,
						view_element_style::line_break));*/

					for (const auto& part : unique)
					{
						const auto attribs = platform::file_attributes(_item->folder().combine_file(part));
						table->add(part, ui::average(ui::style::color::sidecar_background, ui::style::color::view_text),
						           platform::format_date(df::date_t(attribs.modified).system_to_local()),
						           prop::format_size(df::file_size(attribs.size)));
					}
				}


				hover.elements->add(table);
			}
		}

		/*hover.elements.add(std::make_shared<text_element>(tt.indexed_locations_makes_collection, ui::style::font_size::dialog,
			ui::style::text_style::multiline,
			view_element_style::line_break));*/

		hover.elements->add(std::make_shared<action_element>(tt.show_related));
		//hover.elements.add(std::make_shared<keyboard_accelerator_element>(tt.related_keys));
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

static std::vector<view_element_ptr> create_comp_controls(view_state& s, const df::item_element_ptr& i)
{
	std::vector<view_element_ptr> controls;
	controls.emplace_back(std::make_shared<pin_control>(s, i, true, view_element_style::none));
	controls.emplace_back(std::make_shared<rate_label_control>(s, i, true, view_element_style::none));

	if (i->file_type()->has_trait(file_traits::edit))
	{
		controls.emplace_back(std::make_shared<rating_control>(s, i, true, view_element_style::none));
	}

	controls.emplace_back(std::make_shared<delete_element>(s, i, view_element_style::none));
	controls.emplace_back(std::make_shared<unselect_element>(s, i, view_element_style::none));
	return controls;
}

void append_bullet(std::vector<view_element_ptr>& result, icon_index icon,
                   const std::vector<view_element_ptr>& children)
{
	if (!children.empty())
	{
		view_element_ptr elements = std::make_shared<view_elements>(children);
		view_element_ptr bullet = std::make_shared<bullet_element>(icon, elements, view_element_style::none);
		result.emplace_back(bullet);
	}
}

std::shared_ptr<text_element> make_rank_element(std::u8string text, const bool is_rank)
{
	auto result = std::make_shared<text_element>(text);
	if (is_rank) result->foreground_color(calc_rank_color());
	return result;
}

view_elements_ptr view_state::create_selection_controls()
{
	auto result = std::make_shared<view_elements>(view_element_style::grow | view_element_style::prime);

	auto& s = *this;
	const auto& selected = selected_items();

	const auto d = _display;

	if (d)
	{
		if (d->is_one())
		{
			std::vector<view_element_ptr> elements;

			const auto item = d->_item1;
			const auto is_media = item->is_media();
			const auto ft = item->file_type();
			const auto search_result = item->search();

			if (is_media)
			{
				const auto& info = d->_player_media_info;
				const auto md = item->metadata();

				//elements.emplace_back(std::make_shared<divider_element2>());

				if (ft->has_trait(file_traits::av))
				{
					elements.emplace_back(std::make_shared<scrubber_element>(_player, d));
				}

				auto title = std::make_shared<group_title_control>();
				title->style |= view_element_style::line_break;

				const auto name = item->path().file_name_without_extension();
				const auto title_text = md && !is_empty(md->title) ? md->title.sv() : name;

				title->elements.emplace_back(std::make_shared<play_control>(s, view_element_style::no_break));

				auto e = std::make_shared<title_link_element>(*this, item, title_text, view_element_style::grow);
				//e->id(commands::ID_EDIT);
				title->elements.emplace_back(e);

				if (d->_selected_texture1 && d->_selected_texture1->can_preview())
				{
					title->elements.emplace_back(
						std::make_shared<preview_control>(s, d->_selected_texture1, true,
						                                  view_element_style::right_justified));
				}

				if (md && md->orientation != ui::orientation::top_left && md->orientation != ui::orientation::none)
				{
					title->elements.emplace_back(make_icon_link_element(
						icon_index::orientation, commands::option_show_rotated, view_element_style::right_justified));
				}

				title->elements.emplace_back(
					std::make_shared<pin_control>(s, item, true, view_element_style::right_justified));
				title->elements.emplace_back(
					std::make_shared<rate_label_control>(s, item, true, view_element_style::right_justified));

				if (item->file_type()->has_trait(file_traits::edit))
				{
					title->elements.emplace_back(
						std::make_shared<rating_control>(s, item, true, view_element_style::right_justified));
				}

				title->elements.emplace_back(make_icon_link_element(icon_index::fit, commands::option_scale_up,
				                                                    view_element_style::right_justified));
				title->elements.emplace_back(make_icon_link_element(
					s.is_full_screen ? icon_index::fullscreen_exit : icon_index::fullscreen, commands::view_fullscreen,
					view_element_style::right_justified));

				elements.emplace_back(title);

				if (!_search.is_showing_folder())
				{
					elements.emplace_back(std::make_shared<bullet_element>(
						icon_index::folder,
						std::make_shared<link_element>(format(u8"{}\\"sv, item->folder().text()),
						                               commands::browse_open_containingfolder),
						view_element_style::none));
				}

				std::vector<view_element_ptr> file_elements;
				auto file_browser_element = std::make_shared<link_element>(
					item->path().name(), commands::browse_open_in_file_browser);
				if (search_result.is_match(prop::file_name))
					file_browser_element->set_style_bit(
						view_element_style::important, true);
				file_elements.emplace_back(file_browser_element);
				file_elements.emplace_back(std::make_shared<items_dates_control>(s, item));

				auto file_size = std::make_shared<text_element>(item->file_size().str(), view_element_style::none);
				if (search_result.is_match(prop::file_size))
					file_size->set_style_bit(
						view_element_style::important, true);
				file_elements.emplace_back(file_size);

				append_bullet(elements, icon_index::document, file_elements);

				if (md)
				{
					std::vector<view_element_ptr> pixel_elements;
					std::vector<view_element_ptr> audio_elements;

					if (md->width && md->height)
					{
						for (const auto& e : format_dims(md->width, md->height, ft, false))
						{
							pixel_elements.emplace_back(e);
						}
					}

					const auto video_codec = info.video_codec.is_empty() ? md->video_codec : info.video_codec;
					const auto pixel_format = info.pixel_format.is_empty() ? md->pixel_format : info.pixel_format;
					const auto bitrate = info.bitrate ? prop::format_bit_rate(info.bitrate) : md->bitrate.sz();
					const auto audio_codec = prop::is_null(info.audio_codec) ? md->audio_codec : info.audio_codec;
					const auto audio_channels = info.audio_channels == 0 ? md->audio_channels : info.audio_channels;
					const auto audio_sample_rate = prop::is_null(info.audio_sample_rate)
						                               ? md->audio_sample_rate
						                               : info.audio_sample_rate;
					const auto audio_sample_type = info.audio_sample_type == prop::audio_sample_t::none
						                               ? static_cast<prop::audio_sample_t>(md->audio_sample_type)
						                               : info.audio_sample_type;

					if (!is_empty(video_codec))
						pixel_elements.emplace_back(
							make_link(s, video_codec, prop::video_codec, search_result));
					if (!is_empty(pixel_format))
						pixel_elements.emplace_back(
							make_link(s, pixel_format, prop::pixel_format, search_result));
					if (!str::is_empty(bitrate))
						pixel_elements.emplace_back(
							make_link(s, bitrate, prop::bitrate, search_result));

					if (!prop::is_null(audio_sample_rate))
						audio_elements.emplace_back(
							make_link(s, prop::format_audio_sample_rate(audio_sample_rate),
							          df::search_t().with(prop::audio_sample_rate, audio_sample_rate),
							          prop::audio_sample_rate, search_result));
					if (audio_sample_type != prop::audio_sample_t::none)
						audio_elements.emplace_back(
							make_link(s, format_audio_sample_type(audio_sample_type),
							          df::search_t().with(prop::audio_sample_type, static_cast<int>(audio_sample_type)),
							          prop::audio_sample_type, search_result));
					if (!prop::is_null(audio_channels))
						audio_elements.emplace_back(
							make_link(s, prop::format_audio_channels(audio_channels),
							          df::search_t().with(prop::audio_channels, audio_channels), prop::audio_channels,
							          search_result));
					if (!prop::is_null(audio_codec))
						audio_elements.emplace_back(
							make_link(s, audio_codec, prop::audio_codec, search_result));

					append_bullet(elements, ft->icon, pixel_elements);
					append_bullet(elements, icon_index::audio, audio_elements);

					append_bullet(elements, icon_index::camera, create_camera_elements(s, md, search_result));
					append_bullet(elements, icon_index::disk, create_album_elements(s, md, search_result));
					append_bullet(elements, icon_index::person, create_artist_elements(s, md, search_result));
					append_bullet(elements, icon_index::retro, create_retro_elements(s, md, search_result));
					append_bullet(elements, icon_index::location, create_location_elements(s, md, search_result));
					append_bullet(elements, icon_index::copyright, create_copyright_elements(s, md, search_result));
					append_bullet(elements, icon_index::tag, create_tag_elements(s, md, search_result));
				}

				result->add(elements);
			}
			else
			{
				const auto table = std::make_shared<ui::table_element>(view_element_style::center);
				table->no_shrink_col[0] = true;

				std::vector<view_element_ptr> controls;
				controls.emplace_back(
					std::make_shared<title_link_element>(*this, item, item->name(), view_element_style::grow));
				controls.emplace_back(
					std::make_shared<pin_control>(s, item, true, view_element_style::right_justified));
				controls.emplace_back(
					std::make_shared<rate_label_control>(s, item, true, view_element_style::right_justified));
				//controls.emplace_back( std::make_shared<presence_element>(s, item, view_element_style::right_justified) });

				table->add(tt.sort_by_name, std::make_shared<view_elements>(controls));

				if (!_search.is_showing_folder())
				{
					table->add(tt.folder_title, item->name());
				}

				auto file_size = std::make_shared<text_element>(item->file_size().str(), view_element_style::none);
				if (search_result.is_match(prop::file_size))
					file_size->set_style_bit(
						view_element_style::important, true);
				table->add(tt.size_title, file_size);

				auto media_created = std::make_shared<text_element>(platform::format_date_time(item->media_created()),
				                                                    view_element_style::none);
				if (search_result.type == df::search_result_type::match_date && s.search().
					is_match(prop::created_utc, item->media_created()))
					media_created->set_style_bit(
						view_element_style::important, true);
				table->add(tt.prop_name_created, media_created);

				auto file_modified = std::make_shared<text_element>(platform::format_date_time(item->file_modified()),
				                                                    view_element_style::none);
				if (search_result.type == df::search_result_type::match_date && s.search().
					is_match(prop::modified, item->file_modified()))
					file_modified->set_style_bit(
						view_element_style::important, true);
				table->add(tt.prop_name_modified, file_modified);

				const auto icon = ft->icon;
				auto split = std::make_shared<split_element>(
					platform::create_segoe_md2_icon(static_cast<wchar_t>(icon)), table, view_element_style::center);
				split->padding = {4, 4};
				split->margin = {4, 4};
				result->add(split);
			}
		}
		else if (d->is_two())
		{
			auto table = std::make_shared<ui::table_element>(view_element_style::center);
			table->no_shrink_col[0] = true;

			//auto compare = std::make_shared<view_elements>(view_element_style::center);
			const auto i1 = d->_item1;
			const auto i2 = d->_item2;
			const auto search_result1 = i1->search();
			const auto search_result2 = i2->search();

			const auto controls1 = create_comp_controls(s, i1);
			const auto controls2 = create_comp_controls(s, i2);
			add_row(table, {}, controls1, controls2);

			auto t1 = std::make_shared<title_link_element>(*this, i1, i1->name(), view_element_style::grow);
			auto t2 = std::make_shared<title_link_element>(*this, i2, i2->name(), view_element_style::grow);
			table->add(tt.sort_by_name, t1, t2);

			table->add(tt.folder_title,
			           std::make_shared<search_element>(s, i1->folder().text(),
			                                            df::search_t().add_selector(i1->folder())),
			           std::make_shared<search_element>(s, i2->folder().text(),
			                                            df::search_t().add_selector(i2->folder())));

			table->add(tt.size_title,
			           make_rank_element(i1->file_size().str(), i1->file_size() > i2->file_size()),
			           make_rank_element(i2->file_size().str(), i1->file_size() < i2->file_size()));

			table->add(tt.prop_name_created,
			           make_rank_element(platform::format_date_time(i1->media_created()),
			                             i1->media_created() > i2->media_created()),
			           make_rank_element(platform::format_date_time(i2->media_created()),
			                             i1->media_created() < i2->media_created()));

			table->add(tt.prop_name_modified,
			           make_rank_element(platform::format_date_time(i1->file_modified().system_to_local()),
			                             i1->file_modified() > i2->file_modified()),
			           make_rank_element(platform::format_date_time(i2->file_modified().system_to_local()),
			                             i1->file_modified() < i2->file_modified()));

			const auto md1 = i1->metadata();
			const auto md2 = i2->metadata();

			if (md1 && md2)
			{
				if (md1->duration && md2->duration)
				{
					table->add(tt.prop_name_duration, str::format_seconds(md1->duration),
					           str::format_seconds(md2->duration));
				}

				std::vector<view_element_ptr> video1 = format_dims(md1->width, md1->height, i1->file_type(),
				                                                   md1->width * md1->height > md2->width * md2->height);
				std::vector<view_element_ptr> video2 = format_dims(md2->width, md2->height, i2->file_type(),
				                                                   md1->width * md1->height < md2->width * md2->height);
				std::vector<view_element_ptr> audio1;
				std::vector<view_element_ptr> audio2;

				add_media_elements(s, md1, video1, audio1, search_result1);
				add_media_elements(s, md2, video2, audio2, search_result2);


				add_row(table, tt.pixels_title, video1, video2);
				add_row(table, tt.audio_title, audio1, audio2);

				add_row(table, tt.prop_name_camera, create_camera_elements(s, md1, search_result1),
				        create_camera_elements(s, md2, search_result2));
				add_row(table, tt.prop_name_album, create_album_elements(s, md1, search_result1),
				        create_album_elements(s, md2, search_result2));
				add_row(table, tt.prop_name_artist, create_artist_elements(s, md1, search_result1),
				        create_artist_elements(s, md2, search_result2));
				add_row(table, tt.retro_title, create_retro_elements(s, md1, search_result1),
				        create_retro_elements(s, md2, search_result2));
				add_row(table, tt.location_title, create_location_elements(s, md1, search_result1),
				        create_location_elements(s, md2, search_result2));
				add_row(table, tt.copyright_title, create_copyright_elements(s, md1, search_result1),
				        create_copyright_elements(s, md2, search_result2));
				add_row(table, tt.tags_title, create_tag_elements(s, md1, search_result1),
				        create_tag_elements(s, md2, search_result2));

				if (!is_empty(md1->description) || !is_empty(md2->description))
				{
					table->add(tt.prop_name_description, md1->description, md2->description);
				}

				if (!is_empty(md1->comment) || !is_empty(md2->comment))
				{
					table->add(tt.prop_name_comment, md1->comment, md2->comment);
				}

				if (!is_empty(md1->synopsis) || !is_empty(md2->synopsis))
				{
					table->add(tt.prop_name_synopsis, md1->synopsis, md2->synopsis);
				}
			}

			std::u8string_view identical_text;
			

			const bool crc32_loaded = i1->crc32c() != 0 && i2->crc32c() != 0;
			const auto file_size_same = i1->file_size() == i2->file_size();			

			if (crc32_loaded && file_size_same && i1->crc32c() == i2->crc32c())
			{
				identical_text = tt.items_identical;
			}
			else if (d->_pixel_difference == ui::pixel_difference_result::equal)
			{
				identical_text = tt.pixels_identical_files_not_identical;
			}
			else if (d->_pixel_difference == ui::pixel_difference_result::not_equal)
			{
				identical_text = tt.items_not_identical;
			}

			if (!identical_text.empty())
			{
				const auto element = std::make_shared<text_element>(identical_text, ui::style::font_face::dialog,
				                                                    ui::style::text_style::multiline,
				                                                    view_element_style::line_break |
				                                                    view_element_style::center);
				element->set_style_bit(view_element_style::info, true);
				result->add(set_margin(element));
			}

			result->add(table);
		}
		else if (!_selected.is_empty())
		{
			int folder_count = 0;
			int item_count = 0;

			df::item_element_ptr first_folder;
			df::item_element_ptr first_item;

			for (const auto& i : selected.items())
			{
				if (i->is_folder())
				{
					folder_count += 1;
					if (!first_folder) first_folder = i;
				}
				else
				{
					item_count += 1;
					if (!first_item) first_item = i;
				}
			}

			const auto elements = std::make_shared<view_elements>();

			if (folder_count > 0)
			{
				std::u8string title;

				if (folder_count == 1)
				{
					title = format(tt.title_folder, first_folder->name());
				}
				else
				{
					title = format_plural_text(tt.title_folder_count_fmt, folder_count);
				}

				elements->add(std::make_shared<text_element>(title, ui::style::font_face::title,
				                                             ui::style::text_style::multiline,
				                                             view_element_style::line_break));
			}

			if (item_count > 0)
			{
				std::u8string title;

				if (item_count == 1)
				{
					title = format(u8"{}:{}"sv, first_item->file_type()->group->name, first_item->name());
				}
				else
				{
					title = format_plural_text(tt.title_item_count_fmt, item_count);
				}

				elements->add(std::make_shared<text_element>(title, ui::style::font_face::title,
				                                             ui::style::text_style::multiline,
				                                             view_element_style::line_break));
			}

			if ((folder_count + item_count) > 0)
			{
				const auto summary = selected.summary();
				elements->add(std::make_shared<summary_control>(summary, view_element_style::new_line));
			}

			auto icon = file_type::folder.icon;
			if (item_count > 0) icon = icon_index::recursive;
			result->add(std::make_shared<split_element>(platform::create_segoe_md2_icon(static_cast<wchar_t>(icon)),
			                                            elements, view_element_style::center));
		}
	}

	df::assert_true(selected.empty() || !result->is_empty());

	return result;
}

uint64_t view_state::count_total(const file_group_ref fg) const
{
	if (fg->id < static_cast<int>(_summary_total.counts.size()))
	{
		return _summary_total.counts[fg->id].count;
	}

	return 0;
}

uint64_t view_state::count_shown(const file_group_ref fg) const
{
	if (fg->id < static_cast<int>(_summary_shown.counts.size()))
	{
		return _summary_shown.counts[fg->id].count;
	}

	return 0;
}

uint64_t view_state::display_item_count() const
{
	uint32_t result = 0;

	for (const auto& b : _item_groups)
	{
		result += b->items().size();
	}

	return result;
}

void view_state::reset()
{
	close();

	_search_items.clear();
	_display_items.clear();
	_selected.clear();
	_focus.reset();
	_item_groups.clear();
	_edit_item.reset();
	_pin_item.reset();
	_display.reset();
}

static bool calc_search_is_favorite(const df::search_t& search)
{
	if (!search.is_empty())
	{
		for (auto i = 0; i < setting.search.count; i++)
		{
			if (!str::is_empty(setting.search.path[i]))
			{
				const auto fav = df::search_t::parse(setting.search.path[i]);

				if (search == fav)
				{
					return true;
				}
			}
		}
	}

	return false;
}

static bool calc_search_search_is_in_collection(const index_state& index, const df::search_t& search)
{
	const auto item_selectors = search.selectors();

	if (item_selectors.empty())
		return true;

	for (const auto& sel : item_selectors)
	{
		if (!index.is_in_collection(sel.folder()))
			return false;
	}

	return true;
}

void view_state::update_search_is_favorite_or_collection_root()
{
	_search_is_favorite = calc_search_is_favorite(_search);
	_search_is_in_collection = calc_search_search_is_in_collection(item_index, _search);
}

void view_state::update_pixel_difference()
{
	const auto d = _display;

	if (d)
	{
		d->calc_pixel_difference();
	}
}

bool view_state::escape(const view_host_base_ptr& view)
{
	const auto d = _display;

	if (d)
	{
		if (d->is_playing_slideshow())
		{
			stop_slideshow();
			return true;
		}

		if (d->zoom())
		{
			d->toggle_zoom();
			return true;
		}
	}

	if (is_full_screen)
	{
		_events.toggle_full_screen();
		return true;
	}

	if (view_mode() != view_type::items)
	{
		view_mode(view_type::items);
		return true;
	}

	if (d && d->is_playing())
	{
		stop();
		return true;
	}

	if (has_error_items())
	{
		clear_error_items(view);
		invalidate_view(view_invalid::view_redraw);
		return true;
	}

	if (search().has_recursive_selector())
	{
		auto s = search();
		s.remove_recursive();
		open(view, s, {});
		return true;
	}

	if (!_filter.is_empty())
	{
		clear_filters();
		return true;
	}

	if (search().has_related())
	{
		open(view, search().related().path);
		return true;
	}

	return false;
}

df::search_parent view_state::parent_search() const
{
	df::search_parent result;

	if (_search.has_related())
	{
		result.parent = df::search_t().add_selector(df::item_selector(_search.related().path.folder()));
		result.name = _search.related().path.name();
		result.selection.files = {_search.related().path};
	}
	else if (_search.has_selector())
	{
		if (_search.has_media_type())
		{
			result.parent = _search;
			result.parent.clear_media_type();
		}
		else if (_search.has_terms())
		{
			result.parent = _search;
			result.parent.clear_terms();
		}
		else
		{
			const auto selector = _search.selectors().front();
			const auto folder = selector.folder();

			if (folder.is_root())
			{
				result.parent = search();
			}
			else if (selector.has_wildcard() || selector.is_recursive())
			{
				result.parent = df::search_t().add_selector(selector.parent());
			}
			else
			{
				result.parent = df::search_t().add_selector(selector.parent());
				result.name = folder.name();
				result.selection.folders = {folder};
			}
		}
	}
	else if (_search.has_date())
	{
		result.parent = _search;
		result.parent.clear_date_properties();

		const auto parts = _search.find_date_parts();

		if (parts.day && parts.month)
		{
			if (parts.year)
			{
				result.parent.day(0, parts.month, parts.year, parts.target);
			}
			else
			{
				result.parent.month(parts.month, parts.target);
			}
		}
		else if (parts.year && parts.month)
		{
			result.parent.year(parts.year, parts.target);
		}
	}
	else
	{
		for (const auto& i : _selected.items())
		{
			result.parent = df::search_t().add_selector(i->folder());
			result.name = i->name();
			result.selection.files = {i->path()};
			break;
		}

		if (result.parent.is_empty())
		{
			for (const auto& f : _selected.items())
			{
				if (f->is_folder())
				{
					result.parent = df::search_t().add_selector(f->folder());
					result.name = f->name();
					result.selection.folders = { f->folder() };
					break;
				}
			}
		}
	}

	return result;
}


void view_state::open(const view_host_base_ptr& view, const std::u8string_view text)
{
	if (_search.has_selector() && text == u8"**"sv)
	{
		auto search = _search;
		const auto s = search.selectors().front();
		const df::item_selector sel(s.folder(), true, s.wildcard());

		open(view, search.clear_selectors().add_selector(sel), {});
	}
	else if (text == u8".."sv)
	{
		const auto p = parent_search();
		open(view, p.parent, make_unique_paths(p.selection));
	}
	else
	{
		const auto search = _search.parse_from_input(text);
		open(view, search, {});
	}
}

df::item_element_ptr view_state::next_unselected_item() const
{
	auto found = false;
	const auto start = _focus;

	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			if (found && !i->is_selected())
			{
				return i;
			}

			found = found || i == start;
		}
	}

	for (const auto& b : _item_groups)
	{
		for (const auto& i : b->items())
		{
			if (!i->is_selected())
			{
				return i;
			}
		}
	}

	return nullptr;
}

df::item_element_ptr view_state::end_item(bool forward) const
{
	if (!_item_groups.empty())
	{
		if (forward)
		{
			return _item_groups.back()->items().back();
		}
		return _item_groups.front()->items().front();
	}

	return nullptr;
}

df::item_element_ptr view_state::next_item(bool forward, bool extend) const
{
	auto found = false;
	const auto focus = _focus;

	if (forward)
	{
		for (auto b = _item_groups.cbegin(); b != _item_groups.cend(); ++b)
		{
			auto&& items = (*b)->items();

			for (auto i = items.cbegin(); i != items.cend(); ++i)
			{
				const auto& p = *i;

				if (found && (!extend || !p->is_selected()))
				{
					return p;
				}

				found = found || p == focus;
			}
		}
	}
	else
	{
		for (auto b = _item_groups.rbegin(); b != _item_groups.rend(); ++b)
		{
			auto&& items = (*b)->items();

			for (auto i = items.rbegin(); i != items.rend(); ++i)
			{
				const auto& p = *i;

				if (found && (!extend || !p->is_selected()))
				{
					return p;
				}

				found = found || p == focus;
			}
		}
	}

	if (forward)
	{
		for (auto b = _item_groups.cbegin(); b != _item_groups.cend(); ++b)
		{
			auto&& items = (*b)->items();

			for (auto i = items.cbegin(); i != items.cend(); ++i)
			{
				const auto& p = *i;

				if (!extend || !p->is_selected())
				{
					return *i;
				}
			}
		}
	}
	else
	{
		for (auto b = _item_groups.rbegin(); b != _item_groups.rend(); ++b)
		{
			auto&& items = (*b)->items();

			for (auto i = items.rbegin(); i != items.rend(); ++i)
			{
				const auto& p = *i;

				if (!extend || !p->is_selected())
				{
					return *i;
				}
			}
		}
	}

	return nullptr;
}

df::item_element_ptr view_state::next_group_item(bool forward) const
{
	const auto focus = selected_item_group();
	const auto start = focus.item ? focus.group : nullptr;

	auto found = start == nullptr;
	auto groups = _item_groups;

	if (groups.size() > 1)
	{
		if (!forward)
		{
			std::ranges::reverse(groups);
		}

		for (const auto& b : groups)
		{
			if (found)
			{
				for (const auto& i : b->items())
				{
					return i;
				}
			}

			found = found || b == start;
		}

		for (const auto& b : groups)
		{
			for (const auto& i : b->items())
			{
				return i;
			}
		}
	}
	else if (!groups.empty() && !groups[0]->items().empty())
	{
		return forward ? groups[0]->items().back() : groups[0]->items().front();
	}

	return nullptr;
}

df::string_counts view_state::selected_tags() const
{
	df::string_counts result;

	for (const auto& i : _selected.items())
	{
		auto md = i->metadata();

		if (md)
		{
			const auto tag_parts = split(md->tags, true);

			for (const auto& t : tag_parts)
			{
				++result[t];
			}
		}
	}

	return result;
}

void view_state::open(const view_host_base_ptr& view, const df::file_path path)
{
	df::unique_paths sel;
	sel.emplace(path);
	open(view, df::search_t().add_selector(path.folder()), sel);
}


bool view_state::open(const view_host_base_ptr& view, const df::search_t& new_search, const df::unique_paths& selection)
{
	for (const auto& s : new_search.selectors())
	{
		if (!s.can_iterate())
		{
			return false;
		}
	}

	if (!_events.can_open_search(new_search))
	{
		return false;
	}

	static std::atomic_int version;
	auto token = df::cancel_token(version);
	const auto path_changed = new_search != _search;

	if (path_changed)
	{
		stop_slideshow();
		_search = new_search;
		update_search_is_favorite_or_collection_root();
		history.history_add(new_search, _selected.ids());
	}

	_events.invalidate_view(view_invalid::address);

	auto existing = existing_items();

	_async.queue_async(async_queue::query, [this, view, new_search, existing, selection, path_changed, token]
	{
		bool is_first = true;

		auto cb = [this, view, &is_first, new_search, selection, path_changed, token](
			df::item_set append_items, const bool is_complete)
		{
			_async.queue_ui(
				[this, view, new_search, append_items = std::move(append_items), selection, is_first, is_complete,
					path_changed]
				{
					if (new_search == _search)
					{
						this->append_items(view, std::move(append_items), selection, is_first, is_complete);

						if (is_complete)
						{
							_events.search_complete(new_search, path_changed);
						}
					}
				});

			is_first = false;
		};

		item_index.query_items(new_search, existing, cb, token);
	});


	return true;
}

void view_state::open(const view_host_base_ptr& view, const df::item_element_ptr& i)
{
	if (!i->is_selected())
	{
		select(view, i, false, false, false);
	}

	if (i)
	{
		i->open(*this, view);
	}
}


void view_state::toggle_selected_item_tags(const view_host_base_ptr& view, const df::results_ptr& results,
                                           const std::u8string_view tag)
{
	record_feature_use(features::tag);

	const auto can_process = can_process_selection_and_mark_errors(view, df::process_items_type::can_save_metadata);

	if (can_process.success())
	{
		auto remove_tag = true;
		const auto items = _selected.items();

		// If exists on all items then remove it
		for (const auto& i : items)
		{
			auto tag_present = false;
			const auto md = i->metadata();

			if (md)
			{
				const auto tag_parts = split(md->tags, true);

				for (const auto& t : tag_parts)
				{
					tag_present |= str::icmp(tag, t) == 0;
				}
			}

			if (!tag_present)
			{
				remove_tag = false;
			}
		}

		metadata_edits edits;
		if (!remove_tag) edits.add_tags.add_one(tag);
		if (remove_tag) edits.remove_tags.add_one(tag);

		modify_items(results, icon_index::tag, tt.prop_name_tag, items, edits, view);
	}
	else
	{
		results->show_message(can_process.to_string());
	}
}


using existing_textures_t = df::hash_map<df::file_path, std::shared_ptr<texture_state>, df::ihash, df::ieq>;

static texture_state_ptr get_tex(existing_textures_t existing_textures, const df::item_element_ptr& item,
                                 async_strategy& as)
{
	const auto found = existing_textures.find(item->path());

	if (found != existing_textures.end())
	{
		return found->second;
	}

	return std::make_shared<texture_state>(as, item);
}

void view_state::load_display_state()
{
	const auto new_display = std::make_shared<display_state_t>(_async, _common_display_state);
	new_display->populate(*this);

	const auto d = _display;

	if (d)
	{
		const auto display_item = new_display->_item1;

		if (d->is_one() &&
			new_display->is_one() &&
			d->_selected_texture1 &&
			new_display->_selected_texture1)
		{
			new_display->_selected_texture1->clone_fade_out(d->_selected_texture1);
		}

		if (d->_session)
		{
			_player->close(d->_session, {});
		}

		if (new_display->is_one())
		{
			const auto display_file_type = display_item->file_type();
			const auto is_bitmap = display_file_type->has_trait(file_traits::bitmap);
			const auto is_av = display_file_type->has_trait(file_traits::av);
			const auto is_playable = display_item && display_file_type->is_playable();

			if (is_playable)
			{
				const auto auto_play = setting.auto_play && !is_editing() && df::file_handles_detached == 0;

				_player->open(display_item, auto_play, -1, -1, av_can_use_hw(), setting.last_played_pos,
				              [new_display](std::shared_ptr<av_session> ses)
				              {
					              new_display->update_av_session(ses);
				              });
			}
			else
			{
				_async.queue_async(async_queue::load, [new_display, display_item, display_file_type]()
				{
					df::scope_locked_inc l(df::loading_media);
					files ff;
					const auto scan_result = ff.scan_file(display_item->path(), false, display_file_type,
					                                      display_item->xmp());
					auto mi = scan_result.to_info();

					new_display->_async.queue_ui([new_display, mi]
					{
						new_display->_player_media_info = mi;
						new_display->_async.invalidate_view(
							view_invalid::view_layout |
							view_invalid::app_layout |
							view_invalid::media_elements |
							view_invalid::command_state);
					});
				});
			}

			if (!is_bitmap && !is_av)
			{
				_async.queue_async(async_queue::load, [new_display, display_item]()
				{
					df::scope_locked_inc l(df::loading_media);					

					if (display_item->file_type()->group == file_group::archive)
					{
						auto archive_items = files::list_archive(display_item->path());

						new_display->_async.queue_ui([new_display, archive_items = std::move(archive_items)]() mutable
							{
								new_display->_archive_items = std::move(archive_items);
								new_display->_async.invalidate_view(view_invalid::media_elements | view_invalid::view_layout);
							});
					}
					else
					{
						auto data = blob_from_file(display_item->path(), df::one_meg);

						new_display->_async.queue_ui([new_display, data = std::move(data)]() mutable
							{
								new_display->_selected_item_data = std::move(data);
								new_display->_async.invalidate_view(view_invalid::media_elements | view_invalid::view_layout);
							});
					}
				});
			}

			const auto mt = display_file_type;

			if (mt->group == file_group::photo)
			{
				if (files::is_raw(display_item->name()))
				{
					record_feature_use(features::show_raw);
				}
				else
				{
					record_feature_use(features::show_photo);
				}
			}
			else if (mt->group == file_group::video)
			{
				record_feature_use(features::show_video);
			}
			else if (mt->group == file_group::audio)
			{
				record_feature_use(features::show_audio);
			}
		}
		else if (new_display->is_two())
		{
		}

		if (_selected.items().size() <= 2)
		{
			queue_async(async_queue::crc, [&s = *this, new_display]
			{
				const auto two_files_size_same = new_display->_item1 && new_display->_item2 && new_display->_item1->
					file_size() == new_display->_item2->file_size();
				const auto max_load_size = 16u * static_cast<uint64_t>(df::one_meg);

				for (const auto& i : {new_display->_item1, new_display->_item2})
				{
					if (i)
					{
						if (i->crc32c() == 0 &&
							i->online_status() == df::item_online_status::disk &&
							(two_files_size_same || i->file_size().to_int64() < max_load_size))
						{
							df::scope_locked_inc l(df::loading_media);
							const auto crc = platform::file_crc32(i->path());

							if (crc)
							{
								s.item_index.save_crc(i->path(), crc);
								i->crc32c(crc);
								s.invalidate_view(view_invalid::view_layout | view_invalid::presence);
							}
						}
					}
				}
			});
		}
	}

	_display = new_display;
	_events.display_changed();

	df::assert_true(d != nullptr);
}


void media_preview_state::close()
{
	decoder1.reset();
	decoder2.reset();
}

void view_state::close()
{
	const auto d = _display;

	if (d && d->_session)
	{
		_player->close(d->_session, {});
	}

	_async.queue_media_preview([](media_preview_state& decoder) { decoder.close(); });
}

void view_state::tick(const view_host_base_ptr& view, const double time_now)
{
	//_control_hide_time += 1;

	const auto d = _display;

	if (d)
	{
		const auto is_media_playing = d->_session && d->_session->is_playing();

		if (_common_display_state._is_playing || is_media_playing)
		{
			const auto slide_show_ticks = setting.slideshow_delay * ui::default_ticks_per_second;
			const auto display_item = d->_item1;
			const auto is_photo = display_item && display_item->file_type()->has_trait(file_traits::bitmap);

			if (is_photo)
			{
				d->_next_photo_tick += 1;
				invalidate_view(view_invalid::view_redraw);
			}

			const auto is_media_end = !is_photo && d->_session && d->_session->has_ended(time_now);
			const auto is_photo_end = is_photo && d->_next_photo_tick > slide_show_ticks;

			if (is_media_end)
			{
				df::trace("view_state::tick detected media played to end"sv);

				if (d->_session)
				{
					const auto start = d->_player_media_info.start;

					_player->pause(d->_session);
					_player->seek(d->_session, start, false);
				}
			}

			if (is_media_end || is_photo_end)
			{
				const auto mode_can_play = view_mode() == view_type::items || view_mode() == view_type::media;
				const auto can_next = df::command_active == 0 && display_item && mode_can_play && has_display_items();
				auto can_continue_playing = false;

				if (can_next)
				{
					if (setting.repeat == repeat_mode::repeat_all)
					{
						// play next
						const auto next = next_item(true, false);

						if (next)
						{
							select(view, next, false, false, true);
							can_continue_playing = true;
						}
					}
					else if (setting.repeat == repeat_mode::repeat_one && is_media_end)
					{
						// Replay
						can_continue_playing = true;

						if (d->_session)
						{
							_player->play(d->_session);
						}
					}
				}

				_common_display_state._is_playing = can_continue_playing;
				d->_next_photo_tick = 0;
			}

			d->update_scrubber();
		}

		if (d->_selected_texture1)
		{
			d->_selected_texture1->refresh(d->_item1);
		}

		if (d->_selected_texture2)
		{
			d->_selected_texture2->refresh(d->_item2);
		}
	}
}


void texture_state::load_image(const df::item_element_ptr& i)
{
	if (i)
	{
		_photo_loaded = true;
		_photo_timestamp = platform::now();
		_path = i->path();

		_async.queue_async(async_queue::load, [&as = _async, t = shared_from_this(), path = _path]()
		{
			files loader;
			auto loaded = loader.load(path, true);

			if (loaded.success)
			{
				as.queue_ui([t, loaded = std::move(loaded), &as]() mutable
				{
					t->update(std::move(loaded));
					as.invalidate_view(view_invalid::view_layout | view_invalid::image_compare);
				});
			}
		});
	}
}

void texture_state::load_raw()
{
	_async.queue_async(async_queue::load_raw, [&as = _async, t = shared_from_this(), path = _path]()
	{
		df::scope_locked_inc l(t->_preview_rendering);
		files loader;
		auto loaded = loader.load(path, false);

		if (loaded.success)
		{
			as.queue_ui([t, loaded = std::move(loaded), &as]() mutable
			{
				t->update(std::move(loaded));
				as.invalidate_view(view_invalid::view_layout | view_invalid::image_compare);
			});
		}
	});
}

void texture_state::free_graphics_resources()
{
	_tex.reset();
	_vid_tex.reset();
	_zoom_texture.reset();
	_last_draw_tex.reset();
	_fade_out_tex.reset();
	_tex_invalid = true;
}

void texture_state::refresh(const df::item_element_ptr& i)
{
	if (_is_photo)
	{
		const auto out_of_date = i && (_photo_timestamp < i->file_modified() || _photo_timestamp < i->
			thumbnail_timestamp());
		const auto reload = _photo_loaded && out_of_date;
		const auto load_needed = !_photo_loaded && (_display_bounds.width() > _loaded.dimensions().cx || out_of_date);

		if (load_needed || reload)
		{
			load_image(i);
		}
	}
}

void texture_state::update(file_load_result loaded)
{
	_loaded = std::move(loaded);
	_display_dimensions = _loaded.dimensions();
	_display_orientation = _loaded.orientation();
	_tex_invalid = true;

	if (_loaded.is_preview && !setting.raw_preview)
	{
		load_raw();
	}
}

void texture_state::update(const ui::const_surface_ptr& staged_surface)
{
	if (_display_dimensions != staged_surface->dimensions() ||
		_display_orientation != staged_surface->orientation())
	{
		_async.invalidate_view(view_invalid::view_layout);
	}

	_vid_tex.reset();
	_staged_surface = staged_surface;
	_display_dimensions = staged_surface->dimensions();
	_display_orientation = staged_surface->orientation();

	_async.invalidate_view(view_invalid::view_redraw);
}

sizei texture_state::calc_scale_hint() const
{
	auto dims = _loaded.dimensions();

	if (setting.show_rotated && flips_xy(_loaded.orientation()))
	{
		std::swap(dims.cx, dims.cy);
	}

	const auto scale = ui::calc_scale_down_factor(dims, _display_bounds.extent());

	if (scale < 2)
	{
		return dims;
	}

	return {dims.cx / scale, dims.cy / scale};
}


void texture_state::draw(ui::draw_context& rc, const pointi offset, int compare_pos, bool first_texture)
{
	const auto alpha = _display_alpha_animation.val();
	const auto media_bounds = _display_bounds;

	if (!media_bounds.is_empty())
	{
		const auto scale_hint = calc_scale_hint();

		if ((_tex_invalid || _loading_scale_hint != scale_hint) && !_loaded.is_empty())
		{
			_loading_scale_hint = scale_hint;
			_tex_invalid = false;

			if (_loaded.dimensions().area() <= 1000000 || is_valid(_loaded.s))
			{
				_staged_surface = _loaded.to_surface();
			}
			else
			{
				_async.queue_async(async_queue::render, [ld = _loaded, scale_hint, t = shared_from_this()]
				{
					auto s = ld.to_surface(scale_hint);

					t->_async.queue_ui([t, s]
					{
						t->_staged_surface = s;
						t->_async.invalidate_view(view_invalid::view_redraw);
					});
				});
			}
		}

		const auto s = std::move(_staged_surface);

		if (s)
		{
			const auto tex = rc.create_texture();
			if (tex && tex->update(s) != ui::texture_update_result::failed)
			{
				_tex = tex;
				fade_out();
			}
		}
	}

	const auto tex = (_vid_tex && _vid_tex->is_valid()) ? _vid_tex : _tex;

	if (tex && tex->is_valid())
	{
		const auto tex_dims = tex->source_extent();
		const auto orientation = tex->_orientation;
		const auto sampler = calc_sampler(media_bounds.extent(), tex_dims, orientation);
		auto draw_bounds = rectd(media_bounds);
		auto tex_bounds = rectd(tex_dims);

		if (compare_pos)
		{
			if (first_texture)
			{
				const auto split_x = std::clamp(compare_pos - 1.0, draw_bounds.X + 1.0, draw_bounds.right() - 1.0) -
					draw_bounds.X;
				draw_bounds.Width = split_x;

				const auto cx = (tex_dims.cx * draw_bounds.Width) / media_bounds.width();
				const auto cy = (tex_dims.cy * draw_bounds.Width) / media_bounds.width();

				if (flips_xy(orientation))
				{
					if (is_inverted(orientation))
					{
						tex_bounds.Y = tex_bounds.bottom() - cy;
					}

					tex_bounds.Height = cy;
				}
				else
				{
					if (is_inverted(orientation))
					{
						tex_bounds.X = tex_bounds.right() - cx;
					}

					tex_bounds.Width = cx;
				}
			}
			else
			{
				const auto split_x = std::clamp(compare_pos + 1.0, draw_bounds.X + 1.0, draw_bounds.right() - 1.0) -
					draw_bounds.X;
				draw_bounds.X += split_x;
				draw_bounds.Width -= split_x;

				const auto cx = (tex_dims.cx * draw_bounds.Width) / media_bounds.width();
				const auto cy = (tex_dims.cy * draw_bounds.Width) / media_bounds.width();

				if (flips_xy(orientation))
				{
					if (!is_inverted(orientation))
					{
						tex_bounds.Y = tex_bounds.bottom() - cy;
					}

					tex_bounds.Height = cy;
				}
				else
				{
					if (!is_inverted(orientation))
					{
						tex_bounds.X = tex_bounds.right() - cx;
					}

					tex_bounds.Width = cx;
				}
			}
		}

		//if (_tex->has_alpha() && !compare_pos)
		//if (_tex->has_alpha())
		/*{
			auto clr_bg = render::color(0, alpha);
			rc.draw_rect(draw_bounds.offset(offset).inflate(pad), clr_bg, clr_bg);
		}*/

		const auto dst_quad = setting.show_rotated
			                      ? quadd(draw_bounds.offset(offset)).transform(to_simple_transform(orientation))
			                      : draw_bounds.offset(offset);

		rc.draw_texture(tex, dst_quad, tex_bounds.round(), alpha, sampler);

		draw_texture_info(rc, media_bounds.offset(offset), tex, orientation, sampler, alpha);

		_last_draw_tex = tex;
		_last_draw_rect = media_bounds;
		_last_draw_source_rect = tex_bounds.round();
		_last_drawn_sampler = sampler;
	}

	if (_fade_out_tex && _fade_out_tex->is_valid())
	{
		const auto fade_out_alpha = _fade_out_alpha_animation.val();
		const auto fade_orientation = _fade_out_tex->_orientation;

		const auto fade_dst_quad = setting.show_rotated
			                           ? quadd(_fade_out_rect.offset(offset)).transform(
				                           to_simple_transform(fade_orientation))
			                           : _fade_out_rect.offset(offset);

		if (alpha < 1.0f)
		{
			rc.draw_texture(_fade_out_tex, fade_dst_quad, _fade_out_source_rect, fade_out_alpha, _fade_out_sampler);
		}
		else
		{
			_fade_out_tex.reset();
		}
	}
}

void texture_state::layout(ui::measure_context& mc, const recti bounds, const df::item_element_ptr& i)
{
	_display_bounds = bounds;
	refresh(i);
}


ui::texture_ptr texture_state::zoom_texture(ui::draw_context& rc, const sizei extent)
{
	if (!_zoom_texture || _zoom_timestamp != _photo_timestamp)
	{
		const auto t = rc.create_texture();

		if (t && t->update(_loaded.to_surface(sizei(200, 200))) != ui::texture_update_result::failed)
		{
			_zoom_texture = t;
			_zoom_timestamp = _photo_timestamp;
		}
	}

	if (!_zoom_texture)
	{
		throw app_exception(u8"Failed to create zoom texture."s);
	}

	return _zoom_texture;
}

void display_state_t::populate(view_state& state)
{
	existing_textures_t existing_textures;

	const auto d = state._display;

	if (d)
	{
		if (d->_item1 && d->_selected_texture1) existing_textures[d->_item1->path()] = d->_selected_texture1;
		if (d->_item2 && d->_selected_texture2) existing_textures[d->_item2->path()] = d->_selected_texture2;
	}

	const auto& selected_items = state.selected_items();
	const auto item_count = selected_items.items().size();
	const auto no_folders = !selected_items.has_folders();

	_is_one = item_count == 1 && no_folders;
	_is_two = item_count == 2 && no_folders;
	_is_multi = item_count >= 3 && no_folders;
	_can_zoom = false;

	if (_is_one)
	{
		_item1 = selected_items.items()[0];
		_selected_texture1 = get_tex(existing_textures, _item1, _async);
		_can_zoom = _item1->file_type()->has_trait(file_traits::zoom);

		const auto b = state.item_group(_item1);

		if (b.group)
		{
			auto pos = 1u;

			for (const auto& i : b.group->items())
			{
				if (_item1 != i)
				{
					++pos;
				}
				else
				{
					break;
				}
			}

			_item_pos = pos;
			_break_count = b.group->items().size();
			_total_count = state.display_item_count();
		}
	}
	else if (_is_two)
	{
		_item1 = selected_items.items()[0];
		_item2 = selected_items.items()[1];

		_selected_texture1 = get_tex(existing_textures, _item1, _async);
		_selected_texture2 = get_tex(existing_textures, _item2, _async);

		const auto file_type1 = _item1->file_type();
		const auto file_type2 = _item2->file_type();

		_can_compare = file_type1->has_trait(file_traits::bitmap) && file_type2->has_trait(
			file_traits::bitmap);

		_is_compare_video =
			file_type1->has_trait(file_traits::av) &&
			file_type2->has_trait(file_traits::av) &&
			_item1->online_status() == df::item_online_status::disk &&
			_item2->online_status() == df::item_online_status::disk;
	}
	else if (_is_multi)
	{
		_images = state.selected_items().thumbs(5);

		files ff;
		const sizei max_dims(256, 256);

		for (const auto& image : _images)
		{
			_surfaces.emplace_back(ff.image_to_surface(image, ui::scale_dimensions(image->dimensions(), max_dims)));
			if (_surfaces.size() >= max_surfaces) break;
		}
	}

	if (!_can_zoom)
	{
		// If we can zoom then reset zoom setting.
		_common._zoom = false;
	}
}

render_valid display_state_t::update_for_present(const double time_now) const
{
	auto result = render_valid::valid;

	if (_session)
	{
		if (_session->update_for_present(time_now))
		{
			if (_audio_verts)
			{
				auto vert_bounds = _audio_element_bounds;

				if (vert_bounds.width() > 100)
				{
					vert_bounds.left += 20;
					vert_bounds.right -= 20;
				}

				const auto has_tex = _player_media_info.has_video && _selected_texture1 && _selected_texture1->_vid_tex;
				vert_bounds.top = vert_bounds.top + (has_tex ? df::mul_div(_audio_element_bounds.height(), 3, 5) : 0);

				const auto tex_bounds = ui::scale_dimensions(sizei{512, 256}, vert_bounds);
				_session->update_visualizer(_audio_verts, tex_bounds, _audio_element_offset, _audio_element_alpha,
				                            time_now);

				result = render_valid::present;
			}

			if (_selected_texture1 && _selected_texture1->_vid_tex)
			{
				const auto update_texture_result = _session->update_texture(_selected_texture1->_vid_tex);

				if (update_texture_result != render_valid::valid)
				{
					if (!_selected_texture1->_is_video_tex)
					{
						_selected_texture1->_is_video_tex = true;
						_selected_texture1->fade_out();
					}

					if (_selected_texture1->display_dimensions() != _player_media_info.display_dimensions ||
						_selected_texture1->display_orientation() != _player_media_info.display_orientation)
					{
						_selected_texture1->_display_dimensions = _player_media_info.display_dimensions;
						_selected_texture1->_display_orientation = _player_media_info.display_orientation;

						_async.invalidate_view(view_invalid::view_layout);
					}
				}

				if (update_texture_result == render_valid::invalid || result == render_valid::invalid)
				{
					result = render_valid::invalid;
				}
				else if (update_texture_result == render_valid::present || result == render_valid::present)
				{
					result = render_valid::present;
				}
			}
		}
	}

	return result;
}

//texture_state::texture_state(async_strategy& async, const ui::const_image_ptr& i) : _async(async)
//{
//	_is_icon = true;
//	_image = i;
//	_display_dimensions = i.dimensions();
//
//	_display_alpha_animation.reset(0.0f, 1.0f);
//}

texture_state::texture_state(async_strategy& async, const df::item_element_ptr& i) : _async(async)
{
	_loaded.clear();
	_loaded.i = i->thumbnail();
	_loaded.success = !_loaded.is_empty();

	const auto mt = i->file_type();
	const auto md = i->metadata();

	if (md)
	{
		_display_dimensions = md->dimensions();
		_display_orientation = md->orientation;
	}

	_is_photo = mt->has_trait(file_traits::bitmap);
	_is_raw = mt->has_trait(file_traits::raw);
	_tex_invalid = true;

	_display_alpha_animation.reset(0.0f, 1.0f);

	if (_display_dimensions.is_empty() && ui::is_valid(_loaded.i))
	{
		_display_dimensions = _loaded.i->dimensions();
	}
}


void draw_texture_info(ui::draw_context& rc, const recti media_bounds, const ui::texture_ptr& tex,
                       ui::orientation orientation, ui::texture_sampler sampler, const float alpha)
{
	if (tex && setting.show_debug_info && media_bounds.width() > 64)
	{
		auto tex_dims = tex->dimensions();

		if (setting.show_rotated && flips_xy(orientation))
		{
			std::swap(tex_dims.cx, tex_dims.cy);
		}

		auto r = media_bounds;

		const auto text = str::format(u8"{} {}x{} -> {}x{} {}"sv, to_string(tex->format()), tex_dims.cx, tex_dims.cy,
		                              r.width(), r.height(), to_string(sampler));

		r.left += 8;
		r.bottom = r.top + rc.text_line_height(ui::style::font_face::dialog) + 8;
		rc.draw_text(text, r, ui::style::font_face::dialog, ui::style::text_style::single_line,
		             ui::color(0xFFFFFF, alpha), {});
	}
}

ui::texture_sampler calc_sampler(const sizei draw_extent, const sizei texture_extent,
                                 const ui::orientation& orientation)
{
	auto dims = texture_extent;

	if (setting.show_rotated && flips_xy(orientation))
	{
		std::swap(dims.cx, dims.cy);
	}

	const auto sx = draw_extent.cx / static_cast<double>(dims.cx);

	if (sx > 1.05)
	{
		return ui::texture_sampler::bilinear;
	}

	if (sx < 0.95)
	{
		return ui::texture_sampler::bicubic;
	}

	return ui::texture_sampler::point;
}

sizei texture_state::calc_display_dimensions() const
{
	auto dims = _display_dimensions;

	if (setting.show_rotated && flips_xy(_display_orientation))
	{
		std::swap(dims.cx, dims.cy);
	}

	return dims;
}

void texture_state::clear()
{
	_tex.reset();
	_last_draw_tex.reset();
	_fade_out_tex.reset();
}

bool texture_state::is_empty() const
{
	return _tex == nullptr;
}

inline void texture_state::fade_out()
{
	_fade_out_tex = _last_draw_tex;
	_fade_out_rect = _last_draw_rect;
	_fade_out_source_rect = _last_draw_source_rect;
	_fade_out_sampler = _last_drawn_sampler;

	_fade_out_alpha_animation.reset(_display_alpha_animation.val(), 0.0f);
}

void texture_state::display_dimensions(const sizei dims)
{
	if (_display_dimensions != dims)
	{
		_display_dimensions = dims;
	}
}

void texture_state::clone_fade_out(const std::shared_ptr<texture_state>& other)
{
	_fade_out_tex = other->_last_draw_tex;
	_fade_out_rect = other->_last_draw_rect;
	_fade_out_source_rect = other->_last_draw_source_rect;

	_fade_out_alpha_animation.reset(_display_alpha_animation.val(), 0.0f);
}

df::process_result view_state::can_process_selection_and_mark_errors(const view_host_base_ptr& view,
                                                                     df::process_items_type file_types) const
{
	clear_error_items(view);
	return _selected.can_process(file_types, true, view);
}

bool view_state::can_process_selection(const view_host_base_ptr& view, df::process_items_type file_types) const
{
	return !_selected.empty() && _selected.can_process(file_types, false, view).success();
}


df::folder_counts view_state::known_folders() const
{
	df::folder_counts results;

	const auto distinct_folders = item_index.distinct_folders();

	for (const auto& path : distinct_folders)
	{
		results[path] = 255 - std::min(static_cast<int>(path.size()), 255); // short file names more likely
	}

	for (const auto& i : _search_items.items())
	{
		if (i->is_folder())
		{
			results[i->folder()] += 1 << 8;
		}
	}

	recent_folders.count(results, 2 << 8);
	history.count_folders(results, 2 << 8);

	const auto write_folder = df::folder_path(setting.write_folder);
	const auto import_destination_path = df::folder_path(setting.import.destination_path);

	if (!import_destination_path.is_empty()) results[import_destination_path] += 2 << 8;
	if (!write_folder.is_empty()) results[write_folder] += 2 << 8;

	return results;
}


static df::folder_path next_folder(const df::folder_path current, bool is_forward)
{
	const auto parent = current.parent();
	auto result = parent;
	auto peers = platform::select_folders(df::item_selector(parent), setting.show_hidden);

	if (!peers.empty())
	{
		std::ranges::sort(peers, [](auto&& l, auto&& r) { return str::icmp(l.name, r.name) < 0; });

		if (is_forward)
		{
			auto found = false;

			for (const auto& peer : peers)
			{
				if (found)
				{
					result = parent.combine(peer.name);
					break;
				}

				found = current.name() == peer.name;
			}
		}
		else
		{
			auto prev = peers.front();

			for (const auto& peer : peers)
			{
				if (current.name() == peer.name)
				{
					result = parent.combine(prev.name);
					break;
				}

				prev = peer;
			}
		}
	}

	return result;
}

std::u8string view_state::next_path(bool forward) const
{
	auto a = _search;
	const auto vm = _view_mode;

	if (vm == view_type::media)
	{
		const auto i = next_item(forward, false);

		if (i)
		{
			return std::u8string(i->name());
		}
	}
	else if (a.has_selector())
	{
		const auto folder = a.selectors().front().folder();

		if (folder.exists())
		{
			return std::u8string(next_folder(folder, forward).text());
		}
	}
	else if (a.has_date())
	{
		a.next_date(forward);
		return a.text();
	}
	else if (a.is_empty())
	{
		return (vm == view_type::items) ? u8"About"s : u8"Items"s;
	}

	return {};
}

void view_state::open_next_path(const view_host_base_ptr& view, bool forward)
{
	auto a = _search;
	const auto vm = _view_mode;

	if (vm == view_type::media)
	{
		stop();
		select_next(view, forward, false, false);
	}
	else if (a.has_selector())
	{
		const auto folder = a.selectors().front().folder();

		if (folder.exists())
		{
			open(view, a.clear_selectors().add_selector(next_folder(folder, forward)), {});
		}
	}
	else if (a.has_date())
	{
		a.next_date(forward);
		open(view, a, {});
	}
	else if (a.is_empty())
	{
		if (vm != view_type::items)
		{
			view_mode(view_type::items);
		}
	}

	stop_slideshow();
}


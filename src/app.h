// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class sidebar_host;
class search_auto_complete;
class view_state;
class edit_view_controls;
class test_view;
class items_view;
class edit_view;
class media_view;
class rename_view;
class sync_view;
class import_view;

constexpr std::array media_volumes = { 999, 777, 555, 333, 0 };
extern icon_index volumes_icons[5];

std::vector<std::pair<std::u8string_view, std::u8string>> calc_app_info(const index_state& index, bool include_state);


class view_frame final : public std::enable_shared_from_this<view_frame>, public view_host
{
public:
	using this_type = view_frame;

	view_state& _state;
	ui::frame_ptr _frame;
	ui::control_frame_ptr _owner;

	pointi _pan_start_loc;

	std::shared_ptr<view_base> _view;

	int _fps_counter = 0;
	int _fps_avg = 0;
	int _fps_second = 0;

public:
	view_frame(view_state& s) : _state(s)
	{
	}

	double frame_render_time = 0.0;

	void init(const ui::control_frame_ptr& owner)
	{
		ui::frame_style fs;
		fs.hardware_accelerated = true;
		fs.colors = {
			ui::style::color::view_background, ui::style::color::view_text, ui::style::color::view_selected_background
		};
		fs.can_focus = true;
		fs.can_drop = true;
		_frame = owner->create_frame(weak_from_this(), fs);
		_owner = owner;
	}

	void tick() override
	{
		if (_active_controller)
		{
			_active_controller->tick();
		}
	}

	void view(std::shared_ptr<view_base> v)
	{
		_view = std::move(v);
	}

	bool is_occluded() const
	{
		return _frame->is_occluded();
	}

	void invalidate_controller()
	{
		_controller_invalid = true;
		update_controller(_frame->cursor_location());
	}

	int fps() const
	{
		return _fps_avg;
	}

	void calc_fps(double time)
	{
		const auto sec = static_cast<int>(time);

		if (sec == _fps_second)
		{
			_fps_counter += 1;
		}
		else
		{
			_fps_avg = (_fps_avg + _fps_counter + 1) / 2;
			_fps_counter = 1;
			_fps_second = sec;
		}
	}

	void on_window_destroy() override
	{
	}

	void on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized) override
	{
		_extent = extent;

		if (_view)
		{
			_view->layout(mc, extent);
		}
	}

	void on_window_paint(ui::draw_context& dc) override
	{
		const auto time_now = df::now();

		calc_fps(time_now);
		dc.time_now = time_now;
		dc.colors.alpha = 1.0;
		dc.colors.overlay_alpha = 1.0;

		_view->render(dc, _active_controller);

		if (_active_controller)
		{
			_active_controller->draw(dc);
		}

		draw_status(dc);

		if (setting.show_debug_info && _active_controller)
		{
			const auto c = ui::color(1.0f, 0.0f, 0.0f, 1.0f);
			const auto pad = df::round(2 * dc.scale_factor);
			dc.draw_border(_controller_bounds, _controller_bounds.inflate(pad), c, c);
		}

		if (setting.show_shadow)
		{
			dc.draw_edge_shadows(dc.colors.alpha);
		}

		frame_render_time = (frame_render_time + df::now() - time_now) / 2.0;
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
		const int z_delta = delta / 2;
		_view->mouse_wheel(loc, z_delta, keys);
		update_controller(loc);
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	}

	void pan_start(const pointi start_loc) override
	{
		_view->pan_start(_pan_start_loc);
	}

	void pan(const pointi start_loc, const pointi current_loc) override
	{
		_view->pan(_pan_start_loc, current_loc);
	}

	void pan_end(const pointi start_loc, const pointi final_loc) override
	{
		_view->pan_end(_pan_start_loc, final_loc);
	}

	void on_mouse_other_button_up(const ui::other_mouse_button& button, const pointi loc,
	                              const ui::key_state keys) override
	{
		switch (button)
		{
		case ui::other_mouse_button::xb1:
			_state.browse_back(shared_from_this());
			break;
		case ui::other_mouse_button::xb2:
			_state.browse_forward(shared_from_this());
			break;
		default:
			break;
		}
	}

	void focus_changed(bool has_focus, const ui::control_base_ptr& child) override
	{
		df::trace(str::format(u8"render_window::focus {}"sv, has_focus));
		_view->focus(has_focus);
		_frame->invalidate();
	}

	const ui::frame_ptr frame() override
	{
		return _frame;
	}

	const ui::control_frame_ptr owner() override
	{
		return _owner;
	}

	void controller_changed() override
	{
		_state.invalidate_view(view_invalid::tooltip);
	}

	view_controller_ptr controller_from_location(const pointi loc) override
	{
		return _view->controller_from_location(shared_from_this(), loc);
	}

	void invoke(const commands cmd) override
	{
		_state.invoke(cmd);
	}

	bool is_command_checked(const commands cmd) override
	{
		return _state.is_command_checked(cmd);
	}

	void track_menu(const recti bounds, const std::vector<ui::command_ptr>& commands) override
	{
		_state.track_menu(_frame, bounds.offset(_frame->window_bounds().top_left()), commands);
	}

	void layout()
	{
		if (_frame)
		{
			_frame->layout();
		}
	}

	void invalidate_element(const view_element_ptr& e) override
	{
		if (_frame)
		{
			_frame->invalidate();
		}
	}

	std::u8string _status_title;
	std::u8string _status_text;

	void update_status(std::u8string_view title, std::u8string_view text);
	void clear_status();
	void draw_status(ui::draw_context& dc);

	platform::drop_effect
	drag_over(const platform::clipboard_data& data, const ui::key_state keys, const pointi loc) override
	{
		auto result = platform::drop_effect::none;

		if (df::dragging_items == 0)
		{
			if (_view->is_over_items(loc))
			{
				const auto has_save_path = _state.search().is_showing_folder();

				if (has_save_path)
				{
					if (data.has_drop_files())
					{
						const auto dest_path = _state.save_path();
						const auto desc = data.files_description();
						const auto is_copy = !keys.shift && (keys.control || desc.preferred_drop_effect ==
							platform::drop_effect::copy);

						update_status(is_copy ? tt.menu_copy : tt.menu_move,
						              format(u8"{}\n{}\n{}"sv, desc.first_name, tt.copy_to_join, dest_path.text()));

						result = is_copy ? platform::drop_effect::copy : platform::drop_effect::move;
					}
					else if (data.has_bitmap())
					{
						update_status(tt.save_new_photo, {});
						result = platform::drop_effect::copy;
					}
				}
			}
			else if (data.has_drop_files())
			{
				update_status(tt.open_title, data.first_path().str());
				result = platform::drop_effect::link;
			}
		}

		return result;
	}

	platform::drop_effect drag_drop(platform::clipboard_data& data, const ui::key_state keys, const pointi loc) override
	{
		auto result = platform::drop_effect::none;

		if (df::dragging_items == 0)
		{
			if (_view->is_over_items(loc))
			{
				const auto has_save_path = _state.search().is_showing_folder();

				if (has_save_path)
				{
					if (data.has_drop_files())
					{
						const auto save_path = _state.save_path();
						const auto desc = data.files_description();
						const auto is_copy = !keys.shift && (keys.control || desc.preferred_drop_effect ==
							platform::drop_effect::copy);
						const auto drop_action = is_copy ? platform::drop_effect::copy : platform::drop_effect::move;

						detach_file_handles detach(_state);
						const auto drop_result = data.drop_files(save_path, drop_action);

						if (drop_result.success())
						{
							_state.open(shared_from_this(), _state.search(),
							            make_unique_paths(drop_result.created_files));
							result = drop_action;
						}
					}
					else if (data.has_bitmap())
					{
						const auto save_result = data.save_bitmap(_state.save_path(), u8"dropped"sv, false);

						if (save_result.success())
						{
							result = platform::drop_effect::copy;
							_state.open(shared_from_this(), _state.search(),
							            make_unique_paths(save_result.created_files));
						}
					}
				}
			}
			else if (data.has_drop_files())
			{
				const auto path = data.first_path();
				_state.open(shared_from_this(), path);
				result = platform::drop_effect::link;
			}
		}

		drag_leave();
		return result;
	}

	void drag_leave() override
	{
		update_status({}, {});
		_status_title.clear();
		_status_text.clear();
	}

	void invalidate_view(const view_invalid invalid) override
	{
		_state.invalidate_view(invalid);
	}
};

struct lerp_animate
{
	int val = 0;
	int target = 0;

	bool step()
	{
		if (val != target)
		{
			const auto step = (target - val) / 2;
			val = (step == 0) ? target : val + step;
			return true;
		}

		return false;
	}

	ui::color32 lerp(const ui::color32& c1, const ui::color32& c2)
	{
		return (val == 0) ? c1 : ui::lerp(c1, c2, val);
	}
};


class app_frame final :
	public state_strategy,
	public async_strategy,
	public ui::app,
	public ui::frame_host,
	public std::enable_shared_from_this<app_frame>
{
public:
	using this_type = app_frame;

	std::shared_ptr<av_player> _player;
	view_state _state;
	edit_view_state _edit_view_state;
	location_cache _locations;
	index_state _item_index;
	database _db;
	const ui::plat_app_ptr _pa;

	platform::queue<std::function<void()>> _ui_queue;
	platform::task_queue cloud_task_queue;
	platform::task_queue database_task_queue;
	platform::task_queue index_task_queue;
	platform::task_queue load_task_queue;
	platform::task_queue load_raw_task_queue;
	platform::task_queue location_task_queue;
	platform::task_queue sidebar_task_queue;
	platform::task_queue web_task_queue;
	platform::task_queue predictions_task_queue;
	platform::task_queue summary_task_queue;
	platform::task_queue presence_task_queue;
	platform::task_queue auto_complete_task_queue;
	platform::task_queue query_task_queue;
	platform::task_queue render_task_queue;
	platform::task_queue scan_folder_task_queue;
	platform::task_queue scan_modified_items_task_queue;
	platform::task_queue scan_displayed_items_task_queue;
	platform::task_queue crc_task_queue;
	platform::task_queue work_task_queue;
	platform::threads _threads;

	ui::control_frame_ptr _app_frame;

	ui::toolbar_ptr _navigate1;
	ui::toolbar_ptr _navigate2;
	ui::toolbar_ptr _navigate3;

	ui::toolbar_ptr _media_edit_commands;
	ui::toolbar_ptr _rename_commands;
	ui::toolbar_ptr _import_commands;
	ui::toolbar_ptr _sync_commands;
	ui::toolbar_ptr _test_commands;

	ui::edit_ptr _search_edit;
	ui::edit_ptr _filter_edit;
	ui::toolbar_ptr _tools;
	ui::toolbar_ptr _sorting;

	std::u8string _last_favorite_tags;

	std::shared_ptr<search_auto_complete> _search_completes;
	ui::list_window_ptr _search_predictions_frame;

	bool _search_has_focus = false;
	bool _filter_has_focus = false;
	bool _view_has_focus = false;
	bool _toolbar_has_focus = false;
	bool _nav_has_focus = false;
	bool _view_controls_have_focus = false;

	view_controls_host_ptr _view_controls;	
	std::shared_ptr<sidebar_host> _sidebar;
	std::shared_ptr<view_frame> _view_frame;

	std::shared_ptr<test_view> _view_test;
	std::shared_ptr<rename_view> _view_rename;
	std::shared_ptr<import_view> _view_import;
	std::shared_ptr<sync_view> _view_sync;

	std::shared_ptr<items_view> _view_items;
	std::shared_ptr<edit_view> _view_edit;
	std::shared_ptr<media_view> _view_media;
	std::shared_ptr<view_base> _view;

	ui::bubble_window_ptr _bubble;

	int _frame_delay = 0;
	group_by _starting_group_order = group_by::file_type;
	sort_by _starting_sort_order = sort_by::def;

	sizei _extent;
	recti _view_bounds;
	recti _status_bounds;
	recti _title_bounds;
	int _sorting_width = 0;
	std::u8string saved_current_search;
	bool _is_active = false;

	commands_map _commands;
	ui::command_ptr _hover_command;
	recti _hover_command_bounds;

	std::atomic<view_invalid> _invalids = view_invalid::none;

	lerp_animate _search_color_lerp;
	std::atomic_int _pin_search;

	view_hover_element _hover;

	ui::texture_ptr _logo_tex;

	app_frame(ui::plat_app_ptr pa);
	~app_frame() override;

	void free_graphics_resources(bool items_only, bool offscreen_only) override;
	void track_menu(const ui::frame_ptr& parent, recti bounds, const std::vector<ui::command_ptr>& commands) override;
	void idle() override;
	void hide_search_predictions();
	bool key_down(char32_t key, ui::key_state keys) override;
	void create_toolbars();
	void crash(df::file_path dump_file_path) override;
	std::u8string restart_cmd_line() override;
	void save_recovery_state() override;
	void invalidate_view(view_invalid invalid) override;
	void invoke(commands id) override;
	void invoke(const command_info_ptr& c);
	void toggle_full_screen() override;
	bool can_open_search(const df::search_t& path) override;
	void folder_changed() override;
	void dpi_changed() override;
	void on_window_layout(ui::measure_context& mc, sizei extent, bool is_minimized) override;
	void on_window_paint(ui::draw_context& dc) override;
	void activate(bool is_active) override;
	void app_fail(std::u8string_view message, std::u8string_view more_text) override;
	void invalidate_status();
	void update_overlay();
	void tick() override;
	void prepare_frame() override;
	void update_tooltip();
	void item_focus_changed(const df::item_element_ptr& focus, const df::item_element_ptr& previous) override;
	void make_visible(const df::item_element_ptr& i) override;
	bool is_command_checked(commands cmd) override;
	void element_broadcast(const view_element_event& event) override;
	recti calc_search_popup_bounds() const;
	void layout(ui::measure_context& mc);
	void complete_pending_events();
	void load_options(const platform::setting_file_ptr& store);
	void display_changed() override;
	void open_default_folder();
	void view_changed(view_type m) override;
	void play_state_changed(bool play) override;
	void search_complete(const df::search_t& path, bool path_changed) override;
	void save_options(bool search_only = false);
	void reload();
	void stage_update();
	void queue_ui(std::function<void()> f) override;
	void queue_async(async_queue q, std::function<void()> f) override;
	void queue_location(std::function<void(location_cache&)>) override;
	void queue_database(std::function<void(database&)> f) override;
	void web_service_cache(std::u8string key, std::function<void(const std::u8string&)> f) override;
	void web_service_cache(std::u8string key, std::u8string value) override;
	void queue_media_preview(std::function<void(media_preview_state&)> f) override;
	static icon_index repeat_toggle_icon();
	bool update_toolbar_text(commands cc, const std::u8string& text);
	void update_button_state(bool resize);
	void update_address() const;
	void toggle_volume();
	icon_index sound_icon() const;
	void def_command(commands id, command_group group, icon_index icon, std::u8string_view text,
	                 std::u8string_view tooltip = {});
	void update_command_text();
	void initialise_commands();
	command_info_ptr find_or_create_command_info(commands id);
	void add_command_invoke(commands id, std::function<void()> invoke);
	ui::command_ptr find_command(commands id) const;
	void tooltip(view_hover_element& hover, commands id);
	void search_edit_change(const std::u8string& text);
	void filter_edit_change(const std::u8string& text);
	void delete_items(const df::item_set& items) override;

	void focus_view() override;
	bool can_exit() override;
	bool pre_init() override;
	void start_workers();
	void update_font_size();
	bool init(std::u8string_view command_line) override;
	void final_exit() override;
	void exit() override;
	void system_event(ui::os_event_type ost) override;

	void on_mouse_move(const pointi loc, const bool is_tracking) override
	{
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
	}

	void on_mouse_leave(const pointi loc) override
	{
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
	}

	void on_mouse_left_button_double_click(const pointi loc, const ui::key_state keys) override
	{
	}

	void on_mouse_other_button_up(const ui::other_mouse_button& button, const pointi loc,
	                              const ui::key_state keys) override
	{
	}

	void pan_start(const pointi start_loc) override
	{
	}

	void pan(const pointi start_loc, const pointi current_loc) override
	{
	}

	void pan_end(const pointi start_loc, const pointi final_loc) override
	{
	}

	void focus_search(bool has_focus);
	void focus_changed(bool has_focus, const ui::control_base_ptr& child) override;
	void on_window_destroy() override;
	void command_hover(const ui::command_ptr& c, recti window_bounds) override;

	std::vector<ui::command_ptr> menu(pointi loc) override;
};

using app_frame_ptr = std::shared_ptr<app_frame>;


class pause_media
{
	view_state& _state;
	display_state_ptr _display;

	bool _is_playable = false;
	bool _is_playing = false;

public:
	pause_media(view_state& s) : _state(s)
	{
		_display = s.display_state();

		if (_is_playable)
		{
			_is_playable = _display->can_play_media();
			_is_playing = _display->is_playing_media();

			if (_is_playing && _display->_session)
			{
				_state._player->pause(_display->_session);
			}

			_display->stop_slideshow();
		}
	}

	~pause_media()
	{
		if (_is_playable && _is_playing)
		{
			if (_display == _state.display_state() && _display->_item1 && _display->_session)
			{
				const auto path = _display->_item1->path();

				if (path.exists())
				{
					_state._player->play(_display->_session);
				}
			}
		}
	}

	pause_media(const pause_media& other) = delete;
	pause_media(pause_media&& other) noexcept = delete;
	pause_media& operator=(const pause_media& other) = delete;
	pause_media& operator=(pause_media&& other) noexcept = delete;
};

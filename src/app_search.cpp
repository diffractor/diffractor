// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Main application implementation. Handles app initialization, window management,
// command processing, toolbar/menu creation, and coordinates all background worker threads.

#include "pch.h"

#include "app_text.h"

#include "model_location.h"
#include "model_index.h"
#include "model_db.h"
#include "model.h"

#include "ui_dialog.h"

#include "app.h"

#include <utility>

#include "app_match.h"


class search_auto_complete final : public std::enable_shared_from_this<search_auto_complete>,
                                   public ui::complete_strategy_t
{
	view_state& _state;
	ui::edit_ptr _edit;
	df::string_counts _known;
	ui::auto_complete_match_ptr _selected;

public:
	std::string no_results_message() override
	{
		return std::string(tt.type_to_search);
	}

	void clear()
	{
		_known.clear();
	}

	search_auto_complete(view_state& s, ui::edit_ptr edit) : _state(s), _edit(std::move(edit))
	{
		resize_to_show_results = true;
		auto_select_first = false;
		max_predictions = 25u;
	}

	void initialise(std::function<void(const ui::auto_complete_results&)> complete) override
	{
		_selected.reset();
		_known.clear();
		_state.history.count_strings(_known, 1);
		_state.recent_folders.count_strings(_known, 1);
		//_state.recent_apps.count_strings(_recents, 1);
		_state.recent_tags.count_strings(_known, 1, "#");
		_state.recent_tags.count_strings(_known, 1);
		//_state.recent_locations.count_strings(_recents, 1);
		if (!setting.write_folder.empty()) ++_known[str::cache(setting.write_folder)];

		for (const auto ks : prop::key_scopes())
		{
			++_known[str::cache(std::format("with:{}", ks.scope))];
			++_known[str::cache(std::format("without:{}", ks.scope))];
		}
	}

	int calc_auto_complete_word_weight(const index_state::auto_complete_word& word) const
	{
		auto result = 1;
		const auto found = _known.find(word.text);
		if (found != _known.end()) result += found->second;
		return result;
	}

	int calc_auto_complete_word_weight(const index_state::auto_complete_folder& path,
	                                   const std::string_view query) const
	{
		auto result = 1;
		const auto found = _known.find(path.path.text());
		if (found != _known.end()) result += found->second;
		if (str::icmp(path.path.name().sv(), query) == 0) result += 5;
		return result;
	}

	void search(const std::string& query, std::function<void(const ui::auto_complete_results&)> complete) override
	{
		_state._async.queue_async(async_queue::auto_complete, [this, query, complete]
		{
			ui::auto_complete_results found;

			const auto query_parts = str::split(query, true);

			if (query_parts.empty() || str::icmp(query, _state.search().text()) == 0)
			{
				auto recents = _state.recent_searches.items();
				std::ranges::reverse(recents);

				for (const auto& path : recents)
				{
					found.emplace_back(std::make_shared<folder_match>(*this, df::folder_path(path)));
				}
			}
			else
			{
				if (df::is_path(str::trim(query)))
				{
					df::folder_path folder(query);

					if (!folder.exists())
					{
						folder = folder.parent();
					}

					const df::item_selector selector(folder);

					for (const auto& fi : platform::select_folders(selector, setting.show_hidden))
					{
						ui::match_highlights m;
						auto path = folder.combine(fi.name);

						if (find_auto_complete(query_parts, path.text(), true, m))
						{
							found.emplace_back(std::make_shared<folder_match>(*this, path, m, 10));
						}
					}
				}

				if (found.size() < max_predictions)
				{
					const auto found_folders = _state.item_index.auto_complete_folders(query, max_predictions);

					for (const auto& path : found_folders)
					{
						found.emplace_back(std::make_shared<folder_match>(
							*this, path.path, path.highlights, calc_auto_complete_word_weight(path, query)));
					}
				}

				if (found.size() < max_predictions)
				{
					const auto found_words = _state.item_index.auto_complete_words(query, max_predictions);

					for (const auto& word : found_words)
					{
						found.emplace_back(std::make_shared<text_match>(*this, word.text, std::string{},
						                                                word.highlights,
						                                                calc_auto_complete_word_weight(word)));
					}
				}
			}

			if (found.size() < max_predictions && query_parts.size() > 1)
			{
				const auto query_back = query_parts.back();
				const auto lead_text = query.substr(0, query.rfind(query_back));
				const auto found_words = _state.item_index.auto_complete_words(query_back, max_predictions);

				for (const auto& word : found_words)
				{
					found.emplace_back(std::make_shared<text_match>(*this, word.text, lead_text, word.highlights,
					                                                calc_auto_complete_word_weight(word)));
				}

				if (found.size() < max_predictions)
				{
					const auto found_folders = _state.item_index.auto_complete_folders(query_back, max_predictions);

					for (const auto& path : found_folders)
					{
						found.emplace_back(std::make_shared<folder_match>(
							*this, path.path, lead_text, path.highlights, calc_auto_complete_word_weight(path, query)));
					}
				}
			}

			if (found.size() < max_predictions && !query.empty() && std::iswspace(query.back()))
			{
				if (found.size() < max_predictions)
				{
					for (const auto& word : _known)
					{
						found.emplace_back(std::make_shared<text_match>(*this, std::string(word.first), query,
						                                                ui::match_highlights{}, word.second));
					}
				}

				if (found.size() < max_predictions)
				{
					const auto found_groups = _state.item_index.auto_complete_words("@", max_predictions);

					for (const auto& word : found_groups)
					{
						found.emplace_back(std::make_shared<text_match>(*this, word.text, query, word.highlights,
						                                                calc_auto_complete_word_weight(word)));
					}
				}

				if (found.size() < max_predictions)
				{
					const auto found_tags = _state.item_index.auto_complete_words("#", max_predictions);

					for (const auto& word : found_tags)
					{
						found.emplace_back(std::make_shared<text_match>(*this, word.text, query, word.highlights,
						                                                calc_auto_complete_word_weight(word)));
					}
				}
			}

			if (found.size() < max_predictions)
			{
				for (const auto& k : _known)
				{
					const auto trimmed = str::trim(query);

					if (str::starts(k.first, trimmed) &&
						found.size() < max_predictions)
					{
						found.emplace_back(
							std::make_shared<text_match>(*this, std::string(k.first), std::string{}));
					}
				}
			}

			if (found.size() > max_predictions) found.resize(max_predictions);
			_state.queue_ui([complete, found] { complete(found); });
		});
	}

	void selected(const ui::auto_complete_match_ptr& i, const select_type st) override
	{
		if (i)
		{
			const auto text = i->edit_text();

			if (st == select_type::arrow)
			{
				_edit->window_text(text);
			}

			if (st == select_type::click || st == select_type::double_click)
			{
				_state.recent_searches.add(text);
				_state.open({}, text);
				_state._events.focus_view();
			}

			_selected = i;
		}
	}

	ui::auto_complete_match_ptr selected() const override
	{
		return _selected;
	}
};

void app_frame::hide_search_predictions()
{
	if (_search_completes && _search_predictions_frame && _search_predictions_frame->_frame)
	{
		_search_completes->clear();
		_search_predictions_frame->_frame->show(false);
		focus_view();
	}
}

void app_frame::focus_search(const bool has_focus)
{
	if (_search_has_focus != has_focus)
	{
		_search_has_focus = has_focus;

		if (!_search_predictions_frame)
		{
			ui::frame_style style;
			style.child = false;
			style.no_focus = true;
			style.colors = {
				ui::style::color::toolbar_background, ui::style::color::view_text,
				ui::style::color::view_selected_background
			};

			_search_predictions_frame = std::make_shared<ui::list_frame>();
			_search_predictions_frame->init(_app_frame, _search_completes, style);
		}

		if (_search_has_focus)
		{
			_search_completes->initialise([&p = _search_predictions_frame](const ui::auto_complete_results& results)
			{
				p->show_results(results);
			});
			_search_predictions_frame->selected(nullptr, ui::complete_strategy_t::select_type::init);
			_search_predictions_frame->search({});
			_search_predictions_frame->_frame->window_bounds(calc_search_popup_bounds(), true);
		}
		else
		{
			hide_search_predictions();
		}
	}
}

void app_frame::search_enter()
{
	const auto selected = _search_completes->selected();
	const auto text = selected ? selected->edit_text() : _search_edit->window_text();
	_state.open(_view_frame, text);
	focus_view();
}

void app_frame::init_search()
{
	_search_completes = std::make_shared<search_auto_complete>(_state, _search_edit);
}

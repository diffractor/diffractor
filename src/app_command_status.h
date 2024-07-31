// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "ui_elements.h"
#include "ui_dialog.h"


struct folder_scan_item;

class command_status final : public std::enable_shared_from_this<command_status>, public df::status_i
{
private:
	async_strategy& _async;
	int _cancel_ver_inital_val;

	dialog_ptr _dlg;
	icon_index _icon;
	std::u8string _title;

	std::shared_ptr<ui::progress_control> _progress;
	std::shared_ptr<ui::close_control> _cancel;

	bool _closed = false;
	bool _completed = false;
	int64_t _pos = 0;
	int64_t _total = 0;

	int64_t _processed_count = 0;
	int64_t _failed_count = 0;
	int64_t _ignore_count = 0;

	std::u8string _processed_first_name;
	std::u8string _failed_first_name;
	std::u8string _ignore_first_name;

	std::u8string _error_message;
	std::u8string _message;

public:
	command_status(async_strategy& as, const dialog_ptr& dlg, const icon_index& icon, const std::u8string_view title,
		const size_t total) :
		_async(as),
		_dlg(dlg),
		_icon(icon),
		_title(title),
		_progress(std::make_shared<ui::progress_control>(dlg->_frame, title)),
		_cancel(std::make_shared<ui::close_control>(dlg->_frame, [h = dlg->_frame]() { ++ui::cancel_gen; h->close(true); }, tt.button_cancel)),
		_total(static_cast<int>(total)),
		_cancel_ver_inital_val(ui::cancel_gen.load())
	{
		const std::vector<view_element_ptr> controls{
			set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title, std::u8string{})),
			std::make_shared<divider_element>(),
			_progress,
			_cancel
		};

		_dlg->show_controls(controls, { 44 }, { 44 });
	}

	~command_status() override = default;

	void total(size_t t)
	{
		_total = t;
	}

	void show_message(const std::u8string_view message) override
	{
		if (!_closed)
		{
			const std::vector<view_element_ptr> controls = {
				set_margin(std::make_shared<ui::title_control2>(_dlg->_frame, _icon, _title, message)),
				std::make_shared<divider_element>(),
				std::make_shared<ui::close_control>(_dlg->_frame)
			};

			_dlg->show_modal(controls);
		}
	}

	void message(const std::u8string_view message)
	{
		_async.queue_ui([t = shared_from_this(), m = std::u8string(message)]()
			{
				if (!t->_closed)
				{
					t->_progress->message(m, t->_pos, t->_total);
				}
			});
	}

	void message(const std::u8string_view message, int64_t pos, int64_t total) override
	{
		_async.queue_ui([t = shared_from_this(), m = std::u8string(message), pos, total]()
			{
				if (!t->_closed)
				{
					t->_progress->message(m, pos, total);
				}
			});
	}

	bool is_canceled() const override
	{
		return _dlg->is_canceled() || ui::cancel_gen.load() != _cancel_ver_inital_val;
	}

	bool has_failures() const override
	{
		return _failed_count > 0;
	}

	bool has_successes() const
	{
		return _processed_count > 0;
	}

	void start_item(const std::u8string_view name) override
	{
		message(name, _pos++, _total);
	}

	void end_item(const std::u8string_view name, const item_status status) override
	{
		_async.queue_ui([t = shared_from_this(), n = std::u8string(name), status]()
			{
				if (status == item_status::success)
				{
					if (t->_processed_first_name.empty()) t->_processed_first_name = n;
					++t->_processed_count;
				}
				else if (status == item_status::fail)
				{
					if (t->_failed_first_name.empty()) t->_failed_first_name = n;
					++t->_failed_count;
				}
				else if (status == item_status::ignore)
				{
					if (t->_ignore_first_name.empty()) t->_failed_first_name = n;
					++t->_ignore_count;
				}
			});
	}

	void show_errors() override
	{
		_async.queue_ui([t = shared_from_this()]()
			{
				t->_completed = true;
				t->show_results_or_close();
			});
	}

	void wait_for_complete() const override
	{
		while (!_completed)
		{
			_dlg->wait_for_close();
		}
	}

	void abort(const std::u8string_view error_message) override
	{
		_async.queue_ui([t = shared_from_this(), em = std::u8string(error_message)]()
			{
				t->_completed = true;
				t->_error_message = em;
				t->show_results_or_close();
			});
	}

	void complete(const std::u8string_view message = {}) override
	{
		_async.queue_ui([t = shared_from_this(), m = std::u8string(message)]()
			{
				t->_completed = true;
				t->_message = m;
				t->show_results_or_close();
			});
	}

private:
	std::u8string format_processed_message() const
	{
		std::u8string result;

		if (_dlg->is_canceled())
		{
			result += tt.cancel_was_pressed_after;
		}

		result += _total
			? format_plural_text(tt.processed_x_of_x_fmt, _processed_first_name, _processed_count,
				df::file_size{}, _total)
			: format_plural_text(tt.processed_fmt, _processed_first_name, _processed_count, {}, _total);

		if (_failed_count > 0)
		{
			result += u8" "sv;
			result += format_plural_text(tt.failed_items_fmt, _failed_first_name, _failed_count, {}, _total);
		}

		if (_ignore_count > 0)
		{
			result += u8" "sv;
			result += format_plural_text(tt.ignored_fmt, _ignore_first_name, _ignore_count, {}, _total);
		}

		return result;
	}

	void show_results_or_close()
	{
		df::assert_true(ui::is_ui_thread());

		if (!_closed)
		{
			_cancel->text(tt.button_close);

			if (_dlg->is_canceled() || _failed_count > 0)
			{
				std::vector<view_element_ptr> controls;

				controls.emplace_back(set_margin(std::make_shared<ui::title_control>(_icon, _title)));
				controls.emplace_back(set_margin(std::make_shared<text_element>(format_processed_message())));

				if (!_message.empty())
				{
					controls.emplace_back(set_margin(std::make_shared<text_element>(_message)));
				}

				if (!_error_message.empty())
				{
					controls.emplace_back(set_margin(std::make_shared<text_element>(_error_message)));
				}

				controls.emplace_back(_cancel);

				_dlg->show_controls(controls, { 44 }, { 44 });
			}
			else if (!_message.empty())
			{
				const std::vector<view_element_ptr> controls = {
					set_margin(std::make_shared<ui::title_control2>(_dlg->_frame, _icon, _title, _message)),
					std::make_shared<divider_element>(),
					_cancel
				};

				_dlg->show_controls(controls, { 44 }, { 44 });
			}
			else
			{
				_dlg->close(false);
				_closed = true;
			}
		}
	}
};

using item_results_ptr = std::shared_ptr<command_status>;

static item_status to_status(const platform::file_op_result_code code)
{
	switch (code)
	{
	case platform::file_op_result_code::OK: return item_status::success;
	case platform::file_op_result_code::CANCELLED: return item_status::cancel;
	case platform::file_op_result_code::FAILED: return item_status::fail;
	default:;
	}

	return item_status::success;
}

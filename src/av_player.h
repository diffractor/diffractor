// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "files.h"
#include "model_items.h"
#include "av_format.h"
#include "av_sound.h"
#include "av_visualizer.h"

enum class av_play_state
{
	closed,
	playing,
	paused,
	detached,
};

struct av_times
{
	double video = 0.0;
	double audio = 0.0;
	double pos = 0.0;
};

enum class render_valid
{
	invalid,
	present,
	valid
};

class av_host
{
public:
	virtual ~av_host() = default;

	virtual void invalidate_view(view_invalid invalid) = 0;
	virtual void queue_ui(std::function<void()> f) = 0;
};


class av_session final : public std::enable_shared_from_this<av_session>
{
private:
	av_visualizer _visualizer;

	file_type_ref _mt = nullptr;

	av_packet_queue _audio_packets;
	av_packet_queue _video_packets;

	av_frame_queue _audio_frames;
	av_frame_queue _video_frames;

	av_format_decoder _decoder;

	double _end_time = 0;
	double _last_frame_decoded = 0;
	double _start_time = 0;
	double _audio_buffer_seconds = 0;

	volatile int _seek_gen = 1;


	const int max_loop_iteration = 256;

	av_play_state _state = av_play_state::detached;

	volatile bool _scrubbing = false;

	double _last_seek = 0.0;
	double _time_offset = 0.0;
	double _audio_buffer_time = 0.0;

	volatile bool _reset_time_offset = false;
	volatile bool _pending_time_sync = false;

	int _volume = 1000;
	bool _mute = false;

	df::file_item_ptr _item;

	mutable double _last_frame_time = -1;
	mutable double _last_texture_time = -1;
	mutable pointi _last_frame_offset;

	mutable platform::mutex _decoder_rw;

	av_frame_ptr _frame;
	ui::orientation _orientation = ui::orientation::none;

	std::shared_ptr<audio_resampler> _playback_resampler;
	std::shared_ptr<audio_resampler> _vis_resampler;

	av_host& _host;

public:
	av_session(av_host& host) : _host(host)
	{
	}

	~av_session()
	{
		close();
	}

	int video_stream_id() const
	{
		return _decoder.video_stream_id();
	}

	int audio_stream_id() const
	{
		return _decoder.audio_stream_id();
	}

	double pos(const double time_now) const
	{
		if (_scrubbing || _pending_time_sync) return _last_seek;
		if (_state != av_play_state::playing) return _last_frame_time;
		return time_now - _time_offset;
	}

	void adjust_volume()
	{
		_volume = (_scrubbing || _mute) ? 0 : setting.media_volume;
	}

	void toggle_mute()
	{
		_mute = !_mute;
		_volume = _scrubbing || _mute ? 0 : setting.media_volume;
	}

	bool is_open() const
	{
		return _state == av_play_state::playing || _state == av_play_state::paused;
	}

	bool is_closed() const
	{
		return _state == av_play_state::closed || _state == av_play_state::detached;
	}

	bool is_playing() const
	{
		return _state == av_play_state::playing;
	}

	double last_frame_time() const
	{
		return _last_frame_time;
	}

	void state(av_play_state new_state);

	friend class av_player;

	audio_info_t audio_format() const
	{
		return _decoder.audio_info();
	}

	av_media_info info() const
	{
		return _decoder.info();
	}

	void create_resampler()
	{
		if (!_playback_resampler)
		{
			_playback_resampler = _decoder.make_audio_resampler();
		}

		if (!_vis_resampler)
		{
			_vis_resampler = _decoder.make_audio_resampler();
		}
	}

	bool open(const df::file_item_ptr& item, const bool auto_play, const int video_track, const int audio_track,
	          const bool can_use_hw, const
	          bool use_last_played_pos)
	{
		df::assert_true(_state == av_play_state::detached);

		platform::exclusive_lock lock_dec(_decoder_rw);

		const auto result = _decoder.open(item->path());

		if (result)
		{
			_decoder.init_streams(video_track, audio_track, can_use_hw, false);

			const auto start_time = _decoder.start_time();
			const auto end_time = _decoder.end_time();
			const auto duration = end_time - start_time;

			//df::log(__FUNCTION__, "start_time " << start_time;
			//df::log(__FUNCTION__, "end_time" << end_time;

			_audio_buffer_time = 0;
			_end_time = end_time;
			_item = item;
			_last_frame_time = 0;
			_last_seek = 0;
			_mt = _item->file_type();
			_scrubbing = false;
			_start_time = start_time;
			_state = auto_play ? av_play_state::playing : av_play_state::paused;
			_time_offset = df::now();
			_pending_time_sync = true;
			_reset_time_offset = !_decoder.has_audio(); // && !scrubbing;
			_seek_gen = 1;

			_frame.reset();

			/*file_scanner sr;
			_decoder.extract_metadata(sr);
			_metadata = sr.to_props();*/
			_orientation = _decoder.calc_orientation();

			_playback_resampler.reset();
			_vis_resampler.reset();

			create_resampler();

			if (item && use_last_played_pos)
			{
				const auto starting_pos = item->media_position();

				if ((starting_pos > (start_time + 2.0)) && (starting_pos < (end_time - 5.0)) && (duration > 10.0))
				{
					_decoder.seek(starting_pos, _last_frame_decoded);
					_last_seek = starting_pos;
				}
			}
		}

		_video_packets.clear();
		_audio_packets.clear();
		_video_frames.clear();
		_audio_frames.clear();

		return result;
	}

	void close()
	{
		if (_state != av_play_state::closed)
		{
			state(av_play_state::closed);

			platform::exclusive_lock lock(_decoder_rw);

			if (_item)
			{
				_item->media_position(_last_frame_time);
			}

			_decoder.close();
			_mt = nullptr;
			_audio_packets.clear();
			_end_time = 0;
			_last_frame_decoded = 0;
			_start_time = 0;
			_video_packets.clear();
			_frame.reset();
			_video_frames.clear();
			_audio_frames.clear();
			_playback_resampler.reset();
			_vis_resampler.reset();
		}
	}

	void seek(double pos, bool scrubbing);

	void process_video(platform::thread_event& _read_event)
	{
		auto loop_iteration = 0;

		// (_audio_buffer_seconds < 10 && _audio_frames.size() < 64)
		if (_decoder.has_video()) // && (_audio_buffer_end_time - 10) > _last_tick_time)
		{
			while (_video_frames.should_receive())
			{
				{
					platform::shared_lock lock_dec(_decoder_rw);
					_decoder.receive_frames(_video_packets, _video_frames);
				}

				if (_video_packets.should_receive())
				{
					_read_event.set();
					break;
				}

				if (_state == av_play_state::closed || df::is_closing || ++loop_iteration > max_loop_iteration)
				{
					break;
				}
			}
		}
	}

	void process_audio(audio_buffer& playback_buffer, audio_buffer& vis_buffer, platform::thread_event& read_event)
	{
		auto loop_iteration = 0;

		if (_decoder.has_audio())
		{
			while ((_audio_frames.should_receive() || playback_buffer.should_fill() || _seek_gen != playback_buffer.
				generation()))
			{
				if (_audio_frames.should_receive())
				{
					platform::shared_lock lock(_decoder_rw);
					_decoder.receive_frames(_audio_packets, _audio_frames);
				}

				if (playback_buffer.should_fill() || _seek_gen != playback_buffer.generation())
				{
					av_frame_ptr frame;

					if (_audio_frames.pop(frame))
					{
						const auto eof = av_frame_is_eof(frame);

						if (eof)
						{
							playback_buffer.append_blank_second();
							vis_buffer.append_blank_second();
						}
						else
						{
							const auto sv = av_seek_gen_from_frame(frame);

							if (sv == _seek_gen)
							{
								const auto pr = _playback_resampler;
								const auto vr = _vis_resampler;

								if (pr && vr)
								{
									if (sv != playback_buffer.generation())
									{
										pr->flush();
										vr->flush();

										df::trace(str::format(u8"Player clear audio_buffer on seek_ver {}", sv));
									}

									pr->resample(frame, playback_buffer);
									vr->resample(frame, vis_buffer);
								}
							}
						}

						_end_time = std::max(_end_time, playback_buffer.end_time());
						_audio_buffer_seconds = playback_buffer.seconds();
					}
				}

				if (_audio_packets.should_receive())
				{
					read_event.set();
					break;
				}

				if (_state == av_play_state::closed || df::is_closing || ++loop_iteration > max_loop_iteration)
				{
					break;
				}
			}
		}
	}

	void process_io(platform::thread_event& video_event, platform::thread_event& audio_event);

	bool has_ended(const double time_now) const
	{
		return is_playing() && pos(time_now) >= _end_time;
	}

	static double time_distance(const double l, const double r)
	{
		return l < r ? r - l : l - r;
	}

	bool update_for_present(const double time_now)
	{
		bool result = false;
		const auto time = this->pos(time_now);

		if (_item)
		{
			_item->media_position(time);
		}

		const auto is_playing_audio = _mt && (_mt->group->traits && file_type_traits::visualize_audio);

		if (is_playing_audio)
		{
			result = _visualizer.step(time);
		}

		av_frame_ptr f;
		auto frame_popped = false;
		const auto seek_ver_invalid = av_seek_gen_from_frame(_frame) != _seek_gen;
		const auto current_ft = av_time_from_frame(_frame);

		if (seek_ver_invalid)
		{
			frame_popped = _video_frames.pop(f);
		}
		else if (_state == av_play_state::playing && !_scrubbing && !_pending_time_sync && !_reset_time_offset)
		{
			const auto front_time = _video_frames.front_time();

			if (time_distance(current_ft, time) > time_distance(front_time, time) || df::equiv(current_ft, front_time))
			{
				frame_popped = _video_frames.pop(f);
			}
		}

		if (frame_popped && !av_is_frame_empty(f))
		{
			result = true;

			_frame = std::move(f);
			_last_frame_decoded = av_time_from_frame(_frame);
			_end_time = std::max(_end_time, _last_frame_decoded);
		}

		if (_reset_time_offset && av_seek_gen_from_frame(_frame) == _seek_gen)
		{
			_time_offset = time_now - _last_frame_decoded;
			_pending_time_sync = false;
			_reset_time_offset = false;
		}

		return result;
	}

	file_load_result capture_first_frame() const
	{
		platform::shared_lock lock_dec(_decoder_rw);

		if (_frame)
		{
			return _decoder.render_frame(_frame);
		}

		return {};
	}

	double time() const
	{
		return av_time_from_frame(_frame);
	}

	void update_visualizer(const ui::vertices_ptr& verts, const recti rect, const pointi offset, const float alpha,
	                       const double time_now)
	{
		const auto time = pos(time_now);
		_last_frame_time = time;
		_last_frame_offset = offset;
		_visualizer.render(verts, rect, offset, alpha, time);
	}

	bool capture_frame(const av_frame& frame_in, ui::const_image_ptr& result);

	render_valid update_texture(const ui::texture_ptr& texture)
	{
		auto result = render_valid::valid;
		const auto vf = _frame;

		if (vf && texture)
		{
			const auto time = av_time_from_frame(vf);
			const auto timestamp_matches_last = is_equal(_last_texture_time, time);
			const auto needs_render = !timestamp_matches_last || !texture;

			if (needs_render)
			{
				const auto update_result = texture->update(vf);

				if (update_result != ui::texture_update_result::failed)
				{
					_last_frame_time = time;
					_last_texture_time = time;

					if (update_result == ui::texture_update_result::tex_created) result = render_valid::invalid;
					if (update_result == ui::texture_update_result::tex_updated) result = render_valid::present;
				}
			}
		}

		return result;
	}

	av_times times(const double now) const
	{
		av_times result;
		result.pos = pos(now);
		result.audio = _audio_buffer_time;
		result.video = _last_texture_time;
		return result;
	}

	friend class av_player;
};

class av_player final : public std::enable_shared_from_this<av_player>
{
private:
	mutable platform::thread_event _video_event;
	mutable platform::thread_event _audio_event;
	mutable platform::thread_event _read_event;

	platform::mutex _queue_mutex;
	platform::mutex _thread_mutex;
	std::shared_ptr<av_session> _thread_session;

	mutable std::u8string _audio_device_id;
	mutable std::u8string _play_audio_device_id;

	std::deque<std::function<void(std::shared_ptr<av_player>)>> _q;
	av_host& _host;

public:
	av_player(av_host& host) :
		_video_event(false, false),
		_audio_event(false, false),
		_read_event(false, false), _host(host)
	{
	}

	void queue(std::function<void(std::shared_ptr<av_player>)> f)
	{
		{
			platform::exclusive_lock lock(_queue_mutex);
			_q.emplace_back(std::move(f));
		}

		_read_event.set();
	}

	void seek(const std::shared_ptr<av_session>& ses, const double pos, bool scrubbing)
	{
		queue([ses, pos, scrubbing](const std::shared_ptr<av_player>& p) { p->seek_impl(ses, pos, scrubbing); });
	}

	void open(const df::file_item_ptr& item, const bool auto_play, const int video_track, const int audio_track,
	          const bool can_use_hw, const
	          bool use_last_played_pos, const std::function<void(std::shared_ptr<av_session>)>& cb)
	{
		queue([item, auto_play, video_track, audio_track, can_use_hw, use_last_played_pos, cb](
			const std::shared_ptr<av_player>& p)
			{
				df::scope_locked_inc l(df::loading_media);
				auto ses = p->open_impl(item, auto_play, video_track, audio_track, can_use_hw, use_last_played_pos);
				if (cb) p->_host.queue_ui([cb, ses]() { cb(ses); });
			});
	}

	void close(const std::shared_ptr<av_session>& ses, const std::function<void()>& cb)
	{
		queue([ses, cb](const std::shared_ptr<av_player>& p) { p->close_impl(ses, cb); });
	}

	void play(const std::shared_ptr<av_session>& ses)
	{
		queue([ses](const std::shared_ptr<av_player>& p) { p->play_impl(ses); });
	}

	void pause(const std::shared_ptr<av_session>& ses)
	{
		queue([ses](const std::shared_ptr<av_player>& p) { p->pause_impl(ses); });
	}

private:
	void seek_impl(const std::shared_ptr<av_session>& ses, const double pos, bool scrubbing) const
	{
		ses->seek(pos, scrubbing);

		_read_event.set();
		_video_event.set();
		_audio_event.set();
	}

	std::shared_ptr<av_session> open_impl(const df::file_item_ptr& item, const bool auto_play, const int video_track,
	                                      const int audio_track, const bool can_use_hw, const
	                                      bool use_last_played_pos)
	{
		const auto ses = std::make_shared<av_session>(_host);
		const auto open_result = ses->open(item, auto_play, video_track, audio_track, can_use_hw, use_last_played_pos);
		auto result = open_result ? ses : nullptr;

		if (_thread_session != result)
		{
			//const auto old_session = _thread_session;
			_thread_session = result;

			/*if (old_session && old_session->is_open())
			{
				close_impl(old_session);
			}*/
		}

		_read_event.set();
		_audio_event.set();
		_video_event.set();

		return result;
	}

	void close_impl(const std::shared_ptr<av_session>& ses, const std::function<void()>& cb)
	{
		//platform::lock lock(_read_mutex);

		if (ses)
		{
			ses->close();
		}

		if (_thread_session == ses)
		{
			_thread_session.reset();
		}

		_read_event.set();
		_audio_event.set();
		_video_event.set();

		if (cb)
		{
			_host.queue_ui(cb);
		}
	}

public:
	void audio_device_id(const std::u8string_view id) const
	{
		platform::exclusive_lock lock(_thread_mutex);
		_audio_device_id = id;
		_audio_event.set();
	}

	std::u8string_view audio_device_id() const
	{
		platform::exclusive_lock lock(_thread_mutex);
		return _audio_device_id;
	}

	void play_audio_device_id(const std::u8string_view id) const
	{
		platform::exclusive_lock lock(_thread_mutex);
		_play_audio_device_id = id;
		_audio_event.set();
	}

	std::u8string_view play_audio_device_id() const
	{
		platform::exclusive_lock lock(_thread_mutex);
		return _audio_device_id.empty() ? _play_audio_device_id : _audio_device_id;
	}

	void decode_video() const
	{
		const std::vector<std::reference_wrapper<platform::thread_event>> events = {platform::event_exit, _video_event};
		std::shared_ptr<av_session> session;

		while (!df::is_closing)
		{
			wait_for(events, 50, false);

			if (session != _thread_session)
			{
				session = _thread_session;
			}

			if (session)
			{
				session->process_video(_read_event);
			}
		}
	}

	void decode_audio() const
	{
		std::u8string device_id;
		av_audio_device_ptr ds;
		audio_buffer playback_buffer;
		audio_buffer vis_buffer;

		audio_info_t vis_format;
		vis_format.channel_layout = av_get_def_channel_layout(2);
		vis_format.sample_fmt = prop::audio_sample_t::signed_16bit;
		vis_format.sample_rate = 48000;

		vis_buffer.init(vis_format);

		const std::vector<std::reference_wrapper<platform::thread_event>> events = {platform::event_exit, _audio_event};

		auto has_audio = false;
		auto base_time = 0.0;
		auto playback_gen = 0;
		auto vis_gen = 0;
		auto need_create_device = false;

		std::shared_ptr<av_session> session;

		while (!df::is_closing)
		{
			auto e = wait_for(events, 100, false);

			//switch (e)
			//{
			//default:
			//case 0: // Exit
			//case 1: // kick
			//	break;
			//}

			if (audio_device_id() != device_id)
			{
				device_id = audio_device_id();

				if (ds)
				{
					playback_buffer.clear();
					vis_buffer.clear();
					need_create_device = true;
				}
			}

			if (session != _thread_session)
			{
				playback_buffer.clear();
				vis_buffer.clear();

				session = _thread_session;
				playback_gen = 0;
				vis_gen = 0;

				if (session)
				{
					has_audio = session->_decoder.has_audio();

					if (has_audio)
					{
						if (!ds)
						{
							need_create_device = true;
						}
						else
						{
							ds->reset();
						}
					}
				}
				else
				{
					has_audio = false;
					ds.reset();
				}
			}

			if (need_create_device)
			{
				if (ds)
				{
					ds->reset();
					ds.reset();
				}

				ds = create_av_audio_device(device_id);

				if (ds)
				{
					play_audio_device_id(ds->id());
					playback_buffer.init(ds->format());
				}
				else
				{
					playback_buffer.clear();
				}

				need_create_device = false;
				playback_gen = 0;
				vis_gen = 0;
			}

			if (has_audio && session)
			{
				if (ds)
				{
					const auto ds_is_stopped = ds->is_stopped();
					const auto is_playing_state = session->_state == av_play_state::playing;
					const auto should_be_stopped = !is_playing_state || session->_scrubbing;

					if (!ds_is_stopped && should_be_stopped)
					{
						ds->stop();
					}

					session->process_audio(playback_buffer, vis_buffer, _read_event);

					if (!playback_buffer.is_empty())
					{
						if (playback_gen != playback_buffer.generation())
						{
							base_time = playback_buffer.start_time();
							playback_gen = playback_buffer.generation();
							ds->reset();
							session->_pending_time_sync = true;
							session->_time_offset = df::now() - base_time;
						}

						if (!should_be_stopped)
						{
							ds->write(playback_buffer);

							if (ds_is_stopped)
							{
								//df::log(__FUNCTION__, "av_player.start-ds " << ds.time();
								ds->start();
							}

							if (session->_pending_time_sync)
							{
								const auto time = base_time + ds->time();
								//df::log(__FUNCTION__, str::format(u8"sound.clock {}", time));
								session->_time_offset = df::now() - time;
								session->_pending_time_sync = false;
							}
						}
					}

					if (vis_buffer.used_bytes() >= session->_visualizer.min_sample_bytes())
					{
						if (vis_gen != session->_seek_gen)
						{
							vis_gen = vis_buffer.generation();
							session->_visualizer.clear();
						}

						session->_visualizer.update(vis_buffer);
					}

					if (!ds->is_stopped())
					{
						ds->volume(session->_volume / 1000.0);
					}

					if (ds->is_device_lost())
					{
						session->state(av_play_state::paused);
						audio_device_id({});
						need_create_device = true;
					}

					session->_audio_buffer_time = playback_buffer.start_time();
				}
			}
		}
	}

	void reading()
	{
		const auto player = shared_from_this();
		const std::vector<std::reference_wrapper<platform::thread_event>> events = {platform::event_exit, _read_event};

		while (!df::is_closing)
		{
			const auto e = wait_for(events, 50, false);

			switch (e)
			{
			case 0: // Exit
				break;
			case 1: // kick
				break;
			}

			std::function<void(std::shared_ptr<av_player>)> f;

			while (dequeue(f))
			{
				if (df::is_closing) return;
				f(player);
			}

			if (_thread_session)
			{
				_thread_session->process_io(_video_event, _audio_event);
			}
		}
	}

	void pause_impl(const std::shared_ptr<av_session>& ses)
	{
		if (ses->_state == av_play_state::playing)
		{
			ses->state(av_play_state::paused);
			_audio_event.set();
			_video_event.set();
		}
	}

	void play_impl(const std::shared_ptr<av_session>& ses)
	{
		if (ses->_state == av_play_state::playing && ses->_last_frame_time >= (ses->_end_time - 1.0))
		{
			if (ses->_last_seek != ses->_start_time || ses->_start_time == 0)
			{
				seek_impl(ses, ses->_start_time, false);
			}
		}

		ses->_pending_time_sync = true;
		ses->state(av_play_state::playing);
	}

	void capture(const std::shared_ptr<av_session>& ses, const std::function<void(file_load_result)>& cb)
	{
		queue([ses, cb](const std::shared_ptr<av_player>& p)
		{
			cb(ses->capture_first_frame());
		});
	}

private:
	bool dequeue(std::function<void(std::shared_ptr<av_player>)>& f)
	{
		platform::exclusive_lock lock(_queue_mutex);
		if (_q.empty()) return false;
		f = std::move(_q.front());
		_q.pop_front();
		return true;
	}
};

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Media playback session and player management. Coordinates audio/video decoding threads,
// handles play/pause/seek operations, and synchronizes audio-video timing.

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
	av_visualizer _visualizer;

	file_type_ref _mt = nullptr;

	av_packet_queue _audio_packets;
	av_packet_queue _video_packets;

	av_frame_queue _audio_frames;
	av_frame_queue _video_frames;

	mutable platform::mutex _decoder_rw;
	_Guarded_by_(_decoder_rw) av_format_decoder _decoder;

	double _end_time = 0;
	double _last_frame_decoded = 0;
	double _start_time = 0;
	double _audio_buffer_seconds = 0;

	std::atomic<int> _seek_gen = 1;


	const int max_loop_iteration = 256;

	// Read by the audio, video and read threads and written from the read thread
	// (and, on device loss, the audio thread); atomic so a pause/seek is seen
	// promptly and consistently rather than via a benign-but-fragile data race.
	std::atomic<av_play_state> _state = av_play_state::detached;

	std::atomic<bool> _scrubbing = false;

	double _last_seek = 0.0;
	double _time_offset = 0.0;
	double _audio_buffer_time = 0.0;

	std::atomic<bool> _reset_time_offset = false;
	std::atomic<bool> _pending_time_sync = false;
	std::atomic<bool> _settling = false;

	// Set once the audio stream's end has been handled (decoder tail drained and a
	// silence pad queued). has_ended() waits for this for audio sessions so the wall
	// clock - which races ahead when the device under-runs at the end - cannot fire
	// end-of-stream early and seek away before the silence tail is produced (which
	// left the WASAPI ring to loop its last buffer). Re-armed on every open/seek.
	std::atomic<bool> _audio_eof_handled = false;

	// _audio_data_end: absolute time of the last real audio sample (captured when the
	// EOF is handled, after the decoder tail is drained). _audio_clock: the audio
	// device's current play position (base_time + device clock), updated each audio
	// iteration. has_ended() ends the clip when the device has actually played out to
	// _audio_data_end rather than on the wall clock, which drifts ahead of the device.
	std::atomic<double> _audio_data_end = 0.0;
	std::atomic<double> _audio_clock = 0.0;

	int _volume = 1000;
	bool _mute = false;

	df::item_element_ptr _item;

	mutable double _last_frame_time = -1;
	mutable double _last_texture_time = -1;
	mutable pointi _last_frame_offset;

	av_frame_ptr _frame;
	ui::orientation _default_orientation = ui::orientation::none;

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
		_volume = _scrubbing || _mute ? 0 : setting.media_volume;
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

	bool open(const df::item_element_ptr& item, const bool auto_play, const int video_track, const int audio_track,
	          const bool can_use_hw, const bool use_last_played_pos, const bool can_use_threads)
	{
		df::assert_true(_state == av_play_state::detached);

		platform::exclusive_lock lock_dec(_decoder_rw);

		const auto result = _decoder.open(item->path());

		if (result)
		{
			_decoder.init_streams(video_track, audio_track, can_use_hw, false, can_use_threads);

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
			_settling = true;
			_audio_eof_handled = false;
			_seek_gen = 1;

			_frame.reset();

			/*file_scanner sr;
			_decoder.extract_metadata(sr);
			_metadata = sr.to_props();*/
			_default_orientation = _decoder.calc_orientation();

			_playback_resampler.reset();
			_vis_resampler.reset();

			create_resampler();

			if (item && use_last_played_pos)
			{
				const auto starting_pos = item->media_position();

				if (starting_pos > start_time + 2.0 && starting_pos < end_time - 5.0 && duration > 10.0)
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

	void process_video(const platform::thread_event& _read_event)
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

	void process_audio(audio_buffer& playback_buffer, audio_buffer& vis_buffer,
	                   const platform::thread_event& read_event)
	{
		auto loop_iteration = 0;

		if (_decoder.has_audio())
		{
			while (_audio_frames.should_receive() || playback_buffer.should_fill() || _seek_gen != playback_buffer.
				generation())
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
							// End of the audio stream: recover any tail the resamplers
							// still hold, fade the very end so a clip ending on a non-zero
							// sample does not click, then pad with silence. Do this once
							// per end-of-stream (process_io keeps re-issuing EOF markers as
							// the queue drains; appending a blank second for each would grow
							// _end_time without bound and the clip would never end).
							if (!_audio_eof_handled)
							{
								const auto pr = _playback_resampler;
								const auto vr = _vis_resampler;

								if (pr) pr->drain(playback_buffer, _seek_gen);
								if (vr) vr->drain(vis_buffer, _seek_gen);

								// The real audio ends here (after the decoder tail is
								// recovered, before the silence pad). has_ended() ends the
								// clip once the device has played out to this point.
								_audio_data_end = playback_buffer.end_time();

								playback_buffer.apply_fade_out(0.005);

								playback_buffer.append_blank_second();
								vis_buffer.append_blank_second();

								// Tell has_ended() the tail is now queued so it may end the
								// clip. Set last so end_time below reflects the appended pad.
								_audio_eof_handled = true;
							}
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

										df::trace(std::format("Player clear audio_buffer on seek_ver {}", sv));
									}

									// Volume above 100% cannot be delivered by the device
									// (its volume is 0.0-1.0), so boost it here in software.
									// The device handles 0-100% attenuation; the 100%..200%
									// setting range maps onto a 1x..media_volume_boost_gain
									// software gain so the boost is strong enough to lift very
									// quiet sources (clamping in apply_audio_gain stops louder
									// material distorting). 1.0 = no boost for the normal range.
									const auto boost = std::clamp(_volume, 1000, media_volume_boost);
									pr->set_gain(1.0 + (boost - 1000) / 1000.0 * (media_volume_boost_gain - 1.0));

									pr->resample(frame, playback_buffer);
									vr->resample(frame, vis_buffer);
								}
							}
						}

						// Keep _end_time at the media duration: do NOT let the appended
						// silence pad push it out, or has_ended() (which waits for the
						// media end to pass) would hold the last frame for the pad's length.
						if (!eof) _end_time = std::max(_end_time, playback_buffer.end_time());
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

	void process_io(const platform::thread_event& video_event, const platform::thread_event& audio_event);

	bool has_ended(const double time_now) const
	{
		if (!is_playing()) return false;

		// For audio sessions, do not declare the end until the audio stream's EOF has
		// actually been handled (tail drained + silence pad queued). The master clock
		// pos() is wall-clock based and races ahead of the audio device when the ring
		// under-runs at the end; without this guard it crosses _end_time early and the
		// resulting seek-to-start discards the EOF frame before its silence tail is
		// produced, leaving the device to loop its last buffer. A hard +2s margin still
		// ends a stream that never signals EOF so playback can never get stuck.
		if (_decoder.has_audio() && !_audio_eof_handled)
		{
			return pos(time_now) >= _end_time + 2.0;
		}

		if (_decoder.has_audio())
		{
			// The EOF has been handled, so a silence tail is queued. End the clip when
			// the audio device has actually played out the real audio (_audio_clock is
			// the device play position). The wall clock pos() drifts ahead of the device
			// (a start-up under-run stalls the device clock), so ending on it would hold
			// the last frame for ~1s while it catches up. Still require the media end to
			// pass so a video longer than its audio plays fully; +2s is a hard fallback.
			const auto audio_played_out = _audio_clock >= _audio_data_end;
			const auto media_played_out = pos(time_now) >= _end_time;
			return (audio_played_out && media_played_out) || pos(time_now) >= _end_time + 2.0;
		}

		return pos(time_now) >= _end_time;
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

		const auto is_playing_audio = _mt != nullptr && (_mt->group->traits && file_traits::visualize_audio);

		if (is_playing_audio)
		{
			result = _visualizer.step(time);
		}

		av_frame_ptr f;
		auto frame_popped = false;
		const auto seek_ver_invalid = av_seek_gen_from_frame(_frame) != _seek_gen;
		const auto current_ft = av_time_from_frame(_frame);

		// After a seek the demuxer leaves us on a key frame that can be a whole GOP
		// before the sought position. While scrubbing (audio is stopped) and for
		// audio-less video we "settle" the view forward onto the target frame so it
		// matches the scrubber preview and the position the user asked for. During
		// audio playback the audio device stays the master clock, so we keep the
		// existing key-frame-then-play behaviour to avoid an A/V desync.
		const auto settling = _scrubbing || (_settling && !_decoder.has_audio());

		if (seek_ver_invalid)
		{
			frame_popped = _video_frames.pop(f);
		}

		if (settling)
		{
			// Drain decoded frames toward the sought time (pos() returns _last_seek
			// while scrubbing or pending) rather than stepping one frame per present,
			// so the view reaches the target as decoding catches up.
			auto best_ft = frame_popped ? av_time_from_frame(f) : current_ft;
			auto reached = false;

			while (!_video_frames.is_empty())
			{
				const auto front_time = _video_frames.front_time();

				if (time_distance(best_ft, time) <= time_distance(front_time, time) && !df::equiv(best_ft, front_time))
				{
					reached = true; // the next frame is past the target; this one is nearest
					break;
				}

				av_frame_ptr next;

				if (!_video_frames.pop(next))
				{
					break;
				}

				if (av_frame_is_eof(next))
				{
					reached = true; // no frame beyond the target will arrive
					break;
				}

				if (!av_is_frame_empty(next))
				{
					f = std::move(next);
					best_ft = av_time_from_frame(f);
					frame_popped = true;
				}
			}

			if (reached || best_ft >= time)
			{
				_settling = false;
			}
		}
		else if (!seek_ver_invalid && _state == av_play_state::playing && !_pending_time_sync && !_reset_time_offset)
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

		// Lock the wall clock to the displayed frame only once the view has settled
		// on the sought position, so a no-audio seek anchors playback to the target
		// frame rather than the key frame the settle started from.
		if (_reset_time_offset && av_seek_gen_from_frame(_frame) == _seek_gen && !_settling)
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
	                       const double time_now) const
	{
		const auto time = pos(time_now);
		_last_frame_time = time;
		_last_frame_offset = offset;
		_visualizer.render(verts, rect, offset, alpha, time);
	}

	bool capture_frame(const av_frame& frame_in, ui::const_image_ptr& result);

	render_valid update_texture(const ui::texture_ptr& texture) const
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
	mutable platform::thread_event _video_event;
	mutable platform::thread_event _audio_event;
	mutable platform::thread_event _read_event;

	platform::mutex _queue_mutex;
	platform::mutex _thread_mutex;
	std::shared_ptr<av_session> _thread_session;

	mutable _Guarded_by_(_thread_mutex) std::string _audio_device_id;
	mutable std::string _play_audio_device_id;

	_Guarded_by_(_queue_mutex) std::deque<std::function<void(std::shared_ptr<av_player>)>> _q;
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

	void open(const df::item_element_ptr& item, const bool auto_play, const int video_track, const int audio_track,
	          const bool can_use_hw,
	          const bool use_last_played_pos,
	          const std::function<void(std::shared_ptr<av_session>)>& cb)
	{
		queue([item, auto_play, video_track, audio_track, can_use_hw, use_last_played_pos, cb](
			const std::shared_ptr<av_player>& p)
			{
				df::scope_locked_inc l(df::loading_media);
				auto ses = p->open_impl(item, auto_play, video_track, audio_track, can_use_hw, use_last_played_pos);
				if (cb) p->_host.queue_ui([cb, ses] { cb(ses); });
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
	void seek_impl(const std::shared_ptr<av_session>& ses, const double pos, const bool scrubbing) const
	{
		ses->seek(pos, scrubbing);

		_read_event.set();
		_video_event.set();
		_audio_event.set();
	}

	std::shared_ptr<av_session> open_impl(const df::item_element_ptr& item, const bool auto_play, const int video_track,
	                                      const int audio_track, const bool can_use_hw,
	                                      const bool use_last_played_pos)
	{
		const auto ses = std::make_shared<av_session>(_host);
		const auto open_result = ses->open(item, auto_play, video_track, audio_track, can_use_hw, use_last_played_pos,
		                                   true);
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
	void audio_device_id(const std::string_view id) const
	{
		platform::exclusive_lock lock(_thread_mutex);
		_audio_device_id = id;
		_audio_event.set();
	}

	std::string_view audio_device_id() const
	{
		platform::exclusive_lock lock(_thread_mutex);
		return _audio_device_id;
	}

	void play_audio_device_id(const std::string_view id) const
	{
		platform::exclusive_lock lock(_thread_mutex);
		_play_audio_device_id = id;
		_audio_event.set();
	}

	std::string_view play_audio_device_id() const
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
		std::string device_id;
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
			// Short idle poll. While decoding, the read thread signals _audio_event as
			// it pushes packets so this wakes promptly. But at the very end of a clip
			// the audio packets are exhausted (no more pushes) while the device still
			// needs the decoded tail; a long idle wait there would let the decoder
			// deliver only ~one frame per poll - far slower than real-time - draining
			// the ring and looping the last buffer. A short poll keeps the tail (and
			// the trailing silence) flowing fast enough to keep the device fed.
			auto e = wait_for(events, 10, false);

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
					need_create_device = false;
				}
				else
				{
					// Device creation failed - for example the audio endpoint was
					// momentarily unavailable (device switch, exclusive grab by another
					// app, or not yet ready at startup). Leave need_create_device set so
					// we retry on a later iteration. Otherwise the track would advance on
					// the wall clock and play silently for its whole duration.
					playback_buffer.clear();
					need_create_device = has_audio && session != nullptr;
				}

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

					// process_audio can run long enough for the session to be paused -
					// notably at end of stream, where the view pauses AND seeks back to
					// the start. Re-read the state here: using the value captured above
					// would let us write and play the freshly sought start-of-stream
					// audio, an audible blip of the track restarting as the video ended.
					const auto should_play = session->_state == av_play_state::playing && !session->_scrubbing;

					if (!should_play && !ds->is_stopped())
					{
						ds->stop();
					}

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

						if (should_play)
						{
							// Prime the ring before the first start. Starting with an
							// almost-empty buffer makes the device under-run immediately:
							// it loops its tiny initial contents (an audible blip, notably
							// at each loop restart) and its clock stalls, running behind the
							// wall clock for the rest of the clip. Wait for a cushion (or an
							// already-running device, or the stream's end) before starting.
							const auto primed = !ds->is_stopped()
								|| playback_buffer.seconds() >= 0.3
								|| session->_audio_eof_handled;

							if (primed)
							{
								ds->write(playback_buffer);

								if (ds->is_stopped())
								{
									ds->start();
								}

								if (session->_pending_time_sync)
								{
									const auto time = base_time + ds->time();
									//df::log(__FUNCTION__, std::format("sound.clock {}", time));
									session->_time_offset = df::now() - time;
									session->_pending_time_sync = false;
								}
							}
						}
					}
					else if (should_play && !ds->is_stopped())
					{
						// No decoded audio is available (end of stream, or a brief
						// decode gap). Push silence so the WASAPI ring does not replay
						// (loop) its last contents on underrun - that is what made the
						// audio tail repeat a few times after the video ended. The
						// device is stopped normally once has_ended pauses the session.
						ds->write_silence();
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
						// Device volume is 0.0-1.0; anything above 100% is applied as a
						// software gain on the decoded samples (see set_gain above).
						ds->volume(std::min(session->_volume, 1000) / 1000.0);
					}

					if (ds->is_device_lost())
					{
						session->state(av_play_state::paused);
						audio_device_id({});
						need_create_device = true;
					}

					session->_audio_buffer_time = playback_buffer.start_time();
					session->_audio_clock = base_time + ds->time();
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

	void pause_impl(const std::shared_ptr<av_session>& ses) const
	{
		if (ses->_state == av_play_state::playing)
		{
			ses->state(av_play_state::paused);
			_audio_event.set();
			_video_event.set();
		}
	}

	void play_impl(const std::shared_ptr<av_session>& ses) const
	{
		if (ses->_state == av_play_state::playing && ses->_last_frame_time >= ses->_end_time - 1.0)
		{
			if (ses->_last_seek != ses->_start_time || ses->_start_time == 0)
			{
				seek_impl(ses, ses->_start_time, false);
			}
		}

		ses->_pending_time_sync = true;
		// With audio, the audio thread re-establishes the clock and clears
		// _pending_time_sync. With no audio there is no such clock, so we must
		// re-arm _reset_time_offset to let update_for_present rebuild the wall
		// clock from the next frame - otherwise pos() stays frozen at _last_seek
		// and playback appears stuck on resume.
		ses->_reset_time_offset = !ses->_decoder.has_audio();
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

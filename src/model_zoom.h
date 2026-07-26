// Purpose: Pure image zoom scale, source-space center, anchoring, bounds geometry,
// and navigator visibility timing.

#pragma once

#include "util_geometry.h"

namespace df
{
	enum class zoom_scale_mode
	{
		fit,
		fit_width,
		fill,
		explicit_scale
	};

	enum class zoom_pane
	{
		primary,
		secondary
	};

	struct zoom_geometry
	{
		rectd destination;
		pointd center;
	};

	class zoom_view_state
	{
		zoom_scale_mode _mode = zoom_scale_mode::fit;
		double _scale = 1.0;
		pointd _center{0.5, 0.5};
		pointd _last_explicit_center{0.5, 0.5};
		sized _source;
		bool _carried_fit = false;
		bool _has_explicit = false;

		static constexpr std::array<double, 19> scale_ladder{
			0.05, 0.07, 0.10, 0.15, 0.20, 0.25, 0.33, 0.50, 0.67, 0.75,
			1.00, 1.50, 2.00, 3.00, 4.00, 6.00, 8.00, 12.00, 16.00
		};

		static pointd clamp_center(const pointd center, const sized source, const sized viewport,
		                           const double scale) noexcept
		{
			const auto clamp_axis = [scale](const double value, const double source_extent,
			                                const double viewport_extent)
			{
				if (source_extent <= 0.0 || source_extent * scale <= viewport_extent) return 0.5;
				const auto half_visible = viewport_extent / (2.0 * source_extent * scale);
				return std::clamp(value, half_visible, 1.0 - half_visible);
			};

			return {
				clamp_axis(center.X, source.Width, viewport.Width),
				clamp_axis(center.Y, source.Height, viewport.Height)
			};
		}

	public:
		zoom_scale_mode mode() const noexcept { return _mode; }
		bool is_fit() const noexcept { return _mode == zoom_scale_mode::fit; }

		bool is_carried_fit() const noexcept { return _carried_fit; }
		double explicit_scale() const noexcept { return _scale; }
		pointd center() const noexcept { return _center; }

		static double fit_scale(const sized source, const sized viewport, const bool enlarge = false) noexcept
		{
			if (source.Width <= 0.0 || source.Height <= 0.0 || viewport.Width <= 0.0 || viewport.Height <= 0.0)
				return 1.0;
			const auto scale = std::min(viewport.Width / source.Width, viewport.Height / source.Height);
			return enlarge ? scale : std::min(1.0, scale);
		}

		static double fit_variant_scale(const zoom_scale_mode mode, const sized source, const sized viewport,
		                                const bool enlarge = false) noexcept
		{
			if (source.Width <= 0.0 || source.Height <= 0.0 || viewport.Width <= 0.0 || viewport.Height <= 0.0)
				return 1.0;
			auto scale = fit_scale(source, viewport, enlarge);
			if (mode == zoom_scale_mode::fit_width) scale = viewport.Width / source.Width;
			else if (mode == zoom_scale_mode::fill)
				scale = std::max(viewport.Width / source.Width, viewport.Height / source.Height);
			return enlarge || mode != zoom_scale_mode::fit ? scale : std::min(1.0, scale);
		}

		static constexpr const std::array<double, 19>& ladder() noexcept { return scale_ladder; }

		static pointd accelerate_pan(const pointd delta, const double ramp) noexcept
		{
			const auto distance = std::hypot(delta.X, delta.Y);
			if (distance <= 0.0 || ramp <= 0.0) return delta;
			return delta * (1.0 + distance / ramp);
		}
		static pointd auto_pan_velocity(const pointd offset, const double dead_zone = 12.0,
		                                const double max_speed = 1200.0) noexcept
		{
			const auto distance = std::hypot(offset.X, offset.Y);
			if (distance <= dead_zone || max_speed <= 0.0) return {};
			const auto speed = std::min(max_speed, (distance - dead_zone) * 8.0);
			return offset * (speed / distance);
		}

		static pointd navigator_center(const pointd location, const sized extent) noexcept
		{
			return {
				std::clamp(location.X / std::max(1.0, extent.Width), 0.0, 1.0),
				std::clamp(location.Y / std::max(1.0, extent.Height), 0.0, 1.0)
			};
		}

		static int accumulate_wheel_steps(double& pending, const double delta, const double detent = 60.0) noexcept
		{
			if (detent <= 0.0) return 0;
			pending += delta;
			const auto steps = static_cast<int>(pending / detent);
			pending -= steps * detent;
			return steps;
		}

		static constexpr int navigator_dip = 160;

		// Sized for navigator_dip at the highest display scaling we support, so the overview stays sharp.
		static constexpr sizei navigator_surface_extent{512, 512};

		double effective_scale(const double fit) const noexcept
		{
			return is_fit() || _carried_fit ? fit : _scale;
		}

		bool is_magnified(const double fit) const noexcept
		{
			return effective_scale(fit) > fit + 0.000001;
		}

		void update_source(const sized source, const double fit) noexcept
		{
			// Exact != on doubles would call a source "changed" for a difference no pixel can show.
			const auto source_changed = _source.Width > 0.0 && _source.Height > 0.0 &&
				(!df::equiv(_source.Width, source.Width) || !df::equiv(_source.Height, source.Height));
			_source = source;

			if (_mode == zoom_scale_mode::explicit_scale && (source_changed || _carried_fit))
			{
				_carried_fit = _scale < fit;
			}
		}

		void update_fit_variant(const sized source, const sized viewport, const bool enlarge = false) noexcept
		{
			if (_mode == zoom_scale_mode::fit_width || _mode == zoom_scale_mode::fill)
			{
				_scale = fit_variant_scale(_mode, source, viewport, enlarge);
				_center = {0.5, 0.5};
				_carried_fit = false;
			}
		}

		void fit() noexcept
		{
			if (!is_fit())
			{
				_last_explicit_center = _center;
				_has_explicit = true;
			}
			_mode = zoom_scale_mode::fit;
			_center = {0.5, 0.5};
			_carried_fit = false;
		}

		void fit_width(const sized source, const sized viewport) noexcept
		{
			_mode = zoom_scale_mode::fit_width;
			_scale = fit_variant_scale(_mode, source, viewport, true);
			_center = {0.5, 0.5};
			_carried_fit = false;
		}

		void fill(const sized source, const sized viewport) noexcept
		{
			_mode = zoom_scale_mode::fill;
			_scale = fit_variant_scale(_mode, source, viewport, true);
			_center = {0.5, 0.5};
			_carried_fit = false;
		}

		void set_explicit(const double scale, const pointd center = {0.5, 0.5}) noexcept
		{
			_mode = zoom_scale_mode::explicit_scale;
			_scale = scale;
			_center = {std::clamp(center.X, 0.0, 1.0), std::clamp(center.Y, 0.0, 1.0)};
			_last_explicit_center = _center;
			_carried_fit = false;
			_has_explicit = true;
		}

		void set_anchored(const double scale, const double old_scale, const sized source, const sized viewport,
		                  const pointd anchor) noexcept
		{
			if (source.Width <= 0.0 || source.Height <= 0.0 || old_scale <= 0.0 || scale <= 0.0) return;

			const pointd viewport_center{viewport.Width / 2.0, viewport.Height / 2.0};
			const auto source_anchor = pointd{
				_center.X * source.Width + (anchor.X - viewport_center.X) / old_scale,
				_center.Y * source.Height + (anchor.Y - viewport_center.Y) / old_scale
			};
			const auto new_center = pointd{
				(source_anchor.X - (anchor.X - viewport_center.X) / scale) / source.Width,
				(source_anchor.Y - (anchor.Y - viewport_center.Y) / scale) / source.Height
			};

			_mode = zoom_scale_mode::explicit_scale;
			_scale = scale;
			_center = {std::clamp(new_center.X, 0.0, 1.0), std::clamp(new_center.Y, 0.0, 1.0)};
			_last_explicit_center = _center;
			_carried_fit = false;
			_has_explicit = true;
		}

		pointd source_point_at(const sized source, const sized viewport, const double fit,
		                       const pointd anchor) const noexcept
		{
			const auto scale = effective_scale(fit);
			if (source.Width <= 0.0 || source.Height <= 0.0 || scale <= 0.0) return {};
			const pointd viewport_center{viewport.Width / 2.0, viewport.Height / 2.0};
			return {
				_center.X * source.Width + (anchor.X - viewport_center.X) / scale,
				_center.Y * source.Height + (anchor.Y - viewport_center.Y) / scale
			};
		}

		void center_source_point_at(const pointd source_point, const sized source, const sized viewport,
		                            const double fit, const pointd anchor) noexcept
		{
			const auto scale = effective_scale(fit);
			if (is_fit() || source.Width <= 0.0 || source.Height <= 0.0 || scale <= 0.0) return;
			const pointd viewport_center{viewport.Width / 2.0, viewport.Height / 2.0};
			_center = {
				std::clamp((source_point.X - (anchor.X - viewport_center.X) / scale) / source.Width, 0.0, 1.0),
				std::clamp((source_point.Y - (anchor.Y - viewport_center.Y) / scale) / source.Height, 0.0, 1.0)
			};
			_last_explicit_center = _center;
		}

		void toggle_fit() noexcept
		{
			if (is_fit() && _has_explicit)
			{
				_mode = zoom_scale_mode::explicit_scale;
				_center = _last_explicit_center;
				_carried_fit = false;
			}
			else
			{
				fit();
			}
		}

		void pan_source(const pointd delta, const sized source, const sized viewport, const double fit) noexcept
		{
			const auto scale = effective_scale(fit);
			if (source.Width <= 0.0 || source.Height <= 0.0 || scale <= 0.0) return;
			_center = {std::clamp(_center.X + delta.X / source.Width, 0.0, 1.0),
			           std::clamp(_center.Y + delta.Y / source.Height, 0.0, 1.0)};
		}

		void zoom_region(const rectd region, const sized source, const sized viewport, const double fit) noexcept
		{
			if (region.Width <= 0.0 || region.Height <= 0.0 || source.Width <= 0.0 || source.Height <= 0.0)
				return;
			const auto first = source_point_at(source, viewport, fit, {region.X, region.Y});
			const auto last = source_point_at(source, viewport, fit, region.bottom_right());
			const auto left = std::clamp(std::min(first.X, last.X), 0.0, source.Width);
			const auto top = std::clamp(std::min(first.Y, last.Y), 0.0, source.Height);
			const auto right = std::clamp(std::max(first.X, last.X), 0.0, source.Width);
			const auto bottom = std::clamp(std::max(first.Y, last.Y), 0.0, source.Height);
			const auto width = right - left;
			const auto height = bottom - top;
			if (width <= 0.0 || height <= 0.0) return;

			const auto scale = std::clamp(std::min(viewport.Width / width, viewport.Height / height),
			                              std::min(0.05, fit), std::max(16.0, fit));
			set_explicit(scale, {(left + right) / (2.0 * source.Width), (top + bottom) / (2.0 * source.Height)});
		}

		void step(const int direction, const double fit, const sized source, const sized viewport,
		          const pointd anchor) noexcept
		{
			if (direction == 0) return;
			const auto current = effective_scale(fit);
			constexpr auto tolerance = 0.000001;

			if (direction < 0 && current <= fit + tolerance)
			{
				this->fit();
				return;
			}

			// The ladder is a sorted compile-time constant and fit is one extra stop, so the next
			// stop is a scan. Building, sorting and deduplicating a vector on every wheel notch
			// allocated on a path the user drives continuously.
			auto next = current;
			auto found = false;

			const auto consider = [&](const double value)
			{
				if (direction > 0)
				{
					if (value > current + tolerance && (!found || value < next))
					{
						next = value;
						found = true;
					}
				}
				else if (value < current - tolerance && (!found || value > next))
				{
					next = value;
					found = true;
				}
			};

			for (const auto value : scale_ladder) consider(value);
			consider(fit);

			if (!found) next = current;

			if (direction < 0 && current > fit && next <= fit + tolerance)
			{
				this->fit();
				return;
			}

			set_anchored(next, current, source, viewport, anchor);
		}

		zoom_geometry geometry(const sized source, const sized viewport, const double fit) const noexcept
		{
			const auto scale = effective_scale(fit);
			const auto center = clamp_center(_center, source, viewport, scale);
			const auto extent = source * scale;
			const pointd viewport_center{viewport.Width / 2.0, viewport.Height / 2.0};
			return {
				{viewport_center.X - center.X * extent.Width, viewport_center.Y - center.Y * extent.Height,
				 extent.Width, extent.Height},
				center
			};
		}
	};

	// The two panes always show the same scale and center, so switching between them is a blink comparison.
	class comparison_zoom_state
	{
		zoom_view_state _primary;
		zoom_view_state _secondary;
		zoom_pane _active = zoom_pane::primary;

	public:
		zoom_pane active() const noexcept { return _active; }

		static zoom_pane other(const zoom_pane pane) noexcept
		{
			return pane == zoom_pane::primary ? zoom_pane::secondary : zoom_pane::primary;
		}

		void active(const zoom_pane pane) noexcept
		{
			if (pane == _active) return;
			inactive_state() = active_state();
			_active = pane;
		}

		zoom_view_state& state(const zoom_pane pane) noexcept
		{
			return pane == zoom_pane::primary ? _primary : _secondary;
		}

		const zoom_view_state& state(const zoom_pane pane) const noexcept
		{
			return pane == zoom_pane::primary ? _primary : _secondary;
		}

		zoom_view_state& active_state() noexcept { return state(_active); }
		const zoom_view_state& active_state() const noexcept { return state(_active); }

		template <class Mutator>
		void mutate(Mutator&& mutator)
		{
			mutator(active_state());
			inactive_state() = active_state();
		}

	private:
		zoom_view_state& inactive_state() noexcept
		{
			return state(other(_active));
		}
	};
}

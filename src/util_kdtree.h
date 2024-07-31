// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

struct kd_coordinates_t
{
	float x, y;
	uint32_t offset;
	uint32_t country;
};

class kd_tree
{
private:
	static float dist(const float x1, const float y1, const float x2, const float y2)
	{
		const auto dx = x1 - x2;
		const auto dy = y1 - y2;
		return dx * dx + dy * dy;
	}

	static float dist(const kd_coordinates_t& left, const kd_coordinates_t& other)
	{
		return dist(left.x, left.y, other.x, other.y);
	}

	static float fast_sqr(const float x)
	{
		return x * x;
	}

	class node_t : public df::no_copy
	{
	public:
		struct traversal_state : public no_copy
		{
			kd_coordinates_t p, dir;
			kd_coordinates_t closest;
			float closest_d, closest_d2;
			size_t k;
		};

		static constexpr int MAX_PTS_PER_NODE = 16;

		int _num_points = 0;
		float x = 0.0;
		float y = 0.0;
		float r = 0.0f;
		int split_axis = 0;
		size_t _offset = 0;

		std::unique_ptr<node_t> child1;
		std::unique_ptr<node_t> child2;

		node_t(std::vector<kd_coordinates_t>& data, const size_t offset, const size_t n)
		{
			if (n <= MAX_PTS_PER_NODE)
			{
				_num_points = static_cast<int>(n);
				_offset = offset;
				return;
			}

			_num_points = 0;

			auto xmin = data[offset].x;
			auto xmax = data[offset].x;
			auto ymin = data[offset].y;
			auto ymax = data[offset].y;

			for (size_t i = 1; i < n; i++)
			{
				const auto& c = data[offset + i];
				if (c.x < xmin) xmin = c.x;
				if (c.x > xmax) xmax = c.x;
				if (c.y < ymin) ymin = c.y;
				if (c.y > ymax) ymax = c.y;
			}

			x = 0.5f * (xmin + xmax);
			y = 0.5f * (ymin + ymax);

			const auto dx = xmax - xmin;
			const auto dy = ymax - ymin;

			r = 0.5f * sqrt(fast_sqr(dx) + fast_sqr(dy));

			split_axis = dx > dy ? 0 : 1;

			auto left = offset;
			auto right = offset + n - 1;

			if (split_axis == 0)
			{
				const auto split_val = x;

				while (true)
				{
					while (data[left].x < split_val)
						left++;

					while (data[right].x > split_val)
						right--;

					if (right < left)
						break;

					if (df::equiv(data[left].x, data[right].x))
					{
						left += (right - left) / 2;
						break;
					}

					std::swap(data[left], data[right]);
					left++;
					right--;
				}
			}
			else
			{
				const auto split_val = y;

				while (true)
				{
					while (data[left].y < split_val)
						left++;

					while (data[right].y > split_val)
						right--;

					if (right < left)
						break;

					if (df::equiv(data[left].y, data[right].y))
					{
						left += (right - left) / 2;
						break;
					}

					std::swap(data[left], data[right]);
					left++;
					right--;
				}
			}

			child1 = std::make_unique<node_t>(data, offset, left - offset);
			child2 = std::make_unique<node_t>(data, left, n - (left - offset));
		}

		void find_closest_to_pt(const std::vector<kd_coordinates_t>& data, traversal_state& ti) const
		{
			if (_num_points)
			{
				for (auto i = 0; i < _num_points; i++)
				{
					const auto myd2 = dist(data[_offset + i], ti.p);

					if ((myd2 < ti.closest_d2))
					{
						ti.closest_d2 = myd2;
						ti.closest_d = sqrt(ti.closest_d2);
						ti.closest = data[_offset + i];
					}
				}
				return;
			}


			if (dist(x, y, ti.p.x, ti.p.y) >= fast_sqr(r + ti.closest_d))
				return;

			const auto myd = split_axis == 0 ? (x - ti.p.x) : (y - ti.p.y);

			if (myd >= 0.0f)
			{
				child1->find_closest_to_pt(data, ti);

				if (myd < ti.closest_d)
					child2->find_closest_to_pt(data, ti);
			}
			else
			{
				child2->find_closest_to_pt(data, ti);
				if (-myd < ti.closest_d)
					child1->find_closest_to_pt(data, ti);
			}
		}
	};

	std::unique_ptr<node_t> root;

public:
	kd_tree() = default;

	void build(std::vector<kd_coordinates_t>& data)
	{
		root = std::make_unique<node_t>(data, 0, data.size());
	}

	bool is_empty() const
	{
		return root == nullptr;
	}

	kd_coordinates_t find_closest(const std::vector<kd_coordinates_t>& data, const kd_coordinates_t& p) const
	{
		if (root)
		{
			node_t::traversal_state ti;

			ti.p = p;
			ti.closest.x = 0;
			ti.closest.y = 0;
			ti.closest_d2 = fast_sqr(root->r);
			ti.closest_d = sqrt(ti.closest_d2);

			root->find_closest_to_pt(data, ti);

			return ti.closest;
		}

		return {};
	}
};

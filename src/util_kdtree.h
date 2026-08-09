// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: KD-tree spatial index for nearest-neighbor searches. Used by
// location cache to efficiently find closest cities to GPS coordinates.

#pragma once

struct kd_coordinates_t
{
	float x, y;
	uint32_t offset;
	uint32_t country;
	uint32_t id;
	float population;
};

class kd_tree
{
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

	struct traversal_state final : df::no_copy
	{
		kd_coordinates_t p{};
		kd_coordinates_t closest{};
		float closest_d = 0.0f;
		float closest_d2 = 0.0f;
	};

	static constexpr int max_points_per_node = 16;

	// Nodes live in one contiguous array and link by index, so a quarter of a million places cost
	// a handful of allocations instead of one per node, and a descent walks memory in build order.
	// The root is index 0, which is why child2 == 0 can mark a leaf.
	struct node_t
	{
		float x = 0.0f;
		float y = 0.0f;
		float r = 0.0f;
		uint32_t first = 0; // leaf: first point in the caller's array. branch: index of child1
		uint32_t child2 = 0;
		uint8_t num_points = 0;
		uint8_t split_axis = 0;
	};

	static_assert(sizeof(node_t) == 24, "kd-tree nodes are sized to pack the gazetteer tightly");

	std::vector<node_t> _nodes;

	uint32_t build_node(std::vector<kd_coordinates_t>& data, const size_t offset, const size_t n)
	{
		const auto index = static_cast<uint32_t>(_nodes.size());
		_nodes.emplace_back();

		if (n <= max_points_per_node)
		{
			_nodes[index].num_points = static_cast<uint8_t>(n);
			_nodes[index].first = static_cast<uint32_t>(offset);
			return index;
		}

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

		const auto x = 0.5f * (xmin + xmax);
		const auto y = 0.5f * (ymin + ymax);

		const auto dx = xmax - xmin;
		const auto dy = ymax - ymin;

		const auto r = 0.5f * sqrt(fast_sqr(dx) + fast_sqr(dy));
		const auto split_axis = dx > dy ? 0 : 1;

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

		// A split that leaves one side empty would recurse forever on the same range; float
		// rounding of the midpoint makes that reachable when the coordinates are near-identical.
		if (left == offset || left == offset + n)
		{
			left = offset + n / 2;
		}

		const auto child1 = build_node(data, offset, left - offset);
		const auto child2 = build_node(data, left, n - (left - offset));

		// Written after the recursion, which reallocates _nodes and would dangle a held reference.
		auto& node = _nodes[index];
		node.x = x;
		node.y = y;
		node.r = r;
		node.split_axis = static_cast<uint8_t>(split_axis);
		node.first = child1;
		node.child2 = child2;
		return index;
	}

	void find_closest_to_pt(const std::vector<kd_coordinates_t>& data, traversal_state& ti,
	                        const uint32_t index) const
	{
		const auto& node = _nodes[index];

		if (node.child2 == 0)
		{
			for (uint32_t i = 0; i < node.num_points; i++)
			{
				const auto myd2 = dist(data[node.first + i], ti.p);

				if (myd2 < ti.closest_d2)
				{
					ti.closest_d2 = myd2;
					ti.closest_d = sqrt(ti.closest_d2);
					ti.closest = data[node.first + i];
				}
			}
			return;
		}

		if (dist(node.x, node.y, ti.p.x, ti.p.y) >= fast_sqr(node.r + ti.closest_d))
			return;

		const auto myd = node.split_axis == 0 ? node.x - ti.p.x : node.y - ti.p.y;

		if (myd >= 0.0f)
		{
			find_closest_to_pt(data, ti, node.first);

			if (myd < ti.closest_d)
				find_closest_to_pt(data, ti, node.child2);
		}
		else
		{
			find_closest_to_pt(data, ti, node.child2);
			if (-myd < ti.closest_d)
				find_closest_to_pt(data, ti, node.first);
		}
	}

	// Collect every point inside the axis-aligned rectangle [xmin,xmax]x[ymin,ymax].
	// Internal nodes are pruned via their bounding circle (centre x,y radius r,
	// which encloses all descendant points).
	void find_in_bounds(const std::vector<kd_coordinates_t>& data, const float xmin, const float ymin,
	                    const float xmax, const float ymax, std::vector<kd_coordinates_t>& out,
	                    const uint32_t index) const
	{
		const auto& node = _nodes[index];

		if (node.child2 == 0)
		{
			for (uint32_t i = 0; i < node.num_points; i++)
			{
				const auto& c = data[node.first + i];

				if (c.x >= xmin && c.x <= xmax && c.y >= ymin && c.y <= ymax)
				{
					out.push_back(c);
				}
			}

			return;
		}

		// Closest point of the query rect to this node's centre.
		const auto nx = std::clamp(node.x, xmin, xmax);
		const auto ny = std::clamp(node.y, ymin, ymax);

		if (dist(node.x, node.y, nx, ny) > fast_sqr(node.r))
		{
			return; // bounding circle does not reach the rect
		}

		find_in_bounds(data, xmin, ymin, xmax, ymax, out, node.first);
		find_in_bounds(data, xmin, ymin, xmax, ymax, out, node.child2);
	}

public:
	kd_tree() = default;

	void build(std::vector<kd_coordinates_t>& data)
	{
		_nodes.clear();
		// Leaves hold up to max_points_per_node and every branch has two children, so a balanced
		// tree lands near n/8 nodes; reserving that absorbs most of the growth copies.
		_nodes.reserve(data.size() / (max_points_per_node / 2) + 2);
		build_node(data, 0, data.size());
	}

	bool is_empty() const
	{
		return _nodes.empty();
	}

	kd_coordinates_t find_closest(const std::vector<kd_coordinates_t>& data, const kd_coordinates_t& p) const
	{
		if (!_nodes.empty())
		{
			traversal_state ti;

			ti.p = p;
			ti.closest_d2 = fast_sqr(_nodes[0].r);
			ti.closest_d = sqrt(ti.closest_d2);

			find_closest_to_pt(data, ti, 0);

			return ti.closest;
		}

		return {};
	}

	// Append every point within the axis-aligned bounds to `out`.
	void find_in_bounds(const std::vector<kd_coordinates_t>& data, const float xmin, const float ymin,
	                    const float xmax, const float ymax, std::vector<kd_coordinates_t>& out) const
	{
		if (!_nodes.empty())
		{
			find_in_bounds(data, xmin, ymin, xmax, ymax, out, 0);
		}
	}
};

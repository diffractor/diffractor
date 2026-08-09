// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: KD-tree spatial index over caller-owned points, with nearest-neighbor and
// rectangle queries. Used by the location cache to find the closest gazetteer place to a
// GPS coordinate, and by the map to collect the markers inside the visible bounds.

#pragma once

// The descent and the leaf scan read only the coordinate, so coordinates are stored apart from the
// record fields: a cache line then carries 8 candidates instead of the 2 a packed 24-byte record
// allowed. The record fields are read once, on a hit.
struct kd_point
{
	float x, y;
};

struct kd_payload
{
	uint32_t offset;
	uint32_t country;
	uint32_t id;
	float population;
};

// One point materialized for a caller that has a hit and now wants the whole record.
struct kd_coordinates_t
{
	float x, y;
	uint32_t offset;
	uint32_t country;
	uint32_t id;
	float population;
};

// The points a kd_tree indexes. Owned by the caller, not the tree: build() reorders it in place and
// the tree then refers to it by index, so the points are never copied.
class kd_points
{
	std::vector<kd_point> _points;
	std::vector<kd_payload> _payloads;

public:
	size_t size() const { return _points.size(); }
	bool empty() const { return _points.empty(); }

	void clear()
	{
		_points.clear();
		_payloads.clear();
	}

	void reserve(const size_t n)
	{
		_points.reserve(n);
		_payloads.reserve(n);
	}

	void shrink_to_fit()
	{
		_points.shrink_to_fit();
		_payloads.shrink_to_fit();
	}

	void emplace_back(const float x, const float y, const uint32_t offset, const uint32_t country,
	                  const uint32_t id, const float population)
	{
		_points.emplace_back(kd_point{x, y});
		_payloads.emplace_back(kd_payload{offset, country, id, population});
	}

	const kd_point& point(const size_t i) const { return _points[i]; }

	kd_coordinates_t operator[](const size_t i) const
	{
		const auto& p = _points[i];
		const auto& d = _payloads[i];
		return {p.x, p.y, d.offset, d.country, d.id, d.population};
	}

	void swap_entries(const size_t a, const size_t b)
	{
		std::swap(_points[a], _points[b]);
		std::swap(_payloads[a], _payloads[b]);
	}
};

class kd_tree
{
	static float dist(const float x1, const float y1, const float x2, const float y2)
	{
		const auto dx = x1 - x2;
		const auto dy = y1 - y2;
		return dx * dx + dy * dy;
	}

	struct traversal_state final : df::no_copy
	{
		float x = 0.0f;
		float y = 0.0f;
		float closest_d2 = std::numeric_limits<float>::max();
		size_t closest = SIZE_MAX;
	};

	static constexpr uint32_t max_points_per_node = 16;

	// Nodes live in one contiguous array and link by index, so a quarter of a million places cost
	// a handful of allocations instead of one per node, and a descent walks memory in build order.
	// The bounds are the exact extent of the descendant points: a bounding circle around a long thin
	// node overstates its reach badly enough to send either query into subtrees that cannot hold an
	// answer. The first child is always the next node, because the build emplaces it immediately, so
	// only the second child needs an index -- which lets a leaf's point offset share that field,
	// with count alone saying which reading applies.
	struct node_t
	{
		float xmin = 0.0f;
		float ymin = 0.0f;
		float xmax = 0.0f;
		float ymax = 0.0f;
		uint32_t offset_or_child2 = 0;
		uint32_t count = 0; // 0 marks a branch
	};

	static_assert(sizeof(node_t) == 24, "kd-tree nodes are sized to pack the gazetteer tightly");

	std::vector<node_t> _nodes;

	static float dist_to_box(const float x, const float y, const node_t& node)
	{
		const auto dx = x < node.xmin ? node.xmin - x : (x > node.xmax ? x - node.xmax : 0.0f);
		const auto dy = y < node.ymin ? node.ymin - y : (y > node.ymax ? y - node.ymax : 0.0f);
		return dx * dx + dy * dy;
	}

	uint32_t build_node(kd_points& data, const size_t offset, const size_t n)
	{
		const auto index = static_cast<uint32_t>(_nodes.size());
		_nodes.emplace_back();

		auto xmin = data.point(offset).x;
		auto xmax = xmin;
		auto ymin = data.point(offset).y;
		auto ymax = ymin;

		for (size_t i = 1; i < n; i++)
		{
			const auto& c = data.point(offset + i);
			if (c.x < xmin) xmin = c.x;
			if (c.x > xmax) xmax = c.x;
			if (c.y < ymin) ymin = c.y;
			if (c.y > ymax) ymax = c.y;
		}

		{
			auto& node = _nodes[index];
			node.xmin = xmin;
			node.ymin = ymin;
			node.xmax = xmax;
			node.ymax = ymax;

			if (n <= max_points_per_node)
			{
				node.offset_or_child2 = static_cast<uint32_t>(offset);
				node.count = static_cast<uint32_t>(n);
				return index;
			}
		}

		const auto dx = xmax - xmin;
		const auto dy = ymax - ymin;
		const auto split_axis = dx > dy ? 0 : 1;

		auto left = offset;
		auto right = offset + n - 1;

		if (split_axis == 0)
		{
			const auto split_val = 0.5f * (xmin + xmax);

			while (true)
			{
				while (data.point(left).x < split_val)
					left++;

				while (data.point(right).x > split_val)
					right--;

				if (right < left)
					break;

				if (df::equiv(data.point(left).x, data.point(right).x))
				{
					left += (right - left) / 2;
					break;
				}

				data.swap_entries(left, right);
				left++;
				right--;
			}
		}
		else
		{
			const auto split_val = 0.5f * (ymin + ymax);

			while (true)
			{
				while (data.point(left).y < split_val)
					left++;

				while (data.point(right).y > split_val)
					right--;

				if (right < left)
					break;

				if (df::equiv(data.point(left).y, data.point(right).y))
				{
					left += (right - left) / 2;
					break;
				}

				data.swap_entries(left, right);
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

		// This call emplaces the first child, so it lands at index + 1 and needs no stored link.
		build_node(data, offset, left - offset);
		const auto child2 = build_node(data, left, n - (left - offset));

		// Written after the recursion, which reallocates _nodes and would dangle a held reference.
		_nodes[index].offset_or_child2 = child2;
		return index;
	}

	void find_closest_to_pt(const kd_points& data, traversal_state& ti, const uint32_t index) const
	{
		const auto& node = _nodes[index];

		if (node.count != 0)
		{
			for (uint32_t i = 0; i < node.count; i++)
			{
				const auto point = node.offset_or_child2 + i;
				const auto& c = data.point(point);
				const auto myd2 = dist(c.x, c.y, ti.x, ti.y);

				if (myd2 < ti.closest_d2)
				{
					ti.closest_d2 = myd2;
					ti.closest = point;
				}
			}

			return;
		}

		const auto child1 = index + 1;
		const auto child2 = node.offset_or_child2;
		const auto d1 = dist_to_box(ti.x, ti.y, _nodes[child1]);
		const auto d2 = dist_to_box(ti.x, ti.y, _nodes[child2]);

		// Nearer box first: it usually tightens closest_d2 enough to prune the other outright.
		const auto near_first = d1 <= d2;

		if ((near_first ? d1 : d2) < ti.closest_d2)
			find_closest_to_pt(data, ti, near_first ? child1 : child2);

		if ((near_first ? d2 : d1) < ti.closest_d2)
			find_closest_to_pt(data, ti, near_first ? child2 : child1);
	}

	// Collect every point inside the axis-aligned rectangle [xmin,xmax]x[ymin,ymax].
	void find_in_bounds(const kd_points& data, const float xmin, const float ymin, const float xmax,
	                    const float ymax, std::vector<kd_coordinates_t>& out, const uint32_t index) const
	{
		const auto& node = _nodes[index];

		if (node.xmin > xmax || node.xmax < xmin || node.ymin > ymax || node.ymax < ymin)
		{
			return;
		}

		if (node.count != 0)
		{
			for (uint32_t i = 0; i < node.count; i++)
			{
				const auto point = node.offset_or_child2 + i;
				const auto& c = data.point(point);

				if (c.x >= xmin && c.x <= xmax && c.y >= ymin && c.y <= ymax)
				{
					out.push_back(data[point]);
				}
			}

			return;
		}

		find_in_bounds(data, xmin, ymin, xmax, ymax, out, index + 1);
		find_in_bounds(data, xmin, ymin, xmax, ymax, out, node.offset_or_child2);
	}

public:
	kd_tree() = default;

	void build(kd_points& data)
	{
		_nodes.clear();

		if (data.empty())
		{
			return;
		}

		// Leaves hold up to max_points_per_node and every branch has two children, so a balanced
		// tree lands near n/8 nodes; reserving that absorbs most of the growth copies.
		_nodes.reserve(data.size() / (max_points_per_node / 2) + 2);
		build_node(data, 0, data.size());
	}

	bool is_empty() const
	{
		return _nodes.empty();
	}

	kd_coordinates_t find_closest(const kd_points& data, const float x, const float y) const
	{
		if (_nodes.empty())
		{
			return {};
		}

		traversal_state ti;
		ti.x = x;
		ti.y = y;

		find_closest_to_pt(data, ti, 0);

		return ti.closest == SIZE_MAX ? kd_coordinates_t{} : data[ti.closest];
	}

	// Append every point within the axis-aligned bounds to `out`.
	void find_in_bounds(const kd_points& data, const float xmin, const float ymin, const float xmax,
	                    const float ymax, std::vector<kd_coordinates_t>& out) const
	{
		if (!_nodes.empty())
		{
			find_in_bounds(data, xmin, ymin, xmax, ymax, out, 0);
		}
	}
};

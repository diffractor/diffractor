// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for the face foundation - alignment, similarity, anchored grouping, the sample face
// that identifies a group, the stored form and the face token. The subject is docs/faces.md. None of
// it needs a detector, an embedding model or a photograph, because every stage here takes values in
// and hands values back.

#include "pch.h"

#include "model_faces.h"
#include "test.h"
#include "test_fixtures.h"

static faces::landmarks scaled_template(const double scale, const pointd offset)
{
	auto result = faces::reference_template();

	for (auto& m : result)
	{
		m.X = m.X * scale + offset.X;
		m.Y = m.Y * scale + offset.Y;
	}

	return result;
}

static faces::face_vector unit_vector(const int axis)
{
	faces::face_vector v{};
	v[axis] = 1.0f;
	return v;
}

// Two vectors whose similarity is a chosen cosine, built in one plane so the rest stay orthogonal.
static faces::face_vector planar_vector(const double radians)
{
	faces::face_vector v{};
	v[0] = static_cast<float>(std::cos(radians));
	v[1] = static_cast<float>(std::sin(radians));
	return v;
}

class face_invalid_async final : public null_async_strategy
{
	view_invalid _invalids = view_invalid::none;

public:
	void invalidate_view(const view_invalid invalid) override
	{
		_invalids |= invalid;
	}

	bool was_invalidated(const view_invalid invalid) const
	{
		return (_invalids & invalid) != view_invalid::none;
	}
};

static void should_fit_landmarks_to_the_reference_template()
{
	// The template itself must fit as the identity, or every stored vector is measured against a
	// crop that is not the one the model was trained on.
	const auto identity = faces::align_transform(faces::reference_template());

	for (const auto& m : faces::reference_template())
	{
		const auto p = identity.transform(m);
		assert_near(m.X, p.X, 1e-6, "the template fits itself"sv);
		assert_near(m.Y, p.Y, 1e-6, "the template fits itself"sv);
	}

	// A face twice the size and offset in the frame is the ordinary case: the fit has to undo both.
	const auto transform = faces::align_transform(scaled_template(2.0, {300.0, 120.0}));

	for (const auto& m : faces::reference_template())
	{
		const auto p = transform.transform({m.X * 2.0 + 300.0, m.Y * 2.0 + 120.0});
		assert_near(m.X, p.X, 1e-6, "a scaled and offset face fits the template"sv);
		assert_near(m.Y, p.Y, 1e-6, "a scaled and offset face fits the template"sv);
	}
}

static void should_align_faces_without_shearing_them()
{
	// Landmarks stretched along one axis only. A full affine fit has the freedom to squash this flat
	// against the template, which destroys exactly the geometry the embedding reads; a similarity
	// transform cannot, so the fit must remain a rotation, a uniform scale and a translation.
	auto sheared = faces::reference_template();

	for (auto& m : sheared)
	{
		m.X = m.X * 2.0 + m.Y * 0.6;
	}

	const auto transform = faces::align_transform(sheared);
	const auto* const t = transform.coefficients();

	// Columns of equal length and at right angles is what "similarity, not affine" means in numbers.
	assert_near(t[0], t[3], 1e-9, "the fit keeps a uniform scale"sv);
	assert_near(t[1], -t[2], 1e-9, "the fit keeps the axes at right angles"sv);

	const auto scale_x = std::hypot(t[0], t[1]);
	const auto scale_y = std::hypot(t[2], t[3]);
	assert_near(scale_x, scale_y, 1e-9, "both axes are scaled by the same amount"sv);

	// And the same holds for an ordinary rotated face, which is the case the property exists for.
	auto rotated = faces::reference_template();

	for (auto& m : rotated)
	{
		const auto x = m.X - 56.0;
		const auto y = m.Y - 56.0;
		m.X = 56.0 + (x * 0.8 - y * 0.6);
		m.Y = 56.0 + (x * 0.6 + y * 0.8);
	}

	const auto rotated_transform = faces::align_transform(rotated);
	const auto* const r = rotated_transform.coefficients();
	assert_near(r[0], r[3], 1e-9, "a rotated face fits by rotating back"sv);
	assert_near(r[1], -r[2], 1e-9, "a rotated face fits by rotating back"sv);
}

static void should_refuse_a_face_alignment_that_leaves_the_frame()
{
	const auto& t = faces::defaults();

	// A face comfortably inside a large frame.
	const auto inside = faces::align_transform(scaled_template(1.5, {400.0, 300.0}));
	assert_equal(false, faces::alignment_is_clipped(inside, {1200, 900}, t),
	             "a face inside the frame aligns"sv);

	// The same face at the very edge, so most of the crop would be padding. A half face embeds to
	// something confidently similar to other half faces, which is the worst kind of wrong.
	const auto at_edge = faces::align_transform(scaled_template(1.5, {-140.0, 300.0}));
	assert_equal(true, faces::alignment_is_clipped(at_edge, {1200, 900}, t),
	             "a face falling off the edge is refused"sv);

	const auto rotated_source_crop = [](const pointd center, const double radians)
	{
		const auto c = std::cos(radians);
		const auto s = std::sin(radians);
		constexpr auto mid = 56.0;
		const affined to_source{c, s, -s, c, center.X - c * mid + s * mid, center.Y - s * mid - c * mid};
		return to_source.invert();
	};

	constexpr auto quarter_turn = 3.14159265358979323846 / 4.0;
	const auto mostly_inside = rotated_source_crop({100.0, 50.0}, quarter_turn);
	assert_equal(false, faces::alignment_is_clipped(mostly_inside, {200, 100}, t),
	             "rotated clipping measures the actual crop polygon"sv);

	const auto mostly_outside = rotated_source_crop({100.0, 0.0}, quarter_turn);
	assert_equal(true, faces::alignment_is_clipped(mostly_outside, {200, 100}, t),
	             "rotated crops over the padding threshold are refused"sv);
}

static void should_compare_face_vectors_by_cosine()
{
	const auto a = unit_vector(0);
	auto same = unit_vector(0);
	const auto orthogonal = unit_vector(1);

	assert_near(1.0, faces::similarity(a, same), 1e-6, "identical vectors score one"sv);
	assert_near(0.0, faces::similarity(a, orthogonal), 1e-6, "unrelated vectors score zero"sv);

	// Normalising is what makes every later comparison a plain dot product.
	faces::face_vector unnormalised{};
	unnormalised[0] = 3.0f;
	unnormalised[1] = 4.0f;
	faces::normalise(unnormalised);
	assert_near(0.6, unnormalised[0], 1e-6, "normalising divides by the length"sv);
	assert_near(0.8, unnormalised[1], 1e-6, "normalising divides by the length"sv);

	// A zero vector has no direction. Scaling it would make it resemble everything, so it is left
	// alone and compares as similar to nothing.
	faces::face_vector zero{};
	faces::normalise(zero);
	assert_near(0.0, faces::similarity(zero, a), 1e-6, "a zero vector resembles nothing"sv);

	// A failed model or corrupt stored row must not let NaN escape into sorting and grouping.
	auto invalid = unit_vector(0);
	invalid[1] = std::numeric_limits<float>::quiet_NaN();
	faces::normalise(invalid);
	assert_near(0.0, faces::similarity(invalid, a), 1e-6, "a non-finite vector resembles nothing"sv);

	// The mean is what a cluster is refined around before it adopts a real face as its sample.
	const faces::face_vector pair[]{planar_vector(0.0), planar_vector(geom_pi / 2.0)};
	const auto mean = faces::mean_vector(pair);
	assert_near(1.0, faces::similarity(mean, mean), 1e-6, "the mean is normalised"sv);
	assert_near(faces::similarity(mean, pair[0]), faces::similarity(mean, pair[1]), 1e-6,
	            "the mean sits between the faces it was built from"sv);

	faces::normalise(same);
	assert_near(1.0, faces::similarity(a, same), 1e-6, "normalising a unit vector changes nothing"sv);
}

static faces::candidate make_candidate(const std::string_view path, const int index,
                                       const faces::face_vector& vec, const double rank)
{
	faces::candidate c;
	c.ref.path = str::cache(path);
	c.ref.index = index;
	c.vec = vec;
	c.rank = rank;
	return c;
}

static void should_not_chain_similar_faces_into_one_group()
{
	// A resembles B and B resembles C, but A does not resemble C. Union-find over the same tolerance
	// relation is single-linkage clustering, and it would collapse all three into one group that is
	// really two. Measuring against the anchor alone is what stops it, so this is the discriminating
	// test for the anchored construction.
	faces::tuning t;
	t.similar = 0.80f;

	const auto step = std::acos(0.85);
	const auto a = planar_vector(0.0);
	const auto b = planar_vector(step);
	const auto c = planar_vector(step * 2.0);

	assert_equal(true, faces::similarity(a, b) > t.similar, "a resembles b"sv);
	assert_equal(true, faces::similarity(b, c) > t.similar, "b resembles c"sv);
	assert_equal(false, faces::similarity(a, c) > t.similar, "a does not resemble c"sv);

	std::vector candidates{
		make_candidate("a.jpg"sv, 0, a, 0.9),
		make_candidate("b.jpg"sv, 0, b, 0.8),
		make_candidate("c.jpg"sv, 0, c, 0.7),
	};

	const auto result = faces::build_groups(candidates, t);

	assert_equal(2, static_cast<int>(result.groups.size()), "the chain does not collapse"sv);
	assert_equal(2, static_cast<int>(result.groups[0].members.size()), "a leads the first group"sv);
	assert_equal(1, static_cast<int>(result.groups[1].members.size()), "c anchors one of its own"sv);

	auto searchable = result.groups;
	assert_equal(true, faces::match_group_members(searchable, candidates, t), "search membership is projected"sv);
	assert_equal(2, static_cast<int>(searchable[0].members.size()), "the first search finds a and b"sv);
	assert_equal(2, static_cast<int>(searchable[1].members.size()), "the second search finds b and c without bridging a to c"sv);
	for (const auto& group : searchable)
	{
		for (const auto& candidate : candidates)
		{
			const auto expected = faces::similarity(candidate.vec, group.anchor) >= t.similar;
			assert_equal(expected, std::ranges::find(group.members, candidate.ref) != group.members.end(),
			             "projected membership agrees with the search predicate"sv);
		}
	}
}

static void should_keep_refined_faces_searchable()
{
	faces::tuning tuning;
	tuning.similar = 0.8f;
	std::vector candidates{
		make_candidate("seed.jpg"sv, 0, planar_vector(0), 1.0),
		make_candidate("outlier.jpg"sv, 0, planar_vector(-0.6), 0.7),
		make_candidate("other.jpg"sv, 0, planar_vector(geom_pi), 0.6)
	};
	for (auto index = 0; index < 12; ++index)
		candidates.emplace_back(make_candidate(std::format("near-{}.jpg", index), 0, planar_vector(0.6), 0.8));
	const auto result = faces::build_groups(candidates, tuning);
	for (const auto& candidate : candidates)
		assert_equal(true, std::ranges::any_of(result.groups, [&candidate, &tuning](const faces::group& group)
		{
			return faces::similarity(candidate.vec, group.anchor) >= tuning.similar;
		}), "every detected face retains a searchable anchor after refinement"sv);
}

static void should_match_face_groups_like_brute_force()
{
	std::vector<faces::candidate> candidates;
	std::vector<faces::group> groups;
	uint32_t random = 0x12345678;
	for (auto index = 0; index < 256; ++index)
	{
		faces::face_vector vector{};
		for (auto dimension = 0; dimension < 8; ++dimension)
		{
			random = random * 1664525u + 1013904223u;
			vector[dimension] = static_cast<float>(random >> 8) / 8388608.0f - 1.0f;
		}
		faces::normalise(vector);
		candidates.emplace_back(make_candidate(std::format("vector-{}.jpg", index), 0, vector, 1.0));
		if (index % 2 == 0)
		{
			faces::group group;
			group.anchor = vector;
			groups.emplace_back(std::move(group));
		}
	}
	for (const auto threshold : {0.0f, 0.55f, 0.8f, 1.0f})
	{
		auto tuning = faces::defaults();
		tuning.similar = threshold;
		assert_equal(true, faces::match_group_members(groups, candidates, tuning), "the range index completes"sv);
		for (const auto& group : groups)
			for (const auto& candidate : candidates)
				assert_equal(faces::similarity(group.anchor, candidate.vec) >= threshold,
					std::ranges::find(group.members, candidate.ref) != group.members.end(), "range pruning agrees with brute force"sv);
	}
}

// faces.md: a cluster adopts a real face as its sample and re-anchors on that face's own vector, so
// what a tile counts is what a search for that face returns. A mean vector is nobody's face: it
// cannot be shown, cannot be spelled, and a count taken against it would not be the count a search
// returns.
static void should_identify_a_group_by_its_sample_face()
{
	faces::tuning t;
	t.similar = 0.50f;

	const auto make = [](const std::string_view path, const double angle, const double frontality,
	                     const double rank)
	{
		auto c = make_candidate(path, 0, planar_vector(angle), rank);
		c.frontality = frontality;
		c.crop = {0, 0, 300, 300};
		return c;
	};

	// Three faces around one appearance, and a fourth that resembles none of them. The face that
	// stands for the cluster is the square-on one, not the one the ordering led with.
	std::vector candidates{
		make("turned.jpg"sv, -0.4, 0.2, 0.95),
		make("square-on.jpg"sv, 0.0, 0.95, 0.5),
		make("also-turned.jpg"sv, 0.4, 0.3, 0.6),
		make("stranger.jpg"sv, geom_pi, 0.9, 0.9),
	};

	const auto lookup = [&candidates](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(candidates, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == candidates.end() ? nullptr : &*found;
	};

	auto groups = faces::build_groups(candidates, t).groups;
	assert_equal(true, faces::adopt_sample_faces(groups, candidates, lookup, t), "adoption completes"sv);
	assert_equal(2, static_cast<int>(groups.size()), "the stranger is a group of its own"sv);

	const auto found = std::ranges::find_if(groups, [](const faces::group& g)
	{
		return g.sample.path.sv() == "square-on.jpg"sv;
	});
	assert_equal(true, found != groups.end(), "the square-on face stands for the cluster"sv);

	// The anchor IS the sample's vector, which is the whole point: the count on a tile and the
	// results of the term that spells its sample are then the same set.
	assert_near(1.0, faces::similarity(found->anchor, lookup(found->sample)->vec), 1e-6,
	            "a group is anchored on its sample's own vector"sv);

	for (const auto& c : candidates)
	{
		const auto expected = faces::similarity(c.vec, found->anchor) >= t.similar;
		assert_equal(expected, std::ranges::find(found->members, c.ref) != found->members.end(),
		             "membership is what a search for the sample returns"sv);
	}

	// Two clusters that settle on one face are one group: they would draw the same tile, spell the
	// same term and return the same files.
	auto duplicated = groups;
	duplicated.emplace_back(groups.front());
	assert_equal(true, faces::adopt_sample_faces(duplicated, candidates, lookup, t), "adoption completes again"sv);
	assert_equal(static_cast<int>(groups.size()), static_cast<int>(duplicated.size()),
	             "a repeated sample does not produce a second group"sv);
}

// faces.md: neither clustering nor adoption may leave a face behind. A face in no group has no tile,
// no caption and no term, so nothing in the product can reach it - and the user cannot tell it apart
// from a face the detector never found.
//
// The discriminating case is a cluster whose sample lands on one side of it. Refinement only
// promises every member is within the threshold of the MEAN, so a member on the far side can be
// outside the sample's own threshold once the mean is replaced by a real face.
static void should_leave_no_face_out_of_every_group()
{
	auto t = faces::defaults();

	const auto make = [](const std::string_view path, const double degrees, const double frontality,
	                     const double rank)
	{
		auto c = make_candidate(path, 0, planar_vector(degrees * geom_pi / 180.0), rank);
		c.frontality = frontality;
		c.crop = {0, 0, 300, 300};
		return c;
	};

	// The cluster leads at 0 degrees, its weight sits at +50, and one face sits at -20. Every one of
	// them is within the threshold of the leader and then of the mean, so they form one group - but
	// the most frontal face is out at +50, and -20 is 70 degrees away from it.
	std::vector candidates{
		make("leader.jpg"sv, 0.0, 0.60, 1.0),
		make("frontal.jpg"sv, 50.0, 0.95, 0.8),
		make("beside-it.jpg"sv, 52.0, 0.60, 0.7),
		make("beside-it-too.jpg"sv, 48.0, 0.60, 0.6),
		make("far-side.jpg"sv, -20.0, 0.60, 0.5),
	};

	const auto lookup = [&candidates](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(candidates, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == candidates.end() ? nullptr : &*found;
	};

	auto groups = faces::build_groups(candidates, t).groups;
	assert_equal(1, static_cast<int>(groups.size()), "the faces cluster around one mean"sv);

	const auto* const chosen = faces::sample_face(groups[0], lookup, t);
	assert_equal("frontal.jpg"sv, chosen->ref.path.sv(), "the face turned to the camera stands for the cluster"sv);
	assert_equal(true, faces::similarity(chosen->vec, candidates.back().vec) < t.similar,
	             "and the face on the far side is outside its threshold"sv);

	assert_equal(true, faces::adopt_sample_faces(groups, candidates, lookup, t), "adoption completes"sv);

	for (const auto& c : candidates)
	{
		const auto reached = std::ranges::any_of(groups, [&c](const faces::group& g)
		{
			return std::ranges::find(g.members, c.ref) != g.members.end();
		});
		assert_equal(true, reached, std::format("{} belongs to a group", c.ref.path.sv()));

		// Reachable is not enough on its own: every group answers a search for its sample, so a face
		// only counts as reachable if the group holding it would return it.
		const auto searchable = std::ranges::any_of(groups, [&c, &t](const faces::group& g)
		{
			return faces::similarity(c.vec, g.anchor) >= t.similar;
		});
		assert_equal(true, searchable, std::format("{} answers a search for some sample", c.ref.path.sv()));
	}

	// The rescue is a group, not a loose end: it is anchored on a real face, so it is spelled and
	// counted like every other.
	for (const auto& g : groups)
	{
		assert_equal(false, str::is_empty(g.sample.path), "every group carries a sample face"sv);
		assert_equal(true, std::ranges::find(g.members, g.sample) != g.members.end(),
		             "and a sample is a member of its own group"sv);
	}
}

static void should_place_a_face_between_two_groups()
{
	// A near-tie is placed rather than discarded: discarding costs the user a face they can neither
	// see nor search for, and buys nothing, because a resemblance search does not need an exclusive
	// claim to be useful.
	faces::tuning t;
	t.similar = 0.50f;

	std::vector candidates{
		make_candidate("left.jpg"sv, 0, planar_vector(-0.6), 0.9),
		make_candidate("right.jpg"sv, 0, planar_vector(0.6), 0.8),
		make_candidate("between.jpg"sv, 0, planar_vector(0.02), 0.1),
	};

	const auto result = faces::build_groups(candidates, t);

	assert_equal(0, static_cast<int>(result.ungrouped.size()), "a near-tie is still placed"sv);
	assert_equal(2, static_cast<int>(result.groups.size()), "an uncertain face does not mint a third group"sv);

	auto checks = 0;
	const auto cancelled = faces::build_groups(candidates, t, [&checks] { return ++checks == 3; });
	assert_equal(true, cancelled.cancelled, "grouping can be cancelled after it starts"sv);
	assert_equal(true, cancelled.groups.empty(), "cancelled grouping never returns a partial answer"sv);
}

static void should_store_face_geometry_upright()
{
	// A portrait phone photograph decodes landscape and is turned upright at draw time, so a box
	// measured on the decode is on its side by the time anything draws it. The stored form is the
	// upright one; this is the mapping that gets it there.
	constexpr sizei stored{4000, 3000};

	const auto upright = faces::upright_extent(stored, ui::orientation::right_top);
	assert_equal(3000, upright.cx, "an exif-6 photograph is displayed transposed"sv);
	assert_equal(4000, upright.cy, "an exif-6 photograph is displayed transposed"sv);

	// The stored top-left corner is displayed at the top right, which is what "rotate 90 clockwise
	// to view" means.
	const auto corner = faces::to_upright(pointd{0, 0}, stored, ui::orientation::right_top);
	assert_near(3000.0, corner.X, 1e-9, "the stored origin lands on the upright right edge"sv);
	assert_near(0.0, corner.Y, 1e-9, "the stored origin stays at the top"sv);

	const auto face = faces::to_upright(rectd{100, 200, 60, 80}, stored, ui::orientation::right_top);
	assert_near(2720.0, face.X, 1e-9, "the box left edge comes from the stored bottom"sv);
	assert_near(100.0, face.Y, 1e-9, "the box top edge comes from the stored left"sv);
	assert_near(80.0, face.Width, 1e-9, "a right-angle turn swaps the box extents"sv);
	assert_near(60.0, face.Height, 1e-9, "a right-angle turn swaps the box extents"sv);

	// An upright picture is not moved, and neither is one whose decoder already turned it.
	const auto untouched = faces::to_upright(rectd{100, 200, 60, 80}, stored, ui::orientation::top_left);
	assert_near(100.0, untouched.X, 1e-9, "an upright decode is left alone"sv);
	assert_near(200.0, untouched.Y, 1e-9, "an upright decode is left alone"sv);

	// Every orientation stays inside the upright frame. A mapping that escapes it would draw a box
	// off the picture, which is the failure this whole space exists to prevent.
	for (auto o = static_cast<int>(ui::orientation::top_left); o <= static_cast<int>(ui::orientation::left_bottom);
	     ++o)
	{
		const auto orientation = static_cast<ui::orientation>(o);
		const auto frame = faces::upright_extent(stored, orientation);
		const auto mapped = faces::to_upright(rectd{0, 0, 4000, 3000}, stored, orientation);

		assert_near(0.0, mapped.X, 1e-9, "the whole picture maps to the whole upright frame"sv);
		assert_near(0.0, mapped.Y, 1e-9, "the whole picture maps to the whole upright frame"sv);
		assert_near(static_cast<double>(frame.cx), mapped.Width, 1e-9, "the mapped picture fills its frame"sv);
		assert_near(static_cast<double>(frame.cy), mapped.Height, 1e-9, "the mapped picture fills its frame"sv);
	}
}

// faces.md: a group survives regrouping because it is identified by a face in a file rather than by
// an id the next pass is free to reassign. The same collection therefore settles on the same sample,
// and a term written down last session still means the same faces.
static void should_settle_on_the_same_sample_across_passes()
{
	faces::tuning t;
	t.similar = 0.80f;

	const auto make = [](const std::string_view path, const double angle, const double frontality)
	{
		auto c = make_candidate(path, 0, planar_vector(angle), 0.5 + frontality * 0.1);
		c.frontality = frontality;
		c.crop = {0, 0, 300, 300};
		return c;
	};

	std::vector candidates{
		make("party.jpg"sv, 0.0, 0.9),
		make("holiday.jpg"sv, geom_pi / 2.0, 0.9),
	};

	const auto lookup = [&candidates](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(candidates, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == candidates.end() ? nullptr : &*found;
	};

	const auto samples = [&](std::vector<faces::candidate> input)
	{
		auto groups = faces::build_groups(input, t).groups;
		faces::adopt_sample_faces(groups, candidates, lookup, t);
		std::vector<std::string> result;
		for (const auto& g : groups) result.emplace_back(g.sample.path.sv());
		std::ranges::sort(result);
		return result;
	};

	const auto first = samples(candidates);
	assert_equal(2, static_cast<int>(first.size()), "two faces that resemble nothing else are two groups"sv);
	assert_equal("holiday.jpg"s, first[0], "each group is identified by the face it shows"sv);
	assert_equal("party.jpg"s, first[1], "each group is identified by the face it shows"sv);

	// The order faces arrive in is a function of how the collection was enumerated, and must not be
	// what a saved term depends on.
	auto reordered = candidates;
	std::ranges::reverse(reordered);
	const auto second = samples(reordered);
	assert_equal(static_cast<int>(first.size()), static_cast<int>(second.size()),
	             "a second pass over the same faces finds the same groups"sv);
	for (auto i = 0u; i < first.size(); ++i)
		assert_equal(first[i], second[i], "and settles on the same samples"sv);
}

static void should_choose_a_stable_sample_face()
{
	faces::tuning t;
	t.similar = 0.0f;

	const auto v = planar_vector(0.0);

	// b is the largest and most confident face but is turned away; a is square-on and big enough to
	// fill a tile without upscaling. A sample wants a face you can recognise, not the biggest one.
	auto turned = make_candidate("b.jpg"sv, 0, v, 0.9);
	turned.frontality = 0.2;
	turned.crop = {0, 0, 600, 600};

	auto square_on = make_candidate("a.jpg"sv, 0, v, 0.5);
	square_on.frontality = 0.98;
	square_on.crop = {0, 0, 240, 240};

	auto tiny = make_candidate("c.jpg"sv, 0, v, 0.5);
	tiny.frontality = 0.99;
	tiny.crop = {0, 0, 30, 30};

	std::vector candidates{turned, square_on, tiny};

	const auto lookup = [&candidates](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(candidates, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == candidates.end() ? nullptr : &*found;
	};

	const auto result = faces::build_groups(candidates, t);
	assert_equal(1, static_cast<int>(result.groups.size()), "everything resembles everything here"sv);

	const auto* const best = faces::sample_face(result.groups[0], lookup, t);
	assert_equal(true, best != nullptr, "a group has a sample"sv);
	assert_equal("a.jpg"sv, best->ref.path.sv(), "a square-on face of usable size stands for the group"sv);

	// The same answer whichever order the faces arrived in, or the palette reshuffles between
	// sessions and stops working as a visual index.
	std::ranges::reverse(candidates);
	const auto reordered = faces::build_groups(candidates, t);
	const auto* const again = faces::sample_face(reordered.groups[0], lookup, t);
	assert_equal("a.jpg"sv, again->ref.path.sv(), "the sample does not depend on arrival order"sv);

	// A face turned too far from the camera is passed over entirely while any member is square-on:
	// a profile makes a poor tile and a worse reference for everything measured against it.
	auto profiles = candidates;
	for (auto& c : profiles) c.frontality = 0.1;
	const auto lookup_profiles = [&profiles](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(profiles, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == profiles.end() ? nullptr : &*found;
	};
	const auto all_turned = faces::build_groups(profiles, t);
	assert_equal(true, faces::sample_face(all_turned.groups[0], lookup_profiles, t) != nullptr,
	             "a group of nothing but profiles still has a sample"sv);
}

static void should_prefer_a_sample_that_spells_only_itself()
{
	// A face term spells a file NAME, so a sample from a file whose name another file repeats asks
	// for both faces. Where two members are otherwise equal, the unambiguous one is the sample.
	faces::tuning t;
	t.similar = 0.0f;

	const auto v = planar_vector(0.0);

	auto repeated = make_candidate("IMG_0001.jpg"sv, 0, v, 0.9);
	repeated.frontality = 0.9;
	repeated.crop = {0, 0, 300, 300};
	repeated.unique_name = false;

	auto unique = make_candidate("summer-party.jpg"sv, 0, v, 0.5);
	unique.frontality = 0.9;
	unique.crop = {0, 0, 300, 300};

	std::vector candidates{repeated, unique};

	const auto lookup = [&candidates](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(candidates, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == candidates.end() ? nullptr : &*found;
	};

	const auto result = faces::build_groups(candidates, t);
	const auto* const best = faces::sample_face(result.groups[0], lookup, t);
	assert_equal("summer-party.jpg"sv, best->ref.path.sv(), "a sample that spells only itself is preferred"sv);

	// It is a preference, not a requirement: a group every one of whose files repeats a name still
	// has to be shown and still has to be searchable.
	std::vector ambiguous{repeated};
	const auto lookup_ambiguous = [&ambiguous](const faces::face_ref& ref) -> const faces::candidate*
	{
		const auto found = std::ranges::find_if(ambiguous, [&ref](const faces::candidate& c) { return c.ref == ref; });
		return found == ambiguous.end() ? nullptr : &*found;
	};
	const auto only_repeats = faces::build_groups(ambiguous, t);
	assert_equal(true, faces::sample_face(only_repeats.groups[0], lookup_ambiguous, t) != nullptr,
	             "a group whose every file repeats a name still has a sample"sv);
}

static void should_prefer_a_usable_crop_for_a_tile()
{
	// The defect this replaced: candidate_rank measures crop as a fraction of the frame, which
	// barely varies, so a 30 pixel face upscaled to 112 could win a tile from a 240 pixel one.
	const auto v = planar_vector(0.0);

	auto small = make_candidate("small.jpg"sv, 0, v, 0.9);
	small.frontality = 0.9;
	small.crop = {0, 0, 30, 30};

	auto large = make_candidate("large.jpg"sv, 0, v, 0.1);
	large.frontality = 0.9;
	large.crop = {0, 0, 240, 240};

	assert_equal(true, faces::sample_score(large, v) > faces::sample_score(small, v),
	             "a crop that fills the tile beats one that is upscaled into it"sv);
}

// faces.md: the palette is a way in to the collection, so the faces that account for most of it come
// first. A group is shown as, and identified by, the sample face it carries.
static void should_map_a_face_crop_onto_a_thumbnail()
{
	// A crop is measured on the decode the detector ran over; a cached thumbnail is whatever size it
	// happens to be. Both the sidebar tile and the face view scale between the two, so the scaling is
	// here where it can be checked rather than in each of them.
	const auto mapped = faces::crop_in(rectd{100.0, 200.0, 50.0, 80.0}, sizei{1000, 2000}, sizei{100, 200});
	assert_equal(10, mapped.left, "the crop scales with the image"sv);
	assert_equal(20, mapped.top, "on both axes"sv);
	assert_equal(15, mapped.right, "and keeps its width"sv);
	assert_equal(28, mapped.bottom, "and its height"sv);

	// A crop running past the edge is clamped rather than refused: the caller copies rows out of the
	// result without re-checking it, so an out-of-range answer would read off the end of the surface.
	const auto clamped = faces::crop_in(rectd{90.0, 90.0, 40.0, 40.0}, sizei{100, 100}, sizei{100, 100});
	assert_equal(true, clamped.right <= 100, "a crop past the right edge is clamped inside"sv);
	assert_equal(true, clamped.bottom <= 100, "and past the bottom"sv);
	assert_equal(true, clamped.width() >= 1 && clamped.height() >= 1, "and never collapses to nothing"sv);

	assert_equal(true, faces::crop_in(rectd{0, 0, 10, 10}, sizei{}, sizei{100, 100}).is_empty(),
	             "a crop with no space to be measured in maps to nothing"sv);
}

static void should_move_a_face_to_the_group_it_ended_up_nearest()
{
	// A group's anchor starts as the face that led it and moves as the group accretes, so a face
	// placed early can end up nearer a group that did not exist when it was placed. Measured on a 30k
	// corpus: one face sat in its group at 0.686 while being 0.717 from another. Refinement is what
	// corrects that, and this is the shape it has to correct.
	faces::tuning t;
	t.similar = 0.625f;

	// The left group drifts away from the stray as its own members pull the centroid negative; the
	// right group forms later and closes on it.
	std::vector candidates{
		make_candidate("left1.jpg"sv, 0, planar_vector(0.0), 1.00),
		make_candidate("left2.jpg"sv, 0, planar_vector(-0.7), 0.95),
		make_candidate("left3.jpg"sv, 0, planar_vector(-0.6), 0.90),
		make_candidate("stray.jpg"sv, 0, planar_vector(0.5), 0.85),
		make_candidate("right1.jpg"sv, 0, planar_vector(1.0), 0.80),
		make_candidate("right2.jpg"sv, 0, planar_vector(0.7), 0.75),
		make_candidate("right3.jpg"sv, 0, planar_vector(0.8), 0.70),
	};

	const auto result = faces::build_groups(candidates, t);

	const auto group_holding = [&result](const std::string_view file) -> size_t
	{
		for (auto i = 0u; i < result.groups.size(); ++i)
		{
			for (const auto& m : result.groups[i].members)
			{
				if (icmp(m.path, file) == 0) return i;
			}
		}
		return result.groups.size();
	};

	assert_equal(2, static_cast<int>(result.groups.size()), "two groups form"sv);
	assert_equal(group_holding("right1.jpg"sv), group_holding("stray.jpg"sv),
	             "the stray ends up with the group it is nearest, not the one that claimed it first"sv);
	assert_equal(group_holding("left1.jpg"sv), group_holding("left2.jpg"sv),
	             "and the group it left keeps its own"sv);
	assert_equal(0, static_cast<int>(result.ungrouped.size()), "refinement loses nobody"sv);
}

static void should_order_face_groups_so_similar_ones_are_adjacent()
{
	// The view shows every group at once, so the order decides what a user can compare. Two groups of
	// one appearance - which is what a face model that cannot span ten years produces - must land next
	// to each other, or comparing them means hunting for the second one.
	const auto entry = [](const std::string_view path, const int count, const double angle)
	{
		faces::palette_entry e;
		e.sample = {str::cache(path), 0};
		e.term = std::string(path);
		e.file_count = count;
		e.anchor = planar_vector(angle);
		return e;
	};

	// Two neighbourhoods a quarter turn apart, interleaved on arrival and with the sizes deliberately
	// crossed so that size ordering alone could not produce the answer.
	std::vector<faces::palette_entry> entries{
		entry("a1.jpg"sv, 40, 0.0),
		entry("b1.jpg"sv, 30, geom_pi / 2.0),
		entry("a2.jpg"sv, 20, 0.2),
		entry("b2.jpg"sv, 10, geom_pi / 2.0 + 0.2),
	};

	faces::order_by_similarity(entries);

	assert_equal(4, static_cast<int>(entries.size()), "every group is still listed"sv);

	const auto neighbour_of = [&entries](const std::string_view term)
	{
		for (auto i = 0u; i < entries.size(); ++i)
		{
			if (entries[i].term == term) return i;
		}
		return static_cast<unsigned>(entries.size());
	};

	assert_equal(1u, neighbour_of("a2.jpg"sv) - neighbour_of("a1.jpg"sv),
	             "the two that resemble each other are adjacent"sv);
	assert_equal(1u, neighbour_of("b2.jpg"sv) - neighbour_of("b1.jpg"sv),
	             "and so are the other two"sv);
	assert_equal("a1.jpg"s, entries[0].term, "the largest neighbourhood leads, and its largest leads it"sv);
}

static void should_order_the_face_palette_by_size()
{
	const auto entry = [](const int count, const std::string_view path, const int index)
	{
		faces::palette_entry e;
		e.sample = {str::cache(path), index};
		e.term = faces::format_face_token(path, index);
		e.file_count = count;
		return e;
	};

	std::vector<faces::palette_entry> entries{
		entry(3, "alice.jpg"sv, 0),
		entry(40, "big.jpg"sv, 0),
		entry(12, "zoe.jpg"sv, 0),
		entry(9, "small.jpg"sv, 0),
		// Two groups of one size must not swap places between sessions, so the sample - which is what
		// a group IS - breaks the tie.
		entry(9, "small.jpg"sv, 1),
	};

	faces::order_palette(entries);

	assert_equal(40, entries[0].file_count, "the largest group leads"sv);
	assert_equal(12, entries[1].file_count, "and the order is by size throughout"sv);
	assert_equal("small.jpg"s, entries[2].term, "the first of two equal groups is the earlier face"sv);
	assert_equal("small.jpg:2"s, entries[3].term, "and the second is the later one"sv);
	assert_equal(3, entries[4].file_count, "including the smallest"sv);
}
static void should_show_twenty_distinct_sidebar_faces()
{
	std::vector<faces::palette_entry> entries;
	for (auto index = 0; index < 24; ++index)
	{
		faces::palette_entry entry;
		entry.sample = {str::cache(std::format("face-{:02}.jpg", index)), 0};
		entry.term = faces::format_face_token(entry.sample.path.sv(), entry.sample.index);
		entry.file_count = 100 - index;
		entry.anchor = unit_vector(index);
		entries.emplace_back(std::move(entry));
	}
	entries[1].anchor = planar_vector(std::acos(0.50));
	entries[2].sample = entries[0].sample;
	auto displayed = std::make_shared<df::face_set>();
	displayed->faces.emplace_back();
	displayed->faces[0].vec = entries[0].anchor;
	entries[3].source = displayed;
	entries[23].file_count = 0;
	std::ranges::reverse(entries);
	faces::order_palette(entries);
	const auto sidebar = faces::select_sidebar_palette(entries);
	assert_equal(20, static_cast<int>(sidebar.size()), "the sidebar has at most twenty distinct faces"sv);
	assert_equal("face-00.jpg"s, sidebar[0].term, "the largest result set wins duplicate suppression"sv);
	assert_equal("face-04.jpg"s, sidebar[1].term, "similar anchors, repeated samples, and similar displayed faces are skipped"sv);
	assert_equal("face-22.jpg"s, sidebar.back().term, "distinct entries below the original top twenty fill the sidebar"sv);
	// The shortlist is a view of the projection and never a replacement for it: the groups it leaves
	// out are still listed in full by the face view, which the sidebar's entry row opens.
	assert_equal(24, static_cast<int>(entries.size()), "shortlisting does not change the full face projection"sv);
	for (size_t index = 1; index < sidebar.size(); ++index)
		assert_equal(true, sidebar[index - 1].file_count >= sidebar[index].file_count,
		             "sidebar entries stay ranked by their actual result counts"sv);

	const auto single = faces::select_sidebar_palette(std::span<const faces::palette_entry>(entries.data(), 4));
	assert_equal(1, static_cast<int>(single.size()), "duplicates are not used to pad a short sidebar"sv);
	assert_equal(true, faces::select_sidebar_palette({}).empty(), "an empty projection has no tiles"sv);
}

static void should_store_and_reload_faces()
{
	df::face_set fs;
	fs.model_version = faces::model_version;
	fs.scanned = platform::now();
	fs.source_extent = {1600, 1200};

	df::stored_face f;
	f.score = 88.0f;
	f.bounds = rectd{10.0, 20.0, 130.0, 140.0};

	for (auto i = 0; i < faces::landmark_count; ++i)
	{
		f.marks[i] = pointd{10.0 + i, 20.0 + (i * 2)};
	}

	for (auto i = 0; i < faces::vector_size; ++i)
	{
		f.vec[i] = static_cast<float>(i) / faces::vector_size;
	}

	fs.faces.emplace_back(f);
	fs.faces.emplace_back(f);

	const auto encoded = df::encode_faces(fs);
	const auto decoded = df::decode_faces(encoded);

	assert_equal(faces::model_version, decoded.model_version, "the model version travels with the vectors"sv);
	assert_equal(fs.scanned, decoded.scanned, "when it was scanned survives"sv);
	assert_equal(1600, decoded.source_extent.cx, "the coordinate space the boxes are in survives"sv);
	assert_equal(2, static_cast<int>(decoded.faces.size()), "both faces survive"sv);
	assert_equal(88.0, decoded.faces[0].score, "the detector score survives"sv);
	assert_equal(130.0, decoded.faces[0].bounds.Width, "the box survives"sv);
	assert_equal(14.0, decoded.faces[0].marks[4].X, "the landmarks survive"sv);
	assert_near(f.vec[64], decoded.faces[0].vec[64], 1e-6, "the vector survives"sv);

	auto edge = fs;
	edge.faces[0].bounds = rectd{-40.0, 20.0, 130.0, 140.0};
	const auto decoded_edge = df::decode_faces(df::encode_faces(edge));
	assert_equal(faces::model_version, decoded_edge.model_version,
	             "an edge face extending outside the frame remains current"sv);
	assert_equal(-40.0, decoded_edge.faces[0].bounds.X, "edge geometry stays unclamped in storage"sv);

	// A truncated blob is treated as unscanned rather than half-read, so the pass fills it again
	// instead of publishing faces that are not all there.
	const df::cspan truncated{encoded.data(), encoded.size() - 40};
	assert_equal(0, static_cast<int>(df::decode_faces(truncated).faces.size()), "a truncated row answers nothing"sv);
	assert_equal(0, static_cast<int>(df::decode_faces(truncated).model_version),
	             "a truncated row is not current"sv);
	auto short_stride = encoded.clone();
	short_stride[6] = 1;
	short_stride[7] = 0;
	assert_equal(0, static_cast<int>(df::decode_faces(short_stride).model_version),
	             "a short record stride is not current"sv);
	auto non_finite = encoded.clone();
	const auto nan = std::numeric_limits<float>::quiet_NaN();
	memcpy(non_finite.data() + 36, &nan, sizeof(nan));
	assert_equal(0, static_cast<int>(df::decode_faces(non_finite).model_version),
	             "a non-finite face record is not current"sv);
	auto infinite = encoded.clone();
	const auto inf = std::numeric_limits<float>::infinity();
	memcpy(infinite.data() + 36, &inf, sizeof(inf));
	assert_equal(0, static_cast<int>(df::decode_faces(infinite).model_version),
	             "an infinite face coordinate is not current"sv);
	auto extreme = encoded.clone();
	const auto huge = 1e30f;
	memcpy(extreme.data() + 36, &huge, sizeof(huge));
	assert_equal(0, static_cast<int>(df::decode_faces(extreme).model_version),
	             "a finite but out-of-bounds face coordinate is not current"sv);
	auto empty = fs;
	empty.faces[0].bounds.Width = 0.0;
	assert_equal(0, static_cast<int>(df::decode_faces(df::encode_faces(empty)).model_version),
	             "an empty face rectangle is not current"sv);
	auto missing_extent = fs;
	missing_extent.source_extent = {};
	assert_equal(0, static_cast<int>(df::decode_faces(df::encode_faces(missing_extent)).model_version),
	             "faces without a coordinate space are not current"sv);
	assert_equal(0, static_cast<int>(df::decode_faces({}).faces.size()), "an empty row answers nothing"sv);
}

static void should_treat_a_model_change_as_unscanned()
{
	df::face_set fs;
	fs.model_version = faces::model_version;
	fs.scanned = df::date_t(2026, 8, 31, 12, 0, 0);
	assert_equal(true, fs.is_current(), "this build's own rows are current"sv);
	assert_equal(true, fs.is_current(df::date_t(2026, 8, 31, 11, 0, 0)),
	             "a face row newer than the file is current"sv);
	assert_equal(false, fs.is_current(df::date_t(2026, 8, 31, 13, 0, 0)),
	             "a modified file makes its old faces stale"sv);

	// The vector's meaning is the joint product of the detector, the template and the model. A row
	// stamped by any other combination cannot be compared against one stamped by this build.
	fs.model_version = faces::model_version + 1;
	assert_equal(false, fs.is_current(), "a row from another model is not current"sv);

	fs.model_version = 0;
	assert_equal(false, fs.is_current(), "a row with no stamp is not current"sv);
}

// faces.md: how a face is written down - the file's own name and the 1-based position of the face in
// that file's stable order, with `:1` left off because most pictures hold one face. The spelling is
// the foundation a face search is written in, so it is parsed and formatted here rather than by
// whoever composes the query.
static void should_parse_face_tokens()
{
	// What the term means, taken apart. `:1` is left off because most pictures hold one face.
	faces::face_token token;
	assert_equal(true, faces::parse_face_token("IMG_1234.jpg:2"sv, token), "a term with a position parses"sv);
	assert_equal("IMG_1234.jpg"s, token.name, "the file is everything before the position"sv);
	assert_equal(1, token.index, "the text is 1-based and the index is not"sv);

	assert_equal(true, faces::parse_face_token("IMG_1234.jpg"sv, token), "a term without a position parses"sv);
	assert_equal("IMG_1234.jpg"s, token.name, "the whole of it is the file"sv);
	assert_equal(0, token.index, "and it means the first face"sv);

	// A file whose own name ends in digits after a colon is the ambiguity the rule has to settle, and
	// it settles the way the spelling reads: the last colon introduces a position.
	assert_equal(true, faces::parse_face_token("holiday.jpg:12"sv, token), "a two-digit position parses"sv);
	assert_equal(11, token.index, "and counts from one"sv);

	assert_equal(false, faces::parse_face_token("IMG_1234.jpg:0"sv, token), "there is no zeroth face"sv);
	assert_equal(false, faces::parse_face_token("  "sv, token), "and nothing is not a face"sv);

	assert_equal("IMG_1234.jpg"s, faces::format_face_token("IMG_1234.jpg"sv, 0), "the first face is just the file"sv);
	assert_equal("IMG_1234.jpg:3"s, faces::format_face_token("IMG_1234.jpg"sv, 2), "and any other carries its position"sv);
}

// faces.md: the position in a face term counts a stable order, not the order the detector happened
// to scan the frame in. Without this a term written down today means a different face tomorrow.
static void should_order_the_faces_in_a_file_stably()
{
	const auto face = [](const double x, const double y, const double extent)
	{
		df::stored_face f;
		f.bounds = rectd{x, y, extent, extent};
		return f;
	};

	std::vector<df::stored_face> detected{
		face(400, 100, 80),
		face(100, 300, 60),
		face(100, 100, 50),
		face(100, 100, 90),
	};

	auto ordered = detected;
	faces::order_faces(ordered);

	assert_near(100.0, ordered[0].bounds.X, 1e-9, "reading order runs left to right"sv);
	assert_near(100.0, ordered[0].bounds.Y, 1e-9, "then top to bottom"sv);
	assert_near(90.0, ordered[0].bounds.Width, 1e-9, "and the larger of two boxes in one place leads"sv);
	assert_near(50.0, ordered[1].bounds.Width, 1e-9, "followed by the smaller"sv);
	assert_near(300.0, ordered[2].bounds.Y, 1e-9, "then the face below them"sv);
	assert_near(400.0, ordered[3].bounds.X, 1e-9, "and the one further right comes last"sv);

	// The detector's output order is a function of how it scanned the frame, so the answer must not
	// depend on it.
	std::ranges::reverse(detected);
	auto reordered = detected;
	faces::order_faces(reordered);

	for (auto i = 0u; i < ordered.size(); ++i)
	{
		assert_near(ordered[i].bounds.X, reordered[i].bounds.X, 1e-9, "the order does not depend on arrival order"sv);
		assert_near(ordered[i].bounds.Y, reordered[i].bounds.Y, 1e-9, "the order does not depend on arrival order"sv);
		assert_near(ordered[i].bounds.Width, reordered[i].bounds.Width, 1e-9,
		            "the order does not depend on arrival order"sv);
	}
}

static void should_normalize_face_detector_luminance()
{
	const auto packed = std::make_shared<ui::surface>();
	const auto planar = std::make_shared<ui::surface>();
	assert_equal(true, packed->alloc({4, 2}, ui::texture_format::RGB) != nullptr, "packed luma fixture"sv);
	assert_equal(true, planar->alloc({4, 2}, ui::texture_format::NV12) != nullptr, "planar luma fixture"sv);
	ui::surface_ptr scratch;
	std::vector<uint8_t> packed_luma;
	std::vector<uint8_t> planar_luma;
	for (const auto value : {0, 128, 255})
	{
		for (auto row = 0; row < 2; ++row)
			std::fill_n(packed->pixels_line(row), 16, static_cast<uint8_t>(value));
		assert_equal(true, faces::reduce_to_luma(packed, {2, 2}, scratch, packed_luma), "packed reduction"sv);
		assert_equal(value, static_cast<int>(packed_luma[0]), "packed grey preserves full-range luminance"sv);
		for (const auto limited : {false, true})
		{
			planar->color_space(limited ? ui::color_space::rec601_limited : ui::color_space::rec601_full);
			const auto sample = limited ? 16 + (value * 219 + 127) / 255 : value;
			for (auto row = 0; row < 2; ++row)
				std::fill_n(planar->pixels_line(row), 4, static_cast<uint8_t>(sample));
			assert_equal(true, faces::reduce_to_luma(planar, {2, 2}, scratch, planar_luma), "planar reduction"sv);
			assert_equal(packed_luma == planar_luma, true, "decoder range does not change detector brightness"sv);
		}
	}
}

void register_faces_tests(view_state& state, test_registry& tests)
{
	tests.add("Should fit face landmarks to the reference template"s, should_fit_landmarks_to_the_reference_template);
	tests.add("Should align faces without shearing them"s, should_align_faces_without_shearing_them);
	// SRC-040 - rotated crop clipping measures the actual polygon, not its bounding box.
	tests.add("Should refuse a face alignment that leaves the frame"s,
	          should_refuse_a_face_alignment_that_leaves_the_frame);
	tests.add("Should compare face vectors by cosine"s, should_compare_face_vectors_by_cosine);
	tests.add("Should not chain similar faces into one group"s, should_not_chain_similar_faces_into_one_group);
	tests.add("Should identify a face group by its sample face"s, should_identify_a_group_by_its_sample_face);
	tests.add("Should leave no face out of every face group"s, should_leave_no_face_out_of_every_group);
	tests.add("Should place a face between two face groups"s, should_place_a_face_between_two_groups);
	tests.add("Should store face geometry upright"s, should_store_face_geometry_upright);
	tests.add("Should settle on the same face sample across passes"s,
	          should_settle_on_the_same_sample_across_passes);
	tests.add("Should choose a stable face sample"s, should_choose_a_stable_sample_face);
	tests.add("Should prefer a face sample that spells only itself"s,
	          should_prefer_a_sample_that_spells_only_itself);
	tests.add("Should prefer a usable face crop for a tile"s, should_prefer_a_usable_crop_for_a_tile);
	tests.add("Should order the face palette by size"s, should_order_the_face_palette_by_size);
	tests.add("Should map a face crop onto a thumbnail"s, should_map_a_face_crop_onto_a_thumbnail);
	tests.add("Should order face groups so similar ones are adjacent"s,
	          should_order_face_groups_so_similar_ones_are_adjacent);
	tests.add("Should move a face to the face group it ended up nearest"s,
	          should_move_a_face_to_the_group_it_ended_up_nearest);
	tests.add("Should show twenty distinct sidebar faces"s, should_show_twenty_distinct_sidebar_faces);
	// SRC-041 - malformed face records are rejected before integer crop conversion.
	tests.add("Should store and reload faces"s, should_store_and_reload_faces);
	tests.add("Should treat a face model change as unscanned"s, should_treat_a_model_change_as_unscanned);
	tests.add("Should parse face tokens"s, should_parse_face_tokens);
	tests.add("Should order the faces in a file stably"s, should_order_the_faces_in_a_file_stably);
	tests.add("Should normalize face detector luminance"s, should_normalize_face_detector_luminance);
	tests.add("Should keep refined faces searchable"s, should_keep_refined_faces_searchable);
	tests.add("Should match face groups like brute force"s, should_match_face_groups_like_brute_force);
}

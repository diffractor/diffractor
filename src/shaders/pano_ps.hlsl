// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: draws an equirectangular panorama as the sphere it describes. One shader and one
// format: the projection needs a mip chain to survive minification and the planar video formats
// cannot carry one, so the decode hands this path packed pixels.

#include "common.hlsli"
#include "pano_project.hlsli"

Texture2D tx : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
	const float2 uv = panorama_uv(panorama_direction(input.uv));

	// Taken before the discard, while the whole quad is still live: a derivative reads across
	// neighbouring lanes, and lanes killed first would leave it undefined.
	float2 dx = ddx(uv);
	float2 dy = ddy(uv);

	// Across the file's seam the coordinate jumps by the file's own turn, and a derivative that
	// large picks the coarsest level - a blurred stripe down the join. The turn is `u_scale`, not
	// one: a partial panorama's coverage is only part of the circle, so unwrapping by one would
	// leave a partial file's edge still reading as an enormous step. The software rasteriser
	// unwraps by the same span, in its own units.
	const float turn = pano_coverage.y;

	if (abs(dx.x) > turn * 0.5f) dx.x -= sign(dx.x) * turn;
	if (abs(dy.x) > turn * 0.5f) dy.x -= sign(dy.x) * turn;

	if (!panorama_covers(uv)) discard;

	// The source alpha is not the panorama's: a packed decode of an opaque photograph carries
	// whatever the codec left in that byte. Where the file holds sphere it is opaque, and where it
	// does not the pixel is already gone.
	const float4 texel = tx.SampleGrad(tex_sampler, uv, dx, dy);
	return float4(texel.rgb, 1.0f) * input.c;
}

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "common.hlsli"

float4 main(PS_INPUT input) : SV_Target
{
	float4 cc = dither_color(input.c, input.pos.xy);

	const float d = length(input.uv * 0.6f);
	// fwidth() is abs(ddx) + abs(ddy), a cheaper approximation of the gradient magnitude;
	// length() of the same pair is exact and keeps the falloff even on diagonal edges.
	// max() keeps the interior quads (where d is constant) out of a divide by zero.
	const float pwidth = max(length(float2(ddx(d), ddy(d))), 1e-6f);
	// A 1.5 pixel falloff reads as a really smooth circle; a 1 pixel linear falloff can still look minorly aliased.
	cc.a *= smoothstep(0.0f, 1.5f, (0.5f - d) / pwidth);

	return cc;
}

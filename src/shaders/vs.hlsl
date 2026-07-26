// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "common.hlsli"

PS_INPUT main(VS_INPUT input)
{
	PS_INPUT output;

	// Client space (0..1, y down) to clip space (-1..1, y up) in a single mad.
	output.pos = float4(input.pos.xy * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), input.pos.z, input.pos.w);
	output.uv = input.uv;
	output.c = input.c;
	output.tex_size = input.tex_size;

	return output;
}

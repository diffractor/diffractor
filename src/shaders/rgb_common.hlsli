// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: single body for the RGB pixel shaders. Define USE_BICUBIC before including it to
// get the Catmull-Rom variant; the two shaders differ only in how the texel is fetched.

#include "common.hlsli"
#include "texture_transform.hlsli"

Texture2D tx : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
	float2 uv = input.uv;
	if (!transform_uv(uv)) discard;

#ifdef USE_BICUBIC
	const float4 texel = sample_texture_catmull_rom(tx, tex_sampler, uv, input.tex_size);
#else
	const float4 texel = tx.Sample(tex_sampler, uv);
#endif

	return transform_color(texel) * input.c;
}

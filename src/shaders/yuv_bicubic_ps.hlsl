// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "common.hlsli"

Texture2D tex2d1 : register(t0);
Texture2D tex2d2 : register(t1);

float4 main(PS_INPUT input) : SV_Target
{
	// YUV split over two views - NV12 
	//float y = tex2d1.Sample(tex_sampler, input.uv);
	//float2 uv = tex2d2.Sample(tex_sampler, input.uv);

	float4 y = sample_texture_catmull_rom(tex2d1, tex_sampler, input.uv, input.tex_size);
	float4 uv = sample_texture_catmull_rom(tex2d2, tex_sampler, input.uv, input.tex_size / 2.0f);

	// Return RGBA (host-supplied matrix applies the colour space and range)
	return float4(yuv_to_rgb(float3(y.x, uv.x, uv.y)), 1.0) * input.c;
}

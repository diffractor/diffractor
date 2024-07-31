// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "common.hlsli"

Texture2D tex2d1 : register(t0);
Texture2D tex2d2 : register(t1);

// BT.601 (Y [16 .. 235], U/V [16 .. 240]) with linear, full-range RGB output.
// Input YUV must be first subtracted by (0.0625, 0.5, 0.5).
static const float3x3 yuv_coef = {
	1.164f, 1.164f, 1.164f,
	0.000f, -0.392f, 2.017f,
	1.596f, -0.813f, 0.000f
};

float4 main(PS_INPUT input) : SV_Target
{
	// YUV split over two views - NV12 
	//float y = tex2d1.Sample(tex_sampler, input.uv);
	//float2 uv = tex2d2.Sample(tex_sampler, input.uv);

	float4 y = sample_texture_catmull_rom(tex2d1, tex_sampler, input.uv, input.tex_size);
	float4 uv = sample_texture_catmull_rom(tex2d2, tex_sampler, input.uv, input.tex_size / 2.0f);

	float3 yuv = float3(y.x, uv.x, uv.y);

	yuv -= float3(0.0625f, 0.5f, 0.5f);
	yuv = mul(yuv, yuv_coef);
	yuv = saturate(yuv);

	// Return RGBA
	return float4(yuv, 1.0) * input.c;
}

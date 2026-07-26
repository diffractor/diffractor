// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: edit-preview transforms (perspective warp and colour grade) applied by
// the RGB pixel shaders. Packing must match shader_transform_params in platform_win_d3d11.cpp.
//
//   tone_curve            256 curve entries packed 4 per register
//   perspective_params    x horizontal, y vertical, z has_perspective, w has_color_changes
//   color_params          x saturation, y vibrance, z red gain, w green gain
//   color_params2         x blue gain
//
// b0 is shared with yuv_params; the host binds whichever buffer matches the bound shader,
// and no shader ever includes both declarations.
cbuffer texture_transform_params : register(b0)
{
	float4 tone_curve[64];
	float4 perspective_params;
	float4 color_params;
	float4 color_params2;
};

bool transform_uv(inout float2 uv)
{
	if (perspective_params.z < 0.5)
		return true;

	const float2 normalized = uv - 0.5;
	const float denominator = 1.0 + dot(perspective_params.xy, normalized);
	if (denominator <= 0.0)
		return false;

	uv = 0.5 + normalized / denominator;
	return all(uv >= 0.0) && all(uv <= 1.0);
}

float tone_curve_value(const float luminance)
{
	// Unsigned indexing lets the shift/mask replace a signed divide and modulus (fxc X3556).
	const uint index = (uint)clamp(luminance * 256.0f, 0.0f, 255.0f);
	return tone_curve[index >> 2u][index & 3u];
}

float4 transform_color(float4 color)
{
	if (perspective_params.w < 0.5)
		return color;

	const float saturation = color_params.x;
	const float vibrance = color_params.y;
	const float3 gains = float3(color_params.z, color_params.w, color_params2.x);
	const float3 rgb = saturate(color.rgb * gains);
	float luminance = dot(rgb, float3(0.299, 0.587, 0.114));
	float chroma_u = dot(rgb, float3(-0.147, -0.289, 0.436));
	float chroma_v = dot(rgb, float3(0.615, -0.515, -0.100));

	luminance = tone_curve_value(luminance);
	if (abs(vibrance) > 0.00001)
	{
		const float2 chroma_delta = float2(-0.105 - chroma_u, 0.227 - chroma_v);
		const float adjusted_saturation = saturation * (1.0 + vibrance * length(chroma_delta) * 4.0);
		chroma_u *= adjusted_saturation;
		chroma_v *= adjusted_saturation;
	}
	else
	{
		chroma_u *= saturation;
		chroma_v *= saturation;
	}

	chroma_u = clamp(chroma_u, -1.0, 1.0);
	chroma_v = clamp(chroma_v, -1.0, 1.0);
	color.rgb = saturate(float3(
		luminance + 1.14025 * chroma_v,
		luminance - 0.39473 * chroma_u - 0.58081 * chroma_v,
		luminance + 2.03252 * chroma_u));
	return color;
}
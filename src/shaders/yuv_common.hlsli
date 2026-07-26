// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: single body for the NV12/P010 pixel shaders. Define USE_BICUBIC before including
// it to get the Catmull-Rom variant; the two shaders differ only in how the planes are fetched.

#include "common.hlsli"

// Luma and chroma views onto the same NV12/P010 surface (R8/R16 and R8G8/R16G16).
Texture2D tex_y : register(t0);
Texture2D tex_uv : register(t1);

// Affine YUV->RGB transform (3x3 matrix + bias column) supplied by the host.
// A single matrix encodes both the colour matrix (BT.601/709/2020) and the
// signal range (limited 16-235 vs full 0-255 / JPEG), so one shader handles
// every case - see compute_yuv_matrix() on the C++ side.
// b0 is shared with texture_transform_params; the host binds whichever buffer matches the
// bound shader, and no shader ever includes both declarations.
cbuffer yuv_params : register(b0)
{
	row_major float3x4 yuv_to_rgb_matrix;
};

float4 main(PS_INPUT input) : SV_Target
{
#ifdef USE_BICUBIC
	const float y = sample_texture_catmull_rom(tex_y, tex_sampler, input.uv, input.tex_size).x;
#else
	const float y = tex_y.Sample(tex_sampler, input.uv).x;
#endif

	// Chroma is half resolution on both axes and the eye resolves it far less sharply than
	// luma, so a single bilinear tap is indistinguishable from a 9 tap cubic here - and it
	// keeps the bicubic video path at 10 taps per pixel instead of 18.
	const float2 uv = tex_uv.Sample(tex_sampler, input.uv).xy;

	const float3 rgb = saturate(mul(yuv_to_rgb_matrix, float4(y, uv, 1.0f)));
	return float4(rgb, 1.0f) * input.c;
}

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

struct VS_INPUT
{
	float4 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 c : COLOR;
	float2 tex_size : EXTENT;
};

struct PS_INPUT
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 c : COLOR;
	float2 tex_size : EXTENT;
};


SamplerState tex_sampler
{
};

// Affine YUV->RGB transform (3x3 matrix + bias column) supplied by the host.
// A single matrix encodes both the colour matrix (BT.601/709/2020) and the
// signal range (limited 16-235 vs full 0-255 / JPEG), so one shader handles
// every case - see compute_yuv_matrix() on the C++ side.
cbuffer yuv_params : register(b0)
{
	row_major float3x4 yuv_to_rgb_matrix;
};

float3 yuv_to_rgb(float3 yuv)
{
	return saturate(mul(yuv_to_rgb_matrix, float4(yuv, 1.0f)));
}

float nrand(float4 pos)
{
	return frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453) / 100.0;
}

// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
float4 sample_texture_catmull_rom(in Texture2D<float4> tex, in SamplerState samp, in float2 uv, in float2 tex_size)
{
	// We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
	// down the sample location to get the exact center of our "starting" texel. The starting texel will be at
	// location [1, 1] in the grid, where [0, 0] is the top left corner.
	const float2 samplePos = uv * tex_size;
	const float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

	// Compute the fractional offset from our starting texel to our original sample location, which we'll
	// feed into the Catmull-Rom spline function to get our filter weights.
	const float2 f = samplePos - texPos1;
	//float2 f2 = f * f;

	// Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
	// These equations are pre-expanded based on our knowledge of where the texels will be located,
	// which lets us avoid having to evaluate a piece-wise function.
	float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	const float2 w1 = 1.0 + f * f * (1.5 * f - 2.5);
	const float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);

	// Work out weighting factors and sampling offsets that will let us use bilinear filtering to
	// simultaneously evaluate the middle 2 samples from the 4x4 grid.
	float2 w12 = w1 + w2;
	const float2 offset12 = w2 / w12;

	// Compute the final UV coordinates we'll use for sampling the texture
	float2 texPos0 = texPos1 - 1;
	float2 texPos3 = texPos1 + 2;
	float2 texPos12 = texPos1 + offset12;

	texPos0 /= tex_size;
	texPos3 /= tex_size;
	texPos12 /= tex_size;

	// Textures are single-mip, so LOD is always 0; SampleLevel avoids the implicit
	// gradient computation that Sample would perform for each of the 9 taps.
	float4 result = 0.0f;
	result += tex.SampleLevel(samp, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
	result += tex.SampleLevel(samp, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
	result += tex.SampleLevel(samp, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

	result += tex.SampleLevel(samp, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
	result += tex.SampleLevel(samp, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
	result += tex.SampleLevel(samp, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

	result += tex.SampleLevel(samp, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
	result += tex.SampleLevel(samp, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
	result += tex.SampleLevel(samp, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

	return result;
}

// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "common.hlsli"

Texture2D tx : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
	return sample_texture_catmull_rom(tx, tex_sampler, input.uv, input.tex_size) * input.c;
	// return tx.Sample(tex_sampler, input.uv) * input.c;
}

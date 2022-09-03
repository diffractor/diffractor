// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "common.hlsli"

float nrand(float4 pos)
{
	return frac(sin(dot(pos.xy, float2(12.9898, 78.233))) * 43758.5453) / 100.0;
}

float4 main(PS_INPUT input) : SV_Target
{
	const float rr = nrand(input.pos);
	const float4 cc = float4(input.c.r + rr, input.c.g + rr, input.c.b + rr, input.c.a);
	return cc;
}

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: the equirectangular projection - where a point on the destination quad is looking, and
// which source texel that direction lands on. The same inverse the software rasteriser in
// render_panorama.cpp evaluates; ui_panorama.h owns the camera these constants are packed from.
//
//   camera    x yaw, y pitch, z tan(fov/2), w viewport width / height
//   coverage  x longitude at the left of the file, y full width / cropped width,
//             z latitude at the top of the file, w full height / cropped height
//
// b0 is shared with yuv_params and texture_transform_params; the host binds whichever buffer
// matches the bound shader, and no shader ever includes two of these declarations.

cbuffer panorama_params : register(b0)
{
	float4 pano_camera;
	float4 pano_coverage;
};

static const float pano_turn = 6.28318530717958647692f;
static const float pano_half_turn = 3.14159265358979323846f;

// Pitch about X then yaw about Y, matching panorama_view::direction. Rolling the horizon is not
// something a viewer may do to a picture the stitcher levelled, so there is no third angle.
float3 panorama_direction(const float2 uv)
{
	const float tan_half_fov = pano_camera.z;

	// Both axes scale by the same half-width, so the vertical field follows the viewport's shape
	// instead of stretching to fill it.
	const float cam_x = (2.0f * uv.x - 1.0f) * tan_half_fov;
	const float cam_y = -(2.0f * uv.y - 1.0f) * tan_half_fov / pano_camera.w;

	float sin_pitch, cos_pitch, sin_yaw, cos_yaw;
	sincos(pano_camera.y, sin_pitch, cos_pitch);
	sincos(pano_camera.x, sin_yaw, cos_yaw);

	const float pitched_y = cam_y * cos_pitch + sin_pitch;
	const float pitched_z = -cam_y * sin_pitch + cos_pitch;

	return normalize(float3(cam_x * cos_yaw + pitched_z * sin_yaw,
	                        pitched_y,
	                        -cam_x * sin_yaw + pitched_z * cos_yaw));
}

// Normalised to the source texture, which holds the file's own pixels. Outside 0..1 on either axis
// is sphere the file does not hold.
float2 panorama_uv(const float3 direction)
{
	const float longitude = atan2(direction.x, direction.z);
	const float latitude = asin(clamp(direction.y, -1.0f, 1.0f));

	// Into the file's own turn before scaling, so a full circle has no seam to fall through. A value
	// that rounds up to exactly one turn would then read as uncovered and drop a pixel on the join,
	// so it is held just inside - by less than a texel of any real panorama.
	float relative = longitude - pano_coverage.x;
	relative -= pano_turn * floor(relative / pano_turn);
	relative = min(relative, pano_turn * 0.99999994f);

	return float2(relative / pano_turn * pano_coverage.y,
	              (pano_coverage.z - latitude) / pano_half_turn * pano_coverage.w);
}

bool panorama_covers(const float2 uv)
{
	return all(uv >= 0.0f) && all(uv < 1.0f);
}

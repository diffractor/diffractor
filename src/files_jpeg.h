// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2025  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: JPEG encoding and decoding using libjpeg-turbo. Handles JPEG compression,
// decompression, lossless rotation, and metadata marker detection.

#pragma once

class file_scan_result;
class read_stream;

static constexpr auto xmp_signature = u8"http://ns.adobe.com/xap/1.0/\0"sv;
static constexpr auto icc_signature = u8"ICC_PROFILE\0"sv;
static constexpr auto iptc_signature = u8"Photoshop 3.0\08BIM\04\04\0\0\0\0"sv;
static constexpr std::array<uint8_t, 6> exif_signature = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};

bool is_exif_signature(df::cspan cs);
bool is_iptc_signature(df::cspan cs);
bool is_xmp_signature(df::cspan cs);
bool is_icc_signature(df::cspan cs);

extern size_t exif_signature_len;
extern size_t iptc_signature_len;
extern size_t icc_signature_len;
extern size_t xmp_signature_len;

struct metadata_parts;
struct jpeg_encoder_impl;
struct jpeg_decoder_impl;
struct file_encode_params;

class jpeg_encoder final : public df::no_copy
{
public:
	std::unique_ptr<jpeg_encoder_impl> _impl;
	df::blob _result;
	sizei _result_dimensions;

	static uint32_t max_chunk;

	jpeg_encoder();
	~jpeg_encoder() override;

	void setup(uint32_t cx, uint32_t cy, file_encode_params params);
	void start(uint32_t cx, uint32_t cy, ui::orientation orientation, const metadata_parts& metadata,
	           file_encode_params params);
	void encode_chunk(uint8_t** rows, uint32_t chunk) const;

	df::blob complete(bool can_abort = true);
	df::blob encode(uint32_t cx, uint32_t cy, const uint8_t* bitmap, uint32_t stride, ui::orientation orientation,
	                const metadata_parts& metadata, file_encode_params params);
	df::blob result() const;

	friend class jpeg_decoder_x;
};

class jpeg_decoder_x final : df::no_copy
{
public:
	std::unique_ptr<jpeg_decoder_impl> _impl;
	ui::orientation _orientation_out = ui::orientation::top_left;

	jpeg_decoder_x();
	~jpeg_decoder_x() override;

	bool can_render_yuv420() const;

	void create();
	bool read_header(df::cspan cs);
	bool read_header(const ui::const_image_ptr& image);
	bool start_decompress(int scale_hint, bool raw) const;
	void read_nv12(uint8_t* pixels, int stride, int buffer_size) const;
	bool read_rgb(uint8_t* p, int stride, int buffer_size) const;
	void close() const;
	sizei dimensions() const;
	sizei dimensions_out() const;
	void destroy();

	df::blob transform(df::cspan src, jpeg_encoder& encoder, simple_transform transform) const;
};

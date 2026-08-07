// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
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

inline constexpr auto xmp_signature = "http://ns.adobe.com/xap/1.0/\0"sv;
inline constexpr auto icc_signature = "ICC_PROFILE\0"sv;
inline constexpr auto iptc_signature = "Photoshop 3.0\08BIM\04\04\0\0\0\0"sv;
inline constexpr std::array<uint8_t, 6> exif_signature = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};

bool is_exif_signature(df::cspan cs);
bool is_iptc_signature(df::cspan cs);
bool is_xmp_signature(df::cspan cs);
bool is_icc_signature(df::cspan cs);

// Strips the ISO/IEC 23008-12 ExifDataBlock header (a 4-byte big-endian count of the bytes
// between that field and the TIFF header) and any following "Exif\0\0" signature, leaving a
// bare TIFF stream. Used by the HEIF and JXL box readers, which share this container format.
df::blob strip_exif_tiff_prefix(df::blob data);

inline constexpr size_t exif_signature_len = exif_signature.size();
inline constexpr size_t iptc_signature_len = iptc_signature.size();
inline constexpr size_t icc_signature_len = icc_signature.size() + 2; // + sequence number and count
inline constexpr size_t xmp_signature_len = xmp_signature.size();

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

	jpeg_encoder();
	~jpeg_encoder() override;

	void setup(uint32_t cx, uint32_t cy, const file_encode_params& params);
	void start(uint32_t cx, uint32_t cy, ui::orientation orientation, const metadata_parts& metadata,
	           const file_encode_params& params);
	void encode_chunk(uint8_t** rows, uint32_t chunk) const;

	df::blob complete(bool can_abort = true);
	df::blob encode(uint32_t cx, uint32_t cy, const uint8_t* bitmap, uint32_t stride, ui::orientation orientation,
	                const metadata_parts& metadata, const file_encode_params& params);

	friend class jpeg_decoder_x;
};

class jpeg_decoder_x final : df::no_copy
{
public:
	std::unique_ptr<jpeg_decoder_impl> _impl;
	ui::orientation _orientation_out = ui::orientation::top_left;

	jpeg_decoder_x();
	~jpeg_decoder_x() override;

	bool can_render_nv12() const;

	void create();
	bool read_header(df::cspan cs);
	bool read_header(const ui::const_image_ptr& image);
	bool start_decompress(int scale_hint, bool raw, bool fancy_chroma) const;
	bool read_nv12(uint8_t* pixels, int stride, int buffer_size, const df::cancel_token& token) const;
	bool read_rgb(uint8_t* p, int stride, int buffer_size, const df::cancel_token& token) const;
	void close() const;
	sizei dimensions() const;
	sizei dimensions_out() const;
	void destroy();

	df::blob transform(df::cspan src, jpeg_encoder& encoder, simple_transform transform) const;
};

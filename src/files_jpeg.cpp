// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY
//
// Based on code from Thomas render. Lane. and the Independent JPEG Group's software.

#include "pch.h"
#include "files_jpeg.h"

#include "files.h"
#include "metadata_exif.h"
#include "model_propery.h"

extern "C" {
#define JPEG_INTERNAL_OPTIONS
#define TRANSFORMS_SUPPORTED 1

#include <jpeglib.h>


	/*
	 * Codes for supported types of image transformations.
	 */

	using JXFORM_CODE = enum
	{
		JXFORM_NONE,
		/* no transformation */
		JXFORM_FLIP_H,
		/* horizontal flip */
		JXFORM_FLIP_V,
		/* vertical flip */
		JXFORM_TRANSPOSE,
		/* transpose across UL-to-LR axis */
		JXFORM_TRANSVERSE,
		/* transpose across UR-to-LL axis */
		JXFORM_ROT_90,
		/* 90-degree clockwise rotation */
		JXFORM_ROT_180,
		/* 180-degree rotation */
		JXFORM_ROT_270,
		/* 270-degree clockwise (or 90 ccw) */
		JXFORM_WIPE,
		/* wipe */
		JXFORM_DROP /* drop */
	};

	/*
	 * Codes for crop parameters, which can individually be unspecified,
	 * positive or negative for xoffset or yoffset,
	 * positive or force or reflect for width or height.
	 */

	using JCROP_CODE = enum
	{
		JCROP_UNSET,
		JCROP_POS,
		JCROP_NEG,
		JCROP_FORCE,
		JCROP_REFLECT
	};

	/*
	 * Transform parameters struct.
	 * NB: application must not change any elements of this struct after
	 * calling jtransform_request_workspace.
	 */

	using jpeg_transform_info = struct
	{
		/* Options: set by caller */
		JXFORM_CODE transform; /* image transform operator */
		boolean perfect; /* if TRUE, fail if partial MCUs are requested */
		boolean trim; /* if TRUE, trim partial MCUs as needed */
		boolean force_grayscale; /* if TRUE, convert color image to grayscale */
		boolean crop; /* if TRUE, crop or wipe source image, or drop */
		boolean slow_hflip; /* For best performance, the JXFORM_FLIP_H transform
									normally modifies the source coefficients in place.
									Setting this to TRUE will instead use a slower,
									double-buffered algorithm, which leaves the source
									coefficients in tact (necessary if other transformed
									images must be generated from the same set of
									coefficients. */

									/* Crop parameters: application need not set these unless crop is TRUE.
															 * These can be filled in by jtransform_parse_crop_spec().
															 */
		JDIMENSION crop_width; /* Width of selected region */
		JCROP_CODE crop_width_set; /* (force-disables adjustment) */
		JDIMENSION crop_height; /* Height of selected region */
		JCROP_CODE crop_height_set; /* (force-disables adjustment) */
		JDIMENSION crop_xoffset; /* X offset of selected region */
		JCROP_CODE crop_xoffset_set; /* (negative measures from right edge) */
		JDIMENSION crop_yoffset; /* Y offset of selected region */
		JCROP_CODE crop_yoffset_set; /* (negative measures from bottom edge) */

		/* Drop parameters: set by caller for drop request */
		j_decompress_ptr drop_ptr;
		jvirt_barray_ptr* drop_coef_arrays;

		/* Internal workspace: caller should not touch these */
		int num_components; /* # of components in workspace */
		jvirt_barray_ptr* workspace_coef_arrays; /* workspace for transformations */
		JDIMENSION output_width; /* cropped destination dimensions */
		JDIMENSION output_height;
		JDIMENSION x_crop_offset; /* destination crop offsets measured in iMCUs */
		JDIMENSION y_crop_offset;
		JDIMENSION drop_width; /* drop/wipe dimensions measured in iMCUs */
		JDIMENSION drop_height;
		int iMCU_sample_width; /* destination iMCU size */
		int iMCU_sample_height;
	};


#if TRANSFORMS_SUPPORTED

	/* Parse a crop specification (written in X11 geometry style) */
	EXTERN(boolean) jtransform_parse_crop_spec(jpeg_transform_info* info,
		const char* spec);
	/* Request any required workspace */
	EXTERN(boolean) jtransform_request_workspace(j_decompress_ptr srcinfo,
		jpeg_transform_info* info);
	/* Adjust output image parameters */
	EXTERN(jvirt_barray_ptr*) jtransform_adjust_parameters
	(j_decompress_ptr srcinfo, j_compress_ptr dstinfo,
		jvirt_barray_ptr* src_coef_arrays, jpeg_transform_info* info);
	/* Execute the actual transformation, if any */
	EXTERN(void) jtransform_execute_transform(j_decompress_ptr srcinfo,
		j_compress_ptr dstinfo,
		jvirt_barray_ptr* src_coef_arrays,
		jpeg_transform_info* info);
	/* Determine whether lossless transformation is perfectly
	 * possible for a specified image and transformation.
	 */
	EXTERN(boolean) jtransform_perfect_transform(JDIMENSION image_width,
		JDIMENSION image_height,
		int MCU_width, int MCU_height,
		JXFORM_CODE transform);

	/* jtransform_execute_transform used to be called
	 * jtransform_execute_transformation, but some compilers complain about
	 * routine names that long.  This macro is here to avoid breaking any
	 * old source code that uses the original name...
	 */
#define jtransform_execute_transformation       jtransform_execute_transform

#endif /* TRANSFORMS_SUPPORTED */


	 /*
	  * Support for copying optional markers from source to destination file.
	  */

	using JCOPY_OPTION = enum
	{
		JCOPYOPT_NONE,
		/* copy no optional markers */
		JCOPYOPT_COMMENTS,
		/* copy only comment (COM) markers */
		JCOPYOPT_ALL,
		/* copy all optional markers */
		JCOPYOPT_ALL_EXCEPT_ICC,
		/* copy all optional markers except APP2 */
		JCOPYOPT_ICC /* copy only ICC profile (APP2) markers */
	};

#define JCOPYOPT_DEFAULT  JCOPYOPT_COMMENTS     /* recommended default */

	/* Setup decompression object to save desired markers in memory */
	EXTERN(void) jcopy_markers_setup(j_decompress_ptr srcinfo,
		JCOPY_OPTION option);
	/* Copy markers saved in the given source object to the destination object */
	EXTERN(void) jcopy_markers_execute(j_decompress_ptr srcinfo,
		j_compress_ptr dstinfo,
		JCOPY_OPTION option);

}

static constexpr auto write_buffer_size = df::sixty_four_k;

struct jpeg_encoder_impl
{
	jpeg_compress_struct cinfo;
	jpeg_error_mgr jerr;
	jpeg_destination_mgr jdms;

	uint8_t _buffer[write_buffer_size];
};

struct jpeg_decoder_impl
{
	jpeg_decompress_struct dinfo;
	jpeg_source_mgr mem_source;
	jpeg_source_mgr cache_file_source;
	jpeg_error_mgr jerr;

	uint8_t _buffer[write_buffer_size];
};

constexpr uint32_t max_chunk = static_cast<uint32_t>(2) * DCTSIZE;
uint32_t jpeg_encoder::max_chunk = static_cast<uint32_t>(2) * DCTSIZE;


size_t exif_signature_len = exif_signature.size();
size_t iptc_signature_len = 24;
size_t icc_signature_len = icc_signature.size() + 2;
size_t xmp_signature_len = xmp_signature.size();

static constexpr auto XMP_EXIF_MARKER = JPEG_APP0 + 1; // EXIF marker / Adobe XMP marker
static constexpr auto ICC_MARKER = JPEG_APP0 + 2; // ICC profile marker
static constexpr auto IPTC_MARKER = JPEG_APP0 + 13; // IPTC marker / BIM marker

// some defines for the different JPEG block types 
static constexpr auto M_SOF0 = 0xC0; // Start Of Frame N 
static constexpr auto M_SOF1 = 0xC1; // N indicates which compression process 
static constexpr auto M_SOF2 = 0xC2; // Only SOF0-SOF2 are now in common use 
static constexpr auto M_SOF3 = 0xC3;
static constexpr auto M_SOF5 = 0xC5; // NB: codes C4 and CC are NOT SOF markers 
static constexpr auto M_SOF6 = 0xC6;
static constexpr auto M_SOF7 = 0xC7;
static constexpr auto M_SOF9 = 0xC9;
static constexpr auto M_SOF10 = 0xCA;
static constexpr auto M_SOF11 = 0xCB;
static constexpr auto M_SOF13 = 0xCD;
static constexpr auto M_SOF14 = 0xCE;
static constexpr auto M_SOF15 = 0xCF;
static constexpr auto M_SOI = 0xD8;
static constexpr auto M_EOI = 0xD9; // End Of image (end of datastream) 
static constexpr auto M_SOS = 0xDA; // Start Of Scan (begins compressed data) 
static constexpr auto M_APP0 = 0xe0;
static constexpr auto M_APP1 = 0xe1;
static constexpr auto M_APP2 = 0xe2;
static constexpr auto M_APP3 = 0xe3;
static constexpr auto M_APP4 = 0xe4;
static constexpr auto M_APP5 = 0xe5;
static constexpr auto M_APP6 = 0xe6;
static constexpr auto M_APP7 = 0xe7;
static constexpr auto M_APP8 = 0xe8;
static constexpr auto M_APP9 = 0xe9;
static constexpr auto M_APP10 = 0xea;
static constexpr auto M_APP11 = 0xeb;
static constexpr auto M_APP12 = 0xec;
static constexpr auto M_APP13 = 0xed;
static constexpr auto M_APP14 = 0xee;
static constexpr auto M_APP15 = 0xef;

bool is_exif_signature(const df::cspan cs)
{
	if (cs.size <= exif_signature.size())
		return false;

	return memcmp(exif_signature.data(), cs.data, 5) == 0;
}

bool is_iptc_signature(const df::cspan cs)
{
	if (cs.size <= iptc_signature.size())
		return false;

	return memcmp(iptc_signature.data(), cs.data, iptc_signature.size()) == 0;
}

bool is_xmp_signature(const df::cspan cs)
{
	if (cs.size <= xmp_signature.size())
		return false;

	return memcmp(xmp_signature.data(), cs.data, xmp_signature.size()) == 0;
}

bool is_icc_signature(const df::cspan cs)
{
	if (cs.size <= icc_signature.size())
		return false;

	return memcmp(icc_signature.data(), cs.data, icc_signature.size()) == 0;
}

static boolean fill_input_buffer(struct jpeg_decompress_struct* dinfo)
{
	//dinfo->src->bytes_in_buffer = 0;
	//dinfo->src->next_input_byte = nullptr;
	//ERREXIT(dinfo, JERR_BUFFER_SIZE);
	static JOCTET fakeEoi[] = { 0xFF, JPEG_EOI };
	dinfo->src->next_input_byte = fakeEoi;
	dinfo->src->bytes_in_buffer = 2;
	return FALSE;
}

static void skip_input_data(struct jpeg_decompress_struct* dinfo, long num_bytes)
{
	num_bytes = std::min(static_cast<long>(dinfo->src->bytes_in_buffer), num_bytes);
	dinfo->src->next_input_byte += num_bytes;
	dinfo->src->bytes_in_buffer -= num_bytes;
}

static void source_noop(struct jpeg_decompress_struct* dinfo)
{
}

static void handle_error_exit(const j_common_ptr cinfo)
{
	char msg[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, msg);
	df::log(__FUNCTION__, msg);
	throw app_exception(msg);
}

static void handle_output_message(const j_common_ptr cinfo)
{
	static char last_error[JMSG_LENGTH_MAX] = "No error";
	(*cinfo->err->format_message)(cinfo, last_error);
}

static void init_buffer(const j_compress_ptr pi)
{
	const auto* const pThis = static_cast<jpeg_encoder*>(pi->client_data);

	pi->dest->next_output_byte = pThis->_impl->_buffer;
	pi->dest->free_in_buffer = write_buffer_size;
}

static void term_buffer(const j_compress_ptr pi)
{
	auto* pThis = static_cast<jpeg_encoder*>(pi->client_data);
	pThis->_result.insert(pThis->_result.end(), pThis->_impl->_buffer,
		pThis->_impl->_buffer + static_cast<size_t>(pi->dest->next_output_byte - pThis->_impl->
			_buffer));
}

static boolean empty_buffer(const j_compress_ptr pi)
{
	auto* pThis = static_cast<jpeg_encoder*>(pi->client_data);
	pThis->_result.insert(pThis->_result.end(), pThis->_impl->_buffer, pThis->_impl->_buffer + write_buffer_size);
	pi->dest->next_output_byte = pThis->_impl->_buffer;
	pi->dest->free_in_buffer = write_buffer_size;
	return TRUE;
}

jpeg_encoder::jpeg_encoder()
{
	_impl = std::make_unique<jpeg_encoder_impl>();
	//memset(_impl.get(), 0, sizeof(jpeg_encoder_impl));

	if (_impl)
	{
		_impl->cinfo.err = jpeg_std_error(&_impl->jerr);

		jpeg_create_compress(&_impl->cinfo);

		_impl->cinfo.dest = &_impl->jdms;
		_impl->cinfo.client_data = this;

		_impl->jerr.error_exit = handle_error_exit;
		_impl->jerr.output_message = handle_output_message;

		_impl->jdms.init_destination = init_buffer;
		_impl->jdms.empty_output_buffer = empty_buffer;
		_impl->jdms.term_destination = term_buffer;
	}
}

jpeg_encoder::~jpeg_encoder()
{
	try
	{
		if (_impl)
		{
			jpeg_destroy_compress(&_impl->cinfo);
		}

		_result.clear();
		_impl.reset();
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

void jpeg_encoder::setup(const uint32_t cx, const uint32_t cy, const file_encode_params params)
{
	_result.clear();

	_impl->cinfo.image_width = cx;
	_impl->cinfo.image_height = cy;
	_impl->cinfo.input_components = 4;
	_impl->cinfo.in_color_space = JCS_EXT_BGRX;
	_impl->cinfo.dct_method = JDCT_ISLOW;

	jpeg_set_defaults(&_impl->cinfo);
	jpeg_set_quality(&_impl->cinfo, params.jpeg_save_quality, TRUE);
	jpeg_set_colorspace(&_impl->cinfo, JCS_YCbCr);
}


void jpeg_encoder::start(const uint32_t cx, const uint32_t cy, const ui::orientation orientation,
	const metadata_parts& metadata, const file_encode_params params)
{
	setup(cx, cy, params);
	jpeg_start_compress(&_impl->cinfo, TRUE);

	_result_dimensions = sizei{ static_cast<int>(_impl->cinfo.jpeg_width), static_cast<int>(_impl->cinfo.jpeg_height) };

	if (!metadata.exif.empty())
	{
		if (is_exif_signature(metadata.exif))
		{
			jpeg_write_marker(&_impl->cinfo, XMP_EXIF_MARKER, metadata.exif.data(), metadata.exif.size());
		}
		else
		{
			std::vector<uint8_t> marker;
			marker.reserve(exif_signature.size() + metadata.exif.size());
			marker.insert(marker.begin(), exif_signature.begin(), exif_signature.end());
			marker.insert(marker.end(), metadata.exif.data(), metadata.exif.data() + metadata.exif.size());
			jpeg_write_marker(&_impl->cinfo, XMP_EXIF_MARKER, marker.data(), marker.size());
		}
	}
	else if (orientation != ui::orientation::top_left &&
		orientation != ui::orientation::none)
	{
		auto exif = make_orientation_exif(orientation);

		exif.insert(exif.begin(), exif_signature.begin(), exif_signature.end());
		jpeg_write_marker(&_impl->cinfo, XMP_EXIF_MARKER, exif.data(), exif.size());
	}

	if (!metadata.iptc.empty())
	{
		std::vector<uint8_t> marker;
		marker.reserve(iptc_signature.size() + metadata.iptc.size());
		marker.insert(marker.begin(), iptc_signature.begin(), iptc_signature.end());
		marker.insert(marker.end(), metadata.iptc.data(), metadata.iptc.data() + metadata.iptc.size());
		jpeg_write_marker(&_impl->cinfo, IPTC_MARKER, marker.data(), marker.size());
	}

	if (!metadata.xmp.empty())
	{
		std::vector<uint8_t> marker;
		marker.reserve(xmp_signature.size() + metadata.xmp.size());
		marker.insert(marker.begin(), xmp_signature.begin(), xmp_signature.end());
		marker.insert(marker.end(), metadata.xmp.data(), metadata.xmp.data() + metadata.xmp.size());
		jpeg_write_marker(&_impl->cinfo, XMP_EXIF_MARKER, marker.data(), marker.size());
	}

	if (!metadata.icc.empty())
	{
		std::vector<uint8_t> marker;
		marker.reserve(xmp_signature_len + metadata.icc.size());
		marker.insert(marker.begin(), icc_signature.begin(), icc_signature.end());
		marker.push_back(1);
		marker.push_back(1);
		marker.insert(marker.end(), metadata.icc.data(), metadata.icc.data() + metadata.icc.size());
		jpeg_write_marker(&_impl->cinfo, ICC_MARKER, marker.data(), marker.size());
	}
}

void jpeg_encoder::encode_chunk(uint8_t** rows, const uint32_t chunk)
{
	jpeg_write_scanlines(&_impl->cinfo, rows, chunk);
}

df::blob jpeg_encoder::complete(bool can_abort)
{
	if (can_abort && _impl->cinfo.next_scanline < _impl->cinfo.image_height)
	{
		jpeg_abort_compress(&_impl->cinfo);
	}
	else
	{
		jpeg_finish_compress(&_impl->cinfo);
	}
	return _result;
}

df::blob jpeg_encoder::encode(uint32_t cx, uint32_t cy, const uint8_t* bitmap, uint32_t stride,
	ui::orientation orientation, const metadata_parts& metadata,
	const file_encode_params params)
{
	start(cx, cy, orientation, metadata, params);

	const auto rows = df::unique_alloc<uint8_t*>(cy * sizeof(uint8_t*));

	for (auto i = 0u; i < cy; i++)
	{
		rows.get()[i] = const_cast<uint8_t*>(bitmap + (stride * i));
	}

	while (_impl->cinfo.next_scanline < _impl->cinfo.image_height)
	{
		encode_chunk(rows.get(), cy);
	}

	return complete();
}

df::blob jpeg_encoder::result() const
{
	return _result;
};


jpeg_decoder_x::jpeg_decoder_x()
{
	create();
}

jpeg_decoder_x::~jpeg_decoder_x()
{
	destroy();
}

bool jpeg_decoder_x::can_render_yuv420() const
{
	return _impl->dinfo.jpeg_color_space == JCS_YCbCr;
}

static boolean cache_file_fill(struct jpeg_decompress_struct* dinfo)
{
	const auto* const pThis = static_cast<jpeg_decoder_x*>(dinfo->client_data);
	dinfo->src->bytes_in_buffer = 0;
	dinfo->src->next_input_byte = pThis->_impl->_buffer;
	return false;
}

static void cache_file_skip(struct jpeg_decompress_struct* dinfo, long num_bytes)
{
}


void jpeg_decoder_x::create()
{
	_impl = std::make_unique<jpeg_decoder_impl>();

	//memset(_impl.get(), 0, sizeof(jpeg_decoder_impl));

	if (_impl)
	{
		_impl->dinfo.err = jpeg_std_error(&_impl->jerr);

		jpeg_create_decompress(&_impl->dinfo);

		_impl->dinfo.src = &_impl->mem_source;
		_impl->dinfo.client_data = this;

		_impl->jerr.error_exit = handle_error_exit;
		_impl->jerr.output_message = handle_output_message;

		_impl->mem_source.init_source = source_noop;
		_impl->mem_source.fill_input_buffer = fill_input_buffer;
		_impl->mem_source.skip_input_data = skip_input_data;
		_impl->mem_source.resync_to_restart = jpeg_resync_to_restart;
		_impl->mem_source.term_source = source_noop;
		_impl->mem_source.bytes_in_buffer = 0;
		_impl->mem_source.next_input_byte = nullptr;

		_impl->cache_file_source.init_source = source_noop;
		_impl->cache_file_source.fill_input_buffer = cache_file_fill;
		_impl->cache_file_source.skip_input_data = cache_file_skip;
		_impl->cache_file_source.resync_to_restart = jpeg_resync_to_restart;
		_impl->cache_file_source.term_source = source_noop;
		_impl->cache_file_source.bytes_in_buffer = 0;
		_impl->cache_file_source.next_input_byte = nullptr;
	}
}

bool jpeg_decoder_x::read_header(df::cspan cs)
{
	_impl->mem_source.bytes_in_buffer = cs.size;
	_impl->mem_source.next_input_byte = cs.data;
	_impl->dinfo.src = &_impl->mem_source;

	const auto success = JPEG_HEADER_OK == jpeg_read_header(&_impl->dinfo, TRUE);

	if (success)
	{
		for (const auto* marker = _impl->dinfo.marker_list; marker != nullptr; marker = marker->next)
		{
			df::span block = { marker->data, marker->data_length };

			if (marker->marker == XMP_EXIF_MARKER &&
				is_exif_signature(block))
			{
				prop::item_metadata md;
				metadata_exif::parse(md, block.sub(exif_signature_len));
				_orientation_out = md.orientation;
			}
		}
	}

	return success;
}

bool jpeg_decoder_x::read_header(const ui::const_image_ptr& image)
{
	return read_header(image->data());
}

bool jpeg_decoder_x::start_decompress(const int scale_hint, const bool yuv)
{
	if (_impl->dinfo.jpeg_color_space == JCS_YCCK || _impl->dinfo.jpeg_color_space == JCS_CMYK)
	{
		_impl->dinfo.out_color_space = JCS_CMYK;
		_impl->dinfo.raw_data_out = 0;
	}
	else
	{
		_impl->dinfo.out_color_space = yuv ? JCS_YCbCr : JCS_EXT_BGRX;
		_impl->dinfo.raw_data_out = yuv;
	}

	_impl->dinfo.do_fancy_upsampling = FALSE;
	_impl->dinfo.do_block_smoothing = FALSE;
	_impl->dinfo.dct_method = JDCT_ISLOW;

	if (scale_hint > 0)
	{
		_impl->dinfo.scale_num = 1;
		_impl->dinfo.scale_denom = scale_hint;
	}

	return 0 != jpeg_start_decompress(&_impl->dinfo);
}

void jpeg_decoder_x::read_nv12(uint8_t* pixels, const int stride, const int buffer_size)
{
	const auto cx = _impl->dinfo.output_width;
	const auto cy = _impl->dinfo.output_height;

	const auto ucx = _impl->dinfo.comp_info[1].downsampled_width;
	const auto ucy = _impl->dinfo.comp_info[1].downsampled_height;

	const auto vcx = _impl->dinfo.comp_info[2].downsampled_width;
	const auto vcy = _impl->dinfo.comp_info[2].downsampled_height;

	JSAMPROW py[max_chunk];
	JSAMPROW pu[max_chunk];
	JSAMPROW pv[max_chunk];

	const auto uv_buffer_stride = ui::calc_stride(cx, 1);
	const auto uv_buffer_len = uv_buffer_stride * max_chunk * 3;
	const auto uv_buffer = df::unique_alloc<uint8_t>(uv_buffer_len);

	const auto cy_div2 = cy & ~1;
	const auto cx_div2 = cx & ~1;

	JSAMPROW* plane[4]{ py, pu, pv, nullptr };

	for (uint32_t i = 0; i < max_chunk; i++)
	{
		py[i] = uv_buffer.get() + uv_buffer_stride * i;
		pu[i] = uv_buffer.get() + uv_buffer_stride * (i + max_chunk);
		pv[i] = uv_buffer.get() + uv_buffer_stride * (i + max_chunk * 2);
	}

	auto y = 0u;

	while (y < cy_div2)
	{
		constexpr auto chunk = static_cast<int>(max_chunk);
		const auto read = jpeg_read_raw_data(&_impl->dinfo, plane, chunk);

		if (read <= 0)
			break;

		for (auto yy = 0u; (yy < read) && (y + yy < cy_div2); yy++)
		{
			auto* const puvp = pixels + (stride * (y + yy));
			memcpy(puvp, py[yy], std::min(stride, uv_buffer_stride));
		}

		for (auto yy = 0u; (yy < read) && ((y + yy) < cy_div2); yy += 2)
		{
			auto* const puvp = pixels + (stride * (cy_div2 + ((y + yy) / 2)));
			const auto* const pup = pu[(yy * ucy) / cy];
			const auto* const pvp = pv[(yy * vcy) / cy];

			for (auto xx = 0u; xx < cx_div2; xx += 2)
			{
				puvp[xx + 0] = pup[(xx * ucx) / cx];
				puvp[xx + 1] = pvp[(xx * vcx) / cx];
			}
		}

		y += read;
	}
}

bool jpeg_decoder_x::read_rgb(uint8_t* p, int stride, int buffer_size)
{
	auto y = 0u;
	const auto cy = _impl->dinfo.output_height;

	JSAMPROW rows[max_chunk];

	while (y < cy)
	{
		const auto chunk = std::min(max_chunk, cy - y);

		for (auto i = 0u; i < max_chunk; i++)
		{
			rows[i] = p + stride * (y + i);
		}

		const auto read = jpeg_read_scanlines(&_impl->dinfo, rows, chunk);

		if (read <= 0)
			break;

		if (_impl->dinfo.out_color_space == JCS_CMYK)
		{
			const auto cx = _impl->dinfo.output_width;
			const auto* const range_limit = _impl->dinfo.sample_range_limit;

			for (auto yy = 0u; yy < read; yy++)
			{
				auto* const row = rows[yy];

				for (auto x = 0u; x < cx; x++)
				{
					const auto xx = x * 4;
					const int c = row[xx + 0];
					const int m = row[xx + 1];
					const int y = row[xx + 2];
					const int k = row[xx + 3];

					const auto r = std::clamp((c * k) >> 8, 0, 255);
					const auto g = std::clamp((m * k) >> 8, 0, 255);
					const auto b = std::clamp((y * k) >> 8, 0, 255);

					row[xx + EXT_BGRX_RED] = range_limit[r];
					row[xx + EXT_BGRX_GREEN] = range_limit[g];
					row[xx + EXT_BGRX_BLUE] = range_limit[b];
					row[xx + 3] = 0;
				}
			}
		}
		y += read;
	}

	return y > 0;
}

void jpeg_decoder_x::close()
{
	try
	{
		if (_impl->dinfo.output_scanline < _impl->dinfo.output_height)
		{
			jpeg_abort_decompress(&_impl->dinfo);
		}
		else
		{
			jpeg_finish_decompress(&_impl->dinfo);
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());

		try
		{
			jpeg_abort_decompress(&_impl->dinfo);
		}
		catch (std::exception& ee)
		{
			df::log(__FUNCTION__, ee.what());
		}
	}
}

sizei jpeg_decoder_x::dimensions() const
{
	return { static_cast<int>(_impl->dinfo.image_width), static_cast<int>(_impl->dinfo.image_height) };
}

sizei jpeg_decoder_x::dimensions_out() const
{
	return { static_cast<int>(_impl->dinfo.output_width), static_cast<int>(_impl->dinfo.output_height) };
}

void jpeg_decoder_x::destroy()
{
	try
	{
		if (_impl)
		{
			jpeg_destroy_decompress(&_impl->dinfo);
		}

		_impl.reset();
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}


static JXFORM_CODE to_transform(simple_transform orientation_e)
{
	const auto orientation = static_cast<int>(orientation_e);

	static constexpr JXFORM_CODE transform[] = {
		JXFORM_NONE,
		JXFORM_FLIP_H,
		JXFORM_ROT_180,
		JXFORM_FLIP_V,
		JXFORM_TRANSPOSE,
		JXFORM_ROT_90,
		JXFORM_TRANSVERSE,
		JXFORM_ROT_270,
	};

	return (orientation <= 1 || orientation > 8) ? JXFORM_NONE : transform[orientation];
}

df::blob jpeg_decoder_x::transform(const df::cspan src, jpeg_encoder& encoder, const simple_transform transform_in)
{
	df::blob result;

	const auto transform = to_transform(transform_in);
	_impl->mem_source.bytes_in_buffer = src.size;
	_impl->mem_source.next_input_byte = src.data;

	constexpr JCOPY_OPTION copyoption = JCOPYOPT_ALL;

	jpeg_transform_info transformoption = {}; // image transformation options
	transformoption.perfect = FALSE;
	transformoption.slow_hflip = FALSE;
	transformoption.transform = transform;
	transformoption.trim = TRUE;
	transformoption.force_grayscale = FALSE;
	transformoption.crop = FALSE;
	transformoption.crop_width_set = JCROP_UNSET;
	transformoption.crop_height_set = JCROP_UNSET;
	transformoption.crop_xoffset_set = JCROP_UNSET;
	transformoption.crop_yoffset_set = JCROP_UNSET;

	// Enable saving of extra markers that we want to copy
	jcopy_markers_setup(&_impl->dinfo, copyoption);

	// Read file header
	if (JPEG_HEADER_OK == jpeg_read_header(&_impl->dinfo, TRUE))
	{
		constexpr file_encode_params encode_params;

		encoder.setup(_impl->dinfo.image_width, _impl->dinfo.image_height, encode_params);

		// Any space needed by a transform option must be requested before
		// jpeg_read_coefficients so that memory allocation will be done right.
		jtransform_request_workspace(&_impl->dinfo, &transformoption);

		// Read source file as DCT coefficients
		auto* const src_coef_arrays = jpeg_read_coefficients(&_impl->dinfo);

		// Initialize destination compression parameters from source values
		jpeg_copy_critical_parameters(&_impl->dinfo, &encoder._impl->cinfo);

		// Adjust destination parameters if required by transform options;
		// also find out which set of coefficient arrays will hold the output.
		auto* const dst_coef_arrays = jtransform_adjust_parameters(&_impl->dinfo, &encoder._impl->cinfo,
			src_coef_arrays, &transformoption);

		// Start compressor (note no image data is actually written here)
		jpeg_write_coefficients(&encoder._impl->cinfo, dst_coef_arrays);

		// Example of code that also updates thumbnail
		// https://github.com/kraxel/fbida/blob/master/jpegtools.c
		//
		for (auto* marker = _impl->dinfo.marker_list; marker != nullptr; marker = marker->next)
		{
			df::span block = { marker->data, marker->data_length };

			if (marker->marker == XMP_EXIF_MARKER &&
				is_exif_signature(block))
			{
				const auto fixed = metadata_exif::fix_dims(block, _impl->dinfo.image_width, _impl->dinfo.image_height);
				marker->data = static_cast<uint8_t*>(_impl->dinfo.mem->alloc_large(
					std::bit_cast<j_common_ptr>(&_impl->dinfo), JPOOL_IMAGE, fixed.size()));
				marker->original_length = fixed.size();
				marker->data_length = fixed.size();
				memcpy(marker->data, fixed.data(), fixed.size());
			}
		}

		// Copy to the output file any extra markers that we want to preserve
		jcopy_markers_execute(&_impl->dinfo, &encoder._impl->cinfo, copyoption);

		// Execute image transformation, if any 
		jtransform_execute_transform(&_impl->dinfo, &encoder._impl->cinfo, src_coef_arrays, &transformoption);

		result = encoder.complete(false);
	}

	jpeg_finish_decompress(&_impl->dinfo);

	return result;
}


ui::image_ptr save_jpeg(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
	const file_encode_params& encoder_params)
{
	const auto dimensions_in = surface_in->dimensions();
	const auto orientation_in = surface_in->orientation();

	jpeg_encoder encoder;
	const auto result_data = encoder.encode(dimensions_in.cx, dimensions_in.cy, surface_in->pixels(),
		surface_in->stride(), orientation_in, metadata, encoder_params);
	const auto dimensions_out = encoder._result_dimensions;

	return std::make_shared<ui::image>(result_data, dimensions_out, ui::image_format::JPEG, orientation_in);
};


struct jfif_extension
{
	uint16_t length;
	char identifier[5];
	uint8_t extension_code;
	uint8_t data[1];
};

file_scan_result scan_jpg(read_stream& s)
{
	file_scan_result result;
	uint8_t block_data[df::sixty_four_k];

	constexpr auto APP14_DATA_LEN = 12;

	auto has_adobe_marker = false;
	auto has_jfif_marker = false;
	auto adobe_transform = 0;
	auto channels = 0;
	auto success = false;

	const auto file_len = s.size();
	uint64_t block_offset = 2u;

	while (file_len >= block_offset + 2u)
	{
		const auto block_start = s.peek8(block_offset);

		if (block_start != 0xFF)
		{
			// invalid?
			break;
		}

		const auto block_marker = s.peek8(block_offset + 1);

		if (block_marker == M_SOS || block_marker == M_EOI)
		{
			// End of metadata - start of image data
			success = true;
			break;
		}

		const auto block_len = df::byteswap16(s.peek16(block_offset + 2));

		if (file_len + 2u < block_offset + block_len)
		{
			// truncated
			break;
		}

		const auto block_data_len = block_len - 2u;

		switch (block_marker)
		{
		case M_SOF0:
			has_jfif_marker = true;
		case M_SOF1:
		case M_SOF2:
		case M_SOF3:
		case M_SOF5:
		case M_SOF6:
		case M_SOF7:
		case M_SOF9:
		case M_SOF10:
		case M_SOF11:
		case M_SOF13:
		case M_SOF14:
		case M_SOF15:
		{
			s.read(block_offset + 4u, block_data, block_data_len);
			const auto bits = *block_data;
			result.height = df::byteswap16(block_data + 1);
			result.width = df::byteswap16(block_data + 3);
			channels = *(block_data + 5);
		}
		break;

		case M_APP0:
		{
			s.read(block_offset + 4u, block_data, block_data_len);
			const auto* const ex = std::bit_cast<const jfif_extension*>(static_cast<const uint8_t*>(block_data));

			if (strcmp("JFXX", ex->identifier) == 0)
			{
				result.thumbnail_image = load_image_file({ ex->data, block_data_len - 8 });
			}
		}

		break;

		case M_APP1:
		{
			s.read(block_offset + 4u, block_data, block_data_len);
			df::cspan block = { block_data, block_data_len };

			if (is_exif_signature(block))
			{
				const auto exif = block.sub(exif_signature_len);
				result.metadata.exif.assign(exif.begin(), exif.end());
				result.exif_file_offset = block_offset + 4u;
			}
			else if (is_xmp_signature(block))
			{
				const auto xmp = block.sub(xmp_signature_len);
				result.metadata.xmp.assign(xmp.begin(), xmp.end());
			}
		}
		break;

		case M_APP2:
		{
			s.read(block_offset + 4u, block_data, block_data_len);
			df::cspan block = { block_data, block_data_len };

			if (is_icc_signature(block))
			{
				const auto icc = block.sub(icc_signature_len);
				result.metadata.icc.assign(icc.begin(), icc.end());
			}
		}
		break;

		case M_APP13:
		{
			s.read(block_offset + 4u, block_data, block_data_len);
			df::cspan block = { block_data, block_data_len };

			if (is_iptc_signature(block))
			{
				const auto iptc = block.sub(iptc_signature_len);
				result.metadata.iptc.assign(iptc.begin(), iptc.end());
			}
		}
		break;

		case M_APP14:
			if (block_len >= APP14_DATA_LEN)
			{
				s.read(block_offset + 4u, block_data, block_data_len);

				const auto* const data = block_data + 2;

				if (data[0] == 0x41 &&
					data[1] == 0x64 &&
					data[2] == 0x6F &&
					data[3] == 0x62 &&
					data[4] == 0x65)
				{
					/* Found Adobe APP14 marker */
					auto version = (data[5] << 8) + data[6];
					auto flags0 = (data[7] << 8) + data[8];
					auto flags1 = (data[9] << 8) + data[10];
					has_adobe_marker = true;
					adobe_transform = data[11];
				}
			}
			break;

		case M_APP3:
		case M_APP4:
		case M_APP5:
		case M_APP6:
		case M_APP7:
		case M_APP8:
		case M_APP9:
		case M_APP10:
		case M_APP11:
		case M_APP12:
		case M_APP15:
		default:
			break;
		}

		block_offset += block_len + 2u;
	}

	switch (channels)
	{
	case 1:
		result.pixel_format = u8"gray8"_c;
		break;

	case 3:
		if (has_jfif_marker)
		{
			result.pixel_format = u8"YCbCr"_c;
		}
		else if (has_adobe_marker)
		{
			switch (adobe_transform)
			{
			case 0:
				result.pixel_format = u8"rgb24"_c;
				break;
			case 1:
				result.pixel_format = u8"YCbCr"_c;
				break;
			default:
				result.pixel_format = u8"YCbCr"_c;
				break;
			}
		}
		else
		{
			result.pixel_format = u8"YCbCr"_c;
		}
		break;

	case 4:
		if (has_adobe_marker)
		{
			switch (adobe_transform)
			{
			case 0:
				result.pixel_format = u8"cmyk"_c;
				break;
			case 2:
				result.pixel_format = u8"ycck"_c;
				break;
			default:
				result.pixel_format = u8"ycck"_c;
				break;
			}
		}
		else
		{
			result.pixel_format = u8"cmyk"_c;
		}
		break;

	default:
		result.pixel_format = u8"YCbCr"_c;
		break;
	}

	result.success = success;
	return result;
}

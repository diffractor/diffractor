// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: JPEG image processing. Handles loading, saving, lossless rotation,
// metadata extraction, thumbnail generation using libjpeg-turbo, and, when the caller asks to
// inspect rather than index, reporting the file's marker, quantisation and Huffman table structure
// and the further images a Multi-Picture segment indexes.

#include "pch.h"
#include "files_jpeg.h"

#include "files.h"
#include "metadata_exif.h"
#include "model_property.h"

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
	JXFORM_DROP,
	/* drop */
	JXFORM_ROLL /* roll (shift with wraparound) */
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
                                             jpeg_transform_info* info);

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
	jpeg_error_mgr jerr;

	uint8_t _buffer[write_buffer_size];
};

constexpr uint32_t max_chunk = static_cast<uint32_t>(2) * DCTSIZE;

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
static constexpr auto M_DHT = 0xC4; // Huffman tables
static constexpr auto M_DQT = 0xDB; // Quantisation tables
static constexpr auto M_DRI = 0xDD; // Restart interval
static constexpr auto M_COM = 0xFE; // Comment
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

static bool has_signature(const df::cspan cs, const void* signature, const size_t signature_len)
{
	return cs.size >= signature_len && memcmp(signature, cs.data, signature_len) == 0;
}

bool is_exif_signature(const df::cspan cs)
{
	return has_signature(cs, exif_signature.data(), exif_signature.size());
}

df::blob strip_exif_tiff_prefix(df::blob data)
{
	if (data.size() >= 4)
	{
		const size_t tiff_offset = 4u + (static_cast<size_t>(data[0]) << 24 |
			static_cast<size_t>(data[1]) << 16 |
			static_cast<size_t>(data[2]) << 8 |
			static_cast<size_t>(data[3]));

		// The bound is what keeps a hostile offset from erasing past the end.
		if (tiff_offset <= data.size())
		{
			data.erase(data.begin(), data.begin() + tiff_offset);
		}
	}

	if (is_exif_signature(data))
	{
		data.erase(data.begin(), data.begin() + exif_signature.size());
	}

	return data;
}

bool is_iptc_signature(const df::cspan cs)
{
	return has_signature(cs, iptc_signature.data(), iptc_signature.size());
}

bool is_xmp_signature(const df::cspan cs)
{
	return has_signature(cs, xmp_signature.data(), xmp_signature.size());
}

bool is_icc_signature(const df::cspan cs)
{
	return has_signature(cs, icc_signature.data(), icc_signature.size());
}

static boolean fill_input_buffer(jpeg_decompress_struct* dinfo)
{
	static JOCTET fakeEoi[] = {0xFF, JPEG_EOI};
	dinfo->src->next_input_byte = fakeEoi;
	dinfo->src->bytes_in_buffer = 2;
	return FALSE;
}

static void skip_input_data(jpeg_decompress_struct* dinfo, long num_bytes)
{
	num_bytes = std::min(static_cast<long>(dinfo->src->bytes_in_buffer), num_bytes);
	dinfo->src->next_input_byte += num_bytes;
	dinfo->src->bytes_in_buffer -= num_bytes;
}

static void source_noop(jpeg_decompress_struct* dinfo)
{
}

static void handle_error_exit(const j_common_ptr cinfo)
{
	char msg[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, msg);
	df::log_once(__FUNCTION__, msg);
	throw app_exception(msg);
}

// Only purpose is to stop libjpeg writing warnings to stderr. Nothing consumes them, and decoding
// runs on several threads at once, so there is no shared buffer here to race over.
static void handle_output_message(j_common_ptr)
{
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

// Adopt the source file's quantization tables and chroma sampling so a re-encode stays close to a
// fixed point instead of re-quantizing every block against the quality slider's tables, and so a
// 4:4:4 source is not silently reduced to 4:2:0. Returns false when the source cannot be matched.
static bool copy_source_encode_params(const j_compress_ptr cinfo, const df::cspan source)
{
	if (source.data == nullptr || source.size == 0) return false;

	jpeg_decompress_struct dinfo = {};
	jpeg_error_mgr jerr = {};
	jpeg_source_mgr src = {};

	dinfo.err = jpeg_std_error(&jerr);
	jerr.error_exit = handle_error_exit;
	jerr.output_message = handle_output_message;

	jpeg_create_decompress(&dinfo);
	const df::scope_exit destroy_source([&dinfo] { jpeg_destroy_decompress(&dinfo); });

	src.init_source = source_noop;
	src.fill_input_buffer = fill_input_buffer;
	src.skip_input_data = skip_input_data;
	src.resync_to_restart = jpeg_resync_to_restart;
	src.term_source = source_noop;
	src.bytes_in_buffer = source.size;
	src.next_input_byte = source.data;
	dinfo.src = &src;

	try
	{
		if (JPEG_HEADER_OK != jpeg_read_header(&dinfo, TRUE)) return false;

		// Only plain 8-bit YCbCr is adopted. Grayscale, CMYK and YCCK sources would also need the
		// encoder's input conversion to change, and a deeper precision would not fit this encoder.
		if (dinfo.jpeg_color_space != JCS_YCbCr || dinfo.num_components != 3 || dinfo.data_precision != 8)
		{
			return false;
		}

		// Copies tables, sampling and colorspace, but also the source's dimensions - the caller's
		// output extent is the edited one, so restore it along with the encoder's input format.
		const auto cx = cinfo->image_width;
		const auto cy = cinfo->image_height;
		jpeg_copy_critical_parameters(&dinfo, cinfo);
		cinfo->image_width = cx;
		cinfo->image_height = cy;
		cinfo->input_components = 4;
		cinfo->in_color_space = JCS_EXT_BGRX;
	}
	catch (std::exception& e)
	{
		// A source we cannot read is not a reason to fail the save; fall back to the quality slider.
		df::log(__FUNCTION__, e.what());
		return false;
	}

	return true;
}

void jpeg_encoder::setup(const uint32_t cx, const uint32_t cy, const file_encode_params& params)
{
	// The encoder is a long-lived object reused for every image, and handle_error_exit throws out of
	// libjpeg. A compress abandoned mid-scan leaves global_state at CSTATE_SCANNING, which makes the
	// jpeg_set_defaults below ERREXIT for the rest of the session. Safe to call in any state.
	jpeg_abort_compress(&_impl->cinfo);

	_result.clear();

	_impl->cinfo.image_width = cx;
	_impl->cinfo.image_height = cy;
	_impl->cinfo.input_components = 4;
	_impl->cinfo.in_color_space = JCS_EXT_BGRX;

	if (!copy_source_encode_params(&_impl->cinfo, params.jpeg_source))
	{
		jpeg_set_defaults(&_impl->cinfo);
		jpeg_set_quality(&_impl->cinfo, params.jpeg_save_quality, TRUE);
		jpeg_set_colorspace(&_impl->cinfo, JCS_YCbCr);
	}

	// Last, because jpeg_set_defaults resets it and the copy above runs it internally.
	_impl->cinfo.dct_method = JDCT_ISLOW;
}


// The JPEG format limits a single marker segment to 65533 bytes of payload
// (the 2-byte length field is counted within the 65535-byte segment).
static constexpr size_t max_marker_payload = 65533;

// An ICC profile is split across APP2 segments, each carrying the "ICC_PROFILE\0" signature
// followed by a 1-based sequence number and the total segment count.
static constexpr size_t icc_seq_header_len = 2;
static constexpr size_t icc_max_chunk_len = max_marker_payload - icc_signature.size() - icc_seq_header_len;
static constexpr size_t icc_max_segments = 255;

// The JPEG container cannot carry a payload larger than one segment, so an oversized metadata block
// is reported rather than dropped: writing the image without the tags, ratings or edit history it
// arrived with would lose user data with nothing said about it.
static void check_marker_payload(const size_t len, const std::string_view what)
{
	if (len > max_marker_payload)
	{
		const auto message = std::format("the {} block is {} bytes, over the {} byte JPEG marker limit",
		                                 what, len, max_marker_payload);
		df::log(__FUNCTION__, message);
		throw app_exception(message);
	}
}

static void write_marker_checked(const j_compress_ptr cinfo, const int marker_code,
                                 const uint8_t* data, const size_t len)
{
	check_marker_payload(len, "metadata"sv);
	jpeg_write_marker(cinfo, marker_code, data, static_cast<unsigned int>(len));
}

// Write an ICC profile as one or more APP2 segments, following the ICC-in-JPEG convention.
static void write_icc_marker(const j_compress_ptr cinfo, const df::cspan icc)
{
	const size_t total_segments = (icc.size + icc_max_chunk_len - 1) / icc_max_chunk_len;

	for (size_t seg = 0; seg < total_segments; ++seg)
	{
		const size_t offset = seg * icc_max_chunk_len;
		const size_t chunk = std::min(icc_max_chunk_len, icc.size - offset);

		std::vector<uint8_t> marker;
		marker.reserve(icc_signature.size() + icc_seq_header_len + chunk);
		marker.insert(marker.end(), icc_signature.begin(), icc_signature.end());
		marker.push_back(static_cast<uint8_t>(seg + 1));
		marker.push_back(static_cast<uint8_t>(total_segments));
		marker.insert(marker.end(), icc.data + offset, icc.data + offset + chunk);
		jpeg_write_marker(cinfo, ICC_MARKER, marker.data(), static_cast<unsigned int>(marker.size()));
	}
}

// Everything that cannot be embedded is rejected before compression begins, so a refused file
// leaves the encoder untouched and ready for the next one.
static void check_metadata_fits(const metadata_parts& metadata)
{
	if (!metadata.exif.empty())
	{
		check_marker_payload(is_exif_signature(metadata.exif)
			                     ? metadata.exif.size()
			                     : exif_signature.size() + metadata.exif.size(), "exif"sv);
	}

	if (!metadata.iptc.empty()) check_marker_payload(iptc_signature.size() + metadata.iptc.size(), "iptc"sv);
	if (!metadata.xmp.empty()) check_marker_payload(xmp_signature.size() + metadata.xmp.size(), "xmp"sv);

	if (!metadata.icc.empty())
	{
		const auto icc_segments = (metadata.icc.size() + icc_max_chunk_len - 1) / icc_max_chunk_len;

		if (icc_segments > icc_max_segments)
		{
			const auto message = std::format(
				"the icc profile is {} bytes, needing {} of the {} APP2 segments JPEG allows",
				metadata.icc.size(), icc_segments, icc_max_segments);
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}
	}
}

void jpeg_encoder::start(const uint32_t cx, const uint32_t cy, const ui::orientation orientation,
                         const metadata_parts& metadata, const file_encode_params& params)
{
	check_metadata_fits(metadata);

	setup(cx, cy, params);
	jpeg_start_compress(&_impl->cinfo, TRUE);

	_result_dimensions = sizei{static_cast<int>(_impl->cinfo.jpeg_width), static_cast<int>(_impl->cinfo.jpeg_height)};

	if (!metadata.exif.empty())
	{
		if (is_exif_signature(metadata.exif))
		{
			write_marker_checked(&_impl->cinfo, XMP_EXIF_MARKER, metadata.exif.data(), metadata.exif.size());
		}
		else
		{
			std::vector<uint8_t> marker;
			marker.reserve(exif_signature.size() + metadata.exif.size());
			marker.insert(marker.begin(), exif_signature.begin(), exif_signature.end());
			marker.insert(marker.end(), metadata.exif.data(), metadata.exif.data() + metadata.exif.size());
			write_marker_checked(&_impl->cinfo, XMP_EXIF_MARKER, marker.data(), marker.size());
		}
	}
	else if (orientation != ui::orientation::top_left &&
		orientation != ui::orientation::none)
	{
		auto exif = make_orientation_exif(orientation);

		exif.insert(exif.begin(), exif_signature.begin(), exif_signature.end());
		write_marker_checked(&_impl->cinfo, XMP_EXIF_MARKER, exif.data(), exif.size());
	}

	if (!metadata.iptc.empty())
	{
		std::vector<uint8_t> marker;
		marker.reserve(iptc_signature.size() + metadata.iptc.size());
		marker.insert(marker.begin(), iptc_signature.begin(), iptc_signature.end());
		marker.insert(marker.end(), metadata.iptc.data(), metadata.iptc.data() + metadata.iptc.size());
		write_marker_checked(&_impl->cinfo, IPTC_MARKER, marker.data(), marker.size());
	}

	if (!metadata.xmp.empty())
	{
		std::vector<uint8_t> marker;
		marker.reserve(xmp_signature.size() + metadata.xmp.size());
		marker.insert(marker.begin(), xmp_signature.begin(), xmp_signature.end());
		marker.insert(marker.end(), metadata.xmp.data(), metadata.xmp.data() + metadata.xmp.size());
		write_marker_checked(&_impl->cinfo, XMP_EXIF_MARKER, marker.data(), marker.size());
	}

	if (!metadata.icc.empty())
	{
		write_icc_marker(&_impl->cinfo, metadata.icc);
	}
}

void jpeg_encoder::encode_chunk(uint8_t** rows, const uint32_t chunk) const
{
	jpeg_write_scanlines(&_impl->cinfo, rows, chunk);
}

df::blob jpeg_encoder::complete(const bool can_abort)
{
	if (can_abort && _impl->cinfo.next_scanline < _impl->cinfo.image_height)
	{
		jpeg_abort_compress(&_impl->cinfo);
	}
	else
	{
		jpeg_finish_compress(&_impl->cinfo);
	}
	// complete() is terminal for this encoder, so hand the buffer out rather than copying it.
	return std::move(_result);
}

df::blob jpeg_encoder::encode(const uint32_t cx, const uint32_t cy, const uint8_t* bitmap, const uint32_t stride,
                              const ui::orientation orientation, const metadata_parts& metadata,
                              const file_encode_params& params)
{
	start(cx, cy, orientation, metadata, params);

	const auto rows = df::unique_alloc<uint8_t*>(cy * sizeof(uint8_t*));

	for (auto i = 0u; i < cy; i++)
	{
		rows.get()[i] = const_cast<uint8_t*>(bitmap + static_cast<size_t>(stride) * i);
	}

	// jpeg_write_scanlines may consume fewer rows than offered, so always resume
	// from next_scanline rather than resubmitting the array from row 0.
	while (_impl->cinfo.next_scanline < cy)
	{
		const auto next = _impl->cinfo.next_scanline;
		encode_chunk(rows.get() + next, cy - next);
	}

	return complete();
}


jpeg_decoder_x::jpeg_decoder_x()
{
	create();
}

jpeg_decoder_x::~jpeg_decoder_x()
{
	destroy();
}

// NV12 carries one chroma pair per 2x2 luma block, so every source axis must be either already
// halved or full resolution for read_nv12 to average a whole number of samples into it. Cb and Cr
// have to match each other because read_nv12 indexes both planes the same way.
bool jpeg_decoder_x::can_render_nv12() const
{
	const auto& dinfo = _impl->dinfo;

	if (dinfo.jpeg_color_space != JCS_YCbCr || dinfo.num_components != 3 || dinfo.data_precision != 8 ||
		dinfo.comp_info == nullptr)
	{
		return false;
	}

	const auto& luma = dinfo.comp_info[0];
	const auto& cb = dinfo.comp_info[1];
	const auto& cr = dinfo.comp_info[2];

	if (cb.h_samp_factor != cr.h_samp_factor || cb.v_samp_factor != cr.v_samp_factor) return false;

	// read_nv12 pulls max_chunk lines per call, and libjpeg refuses a raw read shorter than one
	// iMCU row, so a taller-than-2 vertical sampling factor has to take the RGB path instead.
	if (luma.v_samp_factor * DCTSIZE > static_cast<int>(max_chunk)) return false;

	return (luma.h_samp_factor == cb.h_samp_factor || luma.h_samp_factor == cb.h_samp_factor * 2) &&
		(luma.v_samp_factor == cb.v_samp_factor || luma.v_samp_factor == cb.v_samp_factor * 2);
}

void jpeg_decoder_x::create()
{
	_impl = std::make_unique<jpeg_decoder_impl>();

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

		// libjpeg discards all APPn markers by default. Request that the APP1
		// marker (EXIF / XMP) be retained so read_header can recover the
		// embedded EXIF orientation.
		jpeg_save_markers(&_impl->dinfo, XMP_EXIF_MARKER, 0xFFFF);
	}
}

bool jpeg_decoder_x::read_header(const df::cspan cs)
{
	// The decoder is a long-lived object reused for every image. A stream that suspended part way
	// through its header, or was abandoned without close(), leaves parse state and a marker list
	// behind that this read would otherwise resume into.
	jpeg_abort_decompress(&_impl->dinfo);

	_impl->mem_source.bytes_in_buffer = cs.size;
	_impl->mem_source.next_input_byte = cs.data;
	_impl->dinfo.src = &_impl->mem_source;

	const auto success = JPEG_HEADER_OK == jpeg_read_header(&_impl->dinfo, TRUE);

	if (success)
	{
		for (const auto* marker = _impl->dinfo.marker_list; marker != nullptr; marker = marker->next)
		{
			df::span block = {marker->data, marker->data_length};

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

bool jpeg_decoder_x::start_decompress(const int scale_hint, const bool yuv, const bool fancy_chroma) const
{
	if (_impl->dinfo.jpeg_color_space == JCS_YCCK || _impl->dinfo.jpeg_color_space == JCS_CMYK)
	{
		_impl->dinfo.out_color_space = JCS_CMYK;
		_impl->dinfo.raw_data_out = 0;
	}
	else if (_impl->dinfo.data_precision > 8 && _impl->dinfo.jpeg_color_space == JCS_GRAYSCALE)
	{
		// 16-bit JPEG only exists in lossless mode, where libjpeg allows no colour conversion at all.
		// Grayscale has to stay grayscale; read_rgb expands it across the surface.
		_impl->dinfo.out_color_space = JCS_GRAYSCALE;
		_impl->dinfo.raw_data_out = 0;
	}
	else
	{
		_impl->dinfo.out_color_space = yuv ? JCS_YCbCr : JCS_EXT_BGRX;
		_impl->dinfo.raw_data_out = yuv;
	}

	// Box-filtered chroma shows as blocky colour on saturated edges, so the caller's intent decides,
	// not the scale: a display decode pays for the triangle filter at any size. The raw NV12 path
	// bypasses the upsampler entirely - the GPU sampler resolves its chroma.
	_impl->dinfo.do_fancy_upsampling = (!yuv && fancy_chroma) ? TRUE : FALSE;
	_impl->dinfo.do_block_smoothing = FALSE;
	_impl->dinfo.dct_method = JDCT_ISLOW;

	if (scale_hint > 0)
	{
		_impl->dinfo.scale_num = 1;
		_impl->dinfo.scale_denom = scale_hint;
	}

	return 0 != jpeg_start_decompress(&_impl->dinfo);
}

// ui::surface::alloc does not zero its pixels. A truncated or corrupt JPEG stops
// decoding early, so any rows libjpeg never wrote must be cleared before the
// surface is displayed, cached or re-encoded - otherwise stale heap contents leak.
static void clear_undecoded_rows(uint8_t* p, const int stride, const uint32_t first_row, const uint32_t row_count,
                                 const int buffer_size)
{
	if (first_row >= row_count || stride <= 0 || buffer_size <= 0)
		return;

	const auto row_bytes = static_cast<size_t>(stride);
	const auto begin = row_bytes * first_row;
	const auto end = std::min(row_bytes * row_count, static_cast<size_t>(buffer_size));

	if (end > begin)
	{
		memset(p + begin, 0, end - begin);
	}
}

bool jpeg_decoder_x::read_nv12(uint8_t* pixels, const int stride, const int buffer_size,
                               const df::cancel_token& token) const
{
	const auto cx = _impl->dinfo.output_width;
	const auto cy = _impl->dinfo.output_height;

	const auto ucx = _impl->dinfo.comp_info[1].downsampled_width;
	const auto ucy = _impl->dinfo.comp_info[1].downsampled_height;

	// can_render_nv12 guarantees Cr matches Cb, so one set of factors indexes both planes. A 4:2:0
	// source needs one sample per output pair; a full resolution axis needs two averaged together.
	const auto taps_x = std::max(1u, 2 * ucx / cx);
	const auto taps_y = std::max(1u, 2 * ucy / cy);
	const auto taps = taps_x * taps_y;

	JSAMPROW py[max_chunk];
	JSAMPROW pu[max_chunk];
	JSAMPROW pv[max_chunk];

	// Rows must hold width_in_blocks * DCT_scaled_size samples: the raw path bypasses the upsampler,
	// so libjpeg IDCTs whole blocks and the last one overruns the component width. The surface
	// stride's 16-byte alignment already covers that for every scale this decoder asks for, but
	// only by coincidence, so the scratch rows carry the padding explicitly.
	const auto uv_buffer_stride = ui::calc_stride(static_cast<int>(cx) + DCTSIZE, 1);
	const auto uv_buffer_len = uv_buffer_stride * max_chunk * 3;
	const auto uv_buffer = df::unique_alloc<uint8_t>(uv_buffer_len);

	const auto cy_div2 = cy & ~1;
	const auto cx_div2 = cx & ~1;

	JSAMPROW* plane[4]{py, pu, pv, nullptr};

	for (uint32_t i = 0; i < max_chunk; i++)
	{
		py[i] = uv_buffer.get() + uv_buffer_stride * i;
		pu[i] = uv_buffer.get() + uv_buffer_stride * (i + max_chunk);
		pv[i] = uv_buffer.get() + uv_buffer_stride * (i + max_chunk * 2);
	}

	auto y = 0u;

	while (y < cy_div2)
	{
		if (token.is_cancelled()) break;
		constexpr auto chunk = static_cast<int>(max_chunk);
		const auto read = jpeg_read_raw_data(&_impl->dinfo, plane, chunk);

		if (read <= 0)
			break;

		for (auto yy = 0u; yy < read && y + yy < cy_div2; yy++)
		{
			auto* const puvp = pixels + static_cast<size_t>(stride) * (y + yy);
			memcpy(puvp, py[yy], std::min(stride, uv_buffer_stride));
		}

		// libjpeg returns exactly one iMCU row per call - max_v_samp_factor * the scaled DCT size -
		// so at 1:8 a source with no vertical subsampling hands back a single luma row and a single
		// chroma row. An output chroma pair starts on an even luma row, so a call that starts on an
		// odd one contributes none, and a call holding fewer than taps_y chroma rows averages only
		// the rows it has: the rest arrive in a later call, and reading them here would take
		// untouched scratch as chroma. That scratch is zeroed whenever the allocation is fresh, and
		// zero in both chroma channels is what the YUV shader renders green.
		const auto chroma_lines = read * taps_y / 2;

		for (auto yy = y & 1u; yy < read && y + yy < cy_div2; yy += 2)
		{
			const auto src_row = ((y + yy) / 2) * taps_y - y * taps_y / 2;

			if (src_row >= chroma_lines) break;

			auto* const puvp = pixels + static_cast<size_t>(stride) * (cy_div2 + (y + yy) / 2);

			if (taps == 1)
			{
				const auto* const pup = pu[src_row];
				const auto* const pvp = pv[src_row];

				for (auto xx = 0u; xx < cx_div2; xx += 2)
				{
					puvp[xx + 0] = pup[xx / 2];
					puvp[xx + 1] = pvp[xx / 2];
				}
			}
			else
			{
				const auto rows = std::min(taps_y, chroma_lines - src_row);
				const auto n = taps_x * rows;

				for (auto xx = 0u; xx < cx_div2; xx += 2)
				{
					const auto src_col = (xx / 2) * taps_x;
					auto u_sum = 0u;
					auto v_sum = 0u;

					for (auto sy = 0u; sy < rows; sy++)
					{
						const auto* const pup = pu[src_row + sy];
						const auto* const pvp = pv[src_row + sy];

						for (auto sx = 0u; sx < taps_x; sx++)
						{
							u_sum += pup[src_col + sx];
							v_sum += pvp[src_col + sx];
						}
					}

					puvp[xx + 0] = static_cast<uint8_t>((u_sum + n / 2) / n);
					puvp[xx + 1] = static_cast<uint8_t>((v_sum + n / 2) / n);
				}
			}
		}

		y += read;
	}

	// Clear only what libjpeg never wrote: the tail of the luma plane, and the matching tail
	// of the chroma plane that follows it. A complete decode clears nothing - clearing the
	// whole chroma plane would leave U = V = 0, which the YUV shader renders as solid green.
	const auto luma_rows = std::min(y, cy_div2);
	const auto chroma_rows = (luma_rows + 1) / 2;

	clear_undecoded_rows(pixels, stride, luma_rows, cy_div2, buffer_size);
	clear_undecoded_rows(pixels, stride, cy_div2 + chroma_rows, cy_div2 + cy_div2 / 2, buffer_size);

	return y > 0 && !token.is_cancelled();
}

bool jpeg_decoder_x::read_rgb(uint8_t* p, const int stride, const int buffer_size,
                              const df::cancel_token& token) const
{
	auto y = 0u;
	const auto cy = _impl->dinfo.output_height;
	const auto precision = _impl->dinfo.data_precision;

	JSAMPROW rows[max_chunk];

	// 12- and 16-bit scans come out of their own libjpeg entry points with a wider sample type, so
	// they are pulled through a scratch buffer and scaled down into the 8-bit surface.
	const auto wide = precision > 8;
	const auto wide_cx = _impl->dinfo.output_width;
	const auto wide_max = wide ? (1u << precision) - 1u : 1u;
	const auto wide_components = static_cast<uint32_t>(_impl->dinfo.out_color_components);
	const auto wide_row_samples = wide_cx * wide_components;
	std::vector<uint16_t> scratch(wide ? static_cast<size_t>(wide_row_samples) * max_chunk : 0u);
	uint16_t* wide_rows[max_chunk];

	while (y < cy)
	{
		if (token.is_cancelled()) break;
		const auto chunk = std::min(max_chunk, cy - y);

		for (auto i = 0u; i < chunk; i++)
		{
			rows[i] = p + static_cast<size_t>(stride) * (y + i);
		}

		auto read = 0u;

		if (wide)
		{
			for (auto i = 0u; i < chunk; i++)
			{
				wide_rows[i] = scratch.data() + static_cast<size_t>(wide_row_samples) * i;
			}

			read = precision == 12
				       ? jpeg12_read_scanlines(&_impl->dinfo, reinterpret_cast<J12SAMPARRAY>(wide_rows), chunk)
				       : jpeg16_read_scanlines(&_impl->dinfo, wide_rows, chunk);

			for (auto i = 0u; i < read; i++)
			{
				const auto* const src = wide_rows[i];
				auto* const dst = rows[i];

				if (wide_components == 1)
				{
					for (auto x = 0u; x < wide_cx; x++)
					{
						const auto v = static_cast<uint8_t>((src[x] * 255u + wide_max / 2u) / wide_max);
						dst[x * 4 + 0] = v;
						dst[x * 4 + 1] = v;
						dst[x * 4 + 2] = v;
						dst[x * 4 + 3] = 0xff;
					}
				}
				else
				{
					for (auto n = 0u; n < wide_row_samples; n++)
					{
						dst[n] = static_cast<uint8_t>((src[n] * 255u + wide_max / 2u) / wide_max);
					}
				}
			}
		}
		else
		{
			read = jpeg_read_scanlines(&_impl->dinfo, rows, chunk);
		}

		if (read <= 0)
			break;

		if (_impl->dinfo.out_color_space == JCS_CMYK)
		{
			const auto cx = _impl->dinfo.output_width;

			// Adobe stores CMYK inverted, so 255 means no ink. Without that marker the values are direct
			// and must be inverted here, or the image renders as its own negative.
			const auto adobe = _impl->dinfo.saw_Adobe_marker != 0;

			for (auto yy = 0u; yy < read; yy++)
			{
				auto* const row = rows[yy];

				for (auto x = 0u; x < cx; x++)
				{
					const auto xx = x * 4;
					const int c = adobe ? row[xx + 0] : 255 - row[xx + 0];
					const int m = adobe ? row[xx + 1] : 255 - row[xx + 1];
					const int ye = adobe ? row[xx + 2] : 255 - row[xx + 2];
					const int k = adobe ? row[xx + 3] : 255 - row[xx + 3];

					row[xx + EXT_BGRX_RED] = static_cast<uint8_t>((c * k + 127) / 255);
					row[xx + EXT_BGRX_GREEN] = static_cast<uint8_t>((m * k + 127) / 255);
					row[xx + EXT_BGRX_BLUE] = static_cast<uint8_t>((ye * k + 127) / 255);
					row[xx + 3] = 0;
				}
			}
		}
		y += read;
	}

	clear_undecoded_rows(p, stride, y, cy, buffer_size);

	return y > 0 && !token.is_cancelled();
}

void jpeg_decoder_x::close() const
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
	return {static_cast<int>(_impl->dinfo.image_width), static_cast<int>(_impl->dinfo.image_height)};
}

sizei jpeg_decoder_x::dimensions_out() const
{
	return {static_cast<int>(_impl->dinfo.output_width), static_cast<int>(_impl->dinfo.output_height)};
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
		JXFORM_NONE, // simple_transform::none
		JXFORM_FLIP_H,
		JXFORM_ROT_180,
		JXFORM_FLIP_V,
		JXFORM_TRANSPOSE,
		JXFORM_ROT_90,
		JXFORM_TRANSVERSE,
		JXFORM_ROT_270, // simple_transform::rot_270
	};

	// Indexed by simple_transform, which is 0-based - flip_h is 1 and must not be
	// treated as a no-op, and index 8 is past the end of the table.
	return orientation <= 0 || orientation >= static_cast<int>(std::size(transform))
		       ? JXFORM_NONE
		       : transform[orientation];
}

df::blob jpeg_decoder_x::transform(const df::cspan src, jpeg_encoder& encoder,
                                   const simple_transform transform_in) const
{
	df::blob result;

	// Both objects are long-lived members of the caller, and every libjpeg call below can throw out
	// of handle_error_exit. Reset on entry and on every exit so one unreadable file cannot leave the
	// decoder or the encoder mid-operation, which would fail every later decode and save.
	jpeg_abort_decompress(&_impl->dinfo);
	jpeg_abort_compress(&encoder._impl->cinfo);

	const df::scope_exit reset_codecs([this, &encoder]
	{
		jpeg_abort_decompress(&_impl->dinfo);
		jpeg_abort_compress(&encoder._impl->cinfo);
	});

	const auto transform = to_transform(transform_in);
	_impl->mem_source.bytes_in_buffer = src.size;
	_impl->mem_source.next_input_byte = src.data;

	constexpr JCOPY_OPTION copyoption = JCOPYOPT_ALL;

	jpeg_transform_info transformoption = {}; // image transformation options
	// perfect refuses a rotation that would land on a partial MCU. The alternative, trimming, drops
	// up to 15 edge pixels the user could see in the preview; the caller re-encodes those instead.
	transformoption.perfect = TRUE;
	transformoption.slow_hflip = FALSE;
	transformoption.transform = transform;
	transformoption.trim = FALSE;
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
		// Any space needed by a transform option must be requested before
		// jpeg_read_coefficients so that memory allocation will be done right.
		if (!jtransform_request_workspace(&_impl->dinfo, &transformoption))
		{
			// Refused as imperfect. An empty result tells the caller to take the re-encode path.
			return result;
		}

		constexpr file_encode_params encode_params;

		encoder.setup(_impl->dinfo.image_width, _impl->dinfo.image_height, encode_params);

		// Read source file as DCT coefficients
		auto* const src_coef_arrays = jpeg_read_coefficients(&_impl->dinfo);

		// Null means libjpeg suspended. The source manager offers the whole file at once, so that
		// only happens when the entropy data ends early, and transupp indexes this array unchecked.
		if (src_coef_arrays == nullptr)
		{
			return result;
		}

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
			df::span block = {marker->data, marker->data_length};

			if (marker->marker == XMP_EXIF_MARKER &&
				is_exif_signature(block))
			{
				const auto fixed = metadata_exif::fix_dims(block, _impl->dinfo.image_width, _impl->dinfo.image_height);
				marker->data = static_cast<uint8_t*>(_impl->dinfo.mem->alloc_large(
					std::bit_cast<j_common_ptr>(&_impl->dinfo), JPOOL_IMAGE, fixed.size()));
				marker->original_length = static_cast<unsigned int>(fixed.size());
				marker->data_length = static_cast<unsigned int>(fixed.size());
				memcpy(marker->data, fixed.data(), fixed.size());
			}
		}

		// Copy to the output file any extra markers that we want to preserve
		jcopy_markers_execute(&_impl->dinfo, &encoder._impl->cinfo, copyoption);

		// Execute image transformation, if any 
		jtransform_execute_transform(&_impl->dinfo, &encoder._impl->cinfo, src_coef_arrays, &transformoption);

		result = encoder.complete(false);
		jpeg_finish_decompress(&_impl->dinfo);
	}

	return result;
}


ui::image_ptr save_jpeg(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                        const file_encode_params& encoder_params)
{
	const auto dimensions_in = surface_in->dimensions();
	const auto orientation_in = surface_in->orientation();

	jpeg_encoder encoder;
	auto result_data = encoder.encode(dimensions_in.cx, dimensions_in.cy, surface_in->pixels(),
	                                  static_cast<uint32_t>(surface_in->stride()), orientation_in, metadata,
	                                  encoder_params);
	const auto dimensions_out = encoder._result_dimensions;

	return std::make_shared<ui::image>(std::move(result_data), dimensions_out, ui::image_format::JPEG, orientation_in);
};


// JPEG structure reporting.
//
// Everything below reads only what the file states about itself: which segments it contains, the
// tables the encoder chose, and the frame geometry. None of it is metadata, so it survives having
// the metadata stripped, and it is reported as found rather than interpreted into a verdict.

// Zig-zag scan order: DQT stores coefficients in this sequence, the reference tables below are in
// natural raster order.
static constexpr uint8_t jpeg_zigzag_to_natural[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

// The example tables from the JPEG specification, Annex K. Quality estimation scales these.
static constexpr uint16_t jpeg_annex_k_luminance_quant[64] = {
	16, 11, 10, 16, 24, 40, 51, 61,
	12, 12, 14, 19, 26, 58, 60, 55,
	14, 13, 16, 24, 40, 57, 69, 56,
	14, 17, 22, 29, 51, 87, 80, 62,
	18, 22, 37, 56, 68, 109, 103, 77,
	24, 35, 55, 64, 81, 104, 113, 92,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103, 99
};

static constexpr uint16_t jpeg_annex_k_chrominance_quant[64] = {
	17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99
};

// The code-length counts of the Annex K example Huffman tables. Camera firmware normally writes
// these unchanged; software encoders usually compute tables fitted to the image instead.
static constexpr uint8_t jpeg_annex_k_dc_luminance_bits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static constexpr uint8_t jpeg_annex_k_dc_chrominance_bits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static constexpr uint8_t jpeg_annex_k_ac_luminance_bits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static constexpr uint8_t jpeg_annex_k_ac_chrominance_bits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};

static std::string_view jpeg_marker_name(const uint8_t marker)
{
	switch (marker)
	{
	case M_SOI: return "SOI (start of image)";
	case M_EOI: return "EOI (end of image)";
	case M_SOS: return "SOS (start of scan)";
	case M_SOF0: return "SOF0 (baseline DCT)";
	case M_SOF1: return "SOF1 (extended sequential DCT)";
	case M_SOF2: return "SOF2 (progressive DCT)";
	case M_SOF3: return "SOF3 (lossless)";
	case M_SOF5: return "SOF5 (differential sequential DCT)";
	case M_SOF6: return "SOF6 (differential progressive DCT)";
	case M_SOF7: return "SOF7 (differential lossless)";
	case M_SOF9: return "SOF9 (arithmetic sequential DCT)";
	case M_SOF10: return "SOF10 (arithmetic progressive DCT)";
	case M_SOF11: return "SOF11 (arithmetic lossless)";
	case M_SOF13: return "SOF13 (differential arithmetic sequential)";
	case M_SOF14: return "SOF14 (differential arithmetic progressive)";
	case M_SOF15: return "SOF15 (differential arithmetic lossless)";
	case 0xC4: return "DHT (Huffman tables)";
	case 0xCC: return "DAC (arithmetic conditioning)";
	case 0xDB: return "DQT (quantisation tables)";
	case 0xDC: return "DNL (define number of lines)";
	case 0xDD: return "DRI (restart interval)";
	case 0xDE: return "DHP (hierarchical progression)";
	case 0xDF: return "EXP (expand reference)";
	case 0xFE: return "COM (comment)";
	default: break;
	}

	if (marker >= M_APP0 && marker <= M_APP15) return "APP";
	return "unknown";
}

// The leading NUL-terminated identifier of an APPn segment, which is what distinguishes an Exif
// APP1 from an XMP APP1 or an Adobe APP14.
static std::string jpeg_app_identifier(const uint8_t* data, const size_t len)
{
	std::string result;

	for (size_t i = 0; i < len && i < 32u; ++i)
	{
		if (data[i] == 0) break;
		result += (data[i] >= 0x20 && data[i] < 0x7f) ? static_cast<char>(data[i]) : '.';
	}

	return result;
}

// Recovers the IJG quality setting that would produce this table, by averaging the scale each
// coefficient implies. Encoders that do not use the IJG scale still get a comparable number.
static int estimate_jpeg_quality(const uint16_t* table, const bool chrominance)
{
	const auto* const reference = chrominance ? jpeg_annex_k_chrominance_quant : jpeg_annex_k_luminance_quant;

	double total = 0.0;
	auto count = 0;

	for (auto i = 0; i < 64; ++i)
	{
		if (reference[i] == 0) continue;
		total += static_cast<double>(table[i]) * 100.0 / reference[i];
		++count;
	}

	if (count == 0) return 0;

	const auto scale = total / count;
	const auto quality = scale <= 100.0 ? (200.0 - scale) / 2.0 : 5000.0 / scale;

	return std::clamp(df::round(quality), 1, 100);
}

// Named the way HEIF, WebP and video already name their pixel formats, so one search for a
// sampling mode reaches every format that uses it.
static str::cached ycbcr_pixel_format(const int luma_h, const int luma_v)
{
	if (luma_h == 1 && luma_v == 1) return "yuv444"_c;
	if (luma_h == 2 && luma_v == 1) return "yuv422"_c;
	if (luma_h == 2 && luma_v == 2) return "yuv420"_c;
	if (luma_h == 1 && luma_v == 2) return "yuv440"_c;
	return "YCbCr"_c;
}

// A Multi-Picture APP2 segment indexes further whole JPEGs stored in the same file: the second frame
// of a burst, a depth or gain map, or the full-size image behind a thumbnail. The index is a TIFF
// directory whose MPEntry tag holds one 16-byte record per image, so each is reported with the
// extent it claims. Offsets are relative to the start of this segment's TIFF header, except the
// first image, which is the file itself and stores zero.
static void parse_mpf_index(metadata_kv_list& rows, const uint8_t* data, const uint32_t len,
                            const uint64_t tiff_offset)
{
	constexpr uint32_t mpf_signature_len = 4u; // "MPF\0"

	if (len < mpf_signature_len + 8u) return;

	const auto* const tiff = data + mpf_signature_len;
	const uint64_t tiff_len = len - mpf_signature_len;

	const auto intel = tiff[0] == 'I' && tiff[1] == 'I';
	const auto motorola = tiff[0] == 'M' && tiff[1] == 'M';

	if (!intel && !motorola) return;

	// Every offset below is read from the file, so the bounds test is done in 64 bits: a 32-bit
	// offset near the top of the range would otherwise wrap past the check and read out of bounds.
	const auto fits = [tiff_len](const uint64_t at, const uint64_t need) { return at + need <= tiff_len; };

	const auto read16 = [tiff, intel](const uint64_t at) -> uint32_t
	{
		const auto* const p = tiff + at;
		return intel
			       ? p[0] | (p[1] << 8)
			       : (p[0] << 8) | p[1];
	};
	const auto read32 = [tiff, intel](const uint64_t at) -> uint32_t
	{
		const auto* const p = tiff + at;
		return intel
			       ? p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)
			       : (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	};

	if (read16(2) != 0x002a) return;

	const uint64_t ifd_offset = read32(4);

	if (!fits(ifd_offset, 2u)) return;

	const auto entry_count = read16(ifd_offset);

	uint64_t entries_offset = 0;
	uint64_t image_count = 0;

	for (auto i = 0u; i < entry_count; ++i)
	{
		const auto entry = ifd_offset + 2u + static_cast<uint64_t>(i) * 12u;

		if (!fits(entry, 12u)) return;

		const auto tag = read16(entry);
		const auto count = read32(entry + 4);
		const auto value = read32(entry + 8);

		if (tag == 0xb001) image_count = value;
		// MPEntry is 16 bytes per image, so it never fits the inline value field.
		else if (tag == 0xb002) entries_offset = count > 4u ? value : 0u;
	}

	if (image_count == 0 || entries_offset == 0) return;

	// A malformed count must not be trusted into a read; the segment's own length bounds it.
	const auto available = entries_offset <= tiff_len ? (tiff_len - entries_offset) / 16u : 0u;
	const auto reported = image_count;

	image_count = std::min(image_count, available);

	for (auto i = 0u; i < image_count; ++i)
	{
		const auto at = entries_offset + static_cast<uint64_t>(i) * 16u;
		const auto attributes = read32(at);
		const auto size = read32(at + 4);
		const auto offset = read32(at + 8);

		// Bits 24-30 of the attributes carry the MP type code.
		const auto type = (attributes >> 24) & 0x7f;
		const auto representative = (attributes & 0x20000000u) != 0;

		std::string_view role;

		switch (type)
		{
		case 0x01: role = "large thumbnail, VGA"; break;
		case 0x02: role = "large thumbnail, full HD"; break;
		case 0x03: role = "multi-frame panorama"; break;
		case 0x04: role = "multi-frame disparity"; break;
		case 0x05: role = "multi-angle"; break;
		case 0x30: role = "baseline primary image"; break;
		default: role = "undefined type"; break;
		}

		auto& row = rows.emplace_back(
			std::format("Image {}", i + 1),
			offset == 0
				? std::format("{}, {} bytes, this file", role, size)
				: std::format("{}, {} bytes, offset {}", role, size, tiff_offset + offset));

		row.depth = 1;
		row.shape = representative ? std::format("0x{:08x}, representative", attributes)
			                           : std::format("0x{:08x}", attributes);
		row.id = std::format("jpeg.mpf.{}", i);
	}

	if (reported > image_count)
	{
		auto& row = rows.emplace_back("Unread images"s,
		                              std::format("{} of {} declared entries lie outside the segment",
		                                          reported - image_count, reported));
		row.depth = 1;
		row.id = "jpeg.mpf.unread";
	}
}

file_scan_result scan_jpg(read_stream& s, const scan_intent intent, const bool want_thumbnail)
{
	file_scan_result result;
	uint8_t block_data[df::sixty_four_k];

	constexpr auto APP14_DATA_LEN = 12;

	// The structure report is what the detail pane asks for; indexing and thumbnailing skip the
	// extra segment reads, the trailer read and the formatting they would only throw away.
	const auto want_structure = intent == scan_intent::inspect;

	auto has_adobe_marker = false;
	auto has_jfif_marker = false;
	auto adobe_transform = 0;
	auto channels = 0;
	auto luma_h = 0;
	auto luma_v = 0;
	auto success = false;

	// Structure reporting state, gathered as the markers go past.
	metadata_kv_list segment_rows;
	metadata_kv_list quant_rows;
	metadata_kv_list huffman_rows;
	metadata_kv_list comment_rows;
	metadata_kv_list embedded_image_rows;
	auto sof_marker = 0;
	auto sof_precision = 0;
	auto restart_interval = 0;
	auto huffman_tables = 0;
	auto standard_huffman_tables = 0;
	auto luminance_quality = 0;
	auto has_exif = false;
	auto has_xmp = false;
	auto has_iptc = false;
	std::string components_text;
	std::string subsampling_text;
	uint64_t sos_offset = 0;

	const auto add_segment = [&segment_rows, want_structure](const uint8_t marker, const uint64_t offset,
	                                                         const uint64_t bytes,
	                                                         const std::string_view identifier)
	{
		if (!want_structure) return;

		auto name = std::string(jpeg_marker_name(marker));

		if (name == "APP")
		{
			name = std::format("APP{}", marker - M_APP0);
		}

		auto value = std::format("offset {}, {} bytes", offset, bytes);

		if (!identifier.empty())
		{
			value = std::format("{}, {}", identifier, value);
		}

		auto& row = segment_rows.emplace_back(std::move(name), std::move(value));
		row.depth = 1;
		row.shape = std::format("0xFF{:02X}", marker);
		row.id = std::format("jpeg.segment.{}", offset);
	};

	const auto file_len = s.size();
	uint64_t block_offset = 2u;

	// ICC profiles may span multiple APP2 segments; collect them keyed by their
	// 1-based sequence number so they can be concatenated in order afterwards.
	std::map<int, std::vector<uint8_t>> icc_segments;

	while (file_len >= block_offset + 2u)
	{
		const auto block_start = s.peek8(block_offset);

		if (block_start != 0xFF)
		{
			// invalid?
			break;
		}

		auto block_marker = s.peek8(block_offset + 1);

		// Any marker may be preceded by any number of 0xFF fill bytes. Reading one as a marker would
		// take the following image data for a segment length and abandon the whole scan.
		while (block_marker == 0xFF && file_len >= block_offset + 3u)
		{
			++block_offset;
			block_marker = s.peek8(block_offset + 1);
		}

		if (block_marker == 0xFF) break;

		// TEM and the restart markers stand alone - they carry no length field to skip past.
		if (block_marker == 0x01 || (block_marker >= 0xD0 && block_marker <= 0xD7))
		{
			block_offset += 2u;
			continue;
		}

		if (block_marker == M_SOS || block_marker == M_EOI)
		{
			// End of metadata - start of image data
			add_segment(block_marker, block_offset, 2, {});
			sos_offset = block_offset;
			success = true;
			break;
		}

		if (file_len < block_offset + 4u)
		{
			// truncated - no room for the segment length field
			break;
		}

		const auto block_len = df::byteswap16(s.peek16(block_offset + 2));

		if (block_len < 2u || block_len > file_len - block_offset - 2u)
		{
			// Invalid or truncated segment.
			break;
		}

		const auto block_data_len = block_len - 2u;

		// APPn segments are told apart by their leading identifier, so it is read for every one of
		// them and not only for the few the decoder needs.
		std::string segment_identifier;

		if (want_structure && block_marker >= M_APP0 && block_marker <= M_APP15)
		{
			uint8_t identifier_data[32] = {};
			const auto identifier_len = std::min(block_data_len, static_cast<uint32_t>(std::size(identifier_data)));
			if (identifier_len) s.read(block_offset + 4u, identifier_data, identifier_len);
			segment_identifier = jpeg_app_identifier(identifier_data, identifier_len);
		}

		switch (block_marker)
		{
		case M_SOF0:
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
				if (block_data_len < 6u) break;
				s.read(block_offset + 4u, block_data, block_data_len);
				const auto bits = *block_data;
				result.height = df::byteswap16(block_data + 1);
				result.width = df::byteswap16(block_data + 3);
				channels = *(block_data + 5);

				sof_marker = block_marker;
				sof_precision = bits;

				// Each component contributes its identifier, sampling factors and the quantisation
				// table it uses. The luma factors are what chroma subsampling is read from, so they
				// are collected whether or not the structure view was asked for.
				for (auto c = 0; c < channels; ++c)
				{
					const auto component_offset = 6u + c * 3u;
					if (component_offset + 3u > block_data_len) break;

					const auto id = block_data[component_offset];
					const auto h = block_data[component_offset + 1] >> 4;
					const auto v = block_data[component_offset + 1] & 0x0f;
					const auto tq = block_data[component_offset + 2];

					if (c == 0)
					{
						luma_h = h;
						luma_v = v;
					}

					if (want_structure)
					{
						if (!components_text.empty()) components_text += ", ";
						components_text += std::format("{}:{}x{} q{}", id, h, v, tq);
					}
				}

				if (!want_structure) break;

				const auto h0 = luma_h;
				const auto v0 = luma_v;

				if (channels >= 3 && h0 > 0 && v0 > 0)
				{
					if (h0 == 1 && v0 == 1) subsampling_text = "4:4:4 (none)";
					else if (h0 == 2 && v0 == 1) subsampling_text = "4:2:2";
					else if (h0 == 2 && v0 == 2) subsampling_text = "4:2:0";
					else if (h0 == 1 && v0 == 2) subsampling_text = "4:4:0";
					else subsampling_text = std::format("{}x{} luma sampling", h0, v0);
				}
				else if (channels == 1)
				{
					subsampling_text = "single component";
				}
			}
			break;

		case M_DQT:
			{
				if (!want_structure) break;

				s.read(block_offset + 4u, block_data, block_data_len);

				uint32_t p = 0;

				while (p < block_data_len)
				{
					const auto precision = block_data[p] >> 4;
					const auto table_id = block_data[p] & 0x0f;
					const auto entry_bytes = precision ? 2u : 1u;
					const auto table_bytes = 64u * entry_bytes;
					++p;

					if (p + table_bytes > block_data_len) break;

					uint16_t table[64] = {};

					for (auto i = 0; i < 64; ++i)
					{
						table[jpeg_zigzag_to_natural[i]] = precision
							                                   ? df::byteswap16(block_data + p + i * 2)
							                                   : block_data[p + i];
					}

					p += table_bytes;

					const auto chrominance = table_id != 0;
					const auto quality = estimate_jpeg_quality(table, chrominance);
					if (!chrominance && luminance_quality == 0) luminance_quality = quality;

					const auto* const reference = chrominance
						                              ? jpeg_annex_k_chrominance_quant
						                              : jpeg_annex_k_luminance_quant;
					const auto is_annex_k = memcmp(table, reference, sizeof(table)) == 0;

					auto& row = quant_rows.emplace_back(
						std::format("Table {} ({})", table_id, chrominance ? "chrominance" : "luminance"),
						std::format("quality about {}{}", quality,
						            is_annex_k ? ", the Annex K example table unscaled" : ""));
					row.depth = 1;
					row.shape = std::format("{}-bit, {} bytes", precision ? 16 : 8, table_bytes);
					row.detail = metadata_numeric_detail{
						std::vector<uint16_t>(std::begin(table), std::end(table)), 8
					};
				}
			}
			break;

		case M_DHT:
			{
				if (!want_structure) break;

				s.read(block_offset + 4u, block_data, block_data_len);

				uint32_t p = 0;

				while (p + 17u <= block_data_len)
				{
					const auto table_class = block_data[p] >> 4;
					const auto table_id = block_data[p] & 0x0f;
					const auto* const bits = block_data + p + 1;

					auto code_count = 0u;
					for (auto i = 0; i < 16; ++i) code_count += bits[i];

					if (p + 17u + code_count > block_data_len) break;

					const auto* const reference = table_class == 0
						                              ? (table_id == 0
							                                 ? jpeg_annex_k_dc_luminance_bits
							                                 : jpeg_annex_k_dc_chrominance_bits)
						                              : (table_id == 0
							                                 ? jpeg_annex_k_ac_luminance_bits
							                                 : jpeg_annex_k_ac_chrominance_bits);

					const auto is_annex_k = table_id < 2 && memcmp(bits, reference, 16) == 0;

					++huffman_tables;
					if (is_annex_k) ++standard_huffman_tables;

					add_structure_bytes(huffman_rows,
					                    std::format("{} table {}", table_class == 0 ? "DC" : "AC", table_id),
					                    is_annex_k
						                    ? std::format("standard (Annex K), {} codes", code_count)
						                    : std::format("optimised, {} codes", code_count),
					                    std::format("{} bytes", 17u + code_count),
					                    block_data + p, 17u + code_count);

					p += 17u + code_count;
				}
			}
			break;

		case M_DRI:
			if (want_structure && block_data_len >= 2u)
			{
				s.read(block_offset + 4u, block_data, block_data_len);
				restart_interval = df::byteswap16(block_data);
			}
			break;

		case M_COM:
			{
				if (!want_structure) break;

				s.read(block_offset + 4u, block_data, block_data_len);
				std::string text(block_data, block_data + block_data_len);
				add_structure_row(comment_rows, "Comment", std::move(text),
				                  std::format("offset {}, {} bytes", block_offset, block_data_len));
				comment_rows.back().id = std::format("jpeg.com.{}", block_offset);
			}
			break;

		case M_APP0:
			{
				s.read(block_offset + 4u, block_data, block_data_len);

				// JFIF identifies the colour convention as YCbCr. It lives here, in APP0 - a
				// baseline SOF0 frame says nothing about it, and treating SOF0 as JFIF hid the
				// Adobe APP14 transform that marks a 3-channel JPEG as RGB.
				constexpr uint8_t jfif_signature[] = {'J', 'F', 'I', 'F', 0};
				if (block_data_len >= std::size(jfif_signature) &&
					memcmp(block_data, jfif_signature, std::size(jfif_signature)) == 0)
				{
					has_jfif_marker = true;
				}

				constexpr uint8_t jfxx_signature[] = {'J', 'F', 'X', 'X', 0};
				constexpr auto jfxx_header_len = std::size(jfxx_signature) + 1u;
				constexpr uint8_t jfxx_extension_jpeg = 0x10;

				// Extension codes 0x11 and 0x13 carry a raw palettised/RGB raster, not a JPEG
				// stream, so only 0x10 can be handed to the image loader.
				if (want_thumbnail &&
					block_data_len >= jfxx_header_len &&
					memcmp(block_data, jfxx_signature, std::size(jfxx_signature)) == 0 &&
					block_data[std::size(jfxx_signature)] == jfxx_extension_jpeg)
				{
					result.thumbnail_image = load_image_file({
						block_data + jfxx_header_len,
						block_data_len - jfxx_header_len
					});
				}
			}

			break;

		case M_APP1:
			{
				s.read(block_offset + 4u, block_data, block_data_len);
				df::cspan block = {block_data, block_data_len};

				if (is_exif_signature(block))
				{
					const auto exif = block.sub(exif_signature_len);
					result.metadata.exif.assign(exif.begin(), exif.end());
					result.exif_file_offset = block_offset + 4u;
					has_exif = true;
				}
				else if (is_xmp_signature(block))
				{
					const auto xmp = block.sub(xmp_signature_len);
					result.metadata.xmp.assign(xmp.begin(), xmp.end());
					has_xmp = true;
				}
			}
			break;

		case M_APP2:
			{
				s.read(block_offset + 4u, block_data, block_data_len);
				df::cspan block = {block_data, block_data_len};

				if (is_icc_signature(block) && block.size >= icc_signature_len)
				{
					const auto seq = block.data[icc_signature.size()];
					const auto icc = block.sub(icc_signature_len);
					icc_segments.insert_or_assign(seq, std::vector<uint8_t>(icc.begin(), icc.end()));
				}
				else if (want_structure && block_data_len >= 4u && memcmp(block_data, "MPF\0", 4) == 0)
				{
					// MPF offsets are measured from this segment's TIFF header, which follows the identifier.
					parse_mpf_index(embedded_image_rows, block_data, block_data_len, block_offset + 4u + 4u);
				}
			}
			break;

		case M_APP13:
			{
				s.read(block_offset + 4u, block_data, block_data_len);
				df::cspan block = {block_data, block_data_len};

				if (is_iptc_signature(block))
				{
					const auto iptc = block.sub(iptc_signature_len);
					result.metadata.iptc.assign(iptc.begin(), iptc.end());
					has_iptc = true;
				}
			}
			break;

		case M_APP14:
			if (block_data_len >= APP14_DATA_LEN)
			{
				s.read(block_offset + 4u, block_data, block_data_len);

				const auto* const data = block_data;

				if (data[0] == 0x41 &&
					data[1] == 0x64 &&
					data[2] == 0x6F &&
					data[3] == 0x62 &&
					data[4] == 0x65)
				{
					/* Found Adobe APP14 marker */
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

		add_segment(block_marker, block_offset, block_len + 2u, segment_identifier);

		block_offset += block_len + 2u;
	}

	// Reassemble the ICC profile from its APP2 segments (ascending sequence).
	for (const auto& [seq, segment] : icc_segments)
	{
		result.metadata.icc.insert(result.metadata.icc.end(), segment.begin(), segment.end());
	}

	switch (channels)
	{
	case 1:
		result.pixel_format = "gray8"_c;
		break;

	case 3:
		if (has_jfif_marker)
		{
			result.pixel_format = ycbcr_pixel_format(luma_h, luma_v);
		}
		else if (has_adobe_marker)
		{
			switch (adobe_transform)
			{
			case 0:
				result.pixel_format = "rgb24"_c;
				break;
			case 1:
				result.pixel_format = ycbcr_pixel_format(luma_h, luma_v);
				break;
			default:
				result.pixel_format = ycbcr_pixel_format(luma_h, luma_v);
				break;
			}
		}
		else
		{
			result.pixel_format = ycbcr_pixel_format(luma_h, luma_v);
		}
		break;

	case 4:
		if (has_adobe_marker)
		{
			switch (adobe_transform)
			{
			case 0:
				result.pixel_format = "cmyk"_c;
				break;
			case 2:
				result.pixel_format = "ycck"_c;
				break;
			default:
				result.pixel_format = "ycck"_c;
				break;
			}
		}
		else
		{
			result.pixel_format = "cmyk"_c;
		}
		break;

	default:
		result.pixel_format = "YCbCr"_c;
		break;
	}

	// Report what the file says about its own construction. The scan data is not decoded here, so
	// only the trailer is examined, which is where appended data would sit.
	if (success && want_structure)
	{
		uint64_t trailing_bytes = 0;
		auto eoi_found = false;

		constexpr uint64_t tail_search_bytes = 64ull * 1024ull;
		const auto tail_len = static_cast<size_t>(std::min<uint64_t>(file_len, tail_search_bytes));

		if (tail_len >= 2u)
		{
			const auto tail_offset = file_len - tail_len;
			std::vector<uint8_t> tail(tail_len);
			s.read(tail_offset, tail.data(), tail_len);

			for (auto i = tail_len - 2u; i != static_cast<size_t>(-1); --i)
			{
				if (tail[i] == 0xFF && tail[i + 1] == M_EOI)
				{
					eoi_found = true;
					trailing_bytes = file_len - (tail_offset + i + 2u);
					break;
				}
			}
		}

		metadata_kv_list kv;

		add_structure_section(kv, "Summary", "jpeg.summary", true);
		add_structure_row(kv, "File size", std::format("{} bytes", file_len));

		if (sof_marker)
		{
			add_structure_row(kv, "Encoding", std::string(jpeg_marker_name(static_cast<uint8_t>(sof_marker))),
			                  std::format("0xFF{:02X}", sof_marker));
			add_structure_row(kv, "Sample precision", std::format("{} bits", sof_precision));
		}

		add_structure_row(kv, "Dimensions", std::format("{} x {}", result.width, result.height));
		add_structure_row(kv, "Components", std::format("{}", channels));

		if (!components_text.empty())
			add_structure_row(kv, "Component sampling", components_text, "id:HxV quant-table");
		if (!subsampling_text.empty())
			add_structure_row(kv, "Chroma subsampling", subsampling_text);

		add_structure_row(kv, "Colour convention",
		                  has_jfif_marker
			                  ? "JFIF (APP0)"
			                  : has_adobe_marker
			                  ? std::format("Adobe APP14, transform {}", adobe_transform)
			                  : "not declared");

		if (restart_interval)
			add_structure_row(kv, "Restart interval", std::format("{} MCU rows", restart_interval));

		if (luminance_quality)
			add_structure_row(kv, "Estimated quality", std::format("about {}", luminance_quality));

		if (huffman_tables)
		{
			// Camera firmware writes the Annex K example tables; software encoders normally fit
			// tables to the image, so a mismatch marks a re-encode.
			add_structure_row(kv, "Huffman tables",
			                  standard_huffman_tables == huffman_tables
				                  ? std::format("{}, all standard (Annex K)", huffman_tables)
				                  : standard_huffman_tables == 0
				                  ? std::format("{}, all optimised", huffman_tables)
				                  : std::format("{}, {} standard and {} optimised", huffman_tables,
				                                standard_huffman_tables,
				                                huffman_tables - standard_huffman_tables));
		}

		add_structure_row(kv, "Metadata carried",
		                  std::format("{}{}{}{}",
		                              has_exif ? "Exif " : "",
		                              has_xmp ? "XMP " : "",
		                              has_iptc ? "IPTC " : "",
		                              result.metadata.icc.empty() ? "" : "ICC"));

		add_structure_row(kv, "End of image marker", eoi_found
			                                             ? trailing_bytes == 0
				                                               ? "present, nothing follows it"
				                                               : std::format("present, {} bytes follow it",
				                                                             trailing_bytes)
			                                             : "not found in the last 64 KB");

		add_structure_section(kv, "Segments", "jpeg.segments");
		kv.insert(kv.end(), std::make_move_iterator(segment_rows.begin()),
		          std::make_move_iterator(segment_rows.end()));

		add_structure_section(kv, "Quantisation tables", "jpeg.dqt");
		kv.insert(kv.end(), std::make_move_iterator(quant_rows.begin()),
		          std::make_move_iterator(quant_rows.end()));

		add_structure_section(kv, "Huffman tables", "jpeg.dht");
		kv.insert(kv.end(), std::make_move_iterator(huffman_rows.begin()),
		          std::make_move_iterator(huffman_rows.end()));

		add_structure_section(kv, "Comments", "jpeg.com");
		kv.insert(kv.end(), std::make_move_iterator(comment_rows.begin()),
		          std::make_move_iterator(comment_rows.end()));

		add_structure_section(kv, "Embedded images", "jpeg.embedded");
		kv.insert(kv.end(), std::make_move_iterator(embedded_image_rows.begin()),
		          std::make_move_iterator(embedded_image_rows.end()));

		finish_structure_sections(kv);
		result.structure_metadata = std::move(kv);
	}

	result.success = success;
	return result;
}

// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: WebP image format support. Loads and saves WebP files using libwebp,
// handles animation, ICC profiles, EXIF, and XMP metadata.

#include "pch.h"
#include "files.h"
#include "metadata_exif.h"

#include "webp/decode.h"
#include "webp/mux.h"

static std::string s_fail_next_webp_chunk;

void files_test_hooks::fail_next_webp_chunk(const std::string_view fourcc)
{
	s_fail_next_webp_chunk = fourcc;
}
#include "webp/demux.h"
#include "webp/encode.h"

// Crops one pixel off an odd axis, as decode_jpeg does, so an odd-sized image still reaches the GPU
// as NV12. Without it a 320x213 thumbnail costs 4 bytes per pixel instead of 1.5.
static ui::surface_ptr decode_webp_nv12(const df::cspan data, const int width, const int height)
{
	const auto even_width = width & ~1;
	const auto even_height = height & ~1;

	if (even_width < 2 || even_height < 2) return {};

	auto result = std::make_shared<ui::surface>();

	if (!result->alloc(even_width, even_height, ui::texture_format::NV12)) return {};

	const auto luma_stride = static_cast<int>(result->stride());
	const auto chroma_width = even_width / 2;
	const auto chroma_height = even_height / 2;
	const auto chroma_size = static_cast<size_t>(chroma_width) * chroma_height;
	const auto chroma = df::unique_alloc<uint8_t>(chroma_size * 2);

	if (!chroma) return {};

	auto* const u = chroma.get();
	auto* const v = u + chroma_size;

	WebPDecoderConfig config;

	if (!WebPInitDecoderConfig(&config)) return {};

	// A crop origin has to be even for 4:2:0 chroma to stay aligned, which zero is.
	config.options.use_cropping = 1;
	config.options.crop_width = even_width;
	config.options.crop_height = even_height;

	config.output.colorspace = MODE_YUV;
	config.output.is_external_memory = 1;
	config.output.u.YUVA.y = result->pixels();
	config.output.u.YUVA.y_stride = luma_stride;
	config.output.u.YUVA.y_size = static_cast<size_t>(luma_stride) * even_height;
	config.output.u.YUVA.u = u;
	config.output.u.YUVA.u_stride = chroma_width;
	config.output.u.YUVA.u_size = chroma_size;
	config.output.u.YUVA.v = v;
	config.output.u.YUVA.v_stride = chroma_width;
	config.output.u.YUVA.v_size = chroma_size;

	const auto status = WebPDecode(data.data, data.size, &config);
	WebPFreeDecBuffer(&config.output);

	if (status != VP8_STATUS_OK) return {};

	auto* const uv = result->pixels() + static_cast<size_t>(luma_stride) * even_height;

	for (auto y = 0; y < chroma_height; ++y)
	{
		auto* const dst = uv + static_cast<size_t>(y) * luma_stride;
		const auto* const src_u = u + static_cast<size_t>(y) * chroma_width;
		const auto* const src_v = v + static_cast<size_t>(y) * chroma_width;

		for (auto x = 0; x < chroma_width; ++x)
		{
			dst[x * 2] = src_u[x];
			dst[x * 2 + 1] = src_v[x];
		}
	}

	result->color_space(ui::color_space::rec601_limited);
	return result;
}

ui::surface_ptr load_webp(const df::cspan data, const bool can_use_yuv, const sizei target_extent)
{
	ui::surface_ptr result;
	WebPBitstreamFeatures features;

	if (WebPGetFeatures(data.data, data.size, &features) == VP8_STATUS_OK)
	{
		const auto width = features.width;
		const auto height = features.height;

		// The df::cspan decode path carries no budget gate of its own, and libwebp's 16383-pixel
		// edge limit still permits a ~1 GB surface. Measured against the SOURCE, because that is what
		// a hostile file declares.
		if (reject_over_budget_source(nullptr, {width, height}, "WEBP"))
		{
			return {};
		}

		// libwebp rescales inside the decoder, so the surface comes back at the size the caller asked
		// for instead of at the file's size with a downscale still owed. Unlike JPEG's scale_denom
		// this is not limited to power-of-two steps.
		//
		// The fitted size comes from ui::scale_dimensions, the same function files::fit_within
		// uses, so a reduced decode is a drop-in for decode-then-scale. Computing the ratio here
		// instead disagreed with it by a pixel on some sizes.
		auto scaled_width = width;
		auto scaled_height = height;

		if (!target_extent.is_empty())
		{
			const auto fitted = ui::scale_dimensions(sizei{width, height}, target_extent);

			if (fitted.cx < width || fitted.cy < height)
			{
				scaled_width = std::max(1, fitted.cx);
				scaled_height = std::max(1, fitted.cy);
			}
		}

		const auto is_scaled = scaled_width != width || scaled_height != height;

		// Whether planar is wanted is the caller's decision. A reduced decode takes the packed path
		// regardless: the NV12 route exists to hand the renderer native planes.
		const auto use_yuv = can_use_yuv && features.format == 1 && !features.has_alpha &&
			!features.has_animation && width >= 2 && height >= 2 && !is_scaled;

		if (use_yuv)
		{
			result = decode_webp_nv12(data, width, height);
		}

		if (!is_valid(result))
		{
			result = std::make_shared<ui::surface>();
			// Opaque images decode into the ignored X byte, so tag them RGB and let the renderer skip blending.
			auto* const buffer = result->alloc(scaled_width, scaled_height,
			                                   features.has_alpha
			                                   ? ui::texture_format::ARGB
			                                   : ui::texture_format::RGB);

			if (!buffer) return {};

			if (is_scaled)
			{
				WebPDecoderConfig config;

				if (!WebPInitDecoderConfig(&config)) return {};

				config.options.use_scaling = 1;
				config.options.scaled_width = scaled_width;
				config.options.scaled_height = scaled_height;
				config.output.colorspace = MODE_BGRA;
				config.output.is_external_memory = 1;
				config.output.u.RGBA.rgba = buffer;
				config.output.u.RGBA.stride = static_cast<int>(result->stride());
				config.output.u.RGBA.size = result->stride() * scaled_height;

				const auto status = WebPDecode(data.data, data.size, &config);
				WebPFreeDecBuffer(&config.output);

				if (status != VP8_STATUS_OK) return {};
			}
			else if (!WebPDecodeBGRAInto(data.data, data.size, buffer,
			                             static_cast<int>(height * result->stride()),
			                             static_cast<int>(result->stride())))
			{
				return {};
			}
		}

		if (is_valid(result))
		{
			WebPData wp_data;
			wp_data.bytes = data.data;
			wp_data.size = data.size;

			auto* mux = WebPMuxCreate(&wp_data, 0);
			df::releaser<WebPMux> mux_releaser(mux, [](auto* i) { WebPMuxDelete(i); });

			if (mux)
			{
				uint32_t flags = 0;
				WebPMuxGetFeatures(mux, &flags);
				const bool has_exif = flags & EXIF_FLAG;

				if (has_exif)
				{
					WebPData chunk;

					if (WEBP_MUX_OK == WebPMuxGetChunk(mux, "EXIF", &chunk))
					{
						const auto exif_skip = is_exif_signature({chunk.bytes, chunk.size}) ? 6u : 0u;
						prop::item_metadata md;
						metadata_exif::parse(md, {chunk.bytes + exif_skip, chunk.size - exif_skip});
						result->orientation(md.orientation);
					}
				}
			}
		}
	}

	return result;
}

webp_parts scan_webp(df::cspan data, bool decode_surface)
{
	webp_parts result;

	int32_t width = 0;
	int32_t height = 0;

	// Validate WebP data 
	if (WebPGetInfo(data.data, data.size, &width, &height))
	{
		result.width = width;
		result.height = height;

		WebPData wp_data;
		wp_data.bytes = data.data;
		wp_data.size = data.size;

		auto* const mux = WebPMuxCreate(&wp_data, 0);
		df::releaser<WebPMux> mux_releaser(mux, [](auto* i) { WebPMuxDelete(i); });

		if (mux)
		{
			uint32_t flags = 0;
			WebPMuxGetFeatures(mux, &flags);

			const bool animation = flags & ANIMATION_FLAG;
			const bool icc = flags & ICCP_FLAG;
			const bool exif = flags & EXIF_FLAG;
			const bool xmp = flags & XMP_FLAG;

			WebPBitstreamFeatures features;
			const bool has_features = WebPGetFeatures(data.data, data.size, &features) == VP8_STATUS_OK;
			const bool lossless = has_features && features.format == 2; // 2 = lossless (VP8L)
			// The bitstream is authoritative; the VP8X flag is only a fallback because it can over-report alpha.
			const bool has_alpha = has_features ? features.has_alpha != 0 : (flags & ALPHA_FLAG) != 0;

			if (lossless)
			{
				// Lossless WebP stores RGB(A) directly, not YUV.
				result.pixel_format = has_alpha ? "rgba"_c : "rgb"_c;
			}
			else
			{
				// Lossy WebP is always 4:2:0; alpha rides alongside it in its own plane.
				result.pixel_format = has_alpha ? "yuva420"_c : "yuv420"_c;
			}

			if (decode_surface)
			{
				if (!animation)
				{
					if (!reject_over_budget_source(nullptr, {width, height}, "WEBP"))
					{
						auto surface = std::make_shared<ui::surface>();
						// Opaque images decode into the ignored X byte, so tag them RGB and let the renderer skip blending.
						auto* buffer = surface->alloc(width, height,
						                              has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB);

						if (buffer && WebPDecodeBGRAInto(data.data, data.size, buffer,
						                                 static_cast<int>(height * surface->stride()),
						                                 static_cast<int>(surface->stride())))
						{
							result.frames.emplace_back(surface);
						}
					}
				}
				else
				{
					WebPAnimInfo anim_info;
					WebPAnimDecoderOptions dec_options;
					const auto frame_bytes = static_cast<uint64_t>(ui::calc_stride(width, 4)) * height;
					const auto budget = df::max_decode_bytes > 0 ? static_cast<uint64_t>(df::max_decode_bytes) : 0;
					// WebPAnimDecoder retains two full compositing canvases. Require room for those and
					// at least one frame before constructing it, then charge every retained frame too.
					const auto max_surface_count = frame_bytes > 0 ? budget / frame_bytes : 0;

					if (max_surface_count >= 3 && WebPAnimDecoderOptionsInit(&dec_options))
					{
						dec_options.color_mode = MODE_BGRA; // Use BGRA to match our surface format
						auto* dec = WebPAnimDecoderNew(&wp_data, &dec_options);

						if (dec)
						{
							const df::releaser<WebPAnimDecoder> dec_releaser(dec, [](auto* i)
							{
								WebPAnimDecoderDelete(i);
							});

							if (WebPAnimDecoderGetInfo(dec, &anim_info))
							{
								// frame_count comes from the file. Bound both work and total live canvases.
								constexpr uint32_t max_frames = 1024;
								const auto retained_budget = max_surface_count - 2;
								const auto frame_count = static_cast<uint32_t>(std::min<uint64_t>(
									std::min(anim_info.frame_count, max_frames), retained_budget));
								auto decoded_count = 0u;

								while (decoded_count < frame_count && WebPAnimDecoderHasMoreFrames(dec))
								{
									uint8_t* frame_data = nullptr;
									int timestamp = 0;

									if (!WebPAnimDecoderGetNext(dec, &frame_data, &timestamp)) break;
									++decoded_count;

									auto surface = std::make_shared<ui::surface>();
									// Always ARGB: the composited canvas is transparent wherever a frame rect
									// does not cover it, even when the container reports no alpha.
									auto* buffer = surface->alloc(anim_info.canvas_width, anim_info.canvas_height,
									                              ui::texture_format::ARGB, ui::orientation::top_left,
									                              timestamp / 1000.0);

									if (!buffer) break;

									// Copy frame data to surface buffer
									constexpr size_t bytes_per_pixel = 4; // BGRA
									const size_t frame_stride = anim_info.canvas_width * bytes_per_pixel;
									const size_t surface_stride = surface->stride();

									for (uint32_t y = 0; y < anim_info.canvas_height; ++y)
									{
										memcpy(buffer + y * surface_stride,
										       frame_data + y * frame_stride,
										       frame_stride);
									}

									result.frames.emplace_back(surface);
								}
							}
						}
					}
				}
			}

			if (icc)
			{
				WebPData chunk;

				if (WEBP_MUX_OK == WebPMuxGetChunk(mux, "ICCP", &chunk))
				{
					result.metadata.icc.assign(chunk.bytes, chunk.bytes + chunk.size);
				}
			}

			if (exif)
			{
				WebPData chunk;

				if (WEBP_MUX_OK == WebPMuxGetChunk(mux, "EXIF", &chunk))
				{
					const auto exif_skip = is_exif_signature({chunk.bytes, chunk.size}) ? exif_signature_len : 0u;
					// The skip trims the leading "Exif\0\0" signature only - the end of the
					// chunk is unchanged.
					result.metadata.exif.assign(chunk.bytes + exif_skip, chunk.bytes + chunk.size);

					if (!result.frames.empty())
					{
						prop::item_metadata md;
						metadata_exif::parse(md, {chunk.bytes + exif_skip, chunk.size - exif_skip});

						for (auto&& s : result.frames)
						{
							s->orientation(md.orientation);
						}
					}
				}
			}

			if (xmp)
			{
				WebPData chunk;

				if (WEBP_MUX_OK == WebPMuxGetChunk(mux, "XMP ", &chunk))
				{
					result.metadata.xmp.assign(chunk.bytes, chunk.bytes + chunk.size);
				}
			}
		}
	}

	return result;
}

namespace
{
	// VP8 encodes YCbCr 4:2:0, so libwebp converts any ARGB picture it is given back to planes before
	// it starts. An NV12 surface already is those planes, and handing them over directly skips both
	// that conversion and the one libjpeg did to produce the packed pixels in the first place.
	//
	// The one thing that does not carry over is range. libwebp's YUV is limited-range BT.601 and VP8
	// signals no range at all, so every decoder applies the limited-range inverse - a full-range
	// source handed over untouched comes back with crushed blacks and blown highlights.
	bool is_encodable_nv12(const ui::const_surface_ptr& surface, const file_encode_params& params)
	{
		if (surface->format() != ui::texture_format::NV12) return false;

		// Lossless is an ARGB codec; there is no planar form of it.
		if (params.webp_lossless) return false;

		// One chroma pair per 2x2 luma block. Every producer crops to even - an odd surface would not
		// even have been allocated a whole chroma plane - so this states the assumption rather than
		// leaving libwebp's last chroma column unwritten.
		const auto dimensions = surface->dimensions();
		if (((dimensions.cx | dimensions.cy) & 1) != 0) return false;

		const auto cs = surface->color_space();
		// Only the matrix VP8 assumes. A rec709 or rec2020 frame would be encoded as if it were 601.
		return cs == ui::color_space::rec601_full || cs == ui::color_space::rec601_limited;
	}

	bool import_nv12(WebPPicture& picture, const ui::const_surface_ptr& surface)
	{
		const auto dimensions = surface->dimensions();

		picture.use_argb = 0;
		picture.colorspace = WEBP_YUV420;

		if (!WebPPictureAlloc(&picture)) return false;

		const auto full_range = surface->color_space() == ui::color_space::rec601_full;

		// 219 of 256 luma codes and 224 chroma codes, offset to 16. Rounded in floating point because
		// integer division truncates toward zero, which biases the whole lower half of the chroma range
		// by a code. Built once rather than per sample - a thumbnail is far more pixels than entries.
		std::array<uint8_t, 256> luma_map{};
		std::array<uint8_t, 256> chroma_map{};

		for (auto i = 0; i < 256; ++i)
		{
			luma_map[i] = full_range
				              ? static_cast<uint8_t>(std::clamp(16 + std::lround(i * 219.0 / 255.0), 16L, 235L))
				              : static_cast<uint8_t>(i);
			chroma_map[i] = full_range
				                ? static_cast<uint8_t>(std::clamp(128 + std::lround((i - 128) * 224.0 / 255.0), 16L,
				                                                  240L))
				                : static_cast<uint8_t>(i);
		}

		const auto src_stride = surface->stride();
		const auto* const src_luma = surface->pixels();
		const auto* const src_chroma = src_luma + src_stride * dimensions.cy;

		for (auto y = 0; y < dimensions.cy; ++y)
		{
			const auto* const src = src_luma + src_stride * y;
			auto* const dst = picture.y + static_cast<ptrdiff_t>(picture.y_stride) * y;

			for (auto x = 0; x < dimensions.cx; ++x) dst[x] = luma_map[src[x]];
		}

		// NV12 interleaves U and V; libwebp wants them apart, so the de-interleave rides along with
		// the range map rather than costing a pass of its own.
		const auto chroma_width = dimensions.cx / 2;
		const auto chroma_height = dimensions.cy / 2;

		for (auto y = 0; y < chroma_height; ++y)
		{
			const auto* const src = src_chroma + src_stride * y;
			auto* const dst_u = picture.u + static_cast<ptrdiff_t>(picture.uv_stride) * y;
			auto* const dst_v = picture.v + static_cast<ptrdiff_t>(picture.uv_stride) * y;

			for (auto x = 0; x < chroma_width; ++x)
			{
				dst_u[x] = chroma_map[src[x * 2]];
				dst_v[x] = chroma_map[src[x * 2 + 1]];
			}
		}

		return true;
	}
}

ui::image_ptr save_webp(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                        const file_encode_params& params)
{
	if (!is_valid(surface_in)) return {};

	const auto planar = is_encodable_nv12(surface_in, params);

	if (!planar && surface_in->format() != ui::texture_format::RGB &&
		surface_in->format() != ui::texture_format::ARGB)
	{
		return {};
	}

	ui::image_ptr result;

	auto* mux = WebPMuxNew();
	df::releaser<WebPMux> mux_releaser(mux, [](auto* i) { WebPMuxDelete(i); });

	if (mux)
	{
		df::blob rotate_exif;

		const auto dimensions = surface_in->dimensions();
		const auto use_alpha = surface_in->format() == ui::texture_format::ARGB;

		WebPPicture picture;
		WebPPictureInit(&picture);

		picture.width = dimensions.cx;
		picture.height = dimensions.cy;

		bool ok;

		if (planar)
		{
			ok = import_nv12(picture, surface_in);
		}
		else
		{
			picture.use_argb = true;
			ok = use_alpha
				     ? WebPPictureImportBGRA(&picture, surface_in->pixels(),
				                             static_cast<int>(surface_in->stride())) != 0
				     : WebPPictureImportBGRX(&picture, surface_in->pixels(),
				                             static_cast<int>(surface_in->stride())) != 0;
		}

		if (ok)
		{
			WebPMemoryWriter memory_writer;
			WebPMemoryWriterInit(&memory_writer);

			WebPConfig config;
			WebPConfigInit(&config);

			if (params.webp_lossless)
			{
				WebPConfigLosslessPreset(&config, 7);
				config.thread_level = 1;
			}
			else
			{
				// A thumbnail is a rebuildable cache entry written for every indexed item, so it takes a
				// much cheaper search than a file the user asked to save.
				config.thread_level = params.webp_fast ? 0 : 1;
				config.lossless = false;
				config.quality = static_cast<float>(params.webp_quality);
				config.method = params.webp_fast ? 2 : 6;
				config.use_sharp_yuv = params.webp_fast ? 0 : 1;
				// https://groups.google.com/a/webmproject.org/forum/#!topic/webp-discuss/7dV1qXrdQ2Y
				config.alpha_quality = params.webp_lossy_alpha ? params.webp_quality : 100;
			}

			// assert_true evaluates nothing in Release, so the validation has to stand on its own.
			const auto valid_config = WebPValidateConfig(&config) != 0;

			if (!valid_config) df::log(__FUNCTION__, "rejected webp encoder configuration");

			picture.writer = WebPMemoryWrite;
			picture.custom_ptr = &memory_writer;

			const int success = valid_config && WebPEncode(&config, &picture);

			if (valid_config && !success)
			{
				df::log(__FUNCTION__, std::format("webp encode failed with error {}",
				                                  static_cast<int>(picture.error_code)));
			}

			if (success)
			{
				WebPData image_data = {memory_writer.mem, memory_writer.size};
				WebPMuxError img_err = WebPMuxSetImage(mux, &image_data, 0);

				if (img_err == WEBP_MUX_OK)
				{
					auto metadata_ok = true;
					const auto set_chunk = [mux, &metadata_ok](const char fourcc[5], const WebPData& chunk_data)
					{
						if (s_fail_next_webp_chunk == fourcc)
						{
							s_fail_next_webp_chunk.clear();
							df::log(__FUNCTION__, std::format("webp {} chunk insertion failed by test hook", fourcc));
							metadata_ok = false;
							return;
						}

						const auto err = WebPMuxSetChunk(mux, fourcc, &chunk_data, 0);
						if (err != WEBP_MUX_OK)
						{
							const auto message = std::format("webp {} chunk insertion failed with error {}", fourcc,
							                                 static_cast<int>(err));
							df::log(__FUNCTION__, message);
							metadata_ok = false;
						}
					};

					if (!metadata.icc.empty())
					{
						WebPData chunk_data;
						chunk_data.bytes = metadata.icc.data();
						chunk_data.size = metadata.icc.size();
						set_chunk("ICCP", chunk_data);
					}

					if (!metadata.exif.empty())
					{
						const auto exif_skip = is_exif_signature(metadata.exif) ? exif_signature_len : 0u;
						WebPData chunk_data;
						chunk_data.bytes = metadata.exif.data() + exif_skip;
						chunk_data.size = metadata.exif.size() - exif_skip;
						set_chunk("EXIF", chunk_data);
					}
					else if (surface_in->orientation() != ui::orientation::top_left)
					{
						rotate_exif = make_orientation_exif(surface_in->orientation());
						const auto exif_skip = is_exif_signature(rotate_exif) ? exif_signature_len : 0u;
						WebPData chunk_data;
						chunk_data.bytes = rotate_exif.data() + exif_skip;
						chunk_data.size = rotate_exif.size() - exif_skip;
						set_chunk("EXIF", chunk_data);
					}

					if (!metadata.xmp.empty())
					{
						WebPData chunk_data;
						chunk_data.bytes = metadata.xmp.data();
						chunk_data.size = metadata.xmp.size();
						set_chunk("XMP ", chunk_data);
					}

					if (metadata_ok)
					{
						WebPData output_data;
						WebPDataInit(&output_data);

						WebPMuxError err = WebPMuxAssemble(mux, &output_data);

						if (err == WEBP_MUX_OK)
						{
							result = std::make_shared<ui::image>(df::cspan(output_data.bytes, output_data.size),
							                                     dimensions, ui::image_format::WEBP,
							                                     surface_in->orientation());
							WebPDataClear(&output_data);
						}
					}
				}
			}

			WebPMemoryWriterClear(&memory_writer);
		}

		WebPPictureFree(&picture);
	}

	return result;
}

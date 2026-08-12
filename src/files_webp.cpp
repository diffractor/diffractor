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

ui::surface_ptr load_webp(const df::cspan data, const bool can_use_yuv)
{
	ui::surface_ptr result;
	WebPBitstreamFeatures features;

	if (WebPGetFeatures(data.data, data.size, &features) == VP8_STATUS_OK)
	{
		const auto width = features.width;
		const auto height = features.height;

		// The df::cspan decode path carries no budget gate of its own, and libwebp's 16383-pixel
		// edge limit still permits a ~1 GB surface.
		if (reject_over_budget_source(nullptr, {width, height}, "WEBP"))
		{
			return {};
		}

		// setting.use_yuv is the one switch behind the Advanced option, safe start and the
		// D3D11 driver-fault fallback, so it has to be read where the format is chosen.
		const auto use_yuv = can_use_yuv && setting.use_yuv && features.format == 1 && !features.has_alpha &&
			!features.has_animation && width >= 2 && height >= 2;

		if (use_yuv)
		{
			result = decode_webp_nv12(data, width, height);
		}

		if (!is_valid(result))
		{
			result = std::make_shared<ui::surface>();
			// Opaque images decode into the ignored X byte, so tag them RGB and let the renderer skip blending.
			auto* const buffer = result->alloc(width, height,
			                                   features.has_alpha
			                                   ? ui::texture_format::ARGB
			                                   : ui::texture_format::RGB);

			if (!buffer || !WebPDecodeBGRAInto(data.data, data.size, buffer,
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

ui::image_ptr save_webp(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                        const file_encode_params& params)
{
	if (!is_valid(surface_in) ||
		(surface_in->format() != ui::texture_format::RGB && surface_in->format() != ui::texture_format::ARGB))
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
		picture.use_argb = true;

		const auto ok = use_alpha
			                ? WebPPictureImportBGRA(&picture, surface_in->pixels(),
			                                        static_cast<int>(surface_in->stride()))
			                : WebPPictureImportBGRX(&picture, surface_in->pixels(),
			                                        static_cast<int>(surface_in->stride()));

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
					if (!metadata.icc.empty())
					{
						WebPData chunk_data;
						chunk_data.bytes = metadata.icc.data();
						chunk_data.size = metadata.icc.size();
						// Metadata chunk errors are not critical - the image is still valid without them.
						WebPMuxSetChunk(mux, "ICCP", &chunk_data, 0);
					}

					if (!metadata.exif.empty())
					{
						const auto exif_skip = is_exif_signature(metadata.exif) ? exif_signature_len : 0u;
						WebPData chunk_data;
						chunk_data.bytes = metadata.exif.data() + exif_skip;
						chunk_data.size = metadata.exif.size() - exif_skip;
						WebPMuxSetChunk(mux, "EXIF", &chunk_data, 0);
					}
					else if (surface_in->orientation() != ui::orientation::top_left)
					{
						rotate_exif = make_orientation_exif(surface_in->orientation());
						const auto exif_skip = is_exif_signature(rotate_exif) ? exif_signature_len : 0u;
						WebPData chunk_data;
						chunk_data.bytes = rotate_exif.data() + exif_skip;
						chunk_data.size = rotate_exif.size() - exif_skip;
						WebPMuxSetChunk(mux, "EXIF", &chunk_data, 0);
					}

					if (!metadata.xmp.empty())
					{
						WebPData chunk_data;
						chunk_data.bytes = metadata.xmp.data();
						chunk_data.size = metadata.xmp.size();
						WebPMuxSetChunk(mux, "XMP ", &chunk_data, 0);
					}

					WebPData output_data;
					WebPDataInit(&output_data);

					WebPMuxError err = WebPMuxAssemble(mux, &output_data);

					if (err == WEBP_MUX_OK)
					{
						result = std::make_shared<ui::image>(df::cspan(output_data.bytes, output_data.size), dimensions,
						                                     ui::image_format::WEBP, surface_in->orientation());
						WebPDataClear(&output_data);
					}
				}
			}

			WebPMemoryWriterClear(&memory_writer);
		}

		WebPPictureFree(&picture);
	}

	return result;
}

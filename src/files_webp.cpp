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

ui::surface_ptr load_webp(const df::cspan data)
{
	ui::surface_ptr result;
	WebPBitstreamFeatures features;

	if (WebPGetFeatures(data.data, data.size, &features) == VP8_STATUS_OK)
	{
		const auto width = features.width;
		const auto height = features.height;

		result = std::make_shared<ui::surface>();
		// Opaque images decode into the ignored X byte, so tag them RGB and let the renderer skip blending.
		auto* const buffer = result->alloc(width, height,
		                                   features.has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB);

		if (buffer && WebPDecodeBGRAInto(data.data, data.size, buffer, static_cast<int>(height * result->stride()),
		                                 static_cast<int>(result->stride())))
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
				else
				{
					WebPAnimInfo anim_info;
					WebPAnimDecoderOptions dec_options;

					if (WebPAnimDecoderOptionsInit(&dec_options))
					{
						dec_options.color_mode = MODE_BGRA; // Use BGRA to match our surface format
						auto* dec = WebPAnimDecoderNew(&wp_data, &dec_options);

						if (dec)
						{
							const df::releaser<WebPAnimDecoder> dec_releaser(dec, [](auto* i) { WebPAnimDecoderDelete(i); });

							if (WebPAnimDecoderGetInfo(dec, &anim_info))
							{
								// frame_count comes from the file, so bound the work a crafted
								// animation can ask us to do.
								constexpr uint32_t max_frames = 1024;
								const auto frame_count = std::min(anim_info.frame_count, max_frames);

								while (result.frames.size() < frame_count && WebPAnimDecoderHasMoreFrames(dec))
								{
									uint8_t* frame_data = nullptr;
									int timestamp = 0;

									if (WebPAnimDecoderGetNext(dec, &frame_data, &timestamp))
									{
										auto surface = std::make_shared<ui::surface>();
										// Always ARGB: the composited canvas is transparent wherever a frame rect
										// does not cover it, even when the container reports no alpha.
										auto* buffer = surface->alloc(anim_info.canvas_width, anim_info.canvas_height,
										                              ui::texture_format::ARGB);

										if (buffer)
										{
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
				config.thread_level = 1;
				config.lossless = false;
				config.quality = static_cast<float>(params.webp_quality);
				config.method = 6;
				config.preprocessing = 4;
				// https://groups.google.com/a/webmproject.org/forum/#!topic/webp-discuss/7dV1qXrdQ2Y
				config.alpha_quality = params.webp_lossy_alpha ? params.webp_quality : 100;
			}

			df::assert_true(WebPValidateConfig(&config));

			picture.writer = WebPMemoryWrite;
			picture.custom_ptr = &memory_writer;

			const int success = WebPEncode(&config, &picture);

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

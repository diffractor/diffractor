// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "files.h"
#include "metadata_exif.h"

#include "webp/decode.h"
#include "webp/mux.h"
#include "webp/demux.h"
#include "webp/encode.h"

ui::surface_ptr load_webp(df::cspan data)
{
	ui::surface_ptr result;
	int32_t width = 0;
	int32_t height = 0;

	if (WebPGetInfo(data.data, data.size, &width, &height))
	{
		result = std::make_shared<ui::surface>();
		auto* const buffer = result->alloc(width, height, ui::texture_format::ARGB);

		if (WebPDecodeBGRAInto(data.data, data.size, buffer, height * result->stride(), result->stride()))
		{
			WebPData wp_data;
			wp_data.bytes = data.data;
			wp_data.size = data.size;

			const auto* const mux = WebPMuxCreate(&wp_data, 0);

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

		if (mux)
		{
			uint32_t flags = 0;

			WebPMuxGetFeatures(mux, &flags);

			const bool animation = flags & ANIMATION_FLAG;
			const bool icc = flags & ICCP_FLAG;
			const bool exif = flags & EXIF_FLAG;
			const bool xmp = flags & XMP_FLAG;

			if (decode_surface)
			{
				if (!animation)
				{
					auto surface = std::make_shared<ui::surface>();
					auto* buffer = surface->alloc(width, height, ui::texture_format::ARGB);

					if (WebPDecodeBGRAInto(data.data, data.size, buffer, height * surface->stride(), surface->stride()))
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
						// dec_options.color_mode is MODE_RGBA by default here
						auto* dec = WebPAnimDecoderNew(&wp_data, &dec_options);

						if (dec)
						{
							if (WebPAnimDecoderGetInfo(dec, &anim_info))
							{
								auto* demux = WebPDemux(&wp_data);

								if (demux)
								{
									WebPIterator iter = {0,};

									if (WebPDemuxGetFrame(demux, 1, &iter))
									{
										while (WebPAnimDecoderHasMoreFrames(dec))
										{
											uint8_t* outdata = nullptr;
											int timestamp = 0;

											if (WebPAnimDecoderGetNext(dec, &outdata, &timestamp))
											{
											}

											WebPDemuxNextFrame(&iter);
										}

										WebPDemuxReleaseIterator(&iter);
									}

									WebPDemuxDelete(demux);
								}
							}

							WebPAnimDecoderDelete(dec);
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
					const auto exif_skip = is_exif_signature({chunk.bytes, chunk.size}) ? 6u : 0u;
					result.metadata.exif.assign(chunk.bytes + exif_skip, chunk.bytes + chunk.size - exif_skip);

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

			WebPMuxDelete(mux);
		}
	}

	return result;
}

ui::image_ptr save_webp(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                        const file_encode_params& params)
{
	ui::image_ptr result;

	WebPMux* mux = WebPMuxNew();

	if (mux)
	{
		std::vector<uint8_t> rotate_exif;

		const auto dimensions = surface_in->dimensions();
		const auto use_alpha = surface_in->format() == ui::texture_format::ARGB;

		WebPPicture picture;
		WebPPictureInit(&picture);

		picture.width = dimensions.cx;
		picture.height = dimensions.cy;
		picture.use_argb = true;

		const auto ok = use_alpha
			                ? WebPPictureImportBGRA(&picture, surface_in->pixels(), surface_in->stride())
			                : WebPPictureImportBGRX(&picture, surface_in->pixels(), surface_in->stride());

		if (ok)
		{
			WebPMemoryWriter memory_writer;
			WebPMemoryWriterInit(&memory_writer);

			WebPConfig config;
			WebPConfigInit(&config);

			if (params.webp_lossless)
			{
				WebPConfigLosslessPreset(&config, 7);
			}
			else
			{
				config.thread_level = 1;
				config.lossless = false;
				config.quality = params.webp_quality;
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
						WebPMuxError chunk_err = WebPMuxSetChunk(mux, "ICCP", &chunk_data, 0);
					}

					if (!metadata.exif.empty())
					{
						const auto exif_skip = is_exif_signature(metadata.exif) ? 6u : 0u;
						WebPData chunk_data;
						chunk_data.bytes = metadata.exif.data() + exif_skip;
						chunk_data.size = metadata.exif.size() - exif_skip;
						WebPMuxError chunk_err = WebPMuxSetChunk(mux, "EXIF", &chunk_data, 0);
					}
					else if (surface_in->orientation() != ui::orientation::top_left)
					{
						rotate_exif = make_orientation_exif(surface_in->orientation());
						const auto exif_skip = is_exif_signature(rotate_exif) ? 6u : 0u;
						WebPData chunk_data;
						chunk_data.bytes = rotate_exif.data() + exif_skip;
						chunk_data.size = rotate_exif.size() - exif_skip;
						WebPMuxError chunk_err = WebPMuxSetChunk(mux, "EXIF", &chunk_data, 0);
					}

					if (!metadata.xmp.empty())
					{
						WebPData chunk_data;
						chunk_data.bytes = metadata.xmp.data();
						chunk_data.size = metadata.xmp.size();
						WebPMuxError chunk_err = WebPMuxSetChunk(mux, "XMP ", &chunk_data, 0);
					}

					WebPData output_data;
					WebPDataInit(&output_data);

					WebPMuxError err = WebPMuxAssemble(mux, &output_data);

					if (err == WEBP_MUX_OK)
					{
						WebPMuxDelete(mux);
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

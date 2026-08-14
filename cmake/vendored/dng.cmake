# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored dng as diffractor::dng.
#
# Imported from third-party/dng/dng.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_dng STATIC
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_1d_function.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_1d_table.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_abort_sniffer.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_area_task.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_bad_pixels.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_big_table.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_bmff.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_bottlenecks.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_camera_profile.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_color_space.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_color_spec.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_date_time.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_exceptions.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_exif.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_file_stream.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_filter_task.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_fingerprint.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_gain_map.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_globals.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_host.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_hue_sat_map.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_ifd.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_image.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_image_writer.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_info.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_iptc.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_jpeg_image.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_jpeg_memory_source.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_jxl.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_lens_correction.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_linearization_info.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_local_string.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_lossless_jpeg.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_lossless_jpeg_shared.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_matrix.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_memory.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_memory_stream.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_misc_opcodes.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_mosaic_info.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_mutex.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_negative.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_opcodes.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_opcode_list.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_orientation.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_parse_utils.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_pixel_buffer.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_point.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_preview.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_pthread.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_rational.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_read_image.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_rect.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_reference.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_ref_counted_block.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_render.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_resample.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_safe_arithmetic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_shared.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_simple_image.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_spline.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_stream.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_string.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_string_list.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_tag_types.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_temperature.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_tile_iterator.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_tone_curve.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_update_meta.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_utils.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_validate.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_xmp.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_xmp_sdk.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/dng/dng_xy_coord.cpp"
)

target_include_directories(diffractor_dng PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/dng"
        "${CMAKE_SOURCE_DIR}/third-party/libjx"
        "${CMAKE_SOURCE_DIR}/third-party/libjx/lib/include"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib"
        "${CMAKE_SOURCE_DIR}/third-party/xmp/public/include"
)

target_compile_definitions(diffractor_dng PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_dng)

add_library(diffractor::dng ALIAS diffractor_dng)

# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored LibRaw as diffractor::raw.
#
# Imported from third-party/LibRaw/libraw.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_raw STATIC
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/demosaic/aahd_demosaic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/adobepano.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/demosaic/ahd_demosaic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/write/apply_profile.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/postprocessing/aspect_ratio.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/tables/cameralist.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/canon.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/canon_600.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/ciff.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/tables/colorconst.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/tables/colordata.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/cr3_parser.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/crx.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/pana8.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/olympus14.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/sonycc.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decompressors/losslessjpeg.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/curves.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/demosaic/dcb_demosaic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/postprocessing/dcraw_process.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/decoder_info.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/decoders_dcraw.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/decoders_libraw.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/decoders_libraw_dcrdefs.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/demosaic/dht_demosaic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/dng.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/integration/dngsdk_glue.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/epson.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/exif_gps.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/preprocessing/ext_preprocess.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/write/file_write.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/fp_dng.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/fuji.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/fuji_compressed.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/generic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/hasselblad_model.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/identify.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/identify_tools.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/init_close_utils.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/kodak.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/kodak_decoders.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/leica.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/libraw_c_api.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/libraw_datastream.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/load_mfbacks.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/makernotes.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/mediumformat.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/postprocessing/mem_image.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/minolta.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/demosaic/misc_demosaic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/misc_parsers.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/nikon.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/normalize_model.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/olympus.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/open.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/p1.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/pentax.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/phaseone_processing.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/postprocessing/postprocessing_aux.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/postprocessing/postprocessing_utils.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/postprocessing/postprocessing_utils_dcrdefs.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/preprocessing/raw2image.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/integration/rawspeed_glue.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/read_utils.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/samsung.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/smal.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/sony.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/preprocessing/subtract_black.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/thumb_utils.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/metadata/tiff.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/write/tiff_writer.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/unpack.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/decoders/unpack_thumb.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/utils_dcraw.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/utils/utils_libraw.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/tables/wblists.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/x3f/x3f_parse_process.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/x3f/x3f_utils_patched.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw/src/demosaic/xtrans_demosaic.cpp"
)

target_include_directories(diffractor_raw PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/LibRaw"
        "${CMAKE_SOURCE_DIR}/third-party/libjx/lib/include"
        "${CMAKE_SOURCE_DIR}/third-party/dng"
        "${CMAKE_SOURCE_DIR}/third-party/LibJpeg/src"
)

target_compile_definitions(diffractor_raw PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:LIBRAW_NODLL>" "$<$<COMPILE_LANGUAGE:C,CXX>:LIBRAW_LIBRARY_BUILD>" "$<$<COMPILE_LANGUAGE:C,CXX>:LIBRAW_WIN32_UNICODEPATHS>" "$<$<COMPILE_LANGUAGE:C,CXX>:USE_DNGSDK>" "$<$<COMPILE_LANGUAGE:C,CXX>:QT_NO_DEBUG>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:QT_LARGEFILE_SUPPORT>" "$<$<COMPILE_LANGUAGE:C,CXX>:LIBRAW_BUILDLIB>" "$<$<COMPILE_LANGUAGE:C,CXX>:USE_JPEG>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_raw)

add_library(diffractor::raw ALIAS diffractor_raw)

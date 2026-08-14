# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored XMP toolkit fork as diffractor::xmp. The SDK's own CMake expects to
# be driven by per-platform generator scripts that only cover Windows, macOS and Android, so the
# source list is taken from the fork's xmp.vcxproj instead: that is what Diffractor actually ships,
# and it is the only list that stays true to the patches the fork carries.

include_guard(GLOBAL)

set(_xmp_root "${CMAKE_SOURCE_DIR}/third-party/xmp")

if (NOT EXISTS "${_xmp_root}/public/include/XMP.hpp")
    message(STATUS "xmp: third-party/xmp is not checked out; run 'git submodule update --init'")
    return()
endif ()

set(_xmp_sources
        XMPCore/source/ExpatAdapter.cpp
        XMPCore/source/ParseRDF.cpp
        XMPCore/source/WXMPIterator.cpp
        XMPCore/source/WXMPMeta.cpp
        XMPCore/source/WXMPUtils.cpp
        XMPCore/source/XMPCore_Impl.cpp
        XMPCore/source/XMPIterator.cpp
        XMPCore/source/XMPMeta-GetSet.cpp
        XMPCore/source/XMPMeta-Parse.cpp
        XMPCore/source/XMPMeta-Serialize.cpp
        XMPCore/source/XMPMeta.cpp
        XMPCore/source/XMPUtils-FileInfo.cpp
        XMPCore/source/XMPUtils.cpp
        XMPFiles/source/FileHandlers/AIFF_Handler.cpp
        XMPFiles/source/FileHandlers/ASF_Handler.cpp
        XMPFiles/source/FileHandlers/AVCHD_Handler.cpp
        XMPFiles/source/FileHandlers/Basic_Handler.cpp
        XMPFiles/source/FileHandlers/FLV_Handler.cpp
        XMPFiles/source/FileHandlers/GIF_Handler.cpp
        XMPFiles/source/FileHandlers/InDesign_Handler.cpp
        XMPFiles/source/FileHandlers/JPEG_Handler.cpp
        XMPFiles/source/FileHandlers/MP3_Handler.cpp
        XMPFiles/source/FileHandlers/MPEG2_Handler.cpp
        XMPFiles/source/FileHandlers/MPEG4_Handler.cpp
        XMPFiles/source/FileHandlers/P2_Handler.cpp
        XMPFiles/source/FileHandlers/PNG_Handler.cpp
        XMPFiles/source/FileHandlers/PSD_Handler.cpp
        XMPFiles/source/FileHandlers/PostScript_Handler.cpp
        XMPFiles/source/FileHandlers/RIFF_Handler.cpp
        XMPFiles/source/FileHandlers/SVG_Handler.cpp
        XMPFiles/source/FileHandlers/SWF_Handler.cpp
        XMPFiles/source/FileHandlers/Scanner_Handler.cpp
        XMPFiles/source/FileHandlers/SonyHDV_Handler.cpp
        XMPFiles/source/FileHandlers/TIFF_Handler.cpp
        XMPFiles/source/FileHandlers/Trivial_Handler.cpp
        XMPFiles/source/FileHandlers/UCF_Handler.cpp
        XMPFiles/source/FileHandlers/WAVE_Handler.cpp
        XMPFiles/source/FileHandlers/WEBP_Handler.cpp
        XMPFiles/source/FileHandlers/XDCAMEX_Handler.cpp
        XMPFiles/source/FileHandlers/XDCAMFAM_Handler.cpp
        XMPFiles/source/FileHandlers/XDCAMSAM_Handler.cpp
        XMPFiles/source/FileHandlers/XDCAM_Handler.cpp
        XMPFiles/source/FormatSupport/AIFF/AIFFBehavior.cpp
        XMPFiles/source/FormatSupport/AIFF/AIFFMetadata.cpp
        XMPFiles/source/FormatSupport/AIFF/AIFFReconcile.cpp
        XMPFiles/source/FormatSupport/ASF_Support.cpp
        XMPFiles/source/FormatSupport/ID3_Support.cpp
        XMPFiles/source/FormatSupport/IFF/Chunk.cpp
        XMPFiles/source/FormatSupport/IFF/ChunkController.cpp
        XMPFiles/source/FormatSupport/IFF/ChunkPath.cpp
        XMPFiles/source/FormatSupport/IFF/IChunkBehavior.cpp
        XMPFiles/source/FormatSupport/IPTC_Support.cpp
        XMPFiles/source/FormatSupport/ISOBaseMedia_Support.cpp
        XMPFiles/source/FormatSupport/META_Support.cpp
        XMPFiles/source/FormatSupport/MOOV_Support.cpp
        XMPFiles/source/FormatSupport/P2_Support.cpp
        XMPFiles/source/FormatSupport/PNG_Support.cpp
        XMPFiles/source/FormatSupport/PSIR_FileWriter.cpp
        XMPFiles/source/FormatSupport/PSIR_MemoryReader.cpp
        XMPFiles/source/FormatSupport/PackageFormat_Support.cpp
        XMPFiles/source/FormatSupport/PostScript_Support.cpp
        XMPFiles/source/FormatSupport/QuickTime_Support.cpp
        XMPFiles/source/FormatSupport/RIFF.cpp
        XMPFiles/source/FormatSupport/RIFF_Support.cpp
        XMPFiles/source/FormatSupport/ReconcileIPTC.cpp
        XMPFiles/source/FormatSupport/ReconcileLegacy.cpp
        XMPFiles/source/FormatSupport/ReconcileTIFF.cpp
        XMPFiles/source/FormatSupport/Reconcile_Impl.cpp
        XMPFiles/source/FormatSupport/SVG_Adapter.cpp
        XMPFiles/source/FormatSupport/SWF_Support.cpp
        XMPFiles/source/FormatSupport/TIFF_FileWriter.cpp
        XMPFiles/source/FormatSupport/TIFF_MemoryReader.cpp
        XMPFiles/source/FormatSupport/TIFF_Support.cpp
        XMPFiles/source/FormatSupport/TimeConversionUtils.cpp
        XMPFiles/source/FormatSupport/WAVE/BEXTMetadata.cpp
        XMPFiles/source/FormatSupport/WAVE/CartMetadata.cpp
        XMPFiles/source/FormatSupport/WAVE/Cr8rMetadata.cpp
        XMPFiles/source/FormatSupport/WAVE/DISPMetadata.cpp
        XMPFiles/source/FormatSupport/WAVE/INFOMetadata.cpp
        XMPFiles/source/FormatSupport/WAVE/PrmLMetadata.cpp
        XMPFiles/source/FormatSupport/WAVE/WAVEBehavior.cpp
        XMPFiles/source/FormatSupport/WAVE/WAVEReconcile.cpp
        XMPFiles/source/FormatSupport/WAVE/iXMLMetadata.cpp
        XMPFiles/source/FormatSupport/WEBP_Support.cpp
        XMPFiles/source/FormatSupport/XDCAM_Support.cpp
        XMPFiles/source/FormatSupport/XMPScanner.cpp
        XMPFiles/source/HandlerRegistry.cpp
        XMPFiles/source/NativeMetadataSupport/IMetadata.cpp
        XMPFiles/source/NativeMetadataSupport/IReconcile.cpp
        XMPFiles/source/NativeMetadataSupport/MetadataSet.cpp
        XMPFiles/source/PluginHandler/FileHandlerInstance.cpp
        XMPFiles/source/PluginHandler/HostAPIImpl.cpp
        XMPFiles/source/PluginHandler/Module.cpp
        XMPFiles/source/PluginHandler/PluginManager.cpp
        XMPFiles/source/PluginHandler/XMPAtoms.cpp
        XMPFiles/source/WXMPFiles.cpp
        XMPFiles/source/XMPFiles.cpp
        XMPFiles/source/XMPFiles_Impl.cpp
        source/IOUtils.cpp
        source/PerfUtils.cpp
        source/SafeStringAPIs.cpp
        source/UnicodeConversions.cpp
        source/XIO.cpp
        source/XML_Node.cpp
        source/XMPFiles_IO.cpp
        source/XMP_LibUtils.cpp
        source/XMP_ProgressTracker.cpp
        third-party/zuid/interfaces/MD5.cpp

        # The two the vcxproj takes from Windows.
        source/Host_IO-POSIX.cpp
        XMPFiles/source/PluginHandler/OS_Utils_Linux.cpp
)

list(TRANSFORM _xmp_sources PREPEND "${_xmp_root}/")

add_library(diffractor_xmp STATIC ${_xmp_sources})

target_include_directories(diffractor_xmp
        PUBLIC "${_xmp_root}/public/include" "${_xmp_root}"
        PRIVATE "${_xmp_root}/XMPFilesPlugins/api/source")

# UNIX_ENV replaces the vcxproj's WIN_ENV; the rest is the same configuration Windows builds.
# XMP_StaticBuild matters to callers too, so it is PUBLIC: metadata_xmp.cpp includes
# XMP.incl_cpp and has to agree about linkage.
target_compile_definitions(diffractor_xmp
        PUBLIC XMP_StaticBuild=1 UNIX_ENV=1
        PRIVATE EnablePluginManager=0 HAVE_EXPAT_CONFIG_H=1 XML_STATIC=1)

# The SDK reaches for expat directly. It is a system package here, and its config header is not
# the one the SDK bundles, so XML_STATIC is stated above rather than taken from expat_config.h.
if (TARGET diffractor::expat)
    target_link_libraries(diffractor_xmp PRIVATE diffractor::expat)
endif ()

set_target_properties(diffractor_xmp PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)

# One Diffractor patch in MP3_Handler.cpp reconciles the ID3 POPM rating using _itoa_s. That is a
# secure-CRT name, and platform_compat.h is where this repository already supplies those, so it is
# forced in rather than the fork being edited.
target_compile_options(diffractor_xmp PRIVATE -include "${CMAKE_SOURCE_DIR}/src/platform_compat.h")

# Upstream is not warning-clean under -Wall and it is not ours to fix.
target_compile_options(diffractor_xmp PRIVATE -w)

add_library(diffractor::xmp ALIAS diffractor_xmp)

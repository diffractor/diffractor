// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include <stdarg.h>

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

//#pragma comment(lib, "libgcc.a")
//#pragma comment(lib, "libmingw32.a")
//#pragma comment(lib, "libmingwex.a")

//#pragma comment(lib, "libavcodec.a")
//#pragma comment(lib, "libavformat.a")
//#pragma comment(lib, "libavutil.a")
//#pragma comment(lib, "libswscale.a")
//#pragma comment(lib, "libavresample.a")


//#pragma comment(lib, "libx264.a")
//
//extern "C" int avpriv_snprintf(_Out_writes_(_BufferCount) char8_t* const _Buffer, _In_ size_t const _BufferCount,
//	_In_z_
//	_Printf_format_string_                                                          char8_t
//	const* const _Format, `
//{
//	va_list arglist;
//	va_start(arglist, _Format);
//	auto result = vsnprintf(_Buffer, _BufferCount, _Format, arglist);
//	va_end(arglist);
//
//	return result;
//}
//
//extern "C" FILE* __iob_func()
//{
//	static FILE results[] = {
//		__acrt_iob_func(0),
//		__acrt_iob_func(1),
//		__acrt_iob_func(2)
//	}
//
//	return results;
//}


////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

extern "C" {

#ifdef _M_IX86

void ff_yuv2rgb_init_tables_ppc()
{
}

void ff_sws_init_swscale_ppc()
{
}

void ff_sws_init_swscale_aarch64()
{
}

void ff_sws_init_swscale_arm()
{
}

void ff_hevc_dsp_init_aarch64()
{
}

void ff_yuv2rgb_init_ppc()
{
}

void ff_get_unscaled_swscale_ppc()
{
}

void ff_get_unscaled_swscale_arm()
{
}

void ff_get_unscaled_swscale_aarch64()
{
}

void ff_get_cpu_max_align_aarch64()
{
}

void ff_get_cpu_flags_arm()
{
}

void ff_get_cpu_max_align_ppc()
{
}

void ff_get_cpu_flags_aarch64()
{
}

void ff_get_cpu_max_align_arm()
{
}

void ff_get_cpu_flags_ppc()
{
}

void swri_audio_convert_init_arm()
{
}

void swri_audio_convert_init_aarch64()
{
}

void ff_aacdec_init_mips()
{
}

void ff_xvmc_init_block()
{
}

void ff_xvmc_pack_pblocks()
{
}

void swri_resample_dsp_aarch64_init()
{
}

void swri_resample_dsp_arm_init()
{
}

void ff_idctdsp_init_mips()
{
}

void ff_idctdsp_init_ppc()
{
}

void ff_idctdsp_init_aarch64()
{
}

void ff_idctdsp_init_arm()
{
}

void ff_idctdsp_init_alpha()
{
}

void ff_blockdsp_init_alpha()
{
}

void ff_blockdsp_init_mips()
{
}

void ff_blockdsp_init_arm()
{
}

void ff_blockdsp_init_ppc()
{
}

void ff_float_dsp_init_aarch64()
{
}

void ff_float_dsp_init_ppc()
{
}

void ff_float_dsp_init_mips()
{
}

void ff_float_dsp_init_arm()
{
}

void ff_mpv_common_init_arm()
{
}

void ff_mpv_common_init_axp()
{
}

void ff_mpv_common_init_neon()
{
}

void ff_mpv_common_init_mips()
{
}

void ff_mpv_common_init_ppc()
{
}

void ff_acelp_vectors_init_mips()
{
}

void ff_acelp_filter_init_mips()
{
}

void ff_celp_filter_init_mips()
{
}

void ff_audiodsp_init_arm()
{
}

void ff_audiodsp_init_ppc()
{
}

void ff_ac3dsp_init_arm()
{
}

void ff_ac3dsp_init_mips()
{
}

void ff_fmt_convert_init_mips()
{
}

void ff_fmt_convert_init_ppc()
{
}

void ff_fmt_convert_init_arm()
{
}

void ff_fmt_convert_init_aarch64()
{
}

void ff_hpeldsp_init_aarch64()
{
}

void ff_hpeldsp_init_ppc()
{
}

void ff_hpeldsp_init_arm()
{
}

void ff_hpeldsp_init_mips()
{
}

void ff_hpeldsp_init_alpha()
{
}

void ff_llviddsp_init_ppc()
{
}

void ff_fft_init_arm()
{
}

void ff_fft_init_mips()
{
}

void ff_fft_init_ppc()
{
}

void ff_fft_init_aarch64()
{
}

void ff_aacsbr_func_ptr_init_mips()
{
}

void ff_vp78dsp_init_aarch64()
{
}

void ff_vp8dsp_init_arm()
{
}

void ff_vp78dsp_init_ppc()
{
}

void ff_vp8dsp_init_aarch64()
{
}

void ff_vp8dsp_init_mips()
{
}

void ff_vp78dsp_init_arm()
{
}

void ff_videodsp_init_ppc()
{
}

void ff_videodsp_init_arm()
{
}

void ff_videodsp_init_aarch64()
{
}

void ff_videodsp_init_mips()
{
}

void ff_h264_pred_init_mips()
{
}

void ff_h264_pred_init_arm()
{
}

void ff_h264_pred_init_aarch64()
{
}

void ff_celp_math_init_mips()
{
}

void ff_mpadsp_init_ppc()
{
}

void ff_mpadsp_init_mipsdsp()
{
}

void ff_mpadsp_init_aarch64()
{
}

void ff_mpadsp_init_arm()
{
}

void ff_mpadsp_init_mipsfpu()
{
}

void ff_vp6dsp_init_arm()
{
}

void ff_mlpdsp_init_arm()
{
}

void ff_xvid_idct_init_mips()
{
}

void ff_qpeldsp_init_mips()
{
}

void ff_g722dsp_init_arm()
{
}

void ff_h263dsp_init_mips()
{
}

void ff_llauddsp_init_arm()
{
}

void ff_llauddsp_init_ppc()
{
}

void ff_h264dsp_init_mips()
{
}

void ff_h264dsp_init_aarch64()
{
}

void ff_h264dsp_init_ppc()
{
}

void ff_h264dsp_init_arm()
{
}

void ff_rdft_init_arm()
{
}

void ff_h264chroma_init_mips()
{
}

void ff_h264chroma_init_aarch64()
{
}

void ff_h264chroma_init_ppc()
{
}

void ff_h264chroma_init_arm()
{
}

void ff_vp3dsp_init_mips()
{
}

void ff_vp3dsp_init_arm()
{
}

void ff_vp3dsp_init_ppc()
{
}

void ff_vc1dsp_init_arm()
{
}

void ff_vc1dsp_init_aarch64()
{
}

void ff_vc1dsp_init_mips()
{
}

void ff_vc1dsp_init_ppc()
{
}

void ff_vorbisdsp_init_aarch64()
{
}

void ff_vorbisdsp_init_arm()
{
}

void ff_vorbisdsp_init_ppc()
{
}

void ff_vp9dsp_init_arm()
{
}

void ff_vp9dsp_init_aarch64()
{
}

void ff_vp9dsp_init_mips()
{
}

void ff_flacdsp_init_arm()
{
}

void ff_hevc_dsp_init_mips()
{
}

void ff_hevc_dsp_init_ppc()
{
}

void ff_hevc_dsp_init_arm()
{
}

void ff_hevc_pred_init_mips()
{
}

void ff_mpegvideoencdsp_init_mips()
{
}

void ff_mpegvideoencdsp_init_ppc()
{
}

void ff_mpegvideoencdsp_init_arm()
{
}

void ff_simple_idct8_add_avx()
{
}

void ff_simple_idct12_sse2()
{
}

void ff_simple_idct8_put_avx()
{
}

void ff_simple_idct10_avx()
{
}

void ff_simple_idct8_sse2()
{
}

void ff_simple_idct10_put_avx()
{
}

void ff_simple_idct8_put_sse2()
{
}

void ff_simple_idct10_put_sse2()
{
}

void ff_simple_idct10_sse2()
{
}

void ff_simple_idct12_put_avx()
{
}

void ff_simple_idct12_avx()
{
}

void ff_simple_idct8_avx()
{
}

void ff_simple_idct8_add_sse2()
{
}

void ff_simple_idct12_put_sse2()
{
}

void ff_rv40dsp_init_aarch64()
{
}

void ff_rv40dsp_init_arm()
{
}

void ff_mpegvideodsp_init_ppc()
{
}

void ff_sbrdsp_init_aarch64()
{
}

void ff_sbrdsp_init_mips()
{
}

void ff_sbrdsp_init_arm()
{
}

void ff_fft15_avx()
{
}

void ff_mdct15_postreindex_avx2()
{
}

void ff_mlp_rematrix_channel_sse4()
{
}

void ff_mlp_rematrix_channel_avx2_bmi2()
{
}

void ff_me_cmp_init_arm()
{
}

void ff_me_cmp_init_alpha()
{
}

void ff_me_cmp_init_mips()
{
}

void ff_me_cmp_init_ppc()
{
}

void ff_opus_dsp_init_aarch64()
{
}

void ff_wmv2dsp_init_mips()
{
}

void ff_vc1dsp_init_mmx()
{
}

void ff_vc1dsp_init_mmxext()
{
}

void ff_h264qpel_init_aarch64()
{
}

void ff_h264qpel_init_mips()
{
}

void ff_h264qpel_init_ppc()
{
}

void ff_h264qpel_init_arm()
{
}

void ff_synth_filter_init_arm()
{
}

void ff_synth_filter_init_aarch64()
{
}

void ff_flac_decorrelate_indep8_16_sse2()
{
}

void ff_flac_decorrelate_indep8_32_avx()
{
}

void ff_flac_decorrelate_indep8_16_avx()
{
}

void ff_flac_decorrelate_indep8_32_sse2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels64_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v6_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h48_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v64_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv48_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv64_10_avx2()
{
}

void ff_hevc_put_hevc_epel_hv16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_pel_pixels32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h24_10_avx2()
{
}

void ff_hevc_put_hevc_epel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h32_8_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_v32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_v64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v24_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h64_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels16_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv48_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels32_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v48_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv64_8_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h64_8_sse4()
{
}

void ff_hevc_h_loop_filter_luma_8_sse2()
{
}

void ff_hevc_put_hevc_bi_qpel_v16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h48_10_avx2()
{
}

void ff_hevc_put_hevc_pel_pixels6_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_h64_8_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h6_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h64_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h32_8_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_h6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv32_8_avx2()
{
}

void ff_hevc_v_loop_filter_luma_10_sse2()
{
}

void ff_hevc_put_hevc_epel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v48_10_avx2()
{
}

void ff_hevc_put_hevc_epel_v48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv48_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv48_10_avx2()
{
}

void ff_hevc_put_hevc_pel_pixels12_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h6_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v32_10_avx2()
{
}

void ff_hevc_put_hevc_epel_v16_10_avx2()
{
}

void ff_hevc_h_loop_filter_luma_12_sse2()
{
}

void ff_hevc_put_hevc_uni_epel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels64_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv32_10_avx2()
{
}

void ff_hevc_put_hevc_epel_v64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h32_8_avx2()
{
}

void ff_hevc_put_hevc_epel_h6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv4_10_sse4()
{
}

void ff_hevc_h_loop_filter_luma_10_avx()
{
}

void ff_hevc_put_hevc_pel_pixels48_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels6_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_v64_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_v16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels24_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h6_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h6_8_sse4()
{
}

void ff_hevc_idct_16x16_8_avx()
{
}

void ff_hevc_put_hevc_uni_epel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h16_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v6_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv6_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_hv24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v4_10_sse4()
{
}

void ff_hevc_v_loop_filter_luma_8_avx()
{
}

void ff_hevc_put_hevc_uni_pel_pixels128_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels32_10_avx2()
{
}

void ff_hevc_put_hevc_pel_pixels32_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv24_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv8_10_sse4()
{
}

void ff_hevc_v_loop_filter_luma_12_sse2()
{
}

void ff_hevc_put_hevc_epel_h8_10_sse4()
{
}

void ff_hevc_idct_16x16_10_sse2()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h64_8_avx2()
{
}

void ff_hevc_put_hevc_epel_hv6_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels64_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h16_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels6_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h32_8_avx2()
{
}

void ff_hevc_put_hevc_epel_v32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels4_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v64_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels64_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_v24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_v48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v32_10_avx2()
{
}

void ff_hevc_put_hevc_epel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv64_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_h6_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv24_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv6_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_epel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h24_10_avx2()
{
}

void ff_hevc_put_hevc_epel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv32_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_v48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v64_10_sse4()
{
}

void ff_hevc_idct_32x32_10_avx()
{
}

void ff_hevc_put_hevc_epel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels64_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h6_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels16_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v24_10_avx2()
{
}

void ff_hevc_v_loop_filter_luma_8_ssse3()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels48_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_hv48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels12_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v4_8_sse4()
{
}

void ff_hevc_idct_16x16_10_avx()
{
}

void ff_hevc_put_hevc_bi_epel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v6_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v6_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v6_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v64_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels32_8_avx2()
{
}

void ff_hevc_put_hevc_epel_h32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels64_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h48_12_sse4()
{
}

void ff_hevc_h_loop_filter_luma_8_avx()
{
}

void ff_hevc_put_hevc_uni_epel_hv8_10_sse4()
{
}

void ff_hevc_idct_32x32_10_sse2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels6_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v32_12_sse4()
{
}

void ff_hevc_h_loop_filter_luma_10_ssse3()
{
}

void ff_hevc_put_hevc_bi_pel_pixels32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv6_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels8_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v6_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels6_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels16_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v24_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v16_12_sse4()
{
}

void ff_hevc_v_loop_filter_luma_12_avx()
{
}

void ff_hevc_put_hevc_uni_epel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v48_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv6_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v6_10_sse4()
{
}

void ff_hevc_idct_16x16_8_sse2()
{
}

void ff_hevc_put_hevc_bi_w_epel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels48_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels16_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_v8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv32_8_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_h16_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_v32_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v6_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels4_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h6_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v48_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv8_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels48_8_sse4()
{
}

void ff_hevc_v_loop_filter_luma_10_avx()
{
}

void ff_hevc_put_hevc_epel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h24_12_sse4()
{
}

void ff_hevc_idct_32x32_8_avx()
{
}

void ff_hevc_h_loop_filter_luma_10_sse2()
{
}

void ff_hevc_put_hevc_uni_epel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels24_12_sse4()
{
}

void ff_hevc_idct_32x32_8_sse2()
{
}

void ff_hevc_put_hevc_qpel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv6_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v6_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_epel_v6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels48_8_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v64_10_avx2()
{
}

void ff_hevc_put_hevc_pel_pixels64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels48_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels64_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels6_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v8_10_sse4()
{
}

void ff_hevc_h_loop_filter_luma_8_ssse3()
{
}

void ff_hevc_put_hevc_uni_epel_v6_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_v32_8_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels64_8_avx2()
{
}

void ff_hevc_put_hevc_epel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_epel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_epel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels4_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels6_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h6_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels6_12_sse4()
{
}

void ff_hevc_put_hevc_epel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_epel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v4_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h48_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_v64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels48_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v32_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_epel_v32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_v48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels24_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_qpel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels64_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels64_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv6_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv32_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h24_10_avx2()
{
}

void ff_hevc_put_hevc_bi_pel_pixels12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_epel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_hv6_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h12_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h48_8_avx2()
{
}

void ff_hevc_put_hevc_epel_h48_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_v16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_qpel_h48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_w_epel_v12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_h32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv12_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels24_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels6_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels24_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels16_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels64_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_epel_v48_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h64_8_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_v24_8_sse4()
{
}

void ff_hevc_h_loop_filter_luma_12_ssse3()
{
}

void ff_hevc_put_hevc_bi_epel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels16_12_sse4()
{
}

void ff_hevc_v_loop_filter_luma_12_ssse3()
{
}

void ff_hevc_put_hevc_bi_w_epel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels64_12_sse4()
{
}

void ff_hevc_h_loop_filter_luma_12_avx()
{
}

void ff_hevc_put_hevc_bi_qpel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h24_10_avx2()
{
}

void ff_hevc_put_hevc_qpel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels8_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels64_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels16_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h4_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_v64_10_sse4()
{
}

void ff_hevc_put_hevc_pel_pixels4_8_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv48_10_avx2()
{
}

void ff_hevc_put_hevc_bi_epel_v32_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv48_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_pel_pixels48_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h12_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h48_8_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_h32_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h4_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels8_10_sse4()
{
}

void ff_hevc_v_loop_filter_luma_10_ssse3()
{
}

void ff_hevc_put_hevc_qpel_h64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_h8_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_v24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_hv16_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels6_8_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels96_8_avx2()
{
}

void ff_hevc_put_hevc_uni_qpel_hv24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv16_10_sse4()
{
}

void ff_hevc_put_hevc_bi_w_qpel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv32_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv12_10_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_v24_10_sse4()
{
}

void ff_hevc_put_hevc_epel_hv64_8_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h8_8_sse4()
{
}

void ff_hevc_put_hevc_bi_pel_pixels24_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v48_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_hv12_12_sse4()
{
}

void ff_hevc_put_hevc_bi_w_epel_hv4_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h48_8_sse4()
{
}

void ff_hevc_put_hevc_bi_w_pel_pixels12_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h64_8_sse4()
{
}

void ff_hevc_put_hevc_epel_h64_8_avx2()
{
}

void ff_hevc_put_hevc_qpel_hv64_8_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_h24_10_avx2()
{
}

void ff_hevc_put_hevc_uni_epel_v4_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_v32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_h24_12_sse4()
{
}

void ff_hevc_put_hevc_uni_pel_pixels32_12_sse4()
{
}

void ff_hevc_put_hevc_epel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_v24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_h24_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h12_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv32_12_sse4()
{
}

void ff_hevc_put_hevc_uni_w_epel_v64_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v8_12_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_h32_8_avx2()
{
}

void ff_hevc_put_hevc_uni_pel_pixels16_12_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_h64_12_sse4()
{
}

void ff_hevc_put_hevc_qpel_hv64_10_avx2()
{
}

void ff_hevc_put_hevc_uni_w_epel_v48_10_sse4()
{
}

void ff_hevc_put_hevc_bi_qpel_hv64_10_sse4()
{
}

void ff_hevc_put_hevc_bi_epel_v6_10_sse4()
{
}

void ff_hevc_put_hevc_qpel_v4_8_sse4()
{
}

void ff_hevc_v_loop_filter_luma_8_sse2()
{
}

void ff_hevc_put_hevc_bi_epel_hv24_10_sse4()
{
}

void ff_hevc_put_hevc_uni_w_qpel_h24_8_sse4()
{
}

void ff_hevc_put_hevc_uni_qpel_hv64_10_avx2()
{
}

void ff_hevc_put_hevc_epel_v16_12_sse4()
{
}

void ff_hevc_put_hevc_uni_epel_hv16_12_sse4()
{
}

void ff_rv34dsp_init_arm()
{
}

void ff_psdsp_init_arm()
{
}

void ff_psdsp_init_mips()
{
}

void ff_psdsp_init_aarch64()
{
}

void rgb2rgb_init_aarch64()
{
}

#endif
#ifdef _M_X64

void ff_aacdec_init_mips()
{
};

void ff_aacsbr_func_ptr_init_mips()
{
};

void ff_ac3dsp_init_arm()
{
};

void ff_ac3dsp_init_mips()
{
};

void ff_acelp_filter_init_mips()
{
};

void ff_acelp_vectors_init_mips()
{
};

void ff_add_bytes_mmx()
{
};

void ff_add_hfyu_left_pred_bgr32_mmx()
{
};

void ff_add_int16_mmx()
{
};

void ff_add_median_pred_mmxext()
{
};

void ff_audiodsp_init_arm()
{
};

void ff_audiodsp_init_ppc()
{
};

void ff_blockdsp_init_alpha()
{
};

void ff_blockdsp_init_arm()
{
};

void ff_blockdsp_init_mips()
{
};

void ff_blockdsp_init_ppc()
{
};

void ff_celp_filter_init_mips()
{
};

void ff_celp_math_init_mips()
{
};

void ff_fft_init_aarch64()
{
};

void ff_fft_init_arm()
{
};

void ff_fft_init_mips()
{
};

void ff_fft_init_ppc()
{
};

void ff_flacdsp_init_arm()
{
};

void ff_float_dsp_init_aarch64()
{
};

void ff_float_dsp_init_arm()
{
};

void ff_float_dsp_init_mips()
{
};

void ff_float_dsp_init_ppc()
{
};

void ff_fmt_convert_init_aarch64()
{
};

void ff_fmt_convert_init_arm()
{
};

void ff_fmt_convert_init_mips()
{
};

void ff_fmt_convert_init_ppc()
{
};

void ff_g722dsp_init_arm()
{
};

void ff_get_cpu_flags_aarch64()
{
};

void ff_get_cpu_flags_arm()
{
};

void ff_get_cpu_flags_ppc()
{
};

void ff_get_cpu_max_align_aarch64()
{
};

void ff_get_cpu_max_align_arm()
{
};

void ff_get_cpu_max_align_ppc()
{
};

void ff_get_unscaled_swscale_aarch64()
{
};

void ff_get_unscaled_swscale_arm()
{
};

void ff_get_unscaled_swscale_ppc()
{
};

void ff_h263dsp_init_mips()
{
};

void ff_h264_pred_init_aarch64()
{
};

void ff_h264_pred_init_arm()
{
};

void ff_h264_pred_init_mips()
{
};

void ff_h264chroma_init_aarch64()
{
};

void ff_h264chroma_init_arm()
{
};

void ff_h264chroma_init_mips()
{
};

void ff_h264chroma_init_ppc()
{
};

void ff_h264dsp_init_aarch64()
{
};

void ff_h264dsp_init_arm()
{
};

void ff_h264dsp_init_mips()
{
};

void ff_h264dsp_init_ppc()
{
};

void ff_h264qpel_init_aarch64()
{
};

void ff_h264qpel_init_arm()
{
};

void ff_h264qpel_init_mips()
{
};

void ff_h264qpel_init_ppc()
{
};

void ff_hevc_dsp_init_aarch64()
{
};

void ff_hevc_dsp_init_arm()
{
};

void ff_hevc_dsp_init_mips()
{
};

void ff_hevc_dsp_init_ppc()
{
};

void ff_hevc_pred_init_mips()
{
};

void ff_hpeldsp_init_aarch64()
{
};

void ff_hpeldsp_init_alpha()
{
};

void ff_hpeldsp_init_arm()
{
};

void ff_hpeldsp_init_mips()
{
};

void ff_hpeldsp_init_ppc()
{
};

void ff_idctdsp_init_aarch64()
{
};

void ff_idctdsp_init_alpha()
{
};

void ff_idctdsp_init_arm()
{
};

void ff_idctdsp_init_mips()
{
};

void ff_idctdsp_init_ppc()
{
};

void ff_lfe_fir0_float_sse()
{
};

void ff_llauddsp_init_arm()
{
};

void ff_llauddsp_init_ppc()
{
};

void ff_llviddsp_init_ppc()
{
};

void ff_me_cmp_init_alpha()
{
};

void ff_me_cmp_init_arm()
{
};

void ff_me_cmp_init_mips()
{
};

void ff_me_cmp_init_ppc()
{
};

void ff_mlpdsp_init_arm()
{
};

void ff_mpadsp_init_aarch64()
{
};

void ff_mpadsp_init_arm()
{
};

void ff_mpadsp_init_mipsdsp()
{
};

void ff_mpadsp_init_mipsfpu()
{
};

void ff_mpadsp_init_ppc()
{
};

void ff_mpegvideodsp_init_ppc()
{
};

void ff_mpegvideoencdsp_init_arm()
{
};

void ff_mpegvideoencdsp_init_mips()
{
};

void ff_mpegvideoencdsp_init_ppc()
{
};

void ff_mpv_common_init_arm()
{
};

void ff_mpv_common_init_axp()
{
};

void ff_mpv_common_init_mips()
{
};

void ff_mpv_common_init_neon()
{
};

void ff_mpv_common_init_ppc()
{
};

void ff_opus_dsp_init_aarch64()
{
};

void ff_psdsp_init_aarch64()
{
};

void ff_psdsp_init_arm()
{
};

void ff_psdsp_init_mips()
{
};

void ff_qpeldsp_init_mips()
{
};

void ff_rdft_init_arm()
{
};

void ff_resample_common_int16_mmxext()
{
};

void ff_resample_linear_int16_mmxext()
{
};

void ff_rv34_idct_dc_add_mmx()
{
};

void ff_rv34dsp_init_arm()
{
};

void ff_rv40dsp_init_aarch64()
{
};

void ff_rv40dsp_init_arm()
{
};

void ff_sbrdsp_init_aarch64()
{
};

void ff_sbrdsp_init_arm()
{
};

void ff_sbrdsp_init_mips()
{
};

void ff_sws_init_swscale_aarch64()
{
};

void ff_sws_init_swscale_arm()
{
};

void ff_sws_init_swscale_ppc()
{
};

void ff_synth_filter_init_aarch64()
{
};

void ff_synth_filter_init_arm()
{
};

void ff_vc1dsp_init_aarch64()
{
};

void ff_vc1dsp_init_arm()
{
};

void ff_vc1dsp_init_mips()
{
};

void ff_vc1dsp_init_mmx()
{
};

void ff_vc1dsp_init_mmxext()
{
};

void ff_vc1dsp_init_ppc()
{
};

void ff_videodsp_init_aarch64()
{
};

void ff_videodsp_init_arm()
{
};

void ff_videodsp_init_mips()
{
};

void ff_videodsp_init_ppc()
{
};

void ff_vorbisdsp_init_aarch64()
{
};

void ff_vorbisdsp_init_arm()
{
};

void ff_vorbisdsp_init_ppc()
{
};

void ff_vp3dsp_init_arm()
{
};

void ff_vp3dsp_init_mips()
{
};

void ff_vp3dsp_init_ppc()
{
};

void ff_vp6dsp_init_arm()
{
};

void ff_vp78dsp_init_aarch64()
{
};

void ff_vp78dsp_init_arm()
{
};

void ff_vp78dsp_init_ppc()
{
};

void ff_vp8dsp_init_aarch64()
{
};

void ff_vp8dsp_init_arm()
{
};

void ff_vp8dsp_init_mips()
{
};

void ff_vp9dsp_init_aarch64()
{
};

void ff_vp9dsp_init_arm()
{
};

void ff_vp9dsp_init_mips()
{
};

void ff_wmv2dsp_init_mips()
{
};

void ff_xvid_idct_init_mips()
{
};

void ff_xvmc_init_block()
{
};

void ff_xvmc_pack_pblocks()
{
};

void ff_yuv2rgb_init_ppc()
{
};

void ff_yuv2rgb_init_tables_ppc()
{
};

void rgb2rgb_init_aarch64()
{
};

void swri_audio_convert_init_aarch64()
{
};

void swri_audio_convert_init_arm()
{
};

void swri_resample_dsp_aarch64_init()
{
};

void swri_resample_dsp_arm_init()
{
};

#endif
#ifdef _M_ARM64

void ff_yuv2yuvX_mmx() {};
void ff_yuv2yuvX_mmxext() {};
void ff_yuv2yuvX_sse3() {};
void ff_yuv2yuvX_avx2() {};
void ff_hscale8to15_4_sse2() {};
void ff_hscale9to15_4_sse2() {};
void ff_hscale10to15_4_sse2() {};
void ff_hscale12to15_4_sse2() {};
void ff_hscale14to15_4_sse2() {};
void ff_hscale16to15_4_sse2() {};
void ff_hscale8to19_4_sse2() {};
void ff_hscale9to19_4_sse2() {};
void ff_hscale10to19_4_sse2() {};
void ff_hscale12to19_4_sse2() {};
void ff_hscale14to19_4_sse2() {};
void ff_hscale16to19_4_sse2() {};
void ff_hscale8to15_8_sse2() {};
void ff_hscale9to15_8_sse2() {};
void ff_hscale10to15_8_sse2() {};
void ff_hscale12to15_8_sse2() {};
void ff_hscale14to15_8_sse2() {};
void ff_hscale16to15_8_sse2() {};
void ff_hscale8to19_8_sse2() {};
void ff_hscale9to19_8_sse2() {};
void ff_hscale10to19_8_sse2() {};
void ff_hscale12to19_8_sse2() {};
void ff_hscale14to19_8_sse2() {};
void ff_hscale16to19_8_sse2() {};
void ff_hscale8to15_X4_sse2() {};
void ff_hscale9to15_X4_sse2() {};
void ff_hscale10to15_X4_sse2() {};
void ff_hscale12to15_X4_sse2() {};
void ff_hscale14to15_X4_sse2() {};
void ff_hscale16to15_X4_sse2() {};
void ff_hscale8to19_X4_sse2() {};
void ff_hscale9to19_X4_sse2() {};
void ff_hscale10to19_X4_sse2() {};
void ff_hscale12to19_X4_sse2() {};
void ff_hscale14to19_X4_sse2() {};
void ff_hscale16to19_X4_sse2() {};
void ff_hscale8to15_X8_sse2() {};
void ff_hscale9to15_X8_sse2() {};
void ff_hscale10to15_X8_sse2() {};
void ff_hscale12to15_X8_sse2() {};
void ff_hscale14to15_X8_sse2() {};
void ff_hscale16to15_X8_sse2() {};
void ff_hscale8to19_X8_sse2() {};
void ff_hscale9to19_X8_sse2() {};
void ff_hscale10to19_X8_sse2() {};
void ff_hscale12to19_X8_sse2() {};
void ff_hscale14to19_X8_sse2() {};
void ff_hscale16to19_X8_sse2() {};
void ff_hscale8to15_4_ssse3() {};
void ff_hscale9to15_4_ssse3() {};
void ff_hscale10to15_4_ssse3() {};
void ff_hscale12to15_4_ssse3() {};
void ff_hscale14to15_4_ssse3() {};
void ff_hscale16to15_4_ssse3() {};
void ff_hscale8to19_4_ssse3() {};
void ff_hscale9to19_4_ssse3() {};
void ff_hscale10to19_4_ssse3() {};
void ff_hscale12to19_4_ssse3() {};
void ff_hscale14to19_4_ssse3() {};
void ff_hscale16to19_4_ssse3() {};
void ff_hscale8to15_8_ssse3() {};
void ff_hscale9to15_8_ssse3() {};
void ff_hscale10to15_8_ssse3() {};
void ff_hscale12to15_8_ssse3() {};
void ff_hscale14to15_8_ssse3() {};
void ff_hscale16to15_8_ssse3() {};
void ff_hscale8to19_8_ssse3() {};
void ff_hscale9to19_8_ssse3() {};
void ff_hscale10to19_8_ssse3() {};
void ff_hscale12to19_8_ssse3() {};
void ff_hscale14to19_8_ssse3() {};
void ff_hscale16to19_8_ssse3() {};
void ff_hscale8to15_X4_ssse3() {};
void ff_hscale9to15_X4_ssse3() {};
void ff_hscale10to15_X4_ssse3() {};
void ff_hscale12to15_X4_ssse3() {};
void ff_hscale14to15_X4_ssse3() {};
void ff_hscale16to15_X4_ssse3() {};
void ff_hscale8to19_X4_ssse3() {};
void ff_hscale9to19_X4_ssse3() {};
void ff_hscale10to19_X4_ssse3() {};
void ff_hscale12to19_X4_ssse3() {};
void ff_hscale14to19_X4_ssse3() {};
void ff_hscale16to19_X4_ssse3() {};
void ff_hscale8to15_X8_ssse3() {};
void ff_hscale9to15_X8_ssse3() {};
void ff_hscale10to15_X8_ssse3() {};
void ff_hscale12to15_X8_ssse3() {};
void ff_hscale14to15_X8_ssse3() {};
void ff_hscale16to15_X8_ssse3() {};
void ff_hscale8to19_X8_ssse3() {};
void ff_hscale9to19_X8_ssse3() {};
void ff_hscale10to19_X8_ssse3() {};
void ff_hscale12to19_X8_ssse3() {};
void ff_hscale14to19_X8_ssse3() {};
void ff_hscale16to19_X8_ssse3() {};
void ff_hscale8to19_4_sse4() {};
void ff_hscale9to19_4_sse4() {};
void ff_hscale10to19_4_sse4() {};
void ff_hscale12to19_4_sse4() {};
void ff_hscale14to19_4_sse4() {};
void ff_hscale16to19_4_sse4() {};
void ff_hscale8to19_8_sse4() {};
void ff_hscale9to19_8_sse4() {};
void ff_hscale10to19_8_sse4() {};
void ff_hscale12to19_8_sse4() {};
void ff_hscale14to19_8_sse4() {};
void ff_hscale16to19_8_sse4() {};
void ff_hscale8to19_X4_sse4() {};
void ff_hscale9to19_X4_sse4() {};
void ff_hscale10to19_X4_sse4() {};
void ff_hscale12to19_X4_sse4() {};
void ff_hscale14to19_X4_sse4() {};
void ff_hscale16to19_X4_sse4() {};
void ff_hscale8to19_X8_sse4() {};
void ff_hscale9to19_X8_sse4() {};
void ff_hscale10to19_X8_sse4() {};
void ff_hscale12to19_X8_sse4() {};
void ff_hscale14to19_X8_sse4() {};
void ff_hscale16to19_X8_sse4() {};
void ff_yuv2planeX_8_sse2() {};
void ff_yuv2planeX_9_sse2() {};
void ff_yuv2planeX_10_sse2() {};
void ff_yuv2planeX_8_sse4() {};
void ff_yuv2planeX_9_sse4() {};
void ff_yuv2planeX_10_sse4() {};
void ff_yuv2planeX_16_sse4() {};
void ff_yuv2planeX_8_avx() {};
void ff_yuv2planeX_9_avx() {};
void ff_yuv2planeX_10_avx() {};
void ff_yuv2plane1_8_sse2() {};
void ff_yuv2plane1_9_sse2() {};
void ff_yuv2plane1_10_sse2() {};
void ff_yuv2plane1_16_sse2() {};
void ff_yuv2plane1_16_sse4() {};
void ff_yuv2plane1_8_avx() {};
void ff_yuv2plane1_9_avx() {};
void ff_yuv2plane1_10_avx() {};
void ff_yuv2plane1_16_avx() {};
void ff_uyvyToY_sse2() {};
void ff_uyvyToUV_sse2() {};
void ff_yuyvToY_sse2() {};
void ff_yuyvToUV_sse2() {};
void ff_nv12ToUV_sse2() {};
void ff_nv21ToUV_sse2() {};
void ff_rgbaToY_sse2() {};
void ff_rgbaToUV_sse2() {};
void ff_bgraToY_sse2() {};
void ff_bgraToUV_sse2() {};
void ff_argbToY_sse2() {};
void ff_argbToUV_sse2() {};
void ff_abgrToY_sse2() {};
void ff_abgrToUV_sse2() {};
void ff_rgb24ToY_sse2() {};
void ff_rgb24ToUV_sse2() {};
void ff_bgr24ToY_sse2() {};
void ff_bgr24ToUV_sse2() {};
void ff_rgb24ToY_ssse3() {};
void ff_rgb24ToUV_ssse3() {};
void ff_bgr24ToY_ssse3() {};
void ff_bgr24ToUV_ssse3() {};
void ff_uyvyToUV_avx() {};
void ff_yuyvToUV_avx() {};
void ff_nv12ToUV_avx() {};
void ff_nv21ToUV_avx() {};
void ff_rgbaToY_avx() {};
void ff_rgbaToUV_avx() {};
void ff_bgraToY_avx() {};
void ff_bgraToUV_avx() {};
void ff_argbToY_avx() {};
void ff_argbToUV_avx() {};
void ff_abgrToY_avx() {};
void ff_abgrToUV_avx() {};
void ff_rgb24ToY_avx() {};
void ff_rgb24ToUV_avx() {};
void ff_bgr24ToY_avx() {};
void ff_bgr24ToUV_avx() {};
void ff_yuv2nv12cX_avx2() {};
void ff_yuv2nv21cX_avx2() {};
void ff_image_copy_plane_uc_from_sse4() {};
void ff_v210_planar_unpack_unaligned_ssse3() {};
void ff_v210_planar_unpack_unaligned_avx() {};
void ff_v210_planar_unpack_unaligned_avx2() {};
void ff_v210_planar_unpack_aligned_ssse3() {};
void ff_v210_planar_unpack_aligned_avx() {};
void ff_v210_planar_unpack_aligned_avx2() {};
void __rdtsc() {};
void ff_cpu_cpuid() {};
void ff_cpu_xgetbv() {};
void ff_shuffle_bytes_2103_mmxext() {};
void ff_shuffle_bytes_2103_ssse3() {};
void ff_shuffle_bytes_0321_ssse3() {};
void ff_shuffle_bytes_1230_ssse3() {};
void ff_shuffle_bytes_3012_ssse3() {};
void ff_shuffle_bytes_3210_ssse3() {};
void ff_uyvytoyuv422_sse2() {};
void ff_uyvytoyuv422_avx() {};
void ff_yuv_420_rgb24_mmx() {};
void ff_yuv_420_bgr24_mmx() {};
void ff_yuv_420_rgb15_mmx() {};
void ff_yuv_420_rgb16_mmx() {};
void ff_yuv_420_rgb32_mmx() {};
void ff_yuv_420_bgr32_mmx() {};
void ff_yuva_420_rgb32_mmx() {};
void ff_yuva_420_bgr32_mmx() {};
void ff_yuv_420_rgb24_mmxext() {};
void ff_yuv_420_bgr24_mmxext() {};
void ff_yuv_420_rgb24_ssse3() {};
void ff_yuv_420_bgr24_ssse3() {};
void ff_yuv_420_rgb15_ssse3() {};
void ff_yuv_420_rgb16_ssse3() {};
void ff_yuv_420_rgb32_ssse3() {};
void ff_yuv_420_bgr32_ssse3() {};
void ff_yuva_420_rgb32_ssse3() {};
void ff_yuva_420_bgr32_ssse3() {};
void ff_mix_1_1_a_float_sse() {};
void ff_mix_2_1_a_float_sse() {};
void ff_mix_1_1_a_float_avx() {};
void ff_mix_2_1_a_float_avx() {};
void ff_mix_1_1_a_int16_mmx() {};
void ff_mix_2_1_a_int16_mmx() {};
void ff_mix_1_1_a_int16_sse2() {};
void ff_mix_2_1_a_int16_sse2() {};
void ff_int32_to_int16_a_mmx() {};
void ff_int16_to_int32_a_mmx() {};
void ff_int32_to_int16_a_sse2() {};
void ff_float_to_int16_a_sse2() {};
void ff_int16_to_int32_a_sse2() {};
void ff_float_to_int32_a_sse2() {};
void ff_int16_to_float_a_sse2() {};
void ff_int32_to_float_a_sse2() {};
void ff_int32_to_float_a_avx() {};
void ff_float_to_int32_a_avx2() {};
void ff_pack_2ch_int16_to_int16_a_sse2() {};
void ff_pack_2ch_int32_to_int16_a_sse2() {};
void ff_pack_2ch_float_to_int16_a_sse2() {};
void ff_pack_2ch_int16_to_int32_a_sse2() {};
void ff_pack_2ch_int32_to_int32_a_sse2() {};
void ff_pack_2ch_float_to_int32_a_sse2() {};
void ff_pack_2ch_int16_to_float_a_sse2() {};
void ff_pack_2ch_int32_to_float_a_sse2() {};
void ff_pack_6ch_float_to_float_a_mmx() {};
void ff_pack_6ch_float_to_float_a_sse() {};
void ff_pack_6ch_float_to_int32_a_sse2() {};
void ff_pack_6ch_int32_to_float_a_sse2() {};
void ff_pack_6ch_float_to_int32_a_avx() {};
void ff_pack_6ch_int32_to_float_a_avx() {};
void ff_pack_6ch_float_to_float_a_avx() {};
void ff_pack_8ch_float_to_int32_a_sse2() {};
void ff_pack_8ch_int32_to_float_a_sse2() {};
void ff_pack_8ch_float_to_float_a_sse2() {};
void ff_pack_8ch_float_to_int32_a_avx() {};
void ff_pack_8ch_int32_to_float_a_avx() {};
void ff_pack_8ch_float_to_float_a_avx() {};
void ff_unpack_2ch_int16_to_int16_a_sse2() {};
void ff_unpack_2ch_int32_to_int16_a_sse2() {};
void ff_unpack_2ch_float_to_int16_a_sse2() {};
void ff_unpack_2ch_int16_to_int32_a_sse2() {};
void ff_unpack_2ch_int32_to_int32_a_sse2() {};
void ff_unpack_2ch_float_to_int32_a_sse2() {};
void ff_unpack_2ch_int16_to_float_a_sse2() {};
void ff_unpack_2ch_int32_to_float_a_sse2() {};
void ff_unpack_2ch_int16_to_int16_a_ssse3() {};
void ff_unpack_2ch_int16_to_int32_a_ssse3() {};
void ff_unpack_2ch_int16_to_float_a_ssse3() {};
void ff_unpack_6ch_float_to_float_a_sse() {};
void ff_unpack_6ch_float_to_int32_a_sse2() {};
void ff_unpack_6ch_int32_to_float_a_sse2() {};
void ff_unpack_6ch_float_to_int32_a_avx() {};
void ff_unpack_6ch_int32_to_float_a_avx() {};
void ff_unpack_6ch_float_to_float_a_avx() {};
void ff_add_pixels_clamped_mmx() {};
void ff_add_pixels_clamped_sse2() {};
void ff_put_pixels_clamped_mmx() {};
void ff_put_pixels_clamped_sse2() {};
void ff_put_signed_pixels_clamped_mmx() {};
void ff_put_signed_pixels_clamped_sse2() {};
void ff_simple_idct_mmx() {};
void ff_simple_idct_add_mmx() {};
void ff_simple_idct_put_mmx() {};
void ff_simple_idct_add_sse2() {};
void ff_simple_idct_put_sse2() {};
void ff_clear_block_mmx() {};
void ff_clear_block_sse() {};
void ff_clear_block_avx() {};
void ff_clear_blocks_mmx() {};
void ff_clear_blocks_sse() {};
void ff_clear_blocks_avx() {};
void ff_avg_pixels8_mmx() {};
void ff_avg_pixels8_mmxext() {};
void ff_avg_pixels16_mmx() {};
void ff_avg_pixels16_sse2() {};
void ff_put_pixels8_mmx() {};
void ff_put_pixels16_mmx() {};
void ff_put_pixels16_sse2() {};
void ff_avg_pixels8_x2_mmx() {};
void ff_avg_pixels8_xy2_mmxext() {};
void ff_avg_pixels8_xy2_ssse3() {};
void ff_avg_pixels16_xy2_sse2() {};
void ff_avg_pixels16_xy2_ssse3() {};
void ff_put_pixels8_xy2_ssse3() {};
void ff_put_pixels16_xy2_sse2() {};
void ff_put_pixels16_xy2_ssse3() {};
void ff_put_pixels8_x2_mmxext() {};
void ff_put_pixels8_x2_3dnow() {};
void ff_put_pixels16_x2_mmxext() {};
void ff_put_pixels16_x2_3dnow() {};
void ff_put_pixels16_x2_sse2() {};
void ff_avg_pixels16_x2_sse2() {};
void ff_put_pixels16_y2_sse2() {};
void ff_avg_pixels16_y2_sse2() {};
void ff_put_no_rnd_pixels8_x2_mmxext() {};
void ff_put_no_rnd_pixels8_x2_3dnow() {};
void ff_put_pixels8_y2_mmxext() {};
void ff_put_pixels8_y2_3dnow() {};
void ff_put_no_rnd_pixels8_y2_mmxext() {};
void ff_put_no_rnd_pixels8_y2_3dnow() {};
void ff_avg_pixels8_3dnow() {};
void ff_avg_pixels8_x2_mmxext() {};
void ff_avg_pixels8_x2_3dnow() {};
void ff_avg_pixels8_y2_mmxext() {};
void ff_avg_pixels8_y2_3dnow() {};
void ff_avg_pixels8_xy2_3dnow() {};
void ff_avg_approx_pixels8_xy2_mmxext() {};
void ff_avg_approx_pixels8_xy2_3dnow() {};
void ff_cfhd_horiz_filter_sse2() {};
void ff_cfhd_vert_filter_sse2() {};
void ff_cfhd_horiz_filter_clip10_sse2() {};
void ff_cfhd_horiz_filter_clip12_sse2() {};
void ff_bswap32_buf_sse2() {};
void ff_bswap32_buf_ssse3() {};
void ff_bswap32_buf_avx2() {};
void ff_pix_sum16_sse2() {};
void ff_pix_sum16_xop() {};
void ff_pix_norm1_sse2() {};
void ff_emu_edge_vfix1_mmx() {};
void ff_emu_edge_vfix2_mmx() {};
void ff_emu_edge_vfix3_mmx() {};
void ff_emu_edge_vfix4_mmx() {};
void ff_emu_edge_vfix5_mmx() {};
void ff_emu_edge_vfix6_mmx() {};
void ff_emu_edge_vfix7_mmx() {};
void ff_emu_edge_vfix8_mmx() {};
void ff_emu_edge_vfix9_mmx() {};
void ff_emu_edge_vfix10_mmx() {};
void ff_emu_edge_vfix11_mmx() {};
void ff_emu_edge_vfix12_mmx() {};
void ff_emu_edge_vfix13_mmx() {};
void ff_emu_edge_vfix14_mmx() {};
void ff_emu_edge_vfix15_mmx() {};
void ff_emu_edge_vfix16_sse() {};
void ff_emu_edge_vfix17_sse() {};
void ff_emu_edge_vfix18_sse() {};
void ff_emu_edge_vfix19_sse() {};
void ff_emu_edge_vfix20_sse() {};
void ff_emu_edge_vfix21_sse() {};
void ff_emu_edge_vfix22_sse() {};
void ff_emu_edge_hfix2_mmx() {};
void ff_emu_edge_hfix4_mmx() {};
void ff_emu_edge_hfix6_mmx() {};
void ff_emu_edge_hfix8_mmx() {};
void ff_emu_edge_hfix10_mmx() {};
void ff_emu_edge_hfix12_mmx() {};
void ff_emu_edge_hfix14_mmx() {};
void ff_emu_edge_hfix16_sse2() {};
void ff_emu_edge_hfix18_sse2() {};
void ff_emu_edge_hfix20_sse2() {};
void ff_emu_edge_hfix22_sse2() {};
void ff_emu_edge_hfix8_avx2() {};
void ff_emu_edge_hfix10_avx2() {};
void ff_emu_edge_hfix12_avx2() {};
void ff_emu_edge_hfix14_avx2() {};
void ff_emu_edge_hfix16_avx2() {};
void ff_emu_edge_hfix18_avx2() {};
void ff_emu_edge_hfix20_avx2() {};
void ff_emu_edge_hfix22_avx2() {};
void ff_emu_edge_vvar_sse() {};
void ff_emu_edge_hvar_sse2() {};
void ff_emu_edge_hvar_avx2() {};
void ff_prefetch_mmxext() {};
void ff_vertical_compose53iL0_sse2() {};
void ff_vertical_compose_dirac53iH0_sse2() {};
void ff_vertical_compose_dd137iL0_sse2() {};
void ff_vertical_compose_dd97iH0_sse2() {};
void ff_vertical_compose_haar_sse2() {};
void ff_horizontal_compose_haar0i_sse2() {};
void ff_horizontal_compose_haar1i_sse2() {};
void ff_horizontal_compose_dd97i_ssse3() {};
void ff_avg_pixels16_mmxext() {};
void ff_add_rect_clamped_sse2() {};
void ff_add_dirac_obmc8_mmx() {};
void ff_add_dirac_obmc16_sse2() {};
void ff_add_dirac_obmc32_sse2() {};
void ff_put_signed_rect_clamped_sse2() {};
void ff_put_signed_rect_clamped_10_sse4() {};
void ff_dequant_subband_32_sse4() {};
void ff_dirac_hpel_filter_v_sse2() {};
void ff_dirac_hpel_filter_h_sse2() {};
void ff_add_int16_mmx() {};
void ff_add_int16_sse2() {};
void ff_add_int16_avx2() {};
void ff_add_hfyu_left_pred_bgr32_mmx() {};
void ff_add_hfyu_left_pred_bgr32_sse2() {};
void ff_add_hfyu_median_pred_int16_mmxext() {};
void ff_add_bytes_mmx() {};
void ff_add_bytes_sse2() {};
void ff_add_bytes_avx2() {};
void ff_add_median_pred_mmxext() {};
void ff_add_median_pred_sse2() {};
void ff_add_left_pred_ssse3() {};
void ff_add_left_pred_unaligned_ssse3() {};
void ff_add_left_pred_unaligned_avx2() {};
void ff_add_left_pred_int16_ssse3() {};
void ff_add_left_pred_int16_unaligned_ssse3() {};
void ff_add_gradient_pred_ssse3() {};
void ff_add_gradient_pred_avx2() {};
void ff_h263_h_loop_filter_mmx() {};
void ff_h263_v_loop_filter_mmx() {};
void ff_put_pixels8_l2_mmxext() {};
void ff_put_no_rnd_pixels8_l2_mmxext() {};
void ff_avg_pixels8_l2_mmxext() {};
void ff_put_pixels16_l2_mmxext() {};
void ff_avg_pixels16_l2_mmxext() {};
void ff_put_no_rnd_pixels16_l2_mmxext() {};
void ff_put_mpeg4_qpel16_h_lowpass_mmxext() {};
void ff_avg_mpeg4_qpel16_h_lowpass_mmxext() {};
void ff_put_no_rnd_mpeg4_qpel16_h_lowpass_mmxext() {};
void ff_put_mpeg4_qpel8_h_lowpass_mmxext() {};
void ff_avg_mpeg4_qpel8_h_lowpass_mmxext() {};
void ff_put_no_rnd_mpeg4_qpel8_h_lowpass_mmxext() {};
void ff_put_mpeg4_qpel16_v_lowpass_mmxext() {};
void ff_avg_mpeg4_qpel16_v_lowpass_mmxext() {};
void ff_put_no_rnd_mpeg4_qpel16_v_lowpass_mmxext() {};
void ff_put_mpeg4_qpel8_v_lowpass_mmxext() {};
void ff_avg_mpeg4_qpel8_v_lowpass_mmxext() {};
void ff_put_no_rnd_mpeg4_qpel8_v_lowpass_mmxext() {};
void ff_hevc_put_hevc_uni_w4_8_sse4() {};
void ff_hevc_put_hevc_bi_w4_8_sse4() {};
void ff_hevc_put_hevc_uni_w6_8_sse4() {};
void ff_hevc_put_hevc_bi_w6_8_sse4() {};
void ff_hevc_put_hevc_uni_w8_8_sse4() {};
void ff_hevc_put_hevc_bi_w8_8_sse4() {};
void ff_hevc_put_hevc_uni_w4_10_sse4() {};
void ff_hevc_put_hevc_bi_w4_10_sse4() {};
void ff_hevc_put_hevc_uni_w6_10_sse4() {};
void ff_hevc_put_hevc_bi_w6_10_sse4() {};
void ff_hevc_put_hevc_uni_w8_10_sse4() {};
void ff_hevc_put_hevc_bi_w8_10_sse4() {};
void ff_hevc_put_hevc_uni_w4_12_sse4() {};
void ff_hevc_put_hevc_bi_w4_12_sse4() {};
void ff_hevc_put_hevc_uni_w6_12_sse4() {};
void ff_hevc_put_hevc_bi_w6_12_sse4() {};
void ff_hevc_put_hevc_uni_w8_12_sse4() {};
void ff_hevc_put_hevc_bi_w8_12_sse4() {};
void ff_hevc_add_residual_4_8_mmxext() {};
void ff_hevc_add_residual_8_8_sse2() {};
void ff_hevc_add_residual_16_8_sse2() {};
void ff_hevc_add_residual_32_8_sse2() {};
void ff_hevc_add_residual_8_8_avx() {};
void ff_hevc_add_residual_16_8_avx() {};
void ff_hevc_add_residual_32_8_avx() {};
void ff_hevc_add_residual_32_8_avx2() {};
void ff_hevc_add_residual_4_10_mmxext() {};
void ff_hevc_add_residual_8_10_sse2() {};
void ff_hevc_add_residual_16_10_sse2() {};
void ff_hevc_add_residual_32_10_sse2() {};
void ff_hevc_add_residual_16_10_avx2() {};
void ff_hevc_add_residual_32_10_avx2() {};
void ff_hevc_h_loop_filter_chroma_8_sse2() {};
void ff_hevc_v_loop_filter_chroma_8_sse2() {};
void ff_hevc_h_loop_filter_chroma_10_sse2() {};
void ff_hevc_v_loop_filter_chroma_10_sse2() {};
void ff_hevc_h_loop_filter_chroma_12_sse2() {};
void ff_hevc_v_loop_filter_chroma_12_sse2() {};
void ff_hevc_h_loop_filter_chroma_8_avx() {};
void ff_hevc_v_loop_filter_chroma_8_avx() {};
void ff_hevc_h_loop_filter_chroma_10_avx() {};
void ff_hevc_v_loop_filter_chroma_10_avx() {};
void ff_hevc_h_loop_filter_chroma_12_avx() {};
void ff_hevc_v_loop_filter_chroma_12_avx() {};
void ff_hevc_idct_4x4_dc_8_mmxext() {};
void ff_hevc_idct_4x4_dc_10_mmxext() {};
void ff_hevc_idct_4x4_dc_12_mmxext() {};
void ff_hevc_idct_8x8_dc_8_mmxext() {};
void ff_hevc_idct_8x8_dc_10_mmxext() {};
void ff_hevc_idct_8x8_dc_12_mmxext() {};
void ff_hevc_idct_8x8_dc_8_sse2() {};
void ff_hevc_idct_8x8_dc_10_sse2() {};
void ff_hevc_idct_8x8_dc_12_sse2() {};
void ff_hevc_idct_16x16_dc_8_sse2() {};
void ff_hevc_idct_16x16_dc_10_sse2() {};
void ff_hevc_idct_16x16_dc_12_sse2() {};
void ff_hevc_idct_32x32_dc_8_sse2() {};
void ff_hevc_idct_32x32_dc_10_sse2() {};
void ff_hevc_idct_32x32_dc_12_sse2() {};
void ff_hevc_idct_16x16_dc_8_avx2() {};
void ff_hevc_idct_16x16_dc_10_avx2() {};
void ff_hevc_idct_16x16_dc_12_avx2() {};
void ff_hevc_idct_32x32_dc_8_avx2() {};
void ff_hevc_idct_32x32_dc_10_avx2() {};
void ff_hevc_idct_32x32_dc_12_avx2() {};
void ff_hevc_idct_4x4_8_sse2() {};
void ff_hevc_idct_4x4_10_sse2() {};
void ff_hevc_idct_8x8_8_sse2() {};
void ff_hevc_idct_8x8_10_sse2() {};
void ff_hevc_idct_4x4_8_avx() {};
void ff_hevc_idct_4x4_10_avx() {};
void ff_hevc_idct_8x8_8_avx() {};
void ff_hevc_idct_8x8_10_avx() {};
void ff_hevc_sao_band_filter_8_8_sse2() {};
void ff_hevc_sao_band_filter_16_8_sse2() {};
void ff_hevc_sao_band_filter_32_8_sse2() {};
void ff_hevc_sao_band_filter_48_8_sse2() {};
void ff_hevc_sao_band_filter_64_8_sse2() {};
void ff_hevc_sao_band_filter_8_10_sse2() {};
void ff_hevc_sao_band_filter_16_10_sse2() {};
void ff_hevc_sao_band_filter_32_10_sse2() {};
void ff_hevc_sao_band_filter_48_10_sse2() {};
void ff_hevc_sao_band_filter_64_10_sse2() {};
void ff_hevc_sao_band_filter_8_12_sse2() {};
void ff_hevc_sao_band_filter_16_12_sse2() {};
void ff_hevc_sao_band_filter_32_12_sse2() {};
void ff_hevc_sao_band_filter_48_12_sse2() {};
void ff_hevc_sao_band_filter_64_12_sse2() {};
void ff_hevc_sao_band_filter_8_8_avx() {};
void ff_hevc_sao_band_filter_16_8_avx() {};
void ff_hevc_sao_band_filter_32_8_avx() {};
void ff_hevc_sao_band_filter_48_8_avx() {};
void ff_hevc_sao_band_filter_64_8_avx() {};
void ff_hevc_sao_band_filter_8_10_avx() {};
void ff_hevc_sao_band_filter_16_10_avx() {};
void ff_hevc_sao_band_filter_32_10_avx() {};
void ff_hevc_sao_band_filter_48_10_avx() {};
void ff_hevc_sao_band_filter_64_10_avx() {};
void ff_hevc_sao_band_filter_8_12_avx() {};
void ff_hevc_sao_band_filter_16_12_avx() {};
void ff_hevc_sao_band_filter_32_12_avx() {};
void ff_hevc_sao_band_filter_48_12_avx() {};
void ff_hevc_sao_band_filter_64_12_avx() {};
void ff_hevc_sao_band_filter_8_8_avx2() {};
void ff_hevc_sao_band_filter_16_8_avx2() {};
void ff_hevc_sao_band_filter_32_8_avx2() {};
void ff_hevc_sao_band_filter_48_8_avx2() {};
void ff_hevc_sao_band_filter_64_8_avx2() {};
void ff_hevc_sao_band_filter_8_10_avx2() {};
void ff_hevc_sao_band_filter_16_10_avx2() {};
void ff_hevc_sao_band_filter_32_10_avx2() {};
void ff_hevc_sao_band_filter_48_10_avx2() {};
void ff_hevc_sao_band_filter_64_10_avx2() {};
void ff_hevc_sao_band_filter_8_12_avx2() {};
void ff_hevc_sao_band_filter_16_12_avx2() {};
void ff_hevc_sao_band_filter_32_12_avx2() {};
void ff_hevc_sao_band_filter_48_12_avx2() {};
void ff_hevc_sao_band_filter_64_12_avx2() {};
void ff_hevc_sao_edge_filter_8_8_ssse3() {};
void ff_hevc_sao_edge_filter_16_8_ssse3() {};
void ff_hevc_sao_edge_filter_32_8_ssse3() {};
void ff_hevc_sao_edge_filter_48_8_ssse3() {};
void ff_hevc_sao_edge_filter_64_8_ssse3() {};
void ff_hevc_sao_edge_filter_32_8_avx2() {};
void ff_hevc_sao_edge_filter_48_8_avx2() {};
void ff_hevc_sao_edge_filter_64_8_avx2() {};
void ff_hevc_sao_edge_filter_8_10_sse2() {};
void ff_hevc_sao_edge_filter_16_10_sse2() {};
void ff_hevc_sao_edge_filter_32_10_sse2() {};
void ff_hevc_sao_edge_filter_48_10_sse2() {};
void ff_hevc_sao_edge_filter_64_10_sse2() {};
void ff_hevc_sao_edge_filter_8_10_avx2() {};
void ff_hevc_sao_edge_filter_16_10_avx2() {};
void ff_hevc_sao_edge_filter_32_10_avx2() {};
void ff_hevc_sao_edge_filter_48_10_avx2() {};
void ff_hevc_sao_edge_filter_64_10_avx2() {};
void ff_hevc_sao_edge_filter_8_12_sse2() {};
void ff_hevc_sao_edge_filter_16_12_sse2() {};
void ff_hevc_sao_edge_filter_32_12_sse2() {};
void ff_hevc_sao_edge_filter_48_12_sse2() {};
void ff_hevc_sao_edge_filter_64_12_sse2() {};
void ff_hevc_sao_edge_filter_8_12_avx2() {};
void ff_hevc_sao_edge_filter_16_12_avx2() {};
void ff_hevc_sao_edge_filter_32_12_avx2() {};
void ff_hevc_sao_edge_filter_48_12_avx2() {};
void ff_hevc_sao_edge_filter_64_12_avx2() {};
void ff_ict_float_sse() {};
void ff_ict_float_avx() {};
void ff_ict_float_fma3() {};
void ff_ict_float_fma4() {};
void ff_rct_int_sse2() {};
void ff_rct_int_avx2() {};
void ff_xvid_idct_sse2() {};
void ff_xvid_idct_put_sse2() {};
void ff_xvid_idct_add_sse2() {};
void ff_vc1_v_loop_filter4_mmxext() {};
void ff_vc1_h_loop_filter4_mmxext() {};
void ff_vc1_v_loop_filter8_mmxext() {};
void ff_vc1_h_loop_filter8_mmxext() {};
void ff_vc1_v_loop_filter8_sse2() {};
void ff_vc1_h_loop_filter8_sse2() {};
void ff_vc1_v_loop_filter4_ssse3() {};
void ff_vc1_h_loop_filter4_ssse3() {};
void ff_vc1_v_loop_filter8_ssse3() {};
void ff_vc1_h_loop_filter8_ssse3() {};
void ff_vc1_h_loop_filter8_sse4() {};
void ff_put_vc1_chroma_mc8_nornd_mmx() {};
void ff_avg_vc1_chroma_mc8_nornd_mmxext() {};
void ff_avg_vc1_chroma_mc8_nornd_3dnow() {};
void ff_put_vc1_chroma_mc8_nornd_ssse3() {};
void ff_avg_vc1_chroma_mc8_nornd_ssse3() {};
void ff_vc1_inv_trans_4x4_dc_mmxext() {};
void ff_vc1_inv_trans_4x8_dc_mmxext() {};
void ff_vc1_inv_trans_8x4_dc_mmxext() {};
void ff_vc1_inv_trans_8x8_dc_mmxext() {};
void ff_prores_idct_put_10_sse2() {};
void ff_prores_idct_put_10_avx() {};
void ff_h264_idct_add_8_mmx() {};
void ff_h264_idct_add_8_sse2() {};
void ff_h264_idct_add_8_avx() {};
void ff_h264_idct_add_10_sse2() {};
void ff_h264_idct_dc_add_8_mmxext() {};
void ff_h264_idct_dc_add_8_sse2() {};
void ff_h264_idct_dc_add_8_avx() {};
void ff_h264_idct_dc_add_10_mmxext() {};
void ff_h264_idct8_dc_add_8_mmxext() {};
void ff_h264_idct8_dc_add_10_sse2() {};
void ff_h264_idct8_add_8_mmx() {};
void ff_h264_idct8_add_8_sse2() {};
void ff_h264_idct8_add_10_sse2() {};
void ff_h264_idct_add_10_avx() {};
void ff_h264_idct8_dc_add_10_avx() {};
void ff_h264_idct8_add_10_avx() {};
void ff_h264_idct8_add4_8_mmx() {};
void ff_h264_idct8_add4_8_mmxext() {};
void ff_h264_idct8_add4_8_sse2() {};
void ff_h264_idct8_add4_10_sse2() {};
void ff_h264_idct8_add4_10_avx() {};
void ff_h264_idct_add16_8_mmx() {};
void ff_h264_idct_add16_8_mmxext() {};
void ff_h264_idct_add16_8_sse2() {};
void ff_h264_idct_add16_10_sse2() {};
void ff_h264_idct_add16intra_8_mmx() {};
void ff_h264_idct_add16intra_8_mmxext() {};
void ff_h264_idct_add16intra_8_sse2() {};
void ff_h264_idct_add16intra_10_sse2() {};
void ff_h264_idct_add16_10_avx() {};
void ff_h264_idct_add16intra_10_avx() {};
void ff_h264_idct_add8_8_mmx() {};
void ff_h264_idct_add8_8_mmxext() {};
void ff_h264_idct_add8_8_sse2() {};
void ff_h264_idct_add8_10_sse2() {};
void ff_h264_idct_add8_10_avx() {};
void ff_h264_idct_add8_422_8_mmx() {};
void ff_h264_idct_add8_422_10_sse2() {};
void ff_h264_idct_add8_422_10_avx() {};
void ff_h264_luma_dc_dequant_idct_mmx() {};
void ff_h264_luma_dc_dequant_idct_sse2() {};
void ff_h264_loop_filter_strength_mmxext() {};
void ff_deblock_h_luma_mbaff_8_sse2() {};
void ff_deblock_h_luma_mbaff_8_avx() {};
void ff_deblock_h_chroma_8_mmxext() {};
void ff_deblock_h_chroma_intra_8_mmxext() {};
void ff_deblock_h_chroma422_8_mmxext() {};
void ff_deblock_h_chroma422_intra_8_mmxext() {};
void ff_deblock_v_chroma_8_mmxext() {};
void ff_deblock_v_chroma_intra_8_mmxext() {};
void ff_deblock_h_luma_8_sse2() {};
void ff_deblock_h_luma_intra_8_sse2() {};
void ff_deblock_v_luma_8_sse2() {};
void ff_deblock_v_luma_intra_8_sse2() {};
void ff_deblock_h_chroma_8_sse2() {};
void ff_deblock_h_chroma_intra_8_sse2() {};
void ff_deblock_h_chroma422_8_sse2() {};
void ff_deblock_h_chroma422_intra_8_sse2() {};
void ff_deblock_v_chroma_8_sse2() {};
void ff_deblock_v_chroma_intra_8_sse2() {};
void ff_deblock_h_luma_8_avx() {};
void ff_deblock_h_luma_intra_8_avx() {};
void ff_deblock_v_luma_8_avx() {};
void ff_deblock_v_luma_intra_8_avx() {};
void ff_deblock_h_chroma_8_avx() {};
void ff_deblock_h_chroma_intra_8_avx() {};
void ff_deblock_h_chroma422_8_avx() {};
void ff_deblock_h_chroma422_intra_8_avx() {};
void ff_deblock_v_chroma_8_avx() {};
void ff_deblock_v_chroma_intra_8_avx() {};
void ff_deblock_h_luma_10_sse2() {};
void ff_deblock_h_luma_intra_10_sse2() {};
void ff_deblock_v_luma_10_sse2() {};
void ff_deblock_v_luma_intra_10_sse2() {};
void ff_deblock_h_chroma_10_sse2() {};
void ff_deblock_h_chroma422_10_sse2() {};
void ff_deblock_v_chroma_10_sse2() {};
void ff_deblock_v_chroma_intra_10_sse2() {};
void ff_deblock_h_luma_10_avx() {};
void ff_deblock_h_luma_intra_10_avx() {};
void ff_deblock_v_luma_10_avx() {};
void ff_deblock_v_luma_intra_10_avx() {};
void ff_deblock_h_chroma_10_avx() {};
void ff_deblock_h_chroma422_10_avx() {};
void ff_deblock_v_chroma_10_avx() {};
void ff_deblock_v_chroma_intra_10_avx() {};
void ff_h264_weight_16_mmxext() {};
void ff_h264_biweight_16_mmxext() {};
void ff_h264_weight_16_sse2() {};
void ff_h264_biweight_16_sse2() {};
void ff_h264_biweight_16_ssse3() {};
void ff_h264_weight_8_mmxext() {};
void ff_h264_biweight_8_mmxext() {};
void ff_h264_weight_8_sse2() {};
void ff_h264_biweight_8_sse2() {};
void ff_h264_biweight_8_ssse3() {};
void ff_h264_weight_4_mmxext() {};
void ff_h264_biweight_4_mmxext() {};
void ff_h264_weight_16_10_sse2() {};
void ff_h264_weight_16_10_sse4() {};
void ff_h264_biweight_16_10_sse2() {};
void ff_h264_biweight_16_10_sse4() {};
void ff_h264_weight_8_10_sse2() {};
void ff_h264_weight_8_10_sse4() {};
void ff_h264_biweight_8_10_sse2() {};
void ff_h264_biweight_8_10_sse4() {};
void ff_h264_weight_4_10_sse2() {};
void ff_h264_weight_4_10_sse4() {};
void ff_h264_biweight_4_10_sse2() {};
void ff_h264_biweight_4_10_sse4() {};
void ff_pred4x4_dc_10_mmxext() {};
void ff_pred4x4_down_left_10_sse2() {};
void ff_pred4x4_down_left_10_avx() {};
void ff_pred4x4_down_right_10_sse2() {};
void ff_pred4x4_down_right_10_ssse3() {};
void ff_pred4x4_down_right_10_avx() {};
void ff_pred4x4_vertical_left_10_sse2() {};
void ff_pred4x4_vertical_left_10_avx() {};
void ff_pred4x4_vertical_right_10_sse2() {};
void ff_pred4x4_vertical_right_10_ssse3() {};
void ff_pred4x4_vertical_right_10_avx() {};
void ff_pred4x4_horizontal_up_10_mmxext() {};
void ff_pred4x4_horizontal_down_10_sse2() {};
void ff_pred4x4_horizontal_down_10_ssse3() {};
void ff_pred4x4_horizontal_down_10_avx() {};
void ff_pred8x8_dc_10_mmxext() {};
void ff_pred8x8_dc_10_sse2() {};
void ff_pred8x8_top_dc_10_sse2() {};
void ff_pred8x8_plane_10_sse2() {};
void ff_pred8x8_vertical_10_sse2() {};
void ff_pred8x8_horizontal_10_sse2() {};
void ff_pred8x8l_dc_10_sse2() {};
void ff_pred8x8l_dc_10_avx() {};
void ff_pred8x8l_128_dc_10_mmxext() {};
void ff_pred8x8l_128_dc_10_sse2() {};
void ff_pred8x8l_top_dc_10_sse2() {};
void ff_pred8x8l_top_dc_10_avx() {};
void ff_pred8x8l_vertical_10_sse2() {};
void ff_pred8x8l_vertical_10_avx() {};
void ff_pred8x8l_horizontal_10_sse2() {};
void ff_pred8x8l_horizontal_10_ssse3() {};
void ff_pred8x8l_horizontal_10_avx() {};
void ff_pred8x8l_down_left_10_sse2() {};
void ff_pred8x8l_down_left_10_ssse3() {};
void ff_pred8x8l_down_left_10_avx() {};
void ff_pred8x8l_down_right_10_sse2() {};
void ff_pred8x8l_down_right_10_ssse3() {};
void ff_pred8x8l_down_right_10_avx() {};
void ff_pred8x8l_vertical_right_10_sse2() {};
void ff_pred8x8l_vertical_right_10_ssse3() {};
void ff_pred8x8l_vertical_right_10_avx() {};
void ff_pred8x8l_horizontal_up_10_sse2() {};
void ff_pred8x8l_horizontal_up_10_ssse3() {};
void ff_pred8x8l_horizontal_up_10_avx() {};
void ff_pred16x16_dc_10_mmxext() {};
void ff_pred16x16_dc_10_sse2() {};
void ff_pred16x16_top_dc_10_mmxext() {};
void ff_pred16x16_top_dc_10_sse2() {};
void ff_pred16x16_128_dc_10_mmxext() {};
void ff_pred16x16_128_dc_10_sse2() {};
void ff_pred16x16_left_dc_10_mmxext() {};
void ff_pred16x16_left_dc_10_sse2() {};
void ff_pred16x16_vertical_10_mmxext() {};
void ff_pred16x16_vertical_10_sse2() {};
void ff_pred16x16_horizontal_10_mmxext() {};
void ff_pred16x16_horizontal_10_sse2() {};
void ff_pred16x16_vertical_8_mmx() {};
void ff_pred16x16_vertical_8_sse() {};
void ff_pred16x16_horizontal_8_mmx() {};
void ff_pred16x16_horizontal_8_mmxext() {};
void ff_pred16x16_horizontal_8_ssse3() {};
void ff_pred16x16_dc_8_mmxext() {};
void ff_pred16x16_dc_8_sse2() {};
void ff_pred16x16_dc_8_ssse3() {};
void ff_pred16x16_plane_h264_8_mmx() {};
void ff_pred16x16_plane_h264_8_mmxext() {};
void ff_pred16x16_plane_h264_8_sse2() {};
void ff_pred16x16_plane_h264_8_ssse3() {};
void ff_pred16x16_plane_rv40_8_mmx() {};
void ff_pred16x16_plane_rv40_8_mmxext() {};
void ff_pred16x16_plane_rv40_8_sse2() {};
void ff_pred16x16_plane_rv40_8_ssse3() {};
void ff_pred16x16_plane_svq3_8_mmx() {};
void ff_pred16x16_plane_svq3_8_mmxext() {};
void ff_pred16x16_plane_svq3_8_sse2() {};
void ff_pred16x16_plane_svq3_8_ssse3() {};
void ff_pred16x16_tm_vp8_8_mmx() {};
void ff_pred16x16_tm_vp8_8_mmxext() {};
void ff_pred16x16_tm_vp8_8_sse2() {};
void ff_pred16x16_tm_vp8_8_avx2() {};
void ff_pred8x8_top_dc_8_mmxext() {};
void ff_pred8x8_dc_rv40_8_mmxext() {};
void ff_pred8x8_dc_8_mmxext() {};
void ff_pred8x8_vertical_8_mmx() {};
void ff_pred8x8_horizontal_8_mmx() {};
void ff_pred8x8_horizontal_8_mmxext() {};
void ff_pred8x8_horizontal_8_ssse3() {};
void ff_pred8x8_plane_8_mmx() {};
void ff_pred8x8_plane_8_mmxext() {};
void ff_pred8x8_plane_8_sse2() {};
void ff_pred8x8_plane_8_ssse3() {};
void ff_pred8x8_tm_vp8_8_mmx() {};
void ff_pred8x8_tm_vp8_8_mmxext() {};
void ff_pred8x8_tm_vp8_8_sse2() {};
void ff_pred8x8_tm_vp8_8_ssse3() {};
void ff_pred8x8l_top_dc_8_mmxext() {};
void ff_pred8x8l_top_dc_8_ssse3() {};
void ff_pred8x8l_dc_8_mmxext() {};
void ff_pred8x8l_dc_8_ssse3() {};
void ff_pred8x8l_horizontal_8_mmxext() {};
void ff_pred8x8l_horizontal_8_ssse3() {};
void ff_pred8x8l_vertical_8_mmxext() {};
void ff_pred8x8l_vertical_8_ssse3() {};
void ff_pred8x8l_down_left_8_mmxext() {};
void ff_pred8x8l_down_left_8_sse2() {};
void ff_pred8x8l_down_left_8_ssse3() {};
void ff_pred8x8l_down_right_8_mmxext() {};
void ff_pred8x8l_down_right_8_sse2() {};
void ff_pred8x8l_down_right_8_ssse3() {};
void ff_pred8x8l_vertical_right_8_mmxext() {};
void ff_pred8x8l_vertical_right_8_sse2() {};
void ff_pred8x8l_vertical_right_8_ssse3() {};
void ff_pred8x8l_vertical_left_8_sse2() {};
void ff_pred8x8l_vertical_left_8_ssse3() {};
void ff_pred8x8l_horizontal_up_8_mmxext() {};
void ff_pred8x8l_horizontal_up_8_ssse3() {};
void ff_pred8x8l_horizontal_down_8_mmxext() {};
void ff_pred8x8l_horizontal_down_8_sse2() {};
void ff_pred8x8l_horizontal_down_8_ssse3() {};
void ff_pred4x4_dc_8_mmxext() {};
void ff_pred4x4_down_left_8_mmxext() {};
void ff_pred4x4_down_right_8_mmxext() {};
void ff_pred4x4_vertical_left_8_mmxext() {};
void ff_pred4x4_vertical_right_8_mmxext() {};
void ff_pred4x4_horizontal_up_8_mmxext() {};
void ff_pred4x4_horizontal_down_8_mmxext() {};
void ff_pred4x4_tm_vp8_8_mmx() {};
void ff_pred4x4_tm_vp8_8_mmxext() {};
void ff_pred4x4_tm_vp8_8_ssse3() {};
void ff_pred4x4_vertical_vp8_8_mmxext() {};
void ff_vp3_idct_put_sse2() {};
void ff_vp3_idct_add_sse2() {};
void ff_vp3_idct_dc_add_mmxext() {};
void ff_vp3_v_loop_filter_mmxext() {};
void ff_vp3_h_loop_filter_mmxext() {};
void ff_put_vp_no_rnd_pixels8_l2_mmx() {};
void ff_restore_rgb_planes_sse2() {};
void ff_restore_rgb_planes_avx2() {};
void ff_restore_rgb_planes10_sse2() {};
void ff_restore_rgb_planes10_avx2() {};
void ff_put_h264_chroma_mc8_rnd_mmx() {};
void ff_avg_h264_chroma_mc8_rnd_mmxext() {};
void ff_avg_h264_chroma_mc8_rnd_3dnow() {};
void ff_put_h264_chroma_mc4_mmx() {};
void ff_avg_h264_chroma_mc4_mmxext() {};
void ff_avg_h264_chroma_mc4_3dnow() {};
void ff_put_h264_chroma_mc2_mmxext() {};
void ff_avg_h264_chroma_mc2_mmxext() {};
void ff_put_h264_chroma_mc8_rnd_ssse3() {};
void ff_put_h264_chroma_mc4_ssse3() {};
void ff_avg_h264_chroma_mc8_rnd_ssse3() {};
void ff_avg_h264_chroma_mc4_ssse3() {};
void ff_put_h264_chroma_mc2_10_mmxext() {};
void ff_avg_h264_chroma_mc2_10_mmxext() {};
void ff_put_h264_chroma_mc4_10_mmxext() {};
void ff_avg_h264_chroma_mc4_10_mmxext() {};
void ff_put_h264_chroma_mc8_10_sse2() {};
void ff_avg_h264_chroma_mc8_10_sse2() {};
void ff_put_h264_chroma_mc8_10_avx() {};
void ff_avg_h264_chroma_mc8_10_avx() {};
void ff_vp6_filter_diag4_sse2() {};
void ff_put_vp8_epel4_h4_mmxext() {};
void ff_put_vp8_epel4_h6_mmxext() {};
void ff_put_vp8_epel4_v4_mmxext() {};
void ff_put_vp8_epel4_v6_mmxext() {};
void ff_put_vp8_epel8_h4_sse2() {};
void ff_put_vp8_epel8_h6_sse2() {};
void ff_put_vp8_epel8_v4_sse2() {};
void ff_put_vp8_epel8_v6_sse2() {};
void ff_put_vp8_epel4_h4_ssse3() {};
void ff_put_vp8_epel4_h6_ssse3() {};
void ff_put_vp8_epel4_v4_ssse3() {};
void ff_put_vp8_epel4_v6_ssse3() {};
void ff_put_vp8_epel8_h4_ssse3() {};
void ff_put_vp8_epel8_h6_ssse3() {};
void ff_put_vp8_epel8_v4_ssse3() {};
void ff_put_vp8_epel8_v6_ssse3() {};
void ff_put_vp8_bilinear4_h_mmxext() {};
void ff_put_vp8_bilinear8_h_sse2() {};
void ff_put_vp8_bilinear4_h_ssse3() {};
void ff_put_vp8_bilinear8_h_ssse3() {};
void ff_put_vp8_bilinear4_v_mmxext() {};
void ff_put_vp8_bilinear8_v_sse2() {};
void ff_put_vp8_bilinear4_v_ssse3() {};
void ff_put_vp8_bilinear8_v_ssse3() {};
void ff_put_vp8_pixels8_mmx() {};
void ff_put_vp8_pixels16_sse() {};
void ff_vp8_idct_dc_add_sse2() {};
void ff_vp8_idct_dc_add_sse4() {};
void ff_vp8_idct_dc_add4y_sse2() {};
void ff_vp8_idct_dc_add4uv_mmx() {};
void ff_vp8_luma_dc_wht_sse() {};
void ff_vp8_idct_add_sse() {};
void ff_vp8_v_loop_filter_simple_sse2() {};
void ff_vp8_h_loop_filter_simple_sse2() {};
void ff_vp8_v_loop_filter16y_inner_sse2() {};
void ff_vp8_h_loop_filter16y_inner_sse2() {};
void ff_vp8_v_loop_filter8uv_inner_sse2() {};
void ff_vp8_h_loop_filter8uv_inner_sse2() {};
void ff_vp8_v_loop_filter16y_mbedge_sse2() {};
void ff_vp8_h_loop_filter16y_mbedge_sse2() {};
void ff_vp8_v_loop_filter8uv_mbedge_sse2() {};
void ff_vp8_h_loop_filter8uv_mbedge_sse2() {};
void ff_vp8_v_loop_filter_simple_ssse3() {};
void ff_vp8_h_loop_filter_simple_ssse3() {};
void ff_vp8_v_loop_filter16y_inner_ssse3() {};
void ff_vp8_h_loop_filter16y_inner_ssse3() {};
void ff_vp8_v_loop_filter8uv_inner_ssse3() {};
void ff_vp8_h_loop_filter8uv_inner_ssse3() {};
void ff_vp8_v_loop_filter16y_mbedge_ssse3() {};
void ff_vp8_h_loop_filter16y_mbedge_ssse3() {};
void ff_vp8_v_loop_filter8uv_mbedge_ssse3() {};
void ff_vp8_h_loop_filter8uv_mbedge_ssse3() {};
void ff_vp8_h_loop_filter_simple_sse4() {};
void ff_vp8_h_loop_filter16y_mbedge_sse4() {};
void ff_vp8_h_loop_filter8uv_mbedge_sse4() {};
void ff_vp9_put4_mmx() {};
void ff_vp9_put8_mmx() {};
void ff_vp9_put16_sse() {};
void ff_vp9_put32_sse() {};
void ff_vp9_put64_sse() {};
void ff_vp9_avg4_8_mmxext() {};
void ff_vp9_avg8_8_mmxext() {};
void ff_vp9_avg16_8_sse2() {};
void ff_vp9_avg32_8_sse2() {};
void ff_vp9_avg64_8_sse2() {};
void ff_vp9_put32_avx() {};
void ff_vp9_put64_avx() {};
void ff_vp9_avg32_8_avx2() {};
void ff_vp9_avg64_8_avx2() {};
void ff_vp9_put_8tap_1d_h_4_8_mmxext() {};
void ff_vp9_avg_8tap_1d_h_4_8_mmxext() {};
void ff_vp9_put_8tap_1d_v_4_8_mmxext() {};
void ff_vp9_avg_8tap_1d_v_4_8_mmxext() {};
void ff_vp9_put_8tap_1d_h_8_8_sse2() {};
void ff_vp9_avg_8tap_1d_h_8_8_sse2() {};
void ff_vp9_put_8tap_1d_v_8_8_sse2() {};
void ff_vp9_avg_8tap_1d_v_8_8_sse2() {};
void ff_vp9_put_8tap_1d_h_4_8_ssse3() {};
void ff_vp9_avg_8tap_1d_h_4_8_ssse3() {};
void ff_vp9_put_8tap_1d_v_4_8_ssse3() {};
void ff_vp9_avg_8tap_1d_v_4_8_ssse3() {};
void ff_vp9_put_8tap_1d_h_8_8_ssse3() {};
void ff_vp9_avg_8tap_1d_h_8_8_ssse3() {};
void ff_vp9_put_8tap_1d_v_8_8_ssse3() {};
void ff_vp9_avg_8tap_1d_v_8_8_ssse3() {};
void ff_vp9_put_8tap_1d_h_16_8_ssse3() {};
void ff_vp9_avg_8tap_1d_h_16_8_ssse3() {};
void ff_vp9_put_8tap_1d_v_16_8_ssse3() {};
void ff_vp9_avg_8tap_1d_v_16_8_ssse3() {};
void ff_vp9_put_8tap_1d_h_32_8_avx2() {};
void ff_vp9_avg_8tap_1d_h_32_8_avx2() {};
void ff_vp9_put_8tap_1d_v_32_8_avx2() {};
void ff_vp9_avg_8tap_1d_v_32_8_avx2() {};
void ff_vp9_idct_idct_4x4_add_mmxext() {};
void ff_vp9_idct_iadst_4x4_add_sse2() {};
void ff_vp9_iadst_idct_4x4_add_sse2() {};
void ff_vp9_iadst_iadst_4x4_add_sse2() {};
void ff_vp9_idct_idct_4x4_add_ssse3() {};
void ff_vp9_iadst_idct_4x4_add_ssse3() {};
void ff_vp9_idct_iadst_4x4_add_ssse3() {};
void ff_vp9_iadst_iadst_4x4_add_ssse3() {};
void ff_vp9_idct_idct_8x8_add_sse2() {};
void ff_vp9_iadst_idct_8x8_add_sse2() {};
void ff_vp9_idct_iadst_8x8_add_sse2() {};
void ff_vp9_iadst_iadst_8x8_add_sse2() {};
void ff_vp9_idct_idct_8x8_add_ssse3() {};
void ff_vp9_iadst_idct_8x8_add_ssse3() {};
void ff_vp9_idct_iadst_8x8_add_ssse3() {};
void ff_vp9_iadst_iadst_8x8_add_ssse3() {};
void ff_vp9_idct_idct_8x8_add_avx() {};
void ff_vp9_iadst_idct_8x8_add_avx() {};
void ff_vp9_idct_iadst_8x8_add_avx() {};
void ff_vp9_iadst_iadst_8x8_add_avx() {};
void ff_vp9_idct_idct_16x16_add_sse2() {};
void ff_vp9_iadst_idct_16x16_add_sse2() {};
void ff_vp9_idct_iadst_16x16_add_sse2() {};
void ff_vp9_iadst_iadst_16x16_add_sse2() {};
void ff_vp9_idct_idct_16x16_add_ssse3() {};
void ff_vp9_iadst_idct_16x16_add_ssse3() {};
void ff_vp9_idct_iadst_16x16_add_ssse3() {};
void ff_vp9_iadst_iadst_16x16_add_ssse3() {};
void ff_vp9_idct_idct_16x16_add_avx() {};
void ff_vp9_iadst_idct_16x16_add_avx() {};
void ff_vp9_idct_iadst_16x16_add_avx() {};
void ff_vp9_iadst_iadst_16x16_add_avx() {};
void ff_vp9_idct_idct_32x32_add_sse2() {};
void ff_vp9_idct_idct_32x32_add_ssse3() {};
void ff_vp9_idct_idct_32x32_add_avx() {};
void ff_vp9_iwht_iwht_4x4_add_mmx() {};
void ff_vp9_idct_idct_16x16_add_avx2() {};
void ff_vp9_iadst_idct_16x16_add_avx2() {};
void ff_vp9_idct_iadst_16x16_add_avx2() {};
void ff_vp9_iadst_iadst_16x16_add_avx2() {};
void ff_vp9_idct_idct_32x32_add_avx2() {};
void ff_vp9_loop_filter_v_4_8_mmxext() {};
void ff_vp9_loop_filter_h_4_8_mmxext() {};
void ff_vp9_loop_filter_v_8_8_mmxext() {};
void ff_vp9_loop_filter_h_8_8_mmxext() {};
void ff_vp9_loop_filter_v_16_16_sse2() {};
void ff_vp9_loop_filter_h_16_16_sse2() {};
void ff_vp9_loop_filter_v_16_16_ssse3() {};
void ff_vp9_loop_filter_h_16_16_ssse3() {};
void ff_vp9_loop_filter_v_16_16_avx() {};
void ff_vp9_loop_filter_h_16_16_avx() {};
void ff_vp9_loop_filter_v_44_16_sse2() {};
void ff_vp9_loop_filter_h_44_16_sse2() {};
void ff_vp9_loop_filter_v_44_16_ssse3() {};
void ff_vp9_loop_filter_h_44_16_ssse3() {};
void ff_vp9_loop_filter_v_44_16_avx() {};
void ff_vp9_loop_filter_h_44_16_avx() {};
void ff_vp9_loop_filter_v_84_16_sse2() {};
void ff_vp9_loop_filter_h_84_16_sse2() {};
void ff_vp9_loop_filter_v_84_16_ssse3() {};
void ff_vp9_loop_filter_h_84_16_ssse3() {};
void ff_vp9_loop_filter_v_84_16_avx() {};
void ff_vp9_loop_filter_h_84_16_avx() {};
void ff_vp9_loop_filter_v_48_16_sse2() {};
void ff_vp9_loop_filter_h_48_16_sse2() {};
void ff_vp9_loop_filter_v_48_16_ssse3() {};
void ff_vp9_loop_filter_h_48_16_ssse3() {};
void ff_vp9_loop_filter_v_48_16_avx() {};
void ff_vp9_loop_filter_h_48_16_avx() {};
void ff_vp9_loop_filter_v_88_16_sse2() {};
void ff_vp9_loop_filter_h_88_16_sse2() {};
void ff_vp9_loop_filter_v_88_16_ssse3() {};
void ff_vp9_loop_filter_h_88_16_ssse3() {};
void ff_vp9_loop_filter_v_88_16_avx() {};
void ff_vp9_loop_filter_h_88_16_avx() {};
void ff_vp9_ipred_v_8x8_mmx() {};
void ff_vp9_ipred_dc_4x4_mmxext() {};
void ff_vp9_ipred_dc_left_4x4_mmxext() {};
void ff_vp9_ipred_dc_top_4x4_mmxext() {};
void ff_vp9_ipred_dc_8x8_mmxext() {};
void ff_vp9_ipred_dc_left_8x8_mmxext() {};
void ff_vp9_ipred_dc_top_8x8_mmxext() {};
void ff_vp9_ipred_tm_4x4_mmxext() {};
void ff_vp9_ipred_dl_4x4_mmxext() {};
void ff_vp9_ipred_dr_4x4_mmxext() {};
void ff_vp9_ipred_hd_4x4_mmxext() {};
void ff_vp9_ipred_hu_4x4_mmxext() {};
void ff_vp9_ipred_vl_4x4_mmxext() {};
void ff_vp9_ipred_vr_4x4_mmxext() {};
void ff_vp9_ipred_v_16x16_sse() {};
void ff_vp9_ipred_v_32x32_sse() {};
void ff_vp9_ipred_dc_16x16_sse2() {};
void ff_vp9_ipred_dc_left_16x16_sse2() {};
void ff_vp9_ipred_dc_top_16x16_sse2() {};
void ff_vp9_ipred_dc_32x32_sse2() {};
void ff_vp9_ipred_dc_left_32x32_sse2() {};
void ff_vp9_ipred_dc_top_32x32_sse2() {};
void ff_vp9_ipred_tm_8x8_sse2() {};
void ff_vp9_ipred_dl_8x8_sse2() {};
void ff_vp9_ipred_dr_8x8_sse2() {};
void ff_vp9_ipred_hd_8x8_sse2() {};
void ff_vp9_ipred_hu_8x8_sse2() {};
void ff_vp9_ipred_vl_8x8_sse2() {};
void ff_vp9_ipred_vr_8x8_sse2() {};
void ff_vp9_ipred_h_8x8_sse2() {};
void ff_vp9_ipred_tm_16x16_sse2() {};
void ff_vp9_ipred_dl_16x16_sse2() {};
void ff_vp9_ipred_dr_16x16_sse2() {};
void ff_vp9_ipred_hd_16x16_sse2() {};
void ff_vp9_ipred_hu_16x16_sse2() {};
void ff_vp9_ipred_vl_16x16_sse2() {};
void ff_vp9_ipred_vr_16x16_sse2() {};
void ff_vp9_ipred_h_16x16_sse2() {};
void ff_vp9_ipred_tm_32x32_sse2() {};
void ff_vp9_ipred_dl_32x32_sse2() {};
void ff_vp9_ipred_dr_32x32_sse2() {};
void ff_vp9_ipred_hd_32x32_sse2() {};
void ff_vp9_ipred_hu_32x32_sse2() {};
void ff_vp9_ipred_vl_32x32_sse2() {};
void ff_vp9_ipred_vr_32x32_sse2() {};
void ff_vp9_ipred_h_32x32_sse2() {};
void ff_vp9_ipred_h_4x4_sse2() {};
void ff_vp9_ipred_dc_4x4_ssse3() {};
void ff_vp9_ipred_dc_left_4x4_ssse3() {};
void ff_vp9_ipred_dc_top_4x4_ssse3() {};
void ff_vp9_ipred_tm_4x4_ssse3() {};
void ff_vp9_ipred_dl_4x4_ssse3() {};
void ff_vp9_ipred_dr_4x4_ssse3() {};
void ff_vp9_ipred_hu_4x4_ssse3() {};
void ff_vp9_ipred_vr_4x4_ssse3() {};
void ff_vp9_ipred_h_4x4_ssse3() {};
void ff_vp9_ipred_dc_8x8_ssse3() {};
void ff_vp9_ipred_dc_left_8x8_ssse3() {};
void ff_vp9_ipred_dc_top_8x8_ssse3() {};
void ff_vp9_ipred_tm_8x8_ssse3() {};
void ff_vp9_ipred_dl_8x8_ssse3() {};
void ff_vp9_ipred_dr_8x8_ssse3() {};
void ff_vp9_ipred_hd_8x8_ssse3() {};
void ff_vp9_ipred_hu_8x8_ssse3() {};
void ff_vp9_ipred_vl_8x8_ssse3() {};
void ff_vp9_ipred_vr_8x8_ssse3() {};
void ff_vp9_ipred_h_8x8_ssse3() {};
void ff_vp9_ipred_dc_16x16_ssse3() {};
void ff_vp9_ipred_dc_left_16x16_ssse3() {};
void ff_vp9_ipred_dc_top_16x16_ssse3() {};
void ff_vp9_ipred_tm_16x16_ssse3() {};
void ff_vp9_ipred_dl_16x16_ssse3() {};
void ff_vp9_ipred_dr_16x16_ssse3() {};
void ff_vp9_ipred_hd_16x16_ssse3() {};
void ff_vp9_ipred_hu_16x16_ssse3() {};
void ff_vp9_ipred_vl_16x16_ssse3() {};
void ff_vp9_ipred_vr_16x16_ssse3() {};
void ff_vp9_ipred_h_16x16_ssse3() {};
void ff_vp9_ipred_dc_32x32_ssse3() {};
void ff_vp9_ipred_dc_left_32x32_ssse3() {};
void ff_vp9_ipred_dc_top_32x32_ssse3() {};
void ff_vp9_ipred_tm_32x32_ssse3() {};
void ff_vp9_ipred_dl_32x32_ssse3() {};
void ff_vp9_ipred_dr_32x32_ssse3() {};
void ff_vp9_ipred_hd_32x32_ssse3() {};
void ff_vp9_ipred_hu_32x32_ssse3() {};
void ff_vp9_ipred_vl_32x32_ssse3() {};
void ff_vp9_ipred_vr_32x32_ssse3() {};
void ff_vp9_ipred_h_32x32_ssse3() {};
void ff_vp9_ipred_tm_8x8_avx() {};
void ff_vp9_ipred_dl_8x8_avx() {};
void ff_vp9_ipred_dr_8x8_avx() {};
void ff_vp9_ipred_hd_8x8_avx() {};
void ff_vp9_ipred_hu_8x8_avx() {};
void ff_vp9_ipred_vl_8x8_avx() {};
void ff_vp9_ipred_vr_8x8_avx() {};
void ff_vp9_ipred_h_8x8_avx() {};
void ff_vp9_ipred_tm_16x16_avx() {};
void ff_vp9_ipred_dl_16x16_avx() {};
void ff_vp9_ipred_dr_16x16_avx() {};
void ff_vp9_ipred_hd_16x16_avx() {};
void ff_vp9_ipred_hu_16x16_avx() {};
void ff_vp9_ipred_vl_16x16_avx() {};
void ff_vp9_ipred_vr_16x16_avx() {};
void ff_vp9_ipred_h_16x16_avx() {};
void ff_vp9_ipred_tm_32x32_avx() {};
void ff_vp9_ipred_dl_32x32_avx() {};
void ff_vp9_ipred_dr_32x32_avx() {};
void ff_vp9_ipred_hd_32x32_avx() {};
void ff_vp9_ipred_hu_32x32_avx() {};
void ff_vp9_ipred_vl_32x32_avx() {};
void ff_vp9_ipred_vr_32x32_avx() {};
void ff_vp9_ipred_h_32x32_avx() {};
void ff_vp9_ipred_v_32x32_avx() {};
void ff_vp9_ipred_dc_32x32_avx2() {};
void ff_vp9_ipred_dc_left_32x32_avx2() {};
void ff_vp9_ipred_dc_top_32x32_avx2() {};
void ff_vp9_ipred_h_32x32_avx2() {};
void ff_vp9_ipred_tm_32x32_avx2() {};
void ff_filters_ssse3() {};
void ff_filters_sse2() {};
void ff_vector_fmul_sse() {};
void ff_vector_fmul_avx() {};
void ff_vector_dmul_sse2() {};
void ff_vector_dmul_avx() {};
void ff_vector_fmac_scalar_sse() {};
void ff_vector_fmac_scalar_avx() {};
void ff_vector_fmac_scalar_fma3() {};
void ff_vector_fmul_scalar_sse() {};
void ff_vector_dmac_scalar_sse2() {};
void ff_vector_dmac_scalar_avx() {};
void ff_vector_dmac_scalar_fma3() {};
void ff_vector_dmul_scalar_sse2() {};
void ff_vector_dmul_scalar_avx() {};
void ff_vector_fmul_window_3dnowext() {};
void ff_vector_fmul_window_sse() {};
void ff_vector_fmul_add_sse() {};
void ff_vector_fmul_add_avx() {};
void ff_vector_fmul_add_fma3() {};
void ff_vector_fmul_reverse_sse() {};
void ff_vector_fmul_reverse_avx() {};
void ff_vector_fmul_reverse_avx2() {};
void ff_scalarproduct_float_sse() {};
void ff_butterflies_float_sse() {};
void ff_mdct15_postreindex_sse3() {};
void ff_butterflies_fixed_sse2() {};
void ff_ac3_exponent_min_mmx() {};
void ff_ac3_exponent_min_mmxext() {};
void ff_ac3_exponent_min_sse2() {};
void ff_float_to_fixed24_3dnow() {};
void ff_float_to_fixed24_sse() {};
void ff_float_to_fixed24_sse2() {};
void ff_ac3_compute_mantissa_size_sse2() {};
void ff_ac3_extract_exponents_sse2() {};
void ff_ac3_extract_exponents_ssse3() {};
void ff_ac3_downmix_3_to_1_sse() {};
void ff_ac3_downmix_3_to_2_sse() {};
void ff_ac3_downmix_4_to_1_sse() {};
void ff_ac3_downmix_4_to_2_sse() {};
void ff_ac3_downmix_5_to_1_sse() {};
void ff_ac3_downmix_5_to_2_sse() {};
void ff_ac3_downmix_6_to_1_sse() {};
void ff_ac3_downmix_6_to_2_sse() {};
void ff_ac3_downmix_3_to_1_avx() {};
void ff_ac3_downmix_3_to_2_avx() {};
void ff_ac3_downmix_4_to_1_avx() {};
void ff_ac3_downmix_4_to_2_avx() {};
void ff_ac3_downmix_5_to_1_avx() {};
void ff_ac3_downmix_5_to_2_avx() {};
void ff_ac3_downmix_6_to_1_avx() {};
void ff_ac3_downmix_6_to_2_avx() {};
void ff_ac3_downmix_3_to_1_fma3() {};
void ff_ac3_downmix_3_to_2_fma3() {};
void ff_ac3_downmix_4_to_1_fma3() {};
void ff_ac3_downmix_4_to_2_fma3() {};
void ff_ac3_downmix_5_to_1_fma3() {};
void ff_ac3_downmix_5_to_2_fma3() {};
void ff_ac3_downmix_6_to_1_fma3() {};
void ff_ac3_downmix_6_to_2_fma3() {};
void ff_int32_to_float_fmul_scalar_sse() {};
void ff_int32_to_float_fmul_scalar_sse2() {};
void ff_int32_to_float_fmul_array8_sse() {};
void ff_int32_to_float_fmul_array8_sse2() {};
void ff_alac_decorrelate_stereo_sse4() {};
void ff_alac_append_extra_bits_stereo_sse2() {};
void ff_alac_append_extra_bits_mono_sse2() {};
void ff_scalarproduct_and_madd_int16_mmxext() {};
void ff_scalarproduct_and_madd_int16_sse2() {};
void ff_scalarproduct_and_madd_int16_ssse3() {};
void ff_scalarproduct_and_madd_int32_sse4() {};
void ff_dct32_float_sse2() {};
void ff_dct32_float_avx() {};
void ff_scalarproduct_int16_mmxext() {};
void ff_scalarproduct_int16_sse2() {};
void ff_vector_clip_int32_mmx() {};
void ff_vector_clip_int32_sse2() {};
void ff_vector_clip_int32_int_sse2() {};
void ff_vector_clip_int32_sse4() {};
void ff_vector_clipf_sse() {};
void ff_lfe_fir0_float_sse() {};
void ff_lfe_fir0_float_sse2() {};
void ff_lfe_fir1_float_sse3() {};
void ff_lfe_fir0_float_avx() {};
void ff_lfe_fir1_float_avx() {};
void ff_lfe_fir0_float_fma3() {};
void ff_flac_lpc_32_sse4() {};
void ff_flac_lpc_32_xop() {};
void ff_flac_decorrelate_ls_16_sse2() {};
void ff_flac_decorrelate_rs_16_sse2() {};
void ff_flac_decorrelate_ms_16_sse2() {};
void ff_flac_decorrelate_indep2_16_sse2() {};
void ff_flac_decorrelate_indep4_16_sse2() {};
void ff_flac_decorrelate_indep6_16_sse2() {};
void ff_flac_decorrelate_ls_32_sse2() {};
void ff_flac_decorrelate_rs_32_sse2() {};
void ff_flac_decorrelate_ms_32_sse2() {};
void ff_flac_decorrelate_indep2_32_sse2() {};
void ff_flac_decorrelate_indep4_32_sse2() {};
void ff_flac_decorrelate_indep6_32_sse2() {};
void ff_flac_decorrelate_indep4_32_avx() {};
void ff_flac_decorrelate_indep6_32_avx() {};
void ff_fft_permute_sse() {};
void ff_fft_calc_avx() {};
void ff_fft_calc_sse() {};
void ff_imdct_calc_sse() {};
void ff_imdct_half_sse() {};
void ff_imdct_half_avx() {};
void ff_imdct36_float_sse2() {};
void ff_imdct36_float_sse3() {};
void ff_imdct36_float_ssse3() {};
void ff_imdct36_float_avx() {};
void ff_four_imdct36_float_sse() {};
void ff_four_imdct36_float_avx() {};
void ff_tak_decorrelate_ls_sse2() {};
void ff_tak_decorrelate_sr_sse2() {};
void ff_tak_decorrelate_sm_sse2() {};
void ff_tak_decorrelate_sf_sse4() {};
void ff_tta_filter_process_ssse3() {};
void ff_tta_filter_process_sse4() {};
void ff_vorbis_inverse_coupling_sse() {};
void ff_g722_apply_qmf_sse2() {};
void ff_resample_common_int16_mmxext() {};
void ff_resample_linear_int16_mmxext() {};
void ff_resample_common_int16_sse2() {};
void ff_resample_linear_int16_sse2() {};
void ff_resample_common_int16_xop() {};
void ff_resample_linear_int16_xop() {};
void ff_resample_common_float_sse() {};
void ff_resample_linear_float_sse() {};
void ff_resample_common_float_avx() {};
void ff_resample_linear_float_avx() {};
void ff_resample_common_float_fma3() {};
void ff_resample_linear_float_fma3() {};
void ff_resample_common_float_fma4() {};
void ff_resample_linear_float_fma4() {};
void ff_resample_common_double_sse2() {};
void ff_resample_linear_double_sse2() {};
void ff_resample_common_double_avx() {};
void ff_resample_linear_double_avx() {};
void ff_resample_common_double_fma3() {};
void ff_resample_linear_double_fma3() {};
void ff_put_no_rnd_pixels8_x2_exact_mmxext() {};
void ff_put_no_rnd_pixels8_x2_exact_3dnow() {};
void ff_put_no_rnd_pixels8_y2_exact_mmxext() {};
void ff_put_no_rnd_pixels8_y2_exact_3dnow() {};
void ff_cavs_idct8_mmx() {};
void ff_cavs_idct8_sse2() {};
void ff_sum_abs_dctelem_mmx() {};
void ff_sum_abs_dctelem_mmxext() {};
void ff_sum_abs_dctelem_sse2() {};
void ff_sum_abs_dctelem_ssse3() {};
void ff_sse8_mmx() {};
void ff_sse16_mmx() {};
void ff_sse16_sse2() {};
void ff_hf_noise8_mmx() {};
void ff_hf_noise16_mmx() {};
void ff_sad8_mmxext() {};
void ff_sad16_mmxext() {};
void ff_sad16_sse2() {};
void ff_sad8_x2_mmxext() {};
void ff_sad16_x2_mmxext() {};
void ff_sad16_x2_sse2() {};
void ff_sad8_y2_mmxext() {};
void ff_sad16_y2_mmxext() {};
void ff_sad16_y2_sse2() {};
void ff_sad8_approx_xy2_mmxext() {};
void ff_sad16_approx_xy2_mmxext() {};
void ff_sad16_approx_xy2_sse2() {};
void ff_vsad_intra8_mmxext() {};
void ff_vsad_intra16_mmxext() {};
void ff_vsad_intra16_sse2() {};
void ff_vsad8_approx_mmxext() {};
void ff_vsad16_approx_mmxext() {};
void ff_vsad16_approx_sse2() {};
void ff_hadamard8_diff_mmx() {};
void ff_hadamard8_diff16_mmx() {};
void ff_hadamard8_diff_mmxext() {};
void ff_hadamard8_diff16_mmxext() {};
void ff_hadamard8_diff_sse2() {};
void ff_hadamard8_diff16_sse2() {};
void ff_hadamard8_diff_ssse3() {};
void ff_hadamard8_diff16_ssse3() {};
void ff_avg_pixels4_mmxext() {};
void ff_put_pixels4_mmx() {};
void ff_put_pixels4_l2_mmxext() {};
void ff_avg_pixels4_l2_mmxext() {};
void ff_avg_h264_qpel4_h_lowpass_mmxext() {};
void ff_avg_h264_qpel8_h_lowpass_mmxext() {};
void ff_avg_h264_qpel8_h_lowpass_ssse3() {};
void ff_avg_h264_qpel4_h_lowpass_l2_mmxext() {};
void ff_avg_h264_qpel8_h_lowpass_l2_mmxext() {};
void ff_avg_h264_qpel8_h_lowpass_l2_ssse3() {};
void ff_avg_h264_qpel4_v_lowpass_mmxext() {};
void ff_avg_h264_qpel8or16_v_lowpass_op_mmxext() {};
void ff_avg_h264_qpel8or16_v_lowpass_sse2() {};
void ff_avg_h264_qpel4_hv_lowpass_v_mmxext() {};
void ff_avg_h264_qpel4_hv_lowpass_h_mmxext() {};
void ff_avg_h264_qpel8or16_hv2_lowpass_op_mmxext() {};
void ff_avg_h264_qpel8or16_hv2_lowpass_ssse3() {};
void ff_avg_pixels4_l2_shift5_mmxext() {};
void ff_avg_pixels8_l2_shift5_mmxext() {};
void ff_put_h264_qpel4_h_lowpass_mmxext() {};
void ff_put_h264_qpel8_h_lowpass_mmxext() {};
void ff_put_h264_qpel8_h_lowpass_ssse3() {};
void ff_put_h264_qpel4_h_lowpass_l2_mmxext() {};
void ff_put_h264_qpel8_h_lowpass_l2_mmxext() {};
void ff_put_h264_qpel8_h_lowpass_l2_ssse3() {};
void ff_put_h264_qpel4_v_lowpass_mmxext() {};
void ff_put_h264_qpel8or16_v_lowpass_op_mmxext() {};
void ff_put_h264_qpel8or16_v_lowpass_sse2() {};
void ff_put_h264_qpel4_hv_lowpass_v_mmxext() {};
void ff_put_h264_qpel4_hv_lowpass_h_mmxext() {};
void ff_put_h264_qpel8or16_hv1_lowpass_op_mmxext() {};
void ff_put_h264_qpel8or16_hv1_lowpass_op_sse2() {};
void ff_put_h264_qpel8or16_hv2_lowpass_op_mmxext() {};
void ff_put_h264_qpel8or16_hv2_lowpass_ssse3() {};
void ff_put_pixels4_l2_shift5_mmxext() {};
void ff_put_pixels8_l2_shift5_mmxext() {};
void ff_avg_h264_qpel16_h_lowpass_l2_ssse3() {};
void ff_put_h264_qpel16_h_lowpass_l2_ssse3() {};
void ff_put_h264_qpel4_mc00_10_mmxext() {};
void ff_avg_h264_qpel4_mc00_10_mmxext() {};
void ff_put_h264_qpel4_mc10_10_mmxext() {};
void ff_avg_h264_qpel4_mc10_10_mmxext() {};
void ff_put_h264_qpel4_mc20_10_mmxext() {};
void ff_avg_h264_qpel4_mc20_10_mmxext() {};
void ff_put_h264_qpel4_mc30_10_mmxext() {};
void ff_avg_h264_qpel4_mc30_10_mmxext() {};
void ff_put_h264_qpel4_mc01_10_mmxext() {};
void ff_avg_h264_qpel4_mc01_10_mmxext() {};
void ff_put_h264_qpel4_mc11_10_mmxext() {};
void ff_avg_h264_qpel4_mc11_10_mmxext() {};
void ff_put_h264_qpel4_mc21_10_mmxext() {};
void ff_avg_h264_qpel4_mc21_10_mmxext() {};
void ff_put_h264_qpel4_mc31_10_mmxext() {};
void ff_avg_h264_qpel4_mc31_10_mmxext() {};
void ff_put_h264_qpel4_mc02_10_mmxext() {};
void ff_avg_h264_qpel4_mc02_10_mmxext() {};
void ff_put_h264_qpel4_mc12_10_mmxext() {};
void ff_avg_h264_qpel4_mc12_10_mmxext() {};
void ff_put_h264_qpel4_mc22_10_mmxext() {};
void ff_avg_h264_qpel4_mc22_10_mmxext() {};
void ff_put_h264_qpel4_mc32_10_mmxext() {};
void ff_avg_h264_qpel4_mc32_10_mmxext() {};
void ff_put_h264_qpel4_mc03_10_mmxext() {};
void ff_avg_h264_qpel4_mc03_10_mmxext() {};
void ff_put_h264_qpel4_mc13_10_mmxext() {};
void ff_avg_h264_qpel4_mc13_10_mmxext() {};
void ff_put_h264_qpel4_mc23_10_mmxext() {};
void ff_avg_h264_qpel4_mc23_10_mmxext() {};
void ff_put_h264_qpel4_mc33_10_mmxext() {};
void ff_avg_h264_qpel4_mc33_10_mmxext() {};
void ff_put_h264_qpel8_mc00_10_sse2() {};
void ff_avg_h264_qpel8_mc00_10_sse2() {};
void ff_put_h264_qpel16_mc00_10_sse2() {};
void ff_avg_h264_qpel16_mc00_10_sse2() {};
void ff_put_h264_qpel8_mc10_10_sse2() {};
void ff_avg_h264_qpel8_mc10_10_sse2() {};
void ff_put_h264_qpel16_mc10_10_sse2() {};
void ff_avg_h264_qpel16_mc10_10_sse2() {};
void ff_put_h264_qpel8_mc10_10_sse2_cache64() {};
void ff_avg_h264_qpel8_mc10_10_sse2_cache64() {};
void ff_put_h264_qpel16_mc10_10_sse2_cache64() {};
void ff_avg_h264_qpel16_mc10_10_sse2_cache64() {};
void ff_put_h264_qpel8_mc10_10_ssse3_cache64() {};
void ff_avg_h264_qpel8_mc10_10_ssse3_cache64() {};
void ff_put_h264_qpel16_mc10_10_ssse3_cache64() {};
void ff_avg_h264_qpel16_mc10_10_ssse3_cache64() {};
void ff_put_h264_qpel8_mc20_10_sse2() {};
void ff_avg_h264_qpel8_mc20_10_sse2() {};
void ff_put_h264_qpel16_mc20_10_sse2() {};
void ff_avg_h264_qpel16_mc20_10_sse2() {};
void ff_put_h264_qpel8_mc20_10_sse2_cache64() {};
void ff_avg_h264_qpel8_mc20_10_sse2_cache64() {};
void ff_put_h264_qpel16_mc20_10_sse2_cache64() {};
void ff_avg_h264_qpel16_mc20_10_sse2_cache64() {};
void ff_put_h264_qpel8_mc20_10_ssse3_cache64() {};
void ff_avg_h264_qpel8_mc20_10_ssse3_cache64() {};
void ff_put_h264_qpel16_mc20_10_ssse3_cache64() {};
void ff_avg_h264_qpel16_mc20_10_ssse3_cache64() {};
void ff_put_h264_qpel8_mc30_10_sse2() {};
void ff_avg_h264_qpel8_mc30_10_sse2() {};
void ff_put_h264_qpel16_mc30_10_sse2() {};
void ff_avg_h264_qpel16_mc30_10_sse2() {};
void ff_put_h264_qpel8_mc30_10_sse2_cache64() {};
void ff_avg_h264_qpel8_mc30_10_sse2_cache64() {};
void ff_put_h264_qpel16_mc30_10_sse2_cache64() {};
void ff_avg_h264_qpel16_mc30_10_sse2_cache64() {};
void ff_put_h264_qpel8_mc30_10_ssse3_cache64() {};
void ff_avg_h264_qpel8_mc30_10_ssse3_cache64() {};
void ff_put_h264_qpel16_mc30_10_ssse3_cache64() {};
void ff_avg_h264_qpel16_mc30_10_ssse3_cache64() {};
void ff_put_h264_qpel8_mc01_10_sse2() {};
void ff_avg_h264_qpel8_mc01_10_sse2() {};
void ff_put_h264_qpel16_mc01_10_sse2() {};
void ff_avg_h264_qpel16_mc01_10_sse2() {};
void ff_put_h264_qpel8_mc11_10_sse2() {};
void ff_avg_h264_qpel8_mc11_10_sse2() {};
void ff_put_h264_qpel16_mc11_10_sse2() {};
void ff_avg_h264_qpel16_mc11_10_sse2() {};
void ff_put_h264_qpel8_mc21_10_sse2() {};
void ff_avg_h264_qpel8_mc21_10_sse2() {};
void ff_put_h264_qpel16_mc21_10_sse2() {};
void ff_avg_h264_qpel16_mc21_10_sse2() {};
void ff_put_h264_qpel8_mc31_10_sse2() {};
void ff_avg_h264_qpel8_mc31_10_sse2() {};
void ff_put_h264_qpel16_mc31_10_sse2() {};
void ff_avg_h264_qpel16_mc31_10_sse2() {};
void ff_put_h264_qpel8_mc02_10_sse2() {};
void ff_avg_h264_qpel8_mc02_10_sse2() {};
void ff_put_h264_qpel16_mc02_10_sse2() {};
void ff_avg_h264_qpel16_mc02_10_sse2() {};
void ff_put_h264_qpel8_mc12_10_sse2() {};
void ff_avg_h264_qpel8_mc12_10_sse2() {};
void ff_put_h264_qpel16_mc12_10_sse2() {};
void ff_avg_h264_qpel16_mc12_10_sse2() {};
void ff_put_h264_qpel8_mc22_10_sse2() {};
void ff_avg_h264_qpel8_mc22_10_sse2() {};
void ff_put_h264_qpel16_mc22_10_sse2() {};
void ff_avg_h264_qpel16_mc22_10_sse2() {};
void ff_put_h264_qpel8_mc32_10_sse2() {};
void ff_avg_h264_qpel8_mc32_10_sse2() {};
void ff_put_h264_qpel16_mc32_10_sse2() {};
void ff_avg_h264_qpel16_mc32_10_sse2() {};
void ff_put_h264_qpel8_mc03_10_sse2() {};
void ff_avg_h264_qpel8_mc03_10_sse2() {};
void ff_put_h264_qpel16_mc03_10_sse2() {};
void ff_avg_h264_qpel16_mc03_10_sse2() {};
void ff_put_h264_qpel8_mc13_10_sse2() {};
void ff_avg_h264_qpel8_mc13_10_sse2() {};
void ff_put_h264_qpel16_mc13_10_sse2() {};
void ff_avg_h264_qpel16_mc13_10_sse2() {};
void ff_put_h264_qpel8_mc23_10_sse2() {};
void ff_avg_h264_qpel8_mc23_10_sse2() {};
void ff_put_h264_qpel16_mc23_10_sse2() {};
void ff_avg_h264_qpel16_mc23_10_sse2() {};
void ff_put_h264_qpel8_mc33_10_sse2() {};
void ff_avg_h264_qpel8_mc33_10_sse2() {};
void ff_put_h264_qpel16_mc33_10_sse2() {};
void ff_avg_h264_qpel16_mc33_10_sse2() {};
void ff_put_rv40_chroma_mc8_mmx() {};
void ff_avg_rv40_chroma_mc8_mmxext() {};
void ff_avg_rv40_chroma_mc8_3dnow() {};
void ff_put_rv40_chroma_mc4_mmx() {};
void ff_avg_rv40_chroma_mc4_mmxext() {};
void ff_avg_rv40_chroma_mc4_3dnow() {};
void ff_rv40_weight_func_rnd_16_mmxext() {};
void ff_rv40_weight_func_rnd_8_mmxext() {};
void ff_rv40_weight_func_nornd_16_mmxext() {};
void ff_rv40_weight_func_nornd_8_mmxext() {};
void ff_rv40_weight_func_rnd_16_sse2() {};
void ff_rv40_weight_func_rnd_8_sse2() {};
void ff_rv40_weight_func_nornd_16_sse2() {};
void ff_rv40_weight_func_nornd_8_sse2() {};
void ff_rv40_weight_func_rnd_16_ssse3() {};
void ff_rv40_weight_func_rnd_8_ssse3() {};
void ff_rv40_weight_func_nornd_16_ssse3() {};
void ff_rv40_weight_func_nornd_8_ssse3() {};
void ff_put_rv40_qpel_h_ssse3() {};
void ff_put_rv40_qpel_v_ssse3() {};
void ff_avg_rv40_qpel_h_ssse3() {};
void ff_avg_rv40_qpel_v_ssse3() {};
void ff_put_rv40_qpel_h_sse2() {};
void ff_put_rv40_qpel_v_sse2() {};
void ff_avg_rv40_qpel_h_sse2() {};
void ff_avg_rv40_qpel_v_sse2() {};
void ff_vp9_put_8tap_1d_h_4_10_sse2() {};
void ff_vp9_avg_8tap_1d_h_4_10_sse2() {};
void ff_vp9_put_8tap_1d_v_4_10_sse2() {};
void ff_vp9_avg_8tap_1d_v_4_10_sse2() {};
void ff_vp9_put_8tap_1d_h_8_10_sse2() {};
void ff_vp9_avg_8tap_1d_h_8_10_sse2() {};
void ff_vp9_put_8tap_1d_v_8_10_sse2() {};
void ff_vp9_avg_8tap_1d_v_8_10_sse2() {};
void ff_vp9_put_8tap_1d_h_16_10_avx2() {};
void ff_vp9_avg_8tap_1d_h_16_10_avx2() {};
void ff_vp9_put_8tap_1d_v_16_10_avx2() {};
void ff_vp9_avg_8tap_1d_v_16_10_avx2() {};
void ff_vp9_loop_filter_h_4_10_sse2() {};
void ff_vp9_loop_filter_h_4_10_ssse3() {};
void ff_vp9_loop_filter_h_4_10_avx() {};
void ff_vp9_loop_filter_h_8_10_sse2() {};
void ff_vp9_loop_filter_h_8_10_ssse3() {};
void ff_vp9_loop_filter_h_8_10_avx() {};
void ff_vp9_loop_filter_h_16_10_sse2() {};
void ff_vp9_loop_filter_h_16_10_ssse3() {};
void ff_vp9_loop_filter_h_16_10_avx() {};
void ff_vp9_loop_filter_v_4_10_sse2() {};
void ff_vp9_loop_filter_v_4_10_ssse3() {};
void ff_vp9_loop_filter_v_4_10_avx() {};
void ff_vp9_loop_filter_v_8_10_sse2() {};
void ff_vp9_loop_filter_v_8_10_ssse3() {};
void ff_vp9_loop_filter_v_8_10_avx() {};
void ff_vp9_loop_filter_v_16_10_sse2() {};
void ff_vp9_loop_filter_v_16_10_ssse3() {};
void ff_vp9_loop_filter_v_16_10_avx() {};
void ff_vp9_ipred_tm_4x4_10_mmxext() {};
void ff_vp9_ipred_tm_8x8_10_sse2() {};
void ff_vp9_ipred_tm_16x16_10_sse2() {};
void ff_vp9_ipred_tm_32x32_10_sse2() {};
void ff_vp9_iwht_iwht_4x4_add_10_mmxext() {};
void ff_vp9_idct_idct_4x4_add_10_mmxext() {};
void ff_vp9_idct_idct_4x4_add_10_ssse3() {};
void ff_vp9_iadst_idct_4x4_add_10_ssse3() {};
void ff_vp9_idct_iadst_4x4_add_10_ssse3() {};
void ff_vp9_iadst_iadst_4x4_add_10_ssse3() {};
void ff_vp9_idct_iadst_4x4_add_10_sse2() {};
void ff_vp9_iadst_idct_4x4_add_10_sse2() {};
void ff_vp9_iadst_iadst_4x4_add_10_sse2() {};
void ff_vp9_idct_idct_8x8_add_10_sse2() {};
void ff_vp9_iadst_idct_8x8_add_10_sse2() {};
void ff_vp9_idct_iadst_8x8_add_10_sse2() {};
void ff_vp9_iadst_iadst_8x8_add_10_sse2() {};
void ff_vp9_idct_idct_16x16_add_10_sse2() {};
void ff_vp9_iadst_idct_16x16_add_10_sse2() {};
void ff_vp9_idct_iadst_16x16_add_10_sse2() {};
void ff_vp9_iadst_iadst_16x16_add_10_sse2() {};
void ff_vp9_idct_idct_32x32_add_10_sse2() {};
void ff_filters_16bpp() {};
void ff_vp9_put_8tap_1d_h_4_12_sse2() {};
void ff_vp9_avg_8tap_1d_h_4_12_sse2() {};
void ff_vp9_put_8tap_1d_v_4_12_sse2() {};
void ff_vp9_avg_8tap_1d_v_4_12_sse2() {};
void ff_vp9_put_8tap_1d_h_8_12_sse2() {};
void ff_vp9_avg_8tap_1d_h_8_12_sse2() {};
void ff_vp9_put_8tap_1d_v_8_12_sse2() {};
void ff_vp9_avg_8tap_1d_v_8_12_sse2() {};
void ff_vp9_put_8tap_1d_h_16_12_avx2() {};
void ff_vp9_avg_8tap_1d_h_16_12_avx2() {};
void ff_vp9_put_8tap_1d_v_16_12_avx2() {};
void ff_vp9_avg_8tap_1d_v_16_12_avx2() {};
void ff_vp9_loop_filter_h_4_12_sse2() {};
void ff_vp9_loop_filter_h_4_12_ssse3() {};
void ff_vp9_loop_filter_h_4_12_avx() {};
void ff_vp9_loop_filter_h_8_12_sse2() {};
void ff_vp9_loop_filter_h_8_12_ssse3() {};
void ff_vp9_loop_filter_h_8_12_avx() {};
void ff_vp9_loop_filter_h_16_12_sse2() {};
void ff_vp9_loop_filter_h_16_12_ssse3() {};
void ff_vp9_loop_filter_h_16_12_avx() {};
void ff_vp9_loop_filter_v_4_12_sse2() {};
void ff_vp9_loop_filter_v_4_12_ssse3() {};
void ff_vp9_loop_filter_v_4_12_avx() {};
void ff_vp9_loop_filter_v_8_12_sse2() {};
void ff_vp9_loop_filter_v_8_12_ssse3() {};
void ff_vp9_loop_filter_v_8_12_avx() {};
void ff_vp9_loop_filter_v_16_12_sse2() {};
void ff_vp9_loop_filter_v_16_12_ssse3() {};
void ff_vp9_loop_filter_v_16_12_avx() {};
void ff_vp9_ipred_tm_4x4_12_mmxext() {};
void ff_vp9_ipred_tm_8x8_12_sse2() {};
void ff_vp9_ipred_tm_16x16_12_sse2() {};
void ff_vp9_ipred_tm_32x32_12_sse2() {};
void ff_vp9_iwht_iwht_4x4_add_12_mmxext() {};
void ff_vp9_idct_idct_4x4_add_12_sse2() {};
void ff_vp9_idct_iadst_4x4_add_12_sse2() {};
void ff_vp9_iadst_idct_4x4_add_12_sse2() {};
void ff_vp9_iadst_iadst_4x4_add_12_sse2() {};
void ff_vp9_idct_idct_8x8_add_12_sse2() {};
void ff_vp9_iadst_idct_8x8_add_12_sse2() {};
void ff_vp9_idct_iadst_8x8_add_12_sse2() {};
void ff_vp9_iadst_iadst_8x8_add_12_sse2() {};
void ff_vp9_idct_idct_16x16_add_12_sse2() {};
void ff_vp9_iadst_idct_16x16_add_12_sse2() {};
void ff_vp9_idct_iadst_16x16_add_12_sse2() {};
void ff_vp9_iadst_iadst_16x16_add_12_sse2() {};
void ff_vp9_idct_idct_32x32_add_12_sse2() {};
void ff_sbr_sum_square_sse() {};
void ff_sbr_sum64x5_sse() {};
void ff_sbr_hf_g_filt_sse() {};
void ff_sbr_hf_gen_sse() {};
void ff_sbr_neg_odd_64_sse() {};
void ff_sbr_qmf_post_shuffle_sse() {};
void ff_sbr_qmf_deint_bfly_sse() {};
void ff_sbr_qmf_deint_bfly_sse2() {};
void ff_sbr_qmf_pre_shuffle_sse2() {};
void ff_sbr_hf_apply_noise_0_sse2() {};
void ff_sbr_hf_apply_noise_1_sse2() {};
void ff_sbr_hf_apply_noise_2_sse2() {};
void ff_sbr_hf_apply_noise_3_sse2() {};
void ff_sbr_qmf_deint_neg_sse() {};
void ff_sbr_autocorrelate_sse() {};
void ff_sbr_autocorrelate_sse3() {};
void ff_synth_filter_inner_sse2() {};
void ff_synth_filter_inner_avx() {};
void ff_synth_filter_inner_fma3() {};
void ff_pvq_search_approx_sse2() {};
void ff_pvq_search_approx_sse4() {};
void ff_pvq_search_exact_avx() {};
void ff_opus_postfilter_fma3() {};
void ff_opus_deemphasis_fma3() {};
void ff_rv34_idct_dc_noround_mmxext() {};
void ff_rv34_idct_dc_add_mmx() {};
void ff_rv34_idct_dc_add_sse2() {};
void ff_rv34_idct_dc_add_sse4() {};
void ff_rv34_idct_add_mmxext() {};
void ff_vp9_avg8_16_mmxext() {};
void ff_vp9_put128_sse() {};
void ff_vp9_avg16_16_sse2() {};
void ff_vp9_avg32_16_sse2() {};
void ff_vp9_avg64_16_sse2() {};
void ff_vp9_avg128_16_sse2() {};
void ff_vp9_put128_avx() {};
void ff_vp9_avg32_16_avx2() {};
void ff_vp9_avg64_16_avx2() {};
void ff_vp9_avg128_16_avx2() {};
void ff_vp9_ipred_v_4x4_16_mmx() {};
void ff_vp9_ipred_v_8x8_16_sse() {};
void ff_vp9_ipred_v_16x16_16_sse() {};
void ff_vp9_ipred_v_32x32_16_sse() {};
void ff_vp9_ipred_h_4x4_16_mmxext() {};
void ff_vp9_ipred_h_8x8_16_sse2() {};
void ff_vp9_ipred_h_16x16_16_sse2() {};
void ff_vp9_ipred_h_32x32_16_sse2() {};
void ff_vp9_ipred_dc_4x4_16_mmxext() {};
void ff_vp9_ipred_dc_8x8_16_sse2() {};
void ff_vp9_ipred_dc_16x16_16_sse2() {};
void ff_vp9_ipred_dc_32x32_16_sse2() {};
void ff_vp9_ipred_dc_top_4x4_16_mmxext() {};
void ff_vp9_ipred_dc_top_8x8_16_sse2() {};
void ff_vp9_ipred_dc_top_16x16_16_sse2() {};
void ff_vp9_ipred_dc_top_32x32_16_sse2() {};
void ff_vp9_ipred_dc_left_4x4_16_mmxext() {};
void ff_vp9_ipred_dc_left_8x8_16_sse2() {};
void ff_vp9_ipred_dc_left_16x16_16_sse2() {};
void ff_vp9_ipred_dc_left_32x32_16_sse2() {};
void ff_vp9_ipred_dl_16x16_16_avx2() {};
void ff_vp9_ipred_dl_32x32_16_avx2() {};
void ff_vp9_ipred_dr_16x16_16_avx2() {};
void ff_vp9_ipred_dr_32x32_16_avx2() {};
void ff_vp9_ipred_dl_4x4_16_sse2() {};
void ff_vp9_ipred_dl_8x8_16_sse2() {};
void ff_vp9_ipred_dl_16x16_16_sse2() {};
void ff_vp9_ipred_dl_32x32_16_sse2() {};
void ff_vp9_ipred_dl_4x4_16_ssse3() {};
void ff_vp9_ipred_dl_8x8_16_ssse3() {};
void ff_vp9_ipred_dl_16x16_16_ssse3() {};
void ff_vp9_ipred_dl_32x32_16_ssse3() {};
void ff_vp9_ipred_dl_4x4_16_avx() {};
void ff_vp9_ipred_dl_8x8_16_avx() {};
void ff_vp9_ipred_dl_16x16_16_avx() {};
void ff_vp9_ipred_dl_32x32_16_avx() {};
void ff_vp9_ipred_dr_4x4_16_sse2() {};
void ff_vp9_ipred_dr_8x8_16_sse2() {};
void ff_vp9_ipred_dr_16x16_16_sse2() {};
void ff_vp9_ipred_dr_32x32_16_sse2() {};
void ff_vp9_ipred_dr_4x4_16_ssse3() {};
void ff_vp9_ipred_dr_8x8_16_ssse3() {};
void ff_vp9_ipred_dr_16x16_16_ssse3() {};
void ff_vp9_ipred_dr_32x32_16_ssse3() {};
void ff_vp9_ipred_dr_4x4_16_avx() {};
void ff_vp9_ipred_dr_8x8_16_avx() {};
void ff_vp9_ipred_dr_16x16_16_avx() {};
void ff_vp9_ipred_dr_32x32_16_avx() {};
void ff_vp9_ipred_vl_4x4_16_sse2() {};
void ff_vp9_ipred_vl_8x8_16_sse2() {};
void ff_vp9_ipred_vl_16x16_16_sse2() {};
void ff_vp9_ipred_vl_32x32_16_sse2() {};
void ff_vp9_ipred_vl_4x4_16_ssse3() {};
void ff_vp9_ipred_vl_8x8_16_ssse3() {};
void ff_vp9_ipred_vl_16x16_16_ssse3() {};
void ff_vp9_ipred_vl_32x32_16_ssse3() {};
void ff_vp9_ipred_vl_4x4_16_avx() {};
void ff_vp9_ipred_vl_8x8_16_avx() {};
void ff_vp9_ipred_vl_16x16_16_avx() {};
void ff_vp9_ipred_vl_32x32_16_avx() {};
void ff_vp9_ipred_vr_4x4_16_sse2() {};
void ff_vp9_ipred_vr_8x8_16_sse2() {};
void ff_vp9_ipred_vr_16x16_16_sse2() {};
void ff_vp9_ipred_vr_32x32_16_sse2() {};
void ff_vp9_ipred_vr_4x4_16_ssse3() {};
void ff_vp9_ipred_vr_8x8_16_ssse3() {};
void ff_vp9_ipred_vr_16x16_16_ssse3() {};
void ff_vp9_ipred_vr_32x32_16_ssse3() {};
void ff_vp9_ipred_vr_4x4_16_avx() {};
void ff_vp9_ipred_vr_8x8_16_avx() {};
void ff_vp9_ipred_vr_16x16_16_avx() {};
void ff_vp9_ipred_vr_32x32_16_avx() {};
void ff_vp9_ipred_hu_4x4_16_sse2() {};
void ff_vp9_ipred_hu_8x8_16_sse2() {};
void ff_vp9_ipred_hu_16x16_16_sse2() {};
void ff_vp9_ipred_hu_32x32_16_sse2() {};
void ff_vp9_ipred_hu_4x4_16_ssse3() {};
void ff_vp9_ipred_hu_8x8_16_ssse3() {};
void ff_vp9_ipred_hu_16x16_16_ssse3() {};
void ff_vp9_ipred_hu_32x32_16_ssse3() {};
void ff_vp9_ipred_hu_4x4_16_avx() {};
void ff_vp9_ipred_hu_8x8_16_avx() {};
void ff_vp9_ipred_hu_16x16_16_avx() {};
void ff_vp9_ipred_hu_32x32_16_avx() {};
void ff_vp9_ipred_hd_4x4_16_sse2() {};
void ff_vp9_ipred_hd_8x8_16_sse2() {};
void ff_vp9_ipred_hd_16x16_16_sse2() {};
void ff_vp9_ipred_hd_32x32_16_sse2() {};
void ff_vp9_ipred_hd_4x4_16_ssse3() {};
void ff_vp9_ipred_hd_8x8_16_ssse3() {};
void ff_vp9_ipred_hd_16x16_16_ssse3() {};
void ff_vp9_ipred_hd_32x32_16_ssse3() {};
void ff_vp9_ipred_hd_4x4_16_avx() {};
void ff_vp9_ipred_hd_8x8_16_avx() {};
void ff_vp9_ipred_hd_16x16_16_avx() {};
void ff_vp9_ipred_hd_32x32_16_avx() {};
void ff_ps_add_squares_sse() {};
void ff_ps_add_squares_sse3() {};
void ff_ps_mul_pair_single_sse() {};
void ff_ps_hybrid_analysis_sse() {};
void ff_ps_hybrid_analysis_sse3() {};
void ff_ps_stereo_interpolate_sse3() {};
void ff_ps_stereo_interpolate_ipdopd_sse3() {};
void ff_ps_hybrid_synthesis_deint_sse() {};
void ff_ps_hybrid_synthesis_deint_sse4() {};
void ff_ps_hybrid_analysis_ileave_sse() {};
void x86_check_features() {};
void insert_string_sse4() {};
void quick_insert_string_sse4() {};
void slide_hash_sse2() {};
void slide_hash_avx2() {};
void adler32_ssse3() {};
void adler32_avx2() {};
void chunksize_sse2() {};
void chunkcopy_sse2() {};
void chunkcopy_safe_sse2() {};
void chunkunroll_sse2() {};
void chunkmemset_sse2() {};
void chunkmemset_safe_sse2() {};
void chunksize_avx() {};
void chunkcopy_avx() {};
void chunkcopy_safe_avx() {};
void chunkunroll_avx() {};
void chunkmemset_avx() {};
void chunkmemset_safe_avx() {};
void x86_cpu_has_avx2() {};
void x86_cpu_has_sse2() {};
void x86_cpu_has_ssse3() {};
void x86_cpu_has_sse42() {};
void avpriv_emms_asm() {};
void ff_aacdec_init_mips() {};
void ff_aacsbr_func_ptr_init_mips() {};
void ff_ac3dsp_init_arm() {};
void ff_ac3dsp_init_mips() {};
void ff_acelp_filter_init_mips() {};
void ff_acelp_vectors_init_mips() {};
void ff_audiodsp_init_arm() {};
void ff_audiodsp_init_ppc() {};
void ff_blockdsp_init_alpha() {};
void ff_blockdsp_init_arm() {};
void ff_blockdsp_init_mips() {};
void ff_blockdsp_init_ppc() {};
void ff_celp_filter_init_mips() {};
void ff_celp_math_init_mips() {};
void ff_fft_init_aarch64() {};
void ff_fft_init_arm() {};
void ff_fft_init_mips() {};
void ff_fft_init_ppc() {};
void ff_fft15_avx() {};
void ff_flac_decorrelate_indep8_16_avx() {};
void ff_flac_decorrelate_indep8_16_sse2() {};
void ff_flac_decorrelate_indep8_32_avx() {};
void ff_flac_decorrelate_indep8_32_sse2() {};
void ff_flacdsp_init_arm() {};
void ff_float_dsp_init_aarch64() {};
void ff_float_dsp_init_arm() {};
void ff_float_dsp_init_mips() {};
void ff_float_dsp_init_ppc() {};
void ff_fmt_convert_init_aarch64() {};
void ff_fmt_convert_init_arm() {};
void ff_fmt_convert_init_mips() {};
void ff_fmt_convert_init_ppc() {};
void ff_g722dsp_init_arm() {};
void ff_get_cpu_flags_aarch64() {};
void ff_get_cpu_flags_arm() {};
void ff_get_cpu_flags_ppc() {};
void ff_get_cpu_max_align_aarch64() {};
void ff_get_cpu_max_align_arm() {};
void ff_get_cpu_max_align_ppc() {};
void ff_get_unscaled_swscale_aarch64() {};
void ff_get_unscaled_swscale_arm() {};
void ff_get_unscaled_swscale_ppc() {};
void ff_h263dsp_init_mips() {};
void ff_h264_pred_init_aarch64() {};
void ff_h264_pred_init_arm() {};
void ff_h264_pred_init_mips() {};
void ff_h264chroma_init_aarch64() {};
void ff_h264chroma_init_arm() {};
void ff_h264chroma_init_mips() {};
void ff_h264chroma_init_ppc() {};
void ff_h264dsp_init_aarch64() {};
void ff_h264dsp_init_arm() {};
void ff_h264dsp_init_mips() {};
void ff_h264dsp_init_ppc() {};
void ff_h264qpel_init_aarch64() {};
void ff_h264qpel_init_arm() {};
void ff_h264qpel_init_mips() {};
void ff_h264qpel_init_ppc() {};
void ff_hevc_dsp_init_aarch64() {};
void ff_hevc_dsp_init_arm() {};
void ff_hevc_dsp_init_mips() {};
void ff_hevc_dsp_init_ppc() {};
void ff_hevc_h_loop_filter_luma_10_avx() {};
void ff_hevc_h_loop_filter_luma_10_sse2() {};
void ff_hevc_h_loop_filter_luma_10_ssse3() {};
void ff_hevc_h_loop_filter_luma_12_avx() {};
void ff_hevc_h_loop_filter_luma_12_sse2() {};
void ff_hevc_h_loop_filter_luma_12_ssse3() {};
void ff_hevc_h_loop_filter_luma_8_avx() {};
void ff_hevc_h_loop_filter_luma_8_sse2() {};
void ff_hevc_h_loop_filter_luma_8_ssse3() {};
void ff_hevc_idct_16x16_10_avx() {};
void ff_hevc_idct_16x16_10_sse2() {};
void ff_hevc_idct_16x16_8_avx() {};
void ff_hevc_idct_16x16_8_sse2() {};
void ff_hevc_idct_32x32_10_avx() {};
void ff_hevc_idct_32x32_10_sse2() {};
void ff_hevc_idct_32x32_8_avx() {};
void ff_hevc_idct_32x32_8_sse2() {};
void ff_hevc_pred_init_mips() {};
void ff_hevc_put_hevc_bi_epel_h12_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h16_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_h16_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h32_8_avx2() {};
void ff_hevc_put_hevc_bi_epel_h4_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h4_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h4_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h6_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h6_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h6_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h8_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h8_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h8_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv16_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_hv16_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv32_8_avx2() {};
void ff_hevc_put_hevc_bi_epel_hv4_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv4_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv4_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv6_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv6_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv6_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv8_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv8_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv8_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v12_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v16_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_v16_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v32_8_avx2() {};
void ff_hevc_put_hevc_bi_epel_v4_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v4_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v4_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v6_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v6_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v6_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v8_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v8_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v8_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels12_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels16_10_avx2() {};
void ff_hevc_put_hevc_bi_pel_pixels16_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels32_8_avx2() {};
void ff_hevc_put_hevc_bi_pel_pixels4_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels4_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels4_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels6_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels6_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels6_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels8_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels8_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels8_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h12_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h16_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_h16_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h32_8_avx2() {};
void ff_hevc_put_hevc_bi_qpel_h4_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h4_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h4_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h8_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h8_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h8_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv16_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_hv4_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv4_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv4_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv8_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv8_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv8_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v12_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v16_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_v16_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v32_8_avx2() {};
void ff_hevc_put_hevc_bi_qpel_v4_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v4_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v4_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v8_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v8_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v8_8_sse4() {};
void ff_hevc_put_hevc_epel_h12_8_sse4() {};
void ff_hevc_put_hevc_epel_h16_10_avx2() {};
void ff_hevc_put_hevc_epel_h16_8_sse4() {};
void ff_hevc_put_hevc_epel_h32_8_avx2() {};
void ff_hevc_put_hevc_epel_h4_10_sse4() {};
void ff_hevc_put_hevc_epel_h4_12_sse4() {};
void ff_hevc_put_hevc_epel_h4_8_sse4() {};
void ff_hevc_put_hevc_epel_h6_10_sse4() {};
void ff_hevc_put_hevc_epel_h6_12_sse4() {};
void ff_hevc_put_hevc_epel_h6_8_sse4() {};
void ff_hevc_put_hevc_epel_h8_10_sse4() {};
void ff_hevc_put_hevc_epel_h8_12_sse4() {};
void ff_hevc_put_hevc_epel_h8_8_sse4() {};
void ff_hevc_put_hevc_epel_hv16_10_avx2() {};
void ff_hevc_put_hevc_epel_hv16_8_sse4() {};
void ff_hevc_put_hevc_epel_hv32_8_avx2() {};
void ff_hevc_put_hevc_epel_hv4_10_sse4() {};
void ff_hevc_put_hevc_epel_hv4_12_sse4() {};
void ff_hevc_put_hevc_epel_hv4_8_sse4() {};
void ff_hevc_put_hevc_epel_hv6_10_sse4() {};
void ff_hevc_put_hevc_epel_hv6_12_sse4() {};
void ff_hevc_put_hevc_epel_hv6_8_sse4() {};
void ff_hevc_put_hevc_epel_hv8_10_sse4() {};
void ff_hevc_put_hevc_epel_hv8_12_sse4() {};
void ff_hevc_put_hevc_epel_hv8_8_sse4() {};
void ff_hevc_put_hevc_epel_v12_8_sse4() {};
void ff_hevc_put_hevc_epel_v16_10_avx2() {};
void ff_hevc_put_hevc_epel_v16_8_sse4() {};
void ff_hevc_put_hevc_epel_v32_8_avx2() {};
void ff_hevc_put_hevc_epel_v4_10_sse4() {};
void ff_hevc_put_hevc_epel_v4_12_sse4() {};
void ff_hevc_put_hevc_epel_v4_8_sse4() {};
void ff_hevc_put_hevc_epel_v6_10_sse4() {};
void ff_hevc_put_hevc_epel_v6_12_sse4() {};
void ff_hevc_put_hevc_epel_v6_8_sse4() {};
void ff_hevc_put_hevc_epel_v8_10_sse4() {};
void ff_hevc_put_hevc_epel_v8_12_sse4() {};
void ff_hevc_put_hevc_epel_v8_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels12_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels16_10_avx2() {};
void ff_hevc_put_hevc_pel_pixels16_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels32_8_avx2() {};
void ff_hevc_put_hevc_pel_pixels4_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels4_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels4_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels6_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels6_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels6_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels8_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels8_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels8_8_sse4() {};
void ff_hevc_put_hevc_qpel_h12_8_sse4() {};
void ff_hevc_put_hevc_qpel_h16_10_avx2() {};
void ff_hevc_put_hevc_qpel_h16_8_sse4() {};
void ff_hevc_put_hevc_qpel_h32_8_avx2() {};
void ff_hevc_put_hevc_qpel_h4_10_sse4() {};
void ff_hevc_put_hevc_qpel_h4_12_sse4() {};
void ff_hevc_put_hevc_qpel_h4_8_sse4() {};
void ff_hevc_put_hevc_qpel_h8_10_sse4() {};
void ff_hevc_put_hevc_qpel_h8_12_sse4() {};
void ff_hevc_put_hevc_qpel_h8_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv16_10_avx2() {};
void ff_hevc_put_hevc_qpel_hv4_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv4_12_sse4() {};
void ff_hevc_put_hevc_qpel_hv4_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv8_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv8_12_sse4() {};
void ff_hevc_put_hevc_qpel_hv8_8_sse4() {};
void ff_hevc_put_hevc_qpel_v12_8_sse4() {};
void ff_hevc_put_hevc_qpel_v16_10_avx2() {};
void ff_hevc_put_hevc_qpel_v16_8_sse4() {};
void ff_hevc_put_hevc_qpel_v32_8_avx2() {};
void ff_hevc_put_hevc_qpel_v4_10_sse4() {};
void ff_hevc_put_hevc_qpel_v4_12_sse4() {};
void ff_hevc_put_hevc_qpel_v4_8_sse4() {};
void ff_hevc_put_hevc_qpel_v8_10_sse4() {};
void ff_hevc_put_hevc_qpel_v8_12_sse4() {};
void ff_hevc_put_hevc_qpel_v8_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h12_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h16_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_h16_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h32_8_avx2() {};
void ff_hevc_put_hevc_uni_epel_h4_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h4_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h4_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h6_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h6_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h6_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h8_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h8_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h8_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv16_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv32_8_avx2() {};
void ff_hevc_put_hevc_uni_epel_hv4_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv4_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv4_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv6_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv6_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv6_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv8_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv8_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv8_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v12_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v16_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_v16_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v32_8_avx2() {};
void ff_hevc_put_hevc_uni_epel_v4_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v4_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v4_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v6_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v6_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v6_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v8_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v8_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v8_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels12_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels16_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels32_8_avx2() {};
void ff_hevc_put_hevc_uni_pel_pixels4_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels4_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels4_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels6_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels6_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels6_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels8_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels8_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels8_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h12_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h16_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_h16_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h32_8_avx2() {};
void ff_hevc_put_hevc_uni_qpel_h4_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h4_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h4_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h8_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h8_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h8_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv16_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_hv4_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv4_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv4_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv8_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv8_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv8_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v12_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v16_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_v16_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v32_8_avx2() {};
void ff_hevc_put_hevc_uni_qpel_v4_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v4_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v4_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v8_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v8_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v8_8_sse4() {};
void ff_hevc_v_loop_filter_luma_10_avx() {};
void ff_hevc_v_loop_filter_luma_10_sse2() {};
void ff_hevc_v_loop_filter_luma_10_ssse3() {};
void ff_hevc_v_loop_filter_luma_12_avx() {};
void ff_hevc_v_loop_filter_luma_12_sse2() {};
void ff_hevc_v_loop_filter_luma_12_ssse3() {};
void ff_hevc_v_loop_filter_luma_8_avx() {};
void ff_hevc_v_loop_filter_luma_8_sse2() {};
void ff_hevc_v_loop_filter_luma_8_ssse3() {};
void ff_hpeldsp_init_aarch64() {};
void ff_hpeldsp_init_alpha() {};
void ff_hpeldsp_init_arm() {};
void ff_hpeldsp_init_mips() {};
void ff_hpeldsp_init_ppc() {};
void ff_hscale8to15_4_avx2() {};
void ff_hscale8to15_X4_avx2() {};
void ff_idctdsp_init_aarch64() {};
void ff_idctdsp_init_alpha() {};
void ff_idctdsp_init_arm() {};
void ff_idctdsp_init_mips() {};
void ff_idctdsp_init_ppc() {};
void ff_llauddsp_init_arm() {};
void ff_llauddsp_init_ppc() {};
void ff_llviddsp_init_ppc() {};
void ff_mdct15_postreindex_avx2() {};
void ff_me_cmp_init_alpha() {};
void ff_me_cmp_init_arm() {};
void ff_me_cmp_init_mips() {};
void ff_me_cmp_init_ppc() {};
void ff_mlp_rematrix_channel_avx2_bmi2() {};
void ff_mlp_rematrix_channel_sse4() {};
void ff_mlpdsp_init_arm() {};
void ff_mpadsp_init_aarch64() {};
void ff_mpadsp_init_arm() {};
void ff_mpadsp_init_mipsdsp() {};
void ff_mpadsp_init_mipsfpu() {};
void ff_mpadsp_init_ppc() {};
void ff_mpegvideodsp_init_ppc() {};
void ff_mpegvideoencdsp_init_arm() {};
void ff_mpegvideoencdsp_init_mips() {};
void ff_mpegvideoencdsp_init_ppc() {};
void ff_mpv_common_init_arm() {};
void ff_mpv_common_init_axp() {};
void ff_mpv_common_init_mips() {};
void ff_mpv_common_init_neon() {};
void ff_mpv_common_init_ppc() {};
void ff_opus_dsp_init_aarch64() {};
void ff_psdsp_init_aarch64() {};
void ff_psdsp_init_arm() {};
void ff_psdsp_init_mips() {};
void ff_qpeldsp_init_mips() {};
void ff_rdft_init_arm() {};
void ff_rv34dsp_init_arm() {};
void ff_rv40dsp_init_aarch64() {};
void ff_rv40dsp_init_arm() {};
void ff_sbrdsp_init_aarch64() {};
void ff_sbrdsp_init_arm() {};
void ff_sbrdsp_init_mips() {};
void ff_shuffle_bytes_0321_avx2() {};
void ff_shuffle_bytes_1230_avx2() {};
void ff_shuffle_bytes_2103_avx2() {};
void ff_shuffle_bytes_3012_avx2() {};
void ff_shuffle_bytes_3210_avx2() {};
void ff_simple_idct10_avx() {};
void ff_simple_idct10_put_avx() {};
void ff_simple_idct10_put_sse2() {};
void ff_simple_idct10_sse2() {};
void ff_simple_idct12_avx() {};
void ff_simple_idct12_put_avx() {};
void ff_simple_idct12_put_sse2() {};
void ff_simple_idct12_sse2() {};
void ff_simple_idct8_add_avx() {};
void ff_simple_idct8_add_sse2() {};
void ff_simple_idct8_avx() {};
void ff_simple_idct8_put_avx() {};
void ff_simple_idct8_put_sse2() {};
void ff_simple_idct8_sse2() {};
void ff_sws_init_swscale_aarch64() {};
void ff_sws_init_swscale_arm() {};
void ff_sws_init_swscale_ppc() {};
void ff_synth_filter_init_aarch64() {};
void ff_synth_filter_init_arm() {};
void ff_vc1dsp_init_aarch64() {};
void ff_vc1dsp_init_arm() {};
void ff_vc1dsp_init_mips() {};
void ff_vc1dsp_init_mmx() {};
void ff_vc1dsp_init_mmxext() {};
void ff_vc1dsp_init_ppc() {};
void ff_videodsp_init_aarch64() {};
void ff_videodsp_init_arm() {};
void ff_videodsp_init_mips() {};
void ff_videodsp_init_ppc() {};
void ff_vorbisdsp_init_aarch64() {};
void ff_vorbisdsp_init_arm() {};
void ff_vorbisdsp_init_ppc() {};
void ff_vp3dsp_init_arm() {};
void ff_vp3dsp_init_mips() {};
void ff_vp3dsp_init_ppc() {};
void ff_vp6dsp_init_arm() {};
void ff_vp78dsp_init_aarch64() {};
void ff_vp78dsp_init_arm() {};
void ff_vp78dsp_init_ppc() {};
void ff_vp8dsp_init_aarch64() {};
void ff_vp8dsp_init_arm() {};
void ff_vp8dsp_init_mips() {};
void ff_vp9dsp_init_aarch64() {};
void ff_vp9dsp_init_arm() {};
void ff_vp9dsp_init_mips() {};
void ff_wmv2dsp_init_mips() {};
void ff_xvid_idct_init_mips() {};
void ff_xvmc_init_block() {};
void ff_xvmc_pack_pblocks() {};
void ff_yuv2rgb_init_ppc() {};
void ff_yuv2rgb_init_tables_ppc() {};
void rgb2rgb_init_aarch64() {};
void swri_audio_convert_init_aarch64() {};
void swri_audio_convert_init_arm() {};
void swri_resample_dsp_aarch64_init() {};
void swri_resample_dsp_arm_init() {};
void yuv2yuvX_mmx() {};
void ff_hevc_put_hevc_uni_pel_pixels48_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h48_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels48_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v16_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv64_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels64_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_hv64_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v8_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h4_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels32_10_sse4() {};
void ff_hevc_put_hevc_epel_v32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv64_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv48_10_sse4() {};
void ff_hevc_put_hevc_epel_hv16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v6_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv12_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h48_10_avx2() {};
void ff_hevc_put_hevc_qpel_hv64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v64_8_avx2() {};
void ff_hevc_put_hevc_qpel_v48_8_sse4() {};
void ff_hevc_put_hevc_epel_hv48_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_h16_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h64_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv64_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv24_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h32_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_v24_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h24_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v16_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h32_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv64_10_avx2() {};
void ff_hevc_put_hevc_bi_pel_pixels32_10_sse4() {};
void ff_hevc_put_hevc_epel_h24_10_avx2() {};
void ff_hevc_put_hevc_epel_v24_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h8_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels12_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v24_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v64_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_v64_10_sse4() {};
void ff_hevc_put_hevc_epel_hv64_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv12_8_sse4() {};
void ff_hevc_put_hevc_qpel_v24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv6_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v48_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_v24_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h4_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v48_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels6_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v24_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_h48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h8_10_sse4() {};
void ff_hevc_put_hevc_qpel_h48_8_avx2() {};
void ff_hevc_put_hevc_uni_epel_h16_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v64_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv4_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v16_10_sse4() {};
void ff_hevc_put_hevc_qpel_h64_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_v24_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v12_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h4_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv64_10_sse4() {};
void ff_hevc_put_hevc_qpel_v12_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v16_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h8_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v12_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v48_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v64_10_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_v24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels64_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv16_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v48_10_sse4() {};
void ff_hevc_put_hevc_epel_v24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv8_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels16_12_sse4() {};
void ff_hevc_put_hevc_qpel_h16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels4_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv48_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_hv12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h16_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v16_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv48_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels32_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv48_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv16_10_sse4() {};
void ff_hevc_put_hevc_epel_v12_10_sse4() {};
void ff_hevc_put_hevc_qpel_h16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v64_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v48_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv8_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h32_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h16_10_sse4() {};
void ff_hevc_put_hevc_epel_h64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv4_8_sse4() {};
void ff_hevc_put_hevc_epel_hv32_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv24_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v48_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_h12_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv64_8_avx2() {};
void ff_hevc_put_hevc_bi_qpel_h16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v4_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h8_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h64_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv48_8_sse4() {};
void ff_hevc_put_hevc_qpel_h48_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_h32_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v24_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv48_8_avx2() {};
void ff_hevc_put_hevc_bi_qpel_h64_8_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_h6_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h8_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h64_8_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_h64_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h64_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v64_8_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv32_8_sse4() {};
void ff_hevc_put_hevc_qpel_h48_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h64_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_h6_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv48_12_sse4() {};
void ff_hevc_put_hevc_qpel_h64_10_sse4() {};
void ff_hevc_put_hevc_epel_hv32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v4_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h48_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v48_10_avx2() {};
void ff_hevc_put_hevc_epel_v48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_v8_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv48_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv48_10_avx2() {};
void ff_hevc_put_hevc_pel_pixels12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h6_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v64_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v16_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v32_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v4_10_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels64_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v24_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_hv12_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels64_8_avx2() {};
void ff_hevc_put_hevc_qpel_hv32_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_hv24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv32_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels12_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h48_10_sse4() {};
void ff_hevc_put_hevc_epel_hv32_10_avx2() {};
void ff_hevc_put_hevc_epel_v64_8_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_h64_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv64_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v24_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h32_10_avx2() {};
void ff_hevc_put_hevc_qpel_v64_8_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_h32_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h24_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h64_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h32_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_hv24_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v12_12_sse4() {};
void ff_hevc_put_hevc_qpel_v16_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels48_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h32_10_avx2() {};
void ff_hevc_put_hevc_bi_w_pel_pixels32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv12_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h24_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h48_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h4_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv24_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_hv12_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv24_12_sse4() {};
void ff_hevc_put_hevc_qpel_v48_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv48_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv16_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv4_12_sse4() {};
void ff_hevc_put_hevc_epel_hv12_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v8_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v8_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h48_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv4_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v12_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels48_12_sse4() {};
void ff_hevc_put_hevc_qpel_v64_10_avx2() {};
void ff_hevc_put_hevc_epel_h32_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h48_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v16_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv48_8_sse4() {};
void ff_hevc_put_hevc_epel_v64_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h32_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v48_8_avx2() {};
void ff_hevc_put_hevc_uni_epel_v64_8_avx2() {};
void ff_hevc_put_hevc_qpel_hv48_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_h16_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv4_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h8_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h48_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v64_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels24_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv48_8_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_h32_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h16_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v12_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv32_12_sse4() {};
void ff_hevc_put_hevc_qpel_hv16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv12_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h64_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_v12_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h32_10_sse4() {};
void ff_hevc_put_hevc_epel_v64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h12_12_sse4() {};
void ff_hevc_put_hevc_epel_h48_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv6_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv24_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_h24_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h32_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv64_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels128_8_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_hv64_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels32_10_avx2() {};
void ff_hevc_put_hevc_pel_pixels32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv8_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv24_10_avx2() {};
void ff_hevc_put_hevc_pel_pixels16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels12_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv8_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv64_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels32_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv64_8_avx2() {};
void ff_hevc_put_hevc_bi_epel_h64_8_avx2() {};
void ff_hevc_put_hevc_epel_v24_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_h16_10_sse4() {};
void ff_hevc_put_hevc_epel_hv32_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv8_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels6_12_sse4() {};
void ff_hevc_put_hevc_qpel_v48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v4_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h48_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_v12_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels4_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv24_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_h32_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v64_8_avx2() {};
void ff_hevc_put_hevc_qpel_h32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels24_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels64_10_avx2() {};
void ff_hevc_put_hevc_qpel_v24_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_v48_8_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_h16_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v32_10_avx2() {};
void ff_hevc_put_hevc_epel_v32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv64_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h32_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv48_12_sse4() {};
void ff_hevc_put_hevc_epel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv64_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h6_8_sse4() {};
void ff_hevc_put_hevc_epel_h24_10_sse4() {};
void ff_hevc_put_hevc_epel_h48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_hv4_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels24_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h48_8_sse4() {};
void ff_hevc_put_hevc_qpel_h24_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv12_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h32_12_sse4() {};
void ff_hevc_put_hevc_epel_hv24_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_h16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv6_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv32_10_sse4() {};
void ff_hevc_put_hevc_epel_v24_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv32_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v16_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v48_10_sse4() {};
void ff_hevc_put_hevc_qpel_h24_10_avx2() {};
void ff_hevc_put_hevc_bi_w_pel_pixels48_10_sse4() {};
void ff_hevc_put_hevc_qpel_v48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_hv48_12_sse4() {};
void ff_hevc_put_hevc_epel_v64_10_sse4() {};
void ff_hevc_put_hevc_epel_v64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v4_12_sse4() {};
void ff_hevc_put_hevc_epel_hv48_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels8_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv48_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv64_12_sse4() {};
void ff_hevc_put_hevc_qpel_hv12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v8_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v4_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels64_10_sse4() {};
void ff_hevc_put_hevc_epel_h48_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv32_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h16_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v32_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_v12_10_sse4() {};
void ff_hevc_put_hevc_qpel_h48_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels16_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels48_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv32_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h12_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv16_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels16_10_sse4() {};
void ff_hevc_put_hevc_epel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv64_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv48_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv48_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h4_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v24_10_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v4_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h24_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels48_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_hv48_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_h24_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_hv48_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h16_10_sse4() {};
void ff_hevc_put_hevc_qpel_h64_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v64_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels12_12_sse4() {};
void ff_hevc_put_hevc_epel_h32_10_sse4() {};
void ff_hevc_put_hevc_epel_h16_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv24_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv8_8_sse4() {};
void ff_hevc_put_hevc_qpel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels4_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h32_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels4_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v4_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v48_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv32_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv64_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_v6_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v4_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels8_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h24_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv16_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v32_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h24_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v16_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv8_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h24_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv64_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h12_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels16_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v8_10_sse4() {};
void ff_hevc_put_hevc_qpel_v12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v16_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v6_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels32_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv32_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v64_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_hv12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv6_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h16_12_sse4() {};
void ff_hevc_put_hevc_qpel_h24_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv8_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h64_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels8_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv12_12_sse4() {};
void ff_hevc_put_hevc_epel_h32_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_h4_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h48_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_v4_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv32_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v48_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels64_8_sse4() {};
void ff_hevc_put_hevc_qpel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h24_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv32_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv64_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_v16_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv8_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v64_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels6_8_sse4() {};
void ff_hevc_put_hevc_qpel_h64_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv4_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v32_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels32_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_v12_12_sse4() {};
void ff_hevc_put_hevc_epel_v48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv6_12_sse4() {};
void ff_hevc_put_hevc_epel_hv12_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv12_8_sse4() {};
void ff_hevc_put_hevc_epel_hv64_12_sse4() {};
void ff_hevc_put_hevc_epel_hv48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_hv24_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv4_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v12_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h8_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v6_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv32_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h32_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h12_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv64_12_sse4() {};
void ff_hevc_put_hevc_qpel_v32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h64_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v48_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v8_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv24_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h4_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels6_10_sse4() {};
void ff_hevc_put_hevc_qpel_v64_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels64_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v12_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h16_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_v24_10_avx2() {};
void ff_hevc_put_hevc_qpel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v64_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h32_10_sse4() {};
void ff_hevc_put_hevc_epel_hv24_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_v16_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv32_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h24_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv48_8_sse4() {};
void ff_hevc_put_hevc_qpel_h48_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv12_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv48_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv16_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v48_8_avx2() {};
void ff_hevc_put_hevc_bi_qpel_v12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h4_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h32_10_sse4() {};
void ff_hevc_put_hevc_epel_hv48_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels48_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_h24_12_sse4() {};
void ff_hevc_put_hevc_qpel_h12_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h48_12_sse4() {};
void ff_hevc_put_hevc_epel_v48_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv32_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_h48_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h64_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_h12_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv4_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v6_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v4_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v6_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v16_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_pel_pixels48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h6_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels4_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv8_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels24_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h4_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v8_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv32_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v8_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v48_10_avx2() {};
void ff_hevc_put_hevc_qpel_hv24_8_sse4() {};
void ff_hevc_put_hevc_qpel_v64_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h12_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v16_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv16_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels32_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv8_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v48_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels16_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_v24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v12_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h32_12_sse4() {};
void ff_hevc_put_hevc_qpel_h32_10_avx2() {};
void ff_hevc_put_hevc_bi_w_pel_pixels48_8_sse4() {};
void ff_hevc_put_hevc_qpel_hv32_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h32_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv32_10_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_h12_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels24_12_sse4() {};
void ff_hevc_put_hevc_qpel_hv16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v24_8_sse4() {};
void ff_hevc_put_hevc_qpel_v16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h16_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v64_12_sse4() {};
void ff_hevc_put_hevc_epel_hv24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv6_8_sse4() {};
void ff_hevc_put_hevc_epel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h32_10_sse4() {};
void ff_hevc_put_hevc_epel_v48_10_sse4() {};
void ff_hevc_put_hevc_epel_h48_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h24_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels48_8_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_v8_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels48_8_avx2() {};
void ff_hevc_put_hevc_epel_hv16_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v64_10_avx2() {};
void ff_hevc_put_hevc_pel_pixels64_8_avx2() {};
void ff_hevc_put_hevc_uni_w_qpel_h4_12_sse4() {};
void ff_hevc_put_hevc_epel_v16_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h64_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels24_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels48_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels64_12_sse4() {};
void ff_hevc_put_hevc_qpel_v32_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h8_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v24_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels6_10_sse4() {};
void ff_hevc_put_hevc_epel_hv64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels8_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v24_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv12_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv48_8_sse4() {};
void ff_hevc_put_hevc_pel_pixels24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h64_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels64_8_avx2() {};
void ff_hevc_put_hevc_epel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h32_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v4_8_sse4() {};
void ff_hevc_put_hevc_epel_hv24_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v48_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels4_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v32_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv24_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_hv4_8_sse4() {};
void ff_hevc_put_hevc_epel_h24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h6_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v32_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_h64_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v16_10_sse4() {};
void ff_hevc_put_hevc_epel_hv12_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv64_8_sse4() {};
void ff_hevc_put_hevc_epel_v12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v48_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv48_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv4_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h48_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h16_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h16_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels32_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_h48_8_avx2() {};
void ff_hevc_put_hevc_qpel_h32_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_v64_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v64_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v64_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels48_8_sse4() {};
void ff_hevc_put_hevc_qpel_v32_10_avx2() {};
void ff_hevc_put_hevc_bi_qpel_v48_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv32_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_v24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h48_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv24_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv16_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h64_10_sse4() {};
void ff_hevc_put_hevc_epel_v32_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_v48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv32_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels24_10_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_hv32_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels64_10_sse4() {};
void ff_hevc_put_hevc_pel_pixels64_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels12_10_sse4() {};
void ff_hevc_put_hevc_epel_hv64_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_h24_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_h64_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h64_12_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels32_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv32_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_v16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels8_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h64_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h24_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_h48_8_sse4() {};
void ff_hevc_put_hevc_epel_h16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h16_8_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv16_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v12_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h8_10_sse4() {};
void ff_hevc_put_hevc_epel_h64_12_sse4() {};
void ff_hevc_put_hevc_pel_pixels48_8_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_h12_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv8_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h48_8_avx2() {};
void ff_hevc_put_hevc_epel_h48_12_sse4() {};
void ff_hevc_put_hevc_qpel_v24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv16_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h48_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_v12_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h48_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h64_10_avx2() {};
void ff_hevc_put_hevc_uni_epel_h64_12_sse4() {};
void ff_hevc_put_hevc_qpel_h32_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv12_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels24_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_h24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels24_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v8_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels16_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h8_8_sse4() {};
void ff_hevc_put_hevc_epel_v48_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_h64_8_avx2() {};
void ff_hevc_put_hevc_bi_epel_v24_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_v24_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h64_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_v12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_v8_12_sse4() {};
void ff_hevc_put_hevc_qpel_v48_10_avx2() {};
void ff_hevc_put_hevc_uni_w_pel_pixels16_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h12_10_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels64_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v24_12_sse4() {};
void ff_hevc_put_hevc_epel_h64_10_avx2() {};
void ff_hevc_put_hevc_uni_qpel_h24_10_avx2() {};
void ff_hevc_put_hevc_bi_w_epel_v48_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h4_8_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels64_8_sse4() {};
void ff_hevc_put_hevc_epel_h64_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv12_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv12_12_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels64_8_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv12_12_sse4() {};
void ff_hevc_put_hevc_qpel_v64_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels24_10_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels16_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_v64_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv12_10_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv48_10_avx2() {};
void ff_hevc_put_hevc_bi_epel_v32_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv48_10_sse4() {};
void ff_hevc_put_hevc_uni_w_pel_pixels48_12_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h12_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv32_8_sse4() {};
void ff_hevc_put_hevc_epel_h32_8_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h8_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h48_8_avx2() {};
void ff_hevc_put_hevc_uni_qpel_hv64_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v24_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h32_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v48_10_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_h32_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels6_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h4_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv24_12_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels8_10_sse4() {};
void ff_hevc_put_hevc_qpel_h64_8_avx2() {};
void ff_hevc_put_hevc_bi_w_qpel_v24_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_hv16_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels24_8_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels96_8_avx2() {};
void ff_hevc_put_hevc_uni_qpel_hv24_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_hv16_10_sse4() {};
void ff_hevc_put_hevc_bi_w_qpel_h48_8_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv32_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv12_10_sse4() {};
void ff_hevc_put_hevc_uni_qpel_v24_10_sse4() {};
void ff_hevc_put_hevc_epel_hv64_8_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_hv32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h64_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h8_8_sse4() {};
void ff_hevc_put_hevc_bi_pel_pixels24_12_sse4() {};
void ff_hevc_put_hevc_bi_epel_v48_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_hv12_12_sse4() {};
void ff_hevc_put_hevc_bi_w_epel_hv4_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_h48_8_sse4() {};
void ff_hevc_put_hevc_bi_w_pel_pixels12_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h64_8_sse4() {};
void ff_hevc_put_hevc_epel_h64_8_avx2() {};
void ff_hevc_put_hevc_qpel_hv64_8_sse4() {};
void ff_hevc_put_hevc_uni_epel_h24_10_avx2() {};
void ff_hevc_put_hevc_qpel_v32_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h64_12_sse4() {};
void ff_hevc_put_hevc_uni_qpel_h24_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels32_12_sse4() {};
void ff_hevc_put_hevc_epel_h12_10_sse4() {};
void ff_hevc_put_hevc_qpel_v24_8_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_h24_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_h12_10_sse4() {};
void ff_hevc_put_hevc_qpel_hv32_12_sse4() {};
void ff_hevc_put_hevc_uni_w_epel_v64_12_sse4() {};
void ff_hevc_put_hevc_uni_pel_pixels16_12_sse4() {};
void ff_hevc_put_hevc_bi_qpel_h64_12_sse4() {};
void ff_hevc_put_hevc_qpel_hv64_10_avx2() {};
void ff_hevc_put_hevc_uni_w_epel_v48_10_sse4() {};
void ff_hevc_put_hevc_bi_qpel_hv64_10_sse4() {};
void ff_hevc_put_hevc_bi_epel_hv24_10_sse4() {};
void ff_hevc_put_hevc_uni_w_qpel_h24_8_sse4() {};
void ff_hevc_put_hevc_uni_qpel_hv64_10_avx2() {};
void ff_hevc_put_hevc_epel_v16_12_sse4() {};
void ff_hevc_put_hevc_uni_epel_hv16_12_sse4() {};

#endif


void ff_get_cpu_flags_loongarch()
{
};

void ff_get_cpu_max_align_loongarch()
{
};

void ff_h264dsp_init_loongarch()
{
};

void ff_h264_pred_init_loongarch()
{
};

void ff_h264chroma_init_loongarch()
{
};

void ff_vp8dsp_init_loongarch()
{
};

void ff_vp9dsp_init_loongarch()
{
};

void ff_h264qpel_init_loongarch()
{
};

void ff_vc1dsp_init_loongarch()
{
};

void ff_hevc_put_hevc_qpel_h4_8_avx512icl() {};
void ff_hevc_put_hevc_qpel_h32_8_avx512icl() {};
void ff_hevc_put_hevc_qpel_h8_8_avx512icl() {};
void ff_hevc_put_hevc_qpel_h16_8_avx512icl() {};
void ff_hevc_put_hevc_qpel_hv8_8_avx512icl() {};
void ff_hevc_put_hevc_qpel_h64_8_avx512icl() {};

}

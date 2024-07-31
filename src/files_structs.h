// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#pragma pack ( push, 1 )

namespace files_structs
{
	using WORD = uint16_t;
	using DWORD = uint32_t;
	using FOURCC = DWORD; /* a four character code */
	using LONG = long; /* a four character code */

	struct GUID
	{
		unsigned long Data1;
		unsigned short Data2;
		unsigned short Data3;
		uint8_t Data4[8];
	};

	struct RIFFCHUNK
	{
		FOURCC fcc;
		DWORD cb;
	};

	struct RIFFLIST
	{
		FOURCC fcc;
		DWORD cb;
		FOURCC fccListType;
	};

	struct AVISTREAMHEADER
	{
		FOURCC fcc; // 'strh'
		DWORD cb; // size of this structure - 8

		FOURCC fccType; // stream type codes

#ifndef streamtypeVIDEO
#define streamtypeVIDEO FCC('vids')
#define streamtypeAUDIO FCC('auds')
#define streamtypeMIDI  FCC('mids')
#define streamtypeTEXT  FCC('txts')
#endif

		FOURCC fccHandler;
		DWORD dwFlags;
#define AVISF_DISABLED          0x00000001
#define AVISF_VIDEO_PALCHANGES  0x00010000

		WORD wPriority;
		WORD wLanguage;
		DWORD dwInitialFrames;
		DWORD dwScale;
		DWORD dwRate; // dwRate/dwScale is stream tick rate in ticks/sec
		DWORD dwStart;
		DWORD dwLength;
		DWORD dwSuggestedBufferSize;
		DWORD dwQuality;
		DWORD dwSampleSize;

		struct
		{
			short int left;
			short int top;
			short int right;
			short int bottom;
		} rcFrame;
	};

#define WAVE_FORMAT_PCM     1
#define  WAVE_FORMAT_EXTENSIBLE                 0xFFFE /* Microsoft */

	struct WAVEFORMATEX
	{
		WORD wFormatTag; /* format type */
		WORD nChannels; /* number of channels (i.e. mono, stereo...) */
		DWORD nSamplesPerSec; /* sample rate */
		DWORD nAvgBytesPerSec; /* for buffer estimation */
		WORD nBlockAlign; /* block size of data */
		WORD wBitsPerSample; /* number of bits per sample of mono data */
		WORD cbSize; /* the count in bytes of the size of */
		/* extra information (after cbSize) */
	};

	struct WAVEFORMATEXTENSIBLE
	{
		WAVEFORMATEX Format;

		union
		{
			WORD wValidBitsPerSample; /* bits of precision  */
			WORD wSamplesPerBlock; /* valid if wBitsPerSample==0 */
			WORD wReserved; /* If neither applies, set to zero. */
		} Samples;

		DWORD dwChannelMask; /* which channels are */
		/* present in stream  */
		GUID SubFormat;
	};

	struct BITMAPINFOHEADER
	{
		DWORD biSize;
		LONG biWidth;
		LONG biHeight;
		WORD biPlanes;
		WORD biBitCount;
		DWORD biCompression;
		DWORD biSizeImage;
		LONG biXPelsPerMeter;
		LONG biYPelsPerMeter;
		DWORD biClrUsed;
		DWORD biClrImportant;
	};

	struct BITMAPFILEHEADER
	{
		WORD bfType;
		DWORD bfSize;
		WORD bfReserved1;
		WORD bfReserved2;
		DWORD bfOffBits;
	};
}

#pragma pack ( pop )

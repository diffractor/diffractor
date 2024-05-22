/*****************************************************************************/
// Copyright 2006-2022 Adobe Systems Incorporated
// All Rights Reserved.
//
// NOTICE:	Adobe permits you to use, modify, and distribute this file in
// accordance with the terms of the Adobe license agreement accompanying it.
/*****************************************************************************/

#include "dng_jxl.h"

/*****************************************************************************/

// libjxl headers.

#include "jxl/encode_cxx.h"
#include "jxl/decode_cxx.h"
#include "jxl/resizable_parallel_runner_cxx.h"

/*****************************************************************************/

#include "dng_abort_sniffer.h"
#include "dng_area_task.h"
#include "dng_bmff.h"
#include "dng_color_space.h"
#include "dng_file_stream.h"
#include "dng_globals.h"
#include "dng_host.h"
#include "dng_image.h"
#include "dng_info.h"
#include "dng_memory_stream.h"
#include "dng_negative.h"
#include "dng_parse_utils.h"
#include "dng_pixel_buffer.h"
#include "dng_tag_codes.h"
#include "dng_utils.h"
#include "dng_xmp.h"
#include "dng_xy_coord.h"

#include <atomic>
#include <memory>

/*****************************************************************************/

#define qLogJXL (qDNGValidate && 0)

/*****************************************************************************/

class jxl_memory_block
	{
		
	public:

		dng_memory_allocator &fAllocator;

		void *fPhysicalBuffer = nullptr;

		void *fLogicalBuffer = nullptr;

		// fBlock is used for allocations up to 2 GB.

		AutoPtr<dng_memory_block> fBlock;

		// dng_memory_allocator::Malloc function is used for allocations above
		// 2 GB.

		bool fUseMalloc = false;

	public:

		jxl_memory_block (dng_memory_allocator &allocator,
			 const uint64 bytesNeeded)
			:	fAllocator (allocator)
			{

			if (bytesNeeded > uint64 (0x80000000))
				fUseMalloc = true;

			if (fUseMalloc)
				{

				// See dng_memory_block::PhysicalSize.

				fPhysicalBuffer = allocator.Malloc (160u + bytesNeeded);

				fLogicalBuffer = (void *) DNG_ALIGN_SIMD (fPhysicalBuffer);

				}

			else
				{

				fBlock.Reset (allocator.Allocate (uint32 (bytesNeeded)));

				fPhysicalBuffer = fBlock->Buffer ();

				fLogicalBuffer = fPhysicalBuffer;

				}

			}

		~jxl_memory_block ()
			{

			if (fUseMalloc)
				fAllocator.Free (fPhysicalBuffer);

			fPhysicalBuffer = nullptr;

			fLogicalBuffer = nullptr;

			}

		void * Buffer ()
			{
			return fLogicalBuffer;
			}
		
	};

/*****************************************************************************/

class dng_jxl_decoder_callback_data
	{

	public:
		
		dng_pixel_buffer fTemplateBuffer;

		dng_pixel_buffer fWholeBuffer;

		AutoPtr<jxl_memory_block> fBlock;		

		AutoPtr<dng_image> fImage;			 // 1 or 3 planes

		AutoPtr<dng_image> fAlphaMask;		 // 1 plane (or empty if no alpha)

		bool fAlphaPremultiplied = false;

	};

/*****************************************************************************/

static void dng_jxl_decoder_callback_func (void* opaque,
										  size_t x,
										  size_t y,
										  size_t num_pixels,
										  const void* pixels)
	{

	#if qLogJXL && 0
	
	printf ("dng_jxl_decoder_callback_func: %u, %u, %u\n",
			(unsigned) x,
			(unsigned) y,
			(unsigned) num_pixels);
	
	#endif

	// The decoder prepares a scanline or partial scanline of pixels for us to
	// copy elsewhere.
	
	auto *cbData = (dng_jxl_decoder_callback_data *) opaque;

	auto buffer = cbData->fTemplateBuffer;

	buffer.fArea.t = (int32) y;
	buffer.fArea.b = (int32) (y + 1);
	buffer.fArea.l = (int32) x;
	buffer.fArea.r = (int32) (x + num_pixels);

	buffer.fData = (void *) pixels;

	#if 0

	// This is conceptually the right thing to do, but it's very inefficient.
	// Overall decode time in the functional/jxl/benchmark test balloons from
	// from 50 ms to 200 ms on my 10-core x64 iMac Pro. The issue is that the
	// decoder produces 1 (partial) scanline at a time, which results in two
	// problems: (1) we're issuing lots and lots of dng_image::Put calls, which
	// is in efficient because each Put needs to do at least 1 and often 2
	// mutex locks to access the relevant tiles, and (2) a scanline (very
	// wide, minimal height) is a poor match for tiles in dng_image, which are
	// roughly square by design.
	
	cbData->fImage->Put (buffer);

	#else

	// Instead, copy to a temp buffer that is in dng_image-friendly planar
	// format. This copy performs the chunky-to-planar swizzle and hence the
	// swizzle overhead is covered by multi-threaded support.

	cbData->fWholeBuffer.CopyArea (buffer,
								   buffer.fArea,
								   0,		 // src plane
								   0,		 // dst plane
								   buffer.fPlanes);

	#endif
	
	}

/*****************************************************************************/

class dng_jxl_parallel_runner_data
	{

	public:
		
		dng_host *fHost = nullptr;

		dng_atomic_error_code fErrorCode { dng_error_none };
		
	};

/*****************************************************************************/

static void CheckResult (JxlDecoderStatus status,
						 const char *msg,
						 const dng_jxl_parallel_runner_data *data,
						 const dng_error_code defaultErrorCode = dng_error_jxl_decoder)
	{
	
	if (status != JXL_DEC_SUCCESS)
		{
		
		#if qLogJXL
		
		printf ("JXL encoder (%s) error with status %d",
				msg,
				(int) status);

		#endif

		// Make a distinction between user cancellation and actual decoder
		// errors.

		if (data)
			{

			dng_error_code code = data->fErrorCode;

			if (code != dng_error_none)
				{

				if (code == dng_error_user_canceled)
					{
					Throw_dng_error (code, "JXL decoder - user cancelled", msg);
					}

				else
					{
					Throw_dng_error (code, "JXL decoder", msg);
					}
				
				}
			
			}

		Throw_dng_error (defaultErrorCode, "JXL decoder", msg);
		
		}
	
	}

/*****************************************************************************/

static void CheckResult (JxlEncoderStatus status,
						 const char *msg,
						 const dng_jxl_parallel_runner_data *data,
						 const dng_error_code defaultErrorCode = dng_error_jxl_encoder)
	{
	
	if (status != JXL_ENC_SUCCESS)
		{

		#if qLogJXL
		
		printf ("JXL encoder (%s) error with status %d",
				msg,
				(int) status);

		#endif

		// Make a distinction between user cancellation and actual encoder
		// errors.

		if (data)
			{

			dng_error_code code = data->fErrorCode;

			if (code != dng_error_none)
				{

				if (code == dng_error_user_canceled)
					{
					Throw_dng_error (code, "JXL encoder - user cancelled", msg);
					}

				else
					{
					Throw_dng_error (code, "JXL encoder", msg);
					}
				
				}
			
			}

		Throw_dng_error (defaultErrorCode, "JXL encoder", msg);
		
		}
	
	}

/*****************************************************************************/

static void * dng_jxl_alloc (void *opaque, size_t size)
	{

	#if 0

	(void) opaque;

	return malloc (size);

	#else
	
	auto allocator = (dng_memory_allocator *) opaque;

	return allocator->Malloc (size);

	#endif
	
	}

/*****************************************************************************/

static void dng_jxl_free (void *opaque, void *addr)
	{

	#if 0

	(void) opaque;

	free (addr);

	#else
	
	auto allocator = (dng_memory_allocator *) opaque;

	return allocator->Free (addr);

	#endif
	
	}

/*****************************************************************************/

static JxlParallelRetCode dng_jxl_parallel_runner (void* runner_opaque, // dng_host 
												   void* jpegxl_opaque,
												   JxlParallelRunInit init,
												   JxlParallelRunFunction func,
												   uint32_t start_range,
												   uint32_t end_range)
	{

	auto *data = (dng_jxl_parallel_runner_data *) runner_opaque;

	const size_t maxThreadCount = (size_t) data->fHost->PerformAreaTaskThreads ();

	JxlParallelRetCode init_ret = (*init) (jpegxl_opaque,
										   maxThreadCount);

	if (init_ret != 0)
		return init_ret;

	auto &result = data->fErrorCode;
	
	try
		{

		dng_range_parallel_task::Do
			(*data->fHost,
			 dng_range_parallel_task::info ((int32) start_range,
											(int32) end_range,
											1,	 // min indices per thread
											(uint32) maxThreadCount),
			 "dng_jxl_parallel_runner_task",
			 [func, jpegxl_opaque] (const dng_range_parallel_task::range &ra)
			 {

			 dng_abort_sniffer::SniffForAbort (ra.fSniffer);

			 for (int32 i = ra.fBegin; i < ra.fEnd; i++)
				 {

				 (*func) (jpegxl_opaque, i, (size_t) ra.fThreadIndex);

				 }

			 });

		// Success.

		return 0;

		}

	// If something bad happened, at least remember the error code so that we
	// can report it later.

	catch (const dng_exception &except)
		{
		
		dng_error_code codeError = except.ErrorCode ();
		dng_error_code codeNormal = dng_error_none;

		(void) result.compare_exchange_strong (codeNormal,
											   codeError);		
		
		}

	catch (...)
		{
		
		dng_error_code codeError = dng_error_unknown;
		dng_error_code codeNormal = dng_error_none;

		(void) result.compare_exchange_strong (codeNormal,
											   codeError);		
		
		}

	// Then tell libjxl that an error occurred, so it can stop running.

	return JXL_PARALLEL_RET_RUNNER_ERROR;
	
	}

/*****************************************************************************/

class dng_jxl_io_buffer
	{

	public:

		AutoPtr<dng_memory_block> fBlock;

		const size_t fMaxBufferSize;

		const size_t fChunkSize;

		size_t fDataSize = 0;

		size_t fIndex = 0;					 // start of block

	public:

		dng_jxl_io_buffer (dng_memory_allocator &allocator,
						   size_t maxBufferSize,
						   size_t chunkSize)

			:	fMaxBufferSize (maxBufferSize)
			,	fChunkSize (chunkSize)

			{

			DNG_REQUIRE (fMaxBufferSize > 2 * fChunkSize,
						 "invalid dng_jxl_io_buffer config");

			fBlock.Reset (allocator.Allocate ((uint32) maxBufferSize));

			}

		uint8 * Ptr ()
			{
			return fBlock->Buffer_uint8 () + fIndex;
			}

		size_t DataSize () const
			{
			return fDataSize;
			}

		void Reset ()
			{
			fDataSize = 0;
			fIndex    = 0;
			}

		void ReadChunk (dng_stream &stream,
						size_t remaining)
			{

			size_t bytes_remaining_in_stream =
				(size_t) (stream.Length () - stream.Position ());

			if (bytes_remaining_in_stream == 0)
				{
				#if qLogJXL
				printf ("dng_jxl_io_buffer -- EOF reached in ReadChunk");
				#endif
				ThrowEndOfFile ("dng_jxl_io_buffer");
				}

			// This is the number of bytes we're going to try to read from the
			// stream. It cannot be more than the number of bytes available in
			// the stream.
			
			size_t next_read_size = std::min (fChunkSize,
											  bytes_remaining_in_stream);

			// This is the number of bytes that jxl previously consumed from
			// our buffer.
			
			size_t prev_read_size = fDataSize - remaining;

			// Update our pointer past the previously-consumed bytes.

			fIndex += prev_read_size;

			// If reading the next chunk would spill beyond the end of our
			// buffer, then reset to the beginning of our buffer. We need to
			// move the remaining data. If our internal buffer is much larger
			// than the chunk size, then this will be relatively rare.

			if (fIndex + next_read_size > fMaxBufferSize)
				{

				memmove (fBlock->Buffer (),
						 fBlock->Buffer_uint8 () + fIndex,
						 remaining);
						 
				fIndex = 0;
				
				}

			// Read from the stream.

			auto ptr = Ptr () + remaining;
			
			stream.Get (ptr, (uint32) next_read_size);

			// Update the amount of data in our buffer.

			fDataSize = remaining + next_read_size;

			}
		
	};

/*****************************************************************************/

static void EnsureUseBoxes (JxlEncoder *enc,
							bool &once_flag)
	{
	
	if (!once_flag)
		{
		
		once_flag = true;

		CheckResult (JxlEncoderUseBoxes (enc),
					 "JxlEncoderUseBoxes",
					 nullptr);
		
		}
	
	}

/*****************************************************************************/

// Move to operator== ?

static bool SamePixelBufferGeometry (const dng_pixel_buffer &a,
									 const dng_pixel_buffer &b)
	{
	
	if (a.fArea		 != b.fArea)
		return false;

	if (a.fPlane	 != b.fPlane ||
		a.fPlanes	 != b.fPlanes)
		return false;
		
	if (a.fRowStep	 != b.fRowStep	 ||
		a.fColStep	 != b.fColStep	 ||
		a.fPlaneStep != b.fPlaneStep)
		return false;
		
	if (a.fPixelType != b.fPixelType   ||
		a.fPixelSize != b.fPixelSize)
		return false;

	// Ignore the fData and fDirty fields.

	return true;
	
	}

/*****************************************************************************/

static void EncodeJXL (dng_host &host,
					   dng_stream &stream,
					   const dng_pixel_buffer &inBuffer,
					   const dng_jxl_encode_settings &settings,
					   const bool useContainer,
					   const dng_jxl_color_space_info &colorSpaceInfo,
					   const dng_metadata *metadata,
					   const bool includeExif,
					   const bool includeXMP,
					   const bool includeIPTC,
					   const dng_bmff_box_list *additionalBoxes)
	{

	#if qDNGValidate && 0
	dng_timer timerOuter ("EncodeJXL");
	#endif

	dng_pixel_buffer buffer = inBuffer;

	const bool isLossless = (settings.Distance () <= 0.0f);

	uint32 &srcPixelType = buffer.fPixelType;

	DNG_REQUIRE (srcPixelType == ttByte		 ||
				 srcPixelType == ttShort	 ||
				 srcPixelType == ttHalfFloat ||
				 srcPixelType == ttFloat,
				 "Unsupported srcPixelType in EncodeJXL");

	if (srcPixelType == ttByte)
		DNG_REQUIRE (buffer.fPixelSize == 1,
					 "unexpected ttByte pixel size");

	else if (srcPixelType == ttShort)
		DNG_REQUIRE (buffer.fPixelSize == 2,
					 "unexpected ttShort pixel size");

	else if (srcPixelType == ttHalfFloat)
		DNG_REQUIRE (buffer.fPixelSize == 2,
					 "unexpected ttHalfFloat pixel size");

	else if (srcPixelType == ttFloat)
		{
		
		DNG_REQUIRE (buffer.fPixelSize == 2 ||
					 buffer.fPixelSize == 4,
					 "unexpected ttFlaot pixel size");

		if (buffer.fPixelSize == 2)
			srcPixelType = ttHalfFloat;

		}

	//printf ("srcPixelType: %u\n", srcPixelType);
	//printf ("effort: %u\n", unsigned (settings.Effort ()));

	const uint32 planes = buffer.Planes ();

	DNG_REQUIRE (planes == 1 ||				 // monochrome
				 planes == 2 ||				 // monochrome + alpha
				 planes == 3 ||				 // RGB
				 planes == 4,				 // RGB + alpha
				 "Unsupported plane count in EncodeJXL");

	// Hook into our memory allocator.

	JxlMemoryManager memManager;

	memManager.opaque = (void *) &host.Allocator ();
	memManager.alloc  = dng_jxl_alloc;
	memManager.free   = dng_jxl_free;

	// Make encoder.

	auto encoder = JxlEncoderMake (&memManager);

	auto enc = encoder.get ();

	// Hook into our thread pool.

	dng_jxl_parallel_runner_data parallelData;

	parallelData.fHost = &host;

	if (!settings.UseSingleThread ())
		{

		#if qLogJXL
		printf ("Encoding using multiple threads\n");
		#endif

		#if 0

		auto nativeRunner = JxlResizableParallelRunnerMake (nullptr);

		const size_t maxThreadCount = (size_t) host.PerformAreaTaskThreads ();

		JxlResizableParallelRunnerSetThreads (nativeRunner.get (),
											  maxThreadCount);

		CheckResult (JxlEncoderSetParallelRunner (enc,
												  JxlResizableParallelRunner,
												  nativeRunner.get ()),
					 "JxlDecoderSetParallelRunner",
					 &parallelData);

		(void) dng_jxl_parallel_runner;

		#else

		CheckResult (JxlEncoderSetParallelRunner (enc,
												  dng_jxl_parallel_runner,
												  (void *) &parallelData),
					 "JxlEncoderSetParallelRunner",
					 &parallelData);

		#endif

		}

	#if qLogJXL

	else
		{
		
		printf ("Encoding using 1 thread\n");
		
		}

	#endif

	// Are we going to use a container?
	
	CheckResult
		(JxlEncoderUseContainer (enc,
								 useContainer ? JXL_TRUE : JXL_FALSE),
		 "JxlEncoderUseContainer",
		 &parallelData);

	#if qLogJXL

	if (!useContainer)
		{
		
		if (metadata && (includeExif || includeXMP || includeIPTC))
			{
			
			printf ("EncodeJXL called with useContainer==false"
					" -- ignoring metadata");
			
			}
		
		}

	#endif	// qLogJXL

	// Add metadata if needed.

	if (metadata && useContainer)
		{

		bool useBoxesOnceFlag = false;

		bool didAddBox = false;
		
		// EXIF.

		if (includeExif)
			{

			const dng_resolution *resolution = nullptr;

			AutoPtr<dng_memory_block> exifData
				(metadata->BuildExifBlock (host.Allocator (),
										   resolution,
										   false,
										   nullptr,
										   kNumLeadingZeroBytesForEXIF));

			uint32 exifSize = exifData->LogicalSize ();

			if (exifSize)
				{

				JxlBoxType type = { 'E', 'x', 'i', 'f' };

				JXL_BOOL compressBox = JXL_FALSE;

				EnsureUseBoxes (enc, useBoxesOnceFlag);

				CheckResult (JxlEncoderAddBox (enc,
											   type,
											   exifData->Buffer_uint8 (),
											   (size_t) exifData->LogicalSize (),
											   compressBox),
							 "JxlEncoderAddBox-Exif",
							 &parallelData);

				didAddBox = true;

				}

			} // exif
			
		// XMP.

		if (includeXMP && metadata->GetXMP ())
			{

			// TODO(erichan): Serialize routine has a forJPEG parameter. Does
			// it make sense to use this for JXL? Hmm.

			AutoPtr<dng_memory_block> xmpBlock
				(metadata->GetXMP ()->Serialize (true));

			uint32 xmpSize = xmpBlock->LogicalSize ();

			if (xmpSize > 0)
				{

				JxlBoxType type = { 'x', 'm', 'l', ' ' };

				// For now, leave XMP block uncompressed for broader
				// compatibility with other tools (e.g., exiftool) which
				// currently do not support compressed XMP. -erichan 2023-8-18

				JXL_BOOL compressBox = JXL_FALSE;

				EnsureUseBoxes (enc, useBoxesOnceFlag);

				CheckResult (JxlEncoderAddBox (enc,
											   type,
											   xmpBlock->Buffer_uint8 (),
											   (size_t) xmpSize,
											   compressBox),
							 "JxlEncoderAddBox-XMP",
							 &parallelData);

				didAddBox = true;

				}

			} // xmp

		// IPTC.

		if (includeIPTC)
			{

			auto iptcData = metadata->IPTCData   ();
			auto iptcSize = metadata->IPTCLength ();

			if (iptcData && iptcSize)
				{

				JxlBoxType type = { 'x', 'm', 'l', ' ' };

				JXL_BOOL compressBox = JXL_FALSE;

				EnsureUseBoxes (enc, useBoxesOnceFlag);

				CheckResult (JxlEncoderAddBox (enc,
											   type,
											   (const uint8 *) iptcData,
											   (size_t) iptcSize,
											   compressBox),
							 "JxlEncoderAddBox-IPTC",
							 &parallelData);

				didAddBox = true;

				}

			}

		// Write additional boxes.

		if (additionalBoxes)
			{
			
			for (const auto &box : *additionalBoxes)
				{
				
				if (!box)
					continue;

				if (box->fName.Length () != 4)
					continue;

				if (!box->fContent)
					continue;

				const char *name = box->fName.Get ();

				JxlBoxType type =
					{
					name [0],
					name [1],
					name [2],
					name [3]
					};
				
				JXL_BOOL compressBox = JXL_FALSE;

				EnsureUseBoxes (enc, useBoxesOnceFlag);

				CheckResult (JxlEncoderAddBox (enc,
											   type,
											   (const uint8 *) box->fContent->Buffer (),
											   (size_t) box->fContent->LogicalSize (),
											   compressBox),
							 "JxlEncoderAddBox-extra",
							 &parallelData);

				didAddBox = true;

				}
			
			}

		// Tell the encoder there will be no more boxes added with
		// JxlEncoderAddBox.

		if (didAddBox)
			{
			
			JxlEncoderCloseBoxes (enc);
			
			}

		} // metadata and container

	// Add color profile info.

	bool useJXLColorEncoding = false;
	
	JxlColorEncoding colorEncoding;

	if (colorSpaceInfo.fJxlColorEncoding.Get ())
		{
		
		// Provide color tag as special jxl color encoding.
		
		colorEncoding = *colorSpaceInfo.fJxlColorEncoding;

		useJXLColorEncoding = true;
		
		}

	else if (colorSpaceInfo.fICCProfile.Get ())
		{
		
		// Use ICC profile.
		
		}

	else
		{
		
		// Fall back to assuming sRGB.

		memset (&colorEncoding, 0, sizeof (colorEncoding));

		colorEncoding.color_space		= JXL_COLOR_SPACE_RGB;
		colorEncoding.white_point		= JXL_WHITE_POINT_D65;
		colorEncoding.primaries			= JXL_PRIMARIES_SRGB;
		colorEncoding.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;

		useJXLColorEncoding = true;
		
		}

	// Set basic info.

	const uint32 bitsPerSample = buffer.fPixelSize * 8;

		{

		JxlBasicInfo info;

		JxlEncoderInitBasicInfo (&info);

		info.have_container		   = useContainer;

		info.xsize				   = buffer.fArea.W ();
		info.ysize				   = buffer.fArea.H ();

		info.bits_per_sample       = bitsPerSample;

		// Set exponent_bits_per_sample for float types.

		if (srcPixelType == ttFloat)
			info.exponent_bits_per_sample = 8;	 // fp32

		else if (srcPixelType == ttHalfFloat)
			info.exponent_bits_per_sample = 5;	 // fp16

		else
			{
			info.exponent_bits_per_sample = 0;	 // integer
			info.bits_per_sample = Min_uint32 (info.bits_per_sample, 16);
			}

		// TODO(erichan): libjxl 0.7.0 does not yet support bits_per_sample above
		// 24, hmm.


		/*
			{

			// More than 16 bits per sample require codestream level 10.

			printf ("info.bits_per_sample %u\n",
					  info.bits_per_sample);

			CheckResult (JxlEncoderSetCodestreamLevel (enc, 10),
						 "JxlEncoderSetCodestreamLevel");

			}
		*/

		// If using lossless, then data must be in original space.

		if (isLossless)
			info.uses_original_profile = JXL_TRUE;

		// With lossy, we have an option. For built-in spaces that we can
		// represent exactly using the JxlColorEncoding, prefer xyb since the
		// files tend to be smaller.
		//
		// However, if the caller requested to use the original encoding, then
		// we'll do that.

		else if (settings.UseOriginalColorEncoding ())
			info.uses_original_profile = JXL_TRUE;

		else
			info.uses_original_profile = useJXLColorEncoding ? JXL_FALSE : JXL_TRUE;

		// Deal with alpha.

		const bool hasAlpha = (planes == 2 ||
							   planes == 4);

		if (hasAlpha)
			{

			info.num_extra_channels = 1;

			info.alpha_bits = bitsPerSample;

			info.num_color_channels = planes - 1;

			}

		else
			{

			info.num_extra_channels = 0;

			info.alpha_bits = 0;

			info.num_color_channels = planes;

			}

		// TODO(erichan):
		#if 0
		info.have_preview = JXL_TRUE;
		info.preview.xsize = 200;
		info.preview.ysize = 200;
		#endif

		// For now always assume intrinsic size matches main size.
		info.intrinsic_xsize	   = info.xsize;
		info.intrinsic_ysize	   = info.ysize;

		info.intensity_target = float (colorSpaceInfo.fIntensityTargetNits);

		CheckResult (JxlEncoderSetBasicInfo (enc, &info),
					 "JxlEncoderSetBasicInfo",
					 &parallelData);

		}

	// Set color encoding or profile info after setting the basic info above
	// (this order of operations required by libjxl circa July 2022).

	if (useJXLColorEncoding)
		{
			
		CheckResult (JxlEncoderSetColorEncoding (enc, &colorEncoding),
					 "JxlEncoderSetColorEncoding",
					 &parallelData);

		}

	else
		{

		DNG_REQUIRE (colorSpaceInfo.fICCProfile.Get (),
					 "invalid icc profile");

		// Provide color tag as icc color profile.

		const void *profileData = colorSpaceInfo.fICCProfile->Buffer ();
		uint32		profileSize = colorSpaceInfo.fICCProfile->LogicalSize ();

		if (profileData && profileSize)
			{

			CheckResult (JxlEncoderSetICCProfile (enc,
												  (const uint8 *) profileData,
												  (size_t) profileSize),
						 "JxlEncoderSetICCProfile",
						 &parallelData);

			}

		else
			{

			#if qLogJXL
			printf ("Writing untagged JXL file");
			#endif
			
			}
		
		}

	// Create settings for the frame.

	auto frameSettings = JxlEncoderFrameSettingsCreate (enc, NULL);

	// Set quality.

	if (isLossless)
		{

		// Use lossless.

		#if qLogJXL
		printf ("Using lossless encoding\n");
		#endif

		CheckResult (JxlEncoderSetFrameLossless (frameSettings,
												 JXL_TRUE),
					 "JxlEncoderSetFrameLossless",
					 &parallelData);

		}

	else
		{

		// Use lossy.

		const real32 distance = settings.Distance ();

		#if qLogJXL
		printf ("using lossy encoding with distance %.3f\n",
				distance);
		#endif

		CheckResult (JxlEncoderSetFrameDistance (frameSettings,
												 distance),
					 "JxlEncoderSetFrameDistance",
					 &parallelData);

		}

	// Set effort.

	CheckResult
		(JxlEncoderFrameSettingsSetOption (frameSettings,	
										   JXL_ENC_FRAME_SETTING_EFFORT,
										   (int) settings.Effort ()),
		 "JxlEncoderFrameSettingsSetOption",
		 &parallelData);

	// Set various parameters that affect the lossy case.

	// Note that as of libjxl 0.7.0 (2022-9-21), enabling the Gaborish and
	// Edge Preserving Filter params in the lossless case will cause the
	// result not to be lossless!

	if (!isLossless)
		{

		// Set decoding speed.

		CheckResult
			(JxlEncoderFrameSettingsSetOption (frameSettings,	
											   JXL_ENC_FRAME_SETTING_DECODING_SPEED,
											   (int) settings.DecodeSpeed ()),
			 "JxlEncoderFrameSettingsSetOption",
			 &parallelData);

		// Disable patches. I don't think this matters because it's only supposed
		// to affect what happens between frames.

		CheckResult
			(JxlEncoderFrameSettingsSetOption (frameSettings,	
											   JXL_ENC_FRAME_SETTING_PATCHES,
											   0),
			 "JxlEncoderFrameSettingsSetOption-Patches",
			 &parallelData);

		if (settings.Distance () > 0.02f)
			{

			// Enable gaborish. This helps to reduce blockiness.

			CheckResult
				(JxlEncoderFrameSettingsSetOption (frameSettings,	
												   JXL_ENC_FRAME_SETTING_GABORISH,
												   1),
				 "JxlEncoderFrameSettingsSetOption-Gaborish",
				 &parallelData);

			// Edge preserving filter. This helps to reduce artifacts along strong
			// contrast edges (mountain against sky).

			CheckResult
				(JxlEncoderFrameSettingsSetOption (frameSettings,	
												   JXL_ENC_FRAME_SETTING_EPF,
												   3),
				 "JxlEncoderFrameSettingsSetOption-EPF",
				 &parallelData);

			}

		// TODO(erichan): customize advanced settings?

		// ...

		} // !isLossless

	// Set color encoding.

	// TODO(erichan): detect special case where data is already linear sRGB

	// TODO(erichan): detect special case where data is already regular sRGB

	// TODO(erichan): For now it seems the encoder implementation requires the
	// entire image to be allocated at once.

	JxlPixelFormat pixelFormat;

	memset (&pixelFormat, 0, sizeof (pixelFormat));

	pixelFormat.num_channels = planes;

	switch (srcPixelType)
		{
		
		case ttByte:
			pixelFormat.data_type = JXL_TYPE_UINT8;
			break;
			
		case ttShort:
			pixelFormat.data_type = JXL_TYPE_UINT16;
			break;
			
		case ttHalfFloat:
			pixelFormat.data_type = JXL_TYPE_FLOAT16;
			break;
			
		case ttFloat:
			pixelFormat.data_type = JXL_TYPE_FLOAT;
			break;
			
		default:
			{
			#if qLogJXL
			printf ("unexpected srcPixelType");
			#endif
			ThrowProgramError ("unexpected srcPixelType");
			break;
			}
		
		}

	// Set up pixel buffer to hold data to hand off to encoder. As of version
	// 0.7.0 libjxl requires starting from a single whole-image buffer
	// (instead of reading chunks at a time). This is a chunky (row-col-plane
	// interleaved) buffer.

	dng_pixel_buffer pixelBuffer = buffer;

	pixelBuffer.fPlaneStep = 1;
	pixelBuffer.fColStep   = (int32) planes;
	pixelBuffer.fRowStep   = (int32) planes * (int32) pixelBuffer.fArea.W ();

	const uint64 bytesNeeded = (uint64 (pixelBuffer.fRowStep) *
								uint64 (pixelBuffer.fArea.H ()) *
								uint64 (pixelBuffer.fPixelSize)); 

	AutoPtr<jxl_memory_block> block;

	// If not already tightly-packed chunky, then allocate a temp buffer and
	// convert to chunky layout.
		
	if (!SamePixelBufferGeometry (pixelBuffer, buffer))
		{

		#if qLogJXL
		printf ("JXL copy step\n");
		#endif

		block.Reset (new jxl_memory_block (host.Allocator (),
										   bytesNeeded));

		pixelBuffer.fData = block->Buffer ();

		dng_copy_buffer_task task (buffer,
								   pixelBuffer);

		host.PerformAreaTask (task,
							  pixelBuffer.fArea);

		}

	// TODO(erichan): It seems that preview frames are not yet supported in
	// libjxl 0.7.0, so turn this off for now.

	#if 0

	dng_pixel_buffer previewPixelBuffer;

	previewPixelBuffer.fArea = dng_rect (200, 200); // TODO(erichan): 

	previewPixelBuffer.fPlanes = planes;

	previewPixelBuffer.fPlaneStep = 1;
	previewPixelBuffer.fColStep   = (int32) planes;
	previewPixelBuffer.fRowStep   = (int32) planes * (int32) previewPixelBuffer.fArea.W ();

	previewPixelBuffer.fPixelType = srcPixelType;
	previewPixelBuffer.fPixelSize = TagTypeSize (previewPixelBuffer.fPixelType);

	uint32 previewBytesNeeded = (previewPixelBuffer.fRowStep *
								 previewPixelBuffer.fArea.H () *
								 previewPixelBuffer.fPixelSize); 

	AutoPtr<dng_memory_block> previewBlock (host.Allocate (previewBytesNeeded));

	previewPixelBuffer.fData = previewBlock->Buffer ();

	// Try preview frame?

	auto previewFrameSettings = JxlEncoderFrameSettingsCreate (enc,
															   frameSettings);

	CheckResult
		(JxlEncoderAddImageFrame (previewFrameSettings,
								  &pixelFormat,
								  previewPixelBuffer.fData,
								  previewBytesNeeded),
		 "JxlEncoderAddImageFrame-preview");

	#endif	// disabled
	
	// Setting the frame name is optional.
	// 
	// In libjxl 0.8.0, setting a non-empty name prevents the fast lossless
	// encoder from being used.

	#if 0
	
	CheckResult (JxlEncoderSetFrameName (frameSettings,
										 "main"),
				 "JxlEncoderSetFrameName",
				 &parallelData);

	#endif

	// Add the main image.

	CheckResult
		(JxlEncoderAddImageFrame (frameSettings,
								  &pixelFormat,
								  pixelBuffer.fData,
								  bytesNeeded),
		 "JxlEncoderAddImageFrame-main",
		 &parallelData);

	// Nothing more to encode.

	JxlEncoderCloseInput (enc);

	// Make temp buffer.

	const uint32 tempSize = 64 * 1024;

	AutoPtr<dng_memory_block> tempBlock (host.Allocate (tempSize));
	 
	uint8 *outBuffer = tempBlock->Buffer_uint8 ();

	size_t outAvailableBytes = (size_t) tempSize;

	uint8 *outBufferPtr = outBuffer;

	// Main encode loop.
	
	for (;;)
		{
		
		JxlEncoderStatus status = JxlEncoderProcessOutput (enc,
														   &outBufferPtr,
														   &outAvailableBytes);

		// Handle errors.

		if (status == JXL_ENC_ERROR)
			{
			if (parallelData.fErrorCode == dng_error_user_canceled)
				{
				ThrowUserCanceled ();
				}
			#if qLogJXL
			printf ("Unknown jxl encoder error");
			#endif
			ThrowBadFormat ("JXL_ENC_ERROR");
			}

		// Check if we're done.

		else if (status == JXL_ENC_SUCCESS)
			{

			uint32 bytesToFlush = (uint32) (outBufferPtr - outBuffer);

			#if qLogJXL
			printf ("jxl encoder success!! bytes to flush: %u\n",
					bytesToFlush);
			#endif
					  
			stream.Put (outBuffer, bytesToFlush);
			
			break;
			
			}

		// Check if we need to update the output buffer.

		else if (status == JXL_ENC_NEED_MORE_OUTPUT)
			{
			
			uint32 bytesToFlush = (uint32) (outBufferPtr - outBuffer);

			#if qLogJXL
			printf ("--- jxl encoder need more output. bytes to flush: %u\n",
					bytesToFlush);
			#endif
					  
			// Copy encoded data to the stream.

			stream.Put (outBuffer, bytesToFlush);

			// Reset temp buffer.
			
			outBufferPtr = outBuffer;

			outAvailableBytes = tempSize;
			
			}

		else
			{

			#if qLogJXL
			
			printf ("Unexpected jxl encoder status 0x%x\n",
					(unsigned) status);

			#endif

			ThrowNotYetImplemented ("unhandled jxl encoder status");
			
			}
		
		}

	}

/*****************************************************************************/

static void EncodeJXL (dng_host &host,
					   dng_stream &stream,
					   const dng_image &image,
					   const dng_jxl_encode_settings &settings,
					   const bool useContainer,
					   const dng_jxl_color_space_info &colorSpaceInfo,
					   const dng_metadata *metadata,
					   const bool includeExif,
					   const bool includeXMP,
					   const bool includeIPTC,
					   const dng_bmff_box_list *additionalBoxes)
	{

	// Set up pixel buffer to hold data to hand off to encoder. As of version
	// 0.7.0 libjxl requires starting from a single whole-image buffer
	// (instead of reading chunks at a time). This is a chunky (row-col-plane
	// interleaved) buffer.

	const uint32 planes = image.Planes ();

	DNG_REQUIRE (planes == 1 ||				 // monochrome
				 planes == 2 ||				 // monochrome + alpha
				 planes == 3 ||				 // RGB
				 planes == 4,				 // RGB + alpha
				 "Unsupported plane count in EncodeJXL");

	dng_pixel_buffer pixelBuffer;

	pixelBuffer.fArea	   = image.Bounds ();

	pixelBuffer.fPlanes	   = planes;

	pixelBuffer.fPlaneStep = 1;
	pixelBuffer.fColStep   = (int32) planes;
	pixelBuffer.fRowStep   = (int32) planes * (int32) pixelBuffer.fArea.W ();

	pixelBuffer.fPixelType = image.PixelType ();
	pixelBuffer.fPixelSize = TagTypeSize (pixelBuffer.fPixelType);

	const uint64 bytesNeeded = (uint64 (pixelBuffer.fRowStep) *
								uint64 (pixelBuffer.fArea.H ()) *
								uint64 (pixelBuffer.fPixelSize));

	jxl_memory_block block (host.Allocator (),
							bytesNeeded);

	pixelBuffer.fData = block.Buffer ();

	// Doing a direct "dng_image::Get" is easy but slow because it's
	// single-threaded. Using a pipe reduces time on a 10 megapixel image from
	// 40 ms to 8 ms on a 2017 10-core iMac Pro.

	// dng_timer timer2 ("EncodeJXL-image-get");
		
	#if 1
		
	dng_get_buffer_task task (image,
							  pixelBuffer);

	host.PerformAreaTask (task,
						  image.Bounds ());

	#else
		
	image.Get (pixelBuffer);
		
	#endif

	EncodeJXL (host,
			   stream,
			   pixelBuffer,
			   settings,
			   useContainer,
			   colorSpaceInfo,
			   metadata,
			   includeExif,
			   includeXMP,
			   includeIPTC,
			   additionalBoxes);

	}

/*****************************************************************************/

class dng_jxl_box_reader
	{
		
	public:

		dng_host &fHost;

		JxlDecoder *fDec = nullptr;

		bool fHasCurrentBox = false;
		
		JxlBoxType fType;
		
		std::vector<uint8> fData;
		
		size_t fIndex = 0;
		
		dng_info *fInfo = nullptr;

		dng_jxl_decoder &fDecoder;

	public:

		dng_jxl_box_reader (dng_host &host,
							JxlDecoder *dec,
							dng_jxl_decoder &decoder)
			:	fHost (host)
			,	fDec (dec)
			,	fInfo (decoder.fInfo)
			,	fDecoder (decoder)
			{
			}

		void HandleDecBox ()
			{
		
			// Wrap up any previous box.

			CheckFinishBox ();

			// Read box type.

			CheckResult (JxlDecoderGetBoxType (fDec,
											   fType,
											   JXL_TRUE), // decompress
						 "JxlDecoderGetBoxType",
						 nullptr);

			// Read box size.

			uint64_t size;

			CheckResult (JxlDecoderGetBoxSizeRaw (fDec, &size),
						 "JxlDecoderGetBoxSizeRaw",
						 nullptr);

			#if qLogJXL

			printf ("jxl box: type: \"%c%c%c%c\" size: %llu\n",
					fType [0],
					fType [1],
					fType [2],
					fType [3],
					(unsigned long long) size);

			#endif

			// Begin new box type.

			if (fType [0] == 'j' &&
				fType [1] == 'x' &&
				fType [2] == 'l' &&
				fType [3] == 'c')
				{

				// Code stream. Don't need to grab the data ourselves.

				// printf ("found jxlc codestream -- ignoring\n");

				}

			// This check might be a problem for large gain maps.

			#if 1

			else if (size > 4 * 1024 * 1024)
				 {

				 #if qLogJXL
					
				 printf ("jxl: Skipping large box with size %llu",
						 (unsigned long long) size);

				 #endif
					
				 }

			#endif

			else
				{

				// Reset buffer. Re-use any already-allocated memory.

				fIndex = 0;

				if (fData.empty ())
					{
					fData.resize (4096);
					}

				CheckResult (JxlDecoderSetBoxBuffer (fDec,
													 fData.data (),
													 fData.size ()),
							 "JxlDecoderSetBoxBuffer",
							 nullptr);

				fHasCurrentBox = true;

				}

			}

		void HandleNeedMoreOutput ()
			{
		
			size_t remaining = JxlDecoderReleaseBoxBuffer (fDec);

			fIndex = fData.size () - remaining;

			size_t newSize = 2 * fData.size ();

			fData.resize (newSize);

			size_t availableSize = newSize - fIndex;
			
			#if qLogJXL
			printf ("box read output: remain=%llu\n",
					(unsigned long long) remaining);
			#endif

			CheckResult (JxlDecoderSetBoxBuffer (fDec,
												 &fData [fIndex],
												 availableSize),
						 "JxlDecoderSetBoxBuffer",
						 nullptr);
		
			}

		void CheckFinishBox ()
			{

			if (fHasCurrentBox)
				{

				size_t remaining = JxlDecoderReleaseBoxBuffer (fDec);

				fData.resize (fData.size () - remaining);

				fHasCurrentBox = false;

				FinishBox ();

				}

			}

	private:

		// If info provided, then contents of boxes will be written to info.
		// Otherwise, contents of boxes will be ignored.

		void FinishBox ()
			{

			#if qLogJXL

			printf ("finished up box type %c%c%c%c: final byte size %llu\n",
					fType [0],
					fType [1],
					fType [2],
					fType [3],
					(unsigned long long) fData.size ());

			#endif

			if (fType [0] == 'E' &&
				fType [1] == 'x' &&
				fType [2] == 'i' &&
				fType [3] == 'f')
				{

				fDecoder.ProcessExifBox (fHost,
										 fData);

				} // exif

			else if ((fType [0] == 'X' &&
					  fType [1] == 'M' &&
					  fType [2] == 'L' &&
					  fType [3] == ' ') ||
					 
					 (fType [0] == 'x' &&
					  fType [1] == 'm' &&
					  fType [2] == 'l' &&
					  fType [3] == ' '))
					 
				{

				fDecoder.ProcessXMPBox (fHost,
										fData);

				} // xml

			else
				{
				
				// Some other box.

				const char rawTypeName [] =
					{
					fType [0],
					fType [1],
					fType [2],
					fType [3],
					'\0'
					};

				dng_string typeName (rawTypeName);

				fDecoder.ProcessBox (fHost,
									 typeName,
									 fData);
				
				}

			// Developer code.

			#if qDNGValidate && 0

			char buf [256];

			snprintf (buf,
					  256,
					  "%c%c%c%c.txt",
					  fType [0],
					  fType [1],
					  fType [2],
					  fType [3]);

			dng_file_stream outStream (buf, true);

			outStream.Put (fData.data (), (uint32) fData.size ());

			outStream.Flush ();

			#endif	// dev code

			}		
		
	};

/*****************************************************************************/

class dng_put_alpha_buffer_task : public dng_area_task
	{
		
	private:

		const dng_pixel_buffer &fSrc;

		dng_image &fDst;

		int32 fSrcAlphaPlane = 0;

	public:

		dng_put_alpha_buffer_task (const dng_pixel_buffer &buffer,
								   dng_image &image)
			
			:	dng_area_task ("dng_put_alpha_buffer_task")
				
			,	fSrc (buffer)
				
			,	fDst (image)
				
			{
			
			DNG_REQUIRE (buffer.Planes () == 2 ||
						 buffer.Planes () == 4,
						 "Unsupported plane count in dng_stage_put_alpha_buffer");

			fSrcAlphaPlane = int32 (buffer.Planes () - 1);

			}

		dng_rect RepeatingTile1 () const override
			{
			
			return fDst.RepeatingTile ();
			
			}
			
		void Process (uint32 /* threadIndex */,
					  const dng_rect &tile,
					  dng_abort_sniffer * /* sniffer */) override
			{

			dng_pixel_buffer temp = fSrc;

			temp.fData = (void *) fSrc.ConstPixel (tile.t,
												   tile.l,
												   fSrcAlphaPlane);

			temp.fArea = tile;

			temp.fPlane  = 0;
			temp.fPlanes = 1;

			fDst.Put (temp);
		
			}
		
	};

/*****************************************************************************/

dng_jxl_decoder::~dng_jxl_decoder ()
	{
	}

/*****************************************************************************/

void dng_jxl_decoder::Decode (dng_host &host,
							  dng_stream &stream)
	{

	const bool needBoxMeta = fNeedBoxMeta;
	const bool needImage   = fNeedImage;
	
	// Hook into our memory allocator.

	JxlMemoryManager memManager;

	memManager.opaque = (void *) &host.Allocator ();
	memManager.alloc  = dng_jxl_alloc;
	memManager.free   = dng_jxl_free;

	// Remember stream position.

	const uint64 startPosition = stream.Position ();

	// Make decoder.

	auto decoder = JxlDecoderMake (&memManager);

	auto dec = decoder.get ();

	// Preserve orientation as-in-bitstream (do not reorient).

	CheckResult (JxlDecoderSetKeepOrientation (dec, 1),
				 "JxlDecoderSetKeepOrientation",
				 nullptr);

	// Hook into our thread pool.

	dng_jxl_parallel_runner_data parallelData;

	parallelData.fHost = &host;
	
	#if 1

	// This seems slightly faster than using our own runner; this might due to
	// some internal overhead.

	auto nativeRunner = JxlResizableParallelRunnerMake (nullptr);

	const size_t maxThreadCount =
		(size_t) (fUseSingleThread ? 1 : host.PerformAreaTaskThreads ());
	
	JxlResizableParallelRunnerSetThreads (nativeRunner.get (),
										  maxThreadCount);

	CheckResult (JxlDecoderSetParallelRunner (dec,
											  JxlResizableParallelRunner,
											  nativeRunner.get ()),
				 "JxlDecoderSetParallelRunner",
				 &parallelData);
	
	#else

	CheckResult (JxlDecoderSetParallelRunner (dec,
											  dng_jxl_parallel_runner,
											  (void *) &parallelData),
				 "JxlDecoderSetParallelRunner",
				 &parallelData);
	
	#endif

	// Subscribe to some events.

	// We will always retrieve the basic and color encoding info, so start
	// with those events.

	int eventsWanted = (JXL_DEC_BASIC_INFO |
						JXL_DEC_COLOR_ENCODING);

	// If meta is requested, then we need to read boxes.

	if (needBoxMeta)
		{
		eventsWanted = eventsWanted | JXL_DEC_BOX;
		}

	// If image is requested, then subscribe to image & frame events, too.

	// TODO(erichan): deal with preview image & thumbs

	if (needImage)
		{
		eventsWanted = eventsWanted | JXL_DEC_FULL_IMAGE | JXL_DEC_FRAME;
		}
	
	CheckResult (JxlDecoderSubscribeEvents (dec, eventsWanted),
				 "JxlDecoderSubscribeEvents",
				 &parallelData);

	// Decompress boxes.

	CheckResult (JxlDecoderSetDecompressBoxes (dec, JXL_TRUE),
				 "JxlDecoderSetDecompressBoxes",
				 &parallelData);

	// Input buffer. Do we want to customize the chunk sizes?

	// Settings these too small hurts decode perf. At first I tried 128 KB and
	// 16 KB for max and chunk size, respectively.

	dng_jxl_io_buffer inputBuffer (host.Allocator (),
								   17 * 1024 * 1024,  // max
								   8  * 1024 * 1024); // chunk

	JxlBasicInfo basicInfo;

	bool foundBasicInfo = false;

	JxlFrameHeader frameHeader;

	dng_jxl_box_reader boxReader (host, dec, *this);

	dng_jxl_decoder_callback_data cbData;

	// Main decode loop.

	for (;;)
		{

		// The first time, this will output JXL_DEC_NEED_MORE_INPUT because no
		// input is set yet, this is ok since the input is set when handling this
		// event.
		
		JxlDecoderStatus status = JxlDecoderProcessInput (dec);

		// Deal with decoder error event.

		if (status == JXL_DEC_ERROR)
			{
			#if qLogJXL
			printf ("Unknown jxl decoder error");
			#endif
			ThrowBadFormat ("JXL_DEC_ERROR");
			}
		  
		// Deal with success event.

		else if (status == JXL_DEC_SUCCESS)
			{
			
			// Finished all processing.
			
			#if qLogJXL
			printf ("jxl decode success\n");
			#endif

			boxReader.CheckFinishBox ();
		
			break;
		
			}

		// Deal with decoder's request for more input data.

		else if (status == JXL_DEC_NEED_MORE_INPUT)
			{
			
			// The first time there is nothing to release and it returns 0, but that
			// is ok.

			#if qLogJXL
			printf ("--- JXL_DEC_NEED_MORE_INPUT\n");
			#endif
			
			size_t remaining = JxlDecoderReleaseInput (dec);

			inputBuffer.ReadChunk (stream, remaining);

			CheckResult (JxlDecoderSetInput (dec,
											 inputBuffer.Ptr (),
											 inputBuffer.DataSize ()),
						 "JxlDecoderSetInput",
						 &parallelData);

			// Let the decoder now if we've reached the end of the stream.

			if (stream.Position () >= stream.Length ())
				{
				JxlDecoderCloseInput (dec);
				}
			
			}

		// Deal with basic info received from the code stream.
		
		else if (status == JXL_DEC_BASIC_INFO)
			{

			CheckResult (JxlDecoderGetBasicInfo (dec, &basicInfo),
						 "JxlDecoderGetBasicInfo",
						 &parallelData);

			foundBasicInfo = true;

			#if qDNGValidate

			if (needBoxMeta && gVerbose)
				{

				printf ("\nJXL basic info:\n\n");

				printf ("dimensions: %ux%u\n", basicInfo.xsize, basicInfo.ysize);
				printf ("have_container: %d\n", basicInfo.have_container);
				printf ("uses_original_profile: %d\n", basicInfo.uses_original_profile);
				printf ("bits_per_sample: %d\n", basicInfo.bits_per_sample);

				if (basicInfo.exponent_bits_per_sample)
					printf ("float, with exponent_bits_per_sample: %d\n",
						   basicInfo.exponent_bits_per_sample);
				
				if (basicInfo.intensity_target != 255.f ||
					basicInfo.min_nits != 0.f ||
					basicInfo.relative_to_max_display != 0 ||
					basicInfo.relative_to_max_display != 0.f)
					{
					printf ("intensity_target: %f\n", basicInfo.intensity_target);
					printf ("min_nits: %f\n", basicInfo.min_nits);
					printf ("relative_to_max_display: %d\n", basicInfo.relative_to_max_display);
					printf ("linear_below: %f\n", basicInfo.linear_below);
					}
				
				printf ("have_preview: %d\n", basicInfo.have_preview);
				
				if (basicInfo.have_preview)
					{
					printf ("preview xsize: %u\n", basicInfo.preview.xsize);
					printf ("preview ysize: %u\n", basicInfo.preview.ysize);
					}
				
				printf ("have_animation: %d\n", basicInfo.have_animation);
				
				if (basicInfo.have_animation)
					{

					printf ("ticks per second (numerator / denominator): %u / %u\n",
							basicInfo.animation.tps_numerator, basicInfo.animation.tps_denominator);

					printf ("num_loops: %u\n", basicInfo.animation.num_loops);

					printf ("have_timecodes: %d\n", basicInfo.animation.have_timecodes);

					}
				
				printf ("intrinsic xsize: %u\n", basicInfo.intrinsic_xsize);
				printf ("intrinsic ysize: %u\n", basicInfo.intrinsic_ysize);

				const char* const orientation_string[8] =
					{
					"Normal",          "Flipped horizontally",
					"Upside down",     "Flipped vertically",
					"Transposed",      "90 degrees clockwise",
					"Anti-Transposed", "90 degrees counter-clockwise"
					};
				
				if (basicInfo.orientation > 0 && basicInfo.orientation < 9)
					{
					printf ("orientation: %d (%s)\n", basicInfo.orientation,
							orientation_string[basicInfo.orientation - 1]);
					}

				else
					{
					fprintf (stderr, "Invalid orientation\n");
					}
				
				printf ("num_color_channels: %d\n", basicInfo.num_color_channels);
				printf ("num_extra_channels: %d\n", basicInfo.num_extra_channels);

				// Read extra channels.
				
				const char* const ec_type_names [7] =
					{
					"Alpha",
					"Depth",
					"Spot color",
					"Selection mask",
					"K (of CMYK)",
					"CFA (Bayer data)",
					"Thermal"
					};
				
				for (uint32_t i = 0; i < basicInfo.num_extra_channels; i++)
					{

					JxlExtraChannelInfo extra;
					
					CheckResult
						(JxlDecoderGetExtraChannelInfo (dec,
														i,
														&extra),
						 "JxlDecoderGetExtraChannelInfo",
						 nullptr);
					
					printf ("extra channel %u:\n", i);
					
					printf ("  type: %s\n",
							(extra.type < 7 ? ec_type_names [extra.type]
							 : (extra.type == JXL_CHANNEL_OPTIONAL
								? "Unknown but can be ignored"
								: "Unknown, please update your libjxl")));
					
					printf ("  bits_per_sample: %u\n",
							extra.bits_per_sample);
					
					if (extra.exponent_bits_per_sample > 0)
						printf ("  float, with exponent_bits_per_sample: %u\n",
								extra.exponent_bits_per_sample);
					
					if (extra.dim_shift > 0)
						printf ("  dim_shift: %u (upsampled %ux)\n",
								extra.dim_shift,
								1 << extra.dim_shift);
					
					if (extra.name_length)
						{

						std::vector<char> temp (extra.name_length + 1);

						char *name = temp.data ();
						
						CheckResult (JxlDecoderGetExtraChannelName
									 (dec,
									  i,
									  name,
									  extra.name_length + 1),
									 "JxlDecoderGetExtraChannelName",
									 nullptr);

						printf ("  name: %s\n", name);
						
						}
					
					if (extra.type == JXL_CHANNEL_ALPHA)
						printf ("  alpha_premultiplied: %d (%s)\n",
								extra.alpha_premultiplied,
								extra.alpha_premultiplied ? "Premultiplied"
														  : "Non-premultiplied");
					
					if (extra.type == JXL_CHANNEL_SPOT_COLOR)
						printf ("  point_color: (%f, %f, %f) with opacity %f\n",
								extra.spot_color[0],
								extra.spot_color[1],
								extra.spot_color[2],
								extra.spot_color[3]);
					
					if (extra.type == JXL_CHANNEL_CFA)
						printf ("  cfa_channel: %u\n", extra.cfa_channel);
					
					} // for extra channels

				} // verbose

			#endif	// qDNGValidate
			
			} // basic info

		// Read color profile info.

		else if (status == JXL_DEC_COLOR_ENCODING)
			{

			//JxlPixelFormat format = {4, JXL_TYPE_FLOAT, JXL_LITTLE_ENDIAN, 0};

			// TODO(erichan): Does the choice of this format unduly affect the
			// result of JxlDecoderGetColorAsEncodedProfile?
			
			JxlPixelFormat format = { 3, JXL_TYPE_FLOAT, JXL_NATIVE_ENDIAN, 0 };

			#if qDNGValidate
			if (gVerbose)
				printf("\nJXL color profile info:\n");
			#endif

			JxlColorEncoding color_encoding;
			
			if (JXL_DEC_SUCCESS ==
				JxlDecoderGetColorAsEncodedProfile (dec,
													//&format,
													JXL_COLOR_PROFILE_TARGET_ORIGINAL,
													&color_encoding))
				{

				// JXL-specific color encoding.

				// TODO(erichan): factory method?

				fColorSpaceInfo.fJxlColorEncoding.Reset (new JxlColorEncoding);

				*fColorSpaceInfo.fJxlColorEncoding = color_encoding;

				#if qDNGValidate

				if (gVerbose)
					{

					printf ("  format: JPEG XL encoded color profile\n");

					const char* const cs_string [4] =
						{
						"RGB color",
						"Grayscale",
						"XYB",
						"Unknown"
						};

					const char* const wp_string [12] =
						{
						"",
						"D65",
						"Custom",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"E",
						"P3"
						};

					const char* const pr_string [12] =
						{
						"", "sRGB", "Custom", "",
						"", "", "", "",
						"", "Rec.2100", "", "P3"
						};

					const char* const tf_string [19] =
						{
						"", "709", "Unknown", "", "",
						"", "",   "",    "Linear", "",
						"", "",    "",        "sRGB",
						"", "", "PQ", "DCI", "HLG"
						};

					const char* const ri_string [4] =
						{
						"Perceptual",
						"Relative",
						"Saturation",
						"Absolute"
						};

					printf ("  color_space: %d (%s)\n",
							color_encoding.color_space,
							cs_string [color_encoding.color_space]);

					printf ("  white_point: %d (%s)\n",
							color_encoding.white_point,
						   wp_string[color_encoding.white_point]);

					if (color_encoding.white_point == JXL_WHITE_POINT_CUSTOM)
						{

						printf ("  white_point XY: %f %f\n",
								color_encoding.white_point_xy [0],
								color_encoding.white_point_xy [1]);

						}

					if (color_encoding.color_space == JXL_COLOR_SPACE_RGB ||
						color_encoding.color_space == JXL_COLOR_SPACE_UNKNOWN)
						{

						printf ("  primaries: %d (%s)\n",
								color_encoding.primaries,
								pr_string[color_encoding.primaries]);

						if (color_encoding.primaries == JXL_PRIMARIES_CUSTOM)
							{

							printf ("  red primaries XY: %f %f\n",
									color_encoding.primaries_red_xy [0],
									color_encoding.primaries_red_xy [1]);

							printf ("  green primaries XY: %f %f\n",
									color_encoding.primaries_green_xy [0],
									color_encoding.primaries_green_xy [1]);

							printf ("  blue primaries XY: %f %f\n",
									color_encoding.primaries_blue_xy [0],
									color_encoding.primaries_blue_xy [1]);

							}

						}

					if (color_encoding.transfer_function ==
						JXL_TRANSFER_FUNCTION_GAMMA)
						{

						printf ("  transfer_function: gamma: %f\n",
								color_encoding.gamma);

						}

					else
						{

						printf ("  transfer_function: %d (%s)\n",
								color_encoding.transfer_function,
								tf_string [color_encoding.transfer_function]);

						}

					printf ("  rendering_intent: %d (%s)\n",
							color_encoding.rendering_intent,
							ri_string [color_encoding.rendering_intent]);

					}

				#endif	// qDNGValidate

				}
			
			else
				{
				
				// The color info cannot be represented as a JxlColorEncoding,
				// so fetch as ICC profile instead.
				
				size_t profile_size;

				CheckResult (JxlDecoderGetICCProfileSize
							 (dec,
							  //&format,
							  JXL_COLOR_PROFILE_TARGET_ORIGINAL,
							  &profile_size),
							 "JxlDecoderGetICCProfileSize",
							 nullptr);
				
				if (profile_size < 132)
					{

					#if qLogJXL
					
					printf ("ICC profile size %llu too small -- ignoring",
							(unsigned long long) profile_size);

					#endif
					
					continue;
					
					}

				AutoPtr<dng_memory_block> profileBlock
					(host.Allocate ((uint32) profile_size));

				auto *profile = profileBlock->Buffer_uint8 ();
				
				CheckResult (JxlDecoderGetColorAsICCProfile
							 (dec,
							  //&format,
							  JXL_COLOR_PROFILE_TARGET_ORIGINAL,
							  profile,
							  profile_size),
							 "JxlDecoderGetColorAsICCProfile",
							 nullptr);

				fColorSpaceInfo.fICCProfile.Reset (profileBlock.Release ());

				#if qDNGValidate

				if (gVerbose)
					{

					printf ("  format: ICC profile\n");

					printf ("  ICC profile size: %u\n", (uint32) profile_size);

					printf ("  CMM type: \"%.4s\"\n", profile + 4);
					printf ("  color space: \"%.4s\"\n", profile + 16);
					printf ("  rendering intent: %d\n", (int) profile [67]);

					}

				#endif	// qDNGValidate

				}
			
			} // color profile info

		else if (status == JXL_DEC_FRAME)
			{

			CheckResult (JxlDecoderGetFrameHeader (dec, &frameHeader),
						 "JxlDecoderGetFrameHeader",
						 &parallelData);

			#if qDNGValidate

			if (gVerbose)
				{
						 
				printf ("frame:\n");

				if (frameHeader.name_length)
					{

					std::vector<char> temp (frameHeader.name_length + 1);

					char *name = temp.data ();

					CheckResult (JxlDecoderGetFrameName (dec,
														 name,
														 frameHeader.name_length + 1),
								 "JxlDecoderGetFrameName",
								 &parallelData);

					printf ("  name: %s\n", name);

					}

				float ms = (frameHeader.duration * 1000.f *
							basicInfo.animation.tps_denominator /
							basicInfo.animation.tps_numerator);

				if (basicInfo.have_animation)
					{

					printf ("  duration: %u ticks (%f ms)\n",
							frameHeader.duration,
							ms);

					if (basicInfo.animation.have_timecodes)
						printf ("  time code: %X\n",
								frameHeader.timecode);

					}

				if (!frameHeader.name_length &&
					!basicInfo.have_animation)
					{
					printf ("  still frame, unnamed\n");
					}

				} // verbose

			#endif	// qDNGValidate

			}

		// Box event.

		else if (status == JXL_DEC_BOX)
			{
			
			boxReader.HandleDecBox ();
						 
			}

		// Handle request for writing box data.

		else if (status == JXL_DEC_BOX_NEED_MORE_OUTPUT)
			{

			#if qLogJXL
			printf ("JXL_DEC_BOX_NEED_MORE_OUTPUT\n");
			#endif

			boxReader.HandleNeedMoreOutput ();

			}

		// Handle request for writing decoded image data.

		else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
			{

			const uint32 colorPlanes = basicInfo.num_color_channels;

			if (colorPlanes != 1 &&
				colorPlanes != 3)
				{
				#if qLogJXL
				printf ("jxl: unsupported plane count");
				#endif
				ThrowNotYetImplemented ("unsupported plane count");
				}

			uint32 pixelType = ttByte;
			
			if (basicInfo.exponent_bits_per_sample == 0)
				{
				
				// Integer.

				if (basicInfo.bits_per_sample <= 8)
					pixelType = ttByte;

				else
					{
					
					pixelType = ttShort;

					#if qLogJXL
					
					if (basicInfo.bits_per_sample > 16)
						printf ("jxl: bits per sample > 16; truncating to 16");

					#endif

					}

				}

			else
				{

				// TODO(erichan): Do we want to try reading as fp16 if
				// exponent_bits_per_sample is 5? For now, just read as fp32.
				
				// Float.

				pixelType = ttFloat;
				
				}
			
			JxlPixelFormat format;

			// Alpha support determines total # of planes. libjxl supports
			// reading RGBA and Gray + Alpha interleaved.

			// TODO(erichan): Should we support premultipled alpha?

			// For now, only read alpha if we're reading the result into an
			// image object.

			const bool doReadAlpha = ((basicInfo.alpha_bits > 0) &&
									  !fUsePixelBuffer);

			const uint32 totalPlanes = doReadAlpha ? (colorPlanes + 1) : colorPlanes;

			format.num_channels = totalPlanes;

			if (pixelType == ttByte)
				format.data_type = JXL_TYPE_UINT8;				

			else if (pixelType == ttShort)
				format.data_type = JXL_TYPE_UINT16;				

			else
				format.data_type = JXL_TYPE_FLOAT;				

			format.endianness = JXL_NATIVE_ENDIAN;

			format.align = 0;

			#if 1

			// Do incremental path. This seems significantly faster than using
			// the one-image path. I wonder if this has to do with parallelism.

			dng_point size;

			size.h = basicInfo.xsize;
			size.v = basicInfo.ysize;

			// Set up chunky (row-col-plane interleaved) template buffer. 

			auto &buffer	  = cbData.fTemplateBuffer;

			buffer.fArea	  = dng_rect (size);

			buffer.fPlanes	  = totalPlanes;
			  
			buffer.fPlaneStep = 1;
			buffer.fColStep	  = totalPlanes;
			buffer.fRowStep	  = buffer.fColStep * size.h;

			buffer.fPixelType = pixelType;
			buffer.fPixelSize = TagTypeSize (buffer.fPixelType);

			buffer.fData	  = nullptr;

			// Create image to hold output.

			if (!fUsePixelBuffer)
				{

				cbData.fImage.Reset (host.Make_dng_image (dng_rect (size),
														  colorPlanes,
														  pixelType));

				if (doReadAlpha)
					{
					
					cbData.fAlphaMask.Reset (host.Make_dng_image (dng_rect (size),
																  1,
																  pixelType));

					cbData.fAlphaPremultiplied = basicInfo.alpha_premultiplied;
					
					}

				}

			// Create temp planar (row-plane-col interleaved) buffer for the
			// whole image. This is wasteful from a memory point of view but
			// accelerates decoding significantly.

			auto &wholeBuffer = cbData.fWholeBuffer;

			wholeBuffer			   = buffer;

			wholeBuffer.fColStep   = 1;
			wholeBuffer.fPlaneStep = size.h;
			wholeBuffer.fRowStep   = wholeBuffer.fPlaneStep * totalPlanes;

			const uint64 bytesNeeded = (uint64 (wholeBuffer.fRowStep) *
										uint64 (wholeBuffer.fArea.H ()) *
										uint64 (wholeBuffer.fPixelSize));

			cbData.fBlock.Reset (new jxl_memory_block (host.Allocator (),
													   bytesNeeded));
			
			cbData.fWholeBuffer.fData = cbData.fBlock->Buffer ();
			  
			CheckResult
				(JxlDecoderSetImageOutCallback
				 (dec,
				  &format,
				  dng_jxl_decoder_callback_func,
				  &cbData),
				 "JxlDecoderSetImageOutCallback",
				 &parallelData);

			#else

			(void) dng_jxl_decoder_callback_func;

			// Full-image path.

			size_t size;

			CheckResult (JxlDecoderImageOutBufferSize (dec,
													   &format,
													   &size),
						 "JxlDecoderImageOutBufferSize",
						 &parallelData);

			block.Reset (host.Allocate ((uint32) size));

			CheckResult (JxlDecoderSetImageOutBuffer (dec,
													  &format,
													  block->Buffer (),
													  size),
						 "JxlDecoderSetImageOutBuffer",
						 &parallelData);

			#endif	// incremental vs full
			  
			}

		// Event that full image has finished decoding or we're past this
		// point in the code stream.

		else if (status == JXL_DEC_FULL_IMAGE)
			{

			#if qLogJXL
			printf ("--- JXL_DEC_FULL_IMAGE\n");
			#endif

			if (needImage)
				{

				if (cbData.fImage.Get ())
					{
				
					#if 1

					// Multi-threaded put: much faster.

					auto &dstImage = *cbData.fImage;

					dng_put_buffer_task task (cbData.fWholeBuffer,
											  dstImage);

					host.PerformAreaTask (task,
										  dstImage.Bounds ());

					#else

					// Single-threaded put -- expensive.

					cbData.fImage->Put (cbData.fWholeBuffer);

					#endif

					}

				if (cbData.fAlphaMask.Get ())
					{
					
					auto &dstImage = *cbData.fAlphaMask;

					dng_put_alpha_buffer_task task (cbData.fWholeBuffer,
													dstImage);

					host.PerformAreaTask (task,
										  dstImage.Bounds ());
					
					}

				}

			}

		// Deal with unhandled events.

		else
			{

			#if qLogJXL

			printf ("Unexpected jxl decoder status 0x%x\n",
					(unsigned) status);

			#endif

			ThrowBadFormat ("unhandled jxl decoder status");

			}
		
		} // jxl decode loop

	// Finished decode loop.

	// Allocate info structures in case there is no
	// exif block.

	if (foundBasicInfo)
		{

		const auto &src = basicInfo;

		if (fInfo)
			{

			auto &info = *fInfo;

			// Store magic number.

			info.fMagic = tcJXL;

			// Create core objects if not already done.

			if (!info.fExif.Get ())
				{
				info.fExif.Reset (host.Make_dng_exif ());
				}

			if (!info.fShared.Get ())
				{
				info.fShared.Reset (host.Make_dng_shared ());
				}

			if (info.IFDCount () == 0)
				{
				info.fIFD.push_back (host.Make_dng_ifd ());
				}

			// Store some essential info so that the reader can make some
			// early decisions about whether or not to support the file.

			auto &ifd = *info.fIFD [0];

			ifd.fImageWidth        = (uint32) src.xsize;
			ifd.fImageLength       = (uint32) src.ysize;

			ifd.fBitsPerSample [0] = (uint32) src.bits_per_sample;

			ifd.fSamplesPerPixel   = (uint32) src.num_color_channels;
			
			ifd.fOrientation	   = (uint32) src.orientation;

			if (src.exponent_bits_per_sample > 0)
				ifd.fSampleFormat [0] = sfFloatingPoint;

			else
				ifd.fSampleFormat [0] = sfUnsignedInteger;

			// Remember the stream position.
			
			ifd.fTileOffset [0]    = (uint64) startPosition;
			
			}

		// Store basic info.

		fUsesOriginalProfile   = (src.uses_original_profile != JXL_FALSE);

		fMainImageSize.h	   = (int32) src.xsize;
		fMainImageSize.v	   = (int32) src.ysize;

		fMainImagePlanes	   = (uint32) src.num_color_channels;
		
		fBitsPerSample		   = (uint32) src.bits_per_sample;
		fExponentBitsPerSample = (uint32) src.exponent_bits_per_sample;
		
		fNumExtraChannels	   = (uint32) src.num_extra_channels;
		
		fHasPreview			   = (src.have_preview != JXL_FALSE);

		fOrientation.SetTIFF ((uint32) src.orientation);

		// Store image.

		if (fUsePixelBuffer)
			{

			fMainPixelBuffer.Reset
				(new dng_pixel_buffer (cbData.fWholeBuffer));

			DNG_REQUIRE (!cbData.fBlock->fUseMalloc,
						 "Expected dng_memory_block use for pixel buffer JXL decode");
			
			DNG_REQUIRE (cbData.fBlock->fBlock.Get (),
						 "Missing dng_memory_block for pixel buffer JXL decode");
			
			fMainBlock.Reset (cbData.fBlock->fBlock.Release ());

			}

		else
			{
			
			fMainImage.Reset (cbData.fImage.Release ());

			fAlphaMask.Reset (cbData.fAlphaMask.Release ());

			if (fAlphaMask.Get ())
				fAlphaPremultiplied = cbData.fAlphaPremultiplied;

			}

		// TODO(erichan): deal with depth, etc.

		// ....
		
		}
	
	}

/*****************************************************************************/

void dng_jxl_decoder::ProcessExifBox (dng_host &host,
									  const std::vector<uint8> &data)
	{

	// First 4 bytes are a TIFF offset which we currently assume is zero.

	if (fInfo && (data.size () > 4))
		{

		dng_stream exifStream ((&data [0]) + 4,
							   uint32 (data.size () - 4));

		fInfo->Parse (host, exifStream);

		}
	
	}

/*****************************************************************************/

void dng_jxl_decoder::ProcessXMPBox (dng_host &host,
									 const std::vector<uint8> &data)
	{
	
	#if qDNGValidate

	if (gVerbose)
		{

		dng_memory_stream temp (host.Allocator ());

		uint32 count = (uint32) data.size ();

		temp.Put (data.data (), count);

		temp.SetReadPosition (0);

		DumpXMP (temp, count);

		}

	#endif	// qDNGValidate

	if (fInfo)
		{

		try
			{

			dng_xmp xmp (host.Allocator ());

			uint32 count = (uint32) data.size ();

			xmp.Parse (host, data.data (), count);

			auto &block = fInfo->fXMPBlock;

			if (!block.Get ())
				{

				block.Reset (host.Allocate (count));

				memcpy (block->Buffer (),
						data.data (),
						(size_t) count);

				}

			}

		catch (...)
			{

			// Not XMP -- ignore.

			}

		}
	
	}

/*****************************************************************************/

void dng_jxl_decoder::ProcessBox (dng_host & /* host */,
								  const dng_string & /* name */,
								  const std::vector<uint8> & /* data */)
	{
	
	}

/*****************************************************************************/

static bool DoParseJXL (dng_host &host,
						dng_stream &stream,
						dng_info &info)
	{
	
	try
		{

		dng_jxl_decoder decoder;

		decoder.fInfo = &info;

		decoder.fNeedImage = false;

		decoder.Decode (host,
						stream);

		return true;
		
		}

	catch (const dng_exception &except)
		{

		// If we found a decoder error while parsing, then assume this is not
		// a valid JXL file.
		
		if (except.ErrorCode () == dng_error_jxl_decoder)
			{
			return false;
			}

		// For any other type of error (user cancellation, memory full, eof of
		// file, etc.) re-throw.

		// TODO(erichan): do we really want to throw for end of stream/file?

		throw;
		
		}

	return false;
	
	}

/*****************************************************************************/

static bool ParseJXL_Basic (dng_host &host,
							dng_stream &stream,
							dng_info &info)
	{
	
	if (stream.Length () < 2)
		{
		return false;
		}

	const auto pos = stream.Position ();

	// Look for JPEG marker that indicates start of JXL codestream.
		
	if (stream.Get_uint8 () != 0xFF)
		{
		return false;
		}
		
	if (stream.Get_uint8 () != 0x0A)
		{
		return false;
		}

	#if qDNGValidate
	
	if (gVerbose)
		{
		printf ("\nJXL (basic) file found\n");
		}
		
	#endif
	
	// Reset stream position.

	stream.SetReadPosition (pos);

	return DoParseJXL (host, stream, info);

	}

/*****************************************************************************/

static bool ParseJXL_Container (dng_host &host,
								dng_stream &stream,
								dng_info &info)
	{

	// Look for start of BMFF-based JXL container.
	
	if (stream.Length () < 12)
		{
		return false;
		}

	const auto pos = stream.Position ();

	uint8 buffer [12];

	stream.Get (buffer, 12);

	uint8 jxlBuffer [] =
		{
		0x00, 0x00, 0x00, 0x0C,
		0x4A, 0x58, 0x4C, 0x20,
		0x0D, 0x0A, 0x87, 0x0A
		};

	if (memcmp (buffer, jxlBuffer, 12) != 0)
		{
		return false;
		}

	#if qDNGValidate
	
	if (gVerbose)
		{
		printf ("\nJXL (container) file found\n");
		}
		
	#endif
	
	// Reset stream position.

	stream.SetReadPosition (pos);

	return DoParseJXL (host, stream, info);

	}

/*****************************************************************************/

bool ParseJXL (dng_host &host,
			   dng_stream &stream,
			   dng_info &info,
			   bool supportBasicCodeStream,
			   bool supportContainer)
	{
	
	const uint64 startPosition = stream.Position ();
	
	if (supportBasicCodeStream)
		{

		if (ParseJXL_Basic (host, stream, info))
			return true;

		stream.SetReadPosition (startPosition);

		}

	if (supportContainer)
		{
		
		if (ParseJXL_Container (host, stream, info))
			return true;
		
		}

	return false;

	}

/*****************************************************************************/

void EncodeJXL_Tile (dng_host &host,
					 dng_stream &stream,
					 const dng_image &image,
					 const dng_jxl_color_space_info &colorSpaceInfo,
					 const dng_jxl_encode_settings &settings)
	{
	
	EncodeJXL (host,
			   stream,
			   image,
			   settings,
			   false,						 // use container
			   colorSpaceInfo,
			   nullptr,						 // meta
			   false,
			   false,
			   false,
			   nullptr);					 // gain map meta
	
	}

/*****************************************************************************/

void EncodeJXL_Tile (dng_host &host,
					 dng_stream &stream,
					 const dng_pixel_buffer &buffer,
					 const dng_jxl_color_space_info &colorSpaceInfo,
					 const dng_jxl_encode_settings &settings)
	{
	
	EncodeJXL (host,
			   stream,
			   buffer,
			   settings,
			   false,						 // use container
			   colorSpaceInfo,
			   nullptr,						 // meta
			   false,
			   false,
			   false,
			   nullptr);					 // gain map meta
	
	}

/*****************************************************************************/

void EncodeJXL_Container (dng_host &host,
						  dng_stream &stream,
						  const dng_image &image,
						  const dng_jxl_encode_settings &settings,
						  const dng_jxl_color_space_info &colorSpaceInfo,
						  const dng_metadata *metadata,
						  const bool includeExif,
						  const bool includeXMP,
						  const bool includeIPTC,
						  const dng_bmff_box_list *additionalBoxes)
	{

	EncodeJXL (host,
			   stream,
			   image,
			   settings,
			   true,						 // use container
			   colorSpaceInfo,
			   metadata,
			   includeExif,
			   includeXMP,
			   includeIPTC,
			   additionalBoxes);

	}

/*****************************************************************************/

void EncodeJXL_Container (dng_host &host,
						  dng_stream &stream,
						  const dng_pixel_buffer &buffer,
						  const dng_jxl_encode_settings &settings,
						  const dng_jxl_color_space_info &colorSpaceInfo,
						  const dng_metadata *metadata,
						  const bool includeExif,
						  const bool includeXMP,
						  const bool includeIPTC,
						  const dng_bmff_box_list *additionalBoxes)
	{

	EncodeJXL (host,
			   stream,
			   buffer,
			   settings,
			   true,						 // use container
			   colorSpaceInfo,
			   metadata,
			   includeExif,
			   includeXMP,
			   includeIPTC,
			   additionalBoxes);

	}

/*****************************************************************************/

real32 JXLQualityToDistance (uint32 quality)
	{

	quality = Pin_uint32 (kMinJXLCompressionQuality,
						  quality,
						  kMaxJXLCompressionQuality);

	// 1 to 13 scale.

	static const real32 kTable [] =
		{
		0.0f,								 // 0 (unused)
		8.0f,								 // 1
		6.0f,								 // 2
		5.0f,								 // 3
		4.0f,								 // 4
		3.0f,								 // 5
		2.0f,								 // 6
		1.6f,								 // 7
		1.3f,								 // 8
		1.0f,								 // 9
		0.7f,								 // 10
		0.4f,								 // 11
		0.1f,								 // 12
		0.0f,								 // 13 (lossless)
		};

	return kTable [quality];

	}

/*****************************************************************************/

dng_jxl_encode_settings * JXLQualityToSettings (uint32 quality)
	{
	
	AutoPtr<dng_jxl_encode_settings> settings (new dng_jxl_encode_settings);

	real32 d = JXLQualityToDistance (quality);

	settings->SetDistance (d);

	if (d <= 0.0f)
		settings->SetFastest ();			 // Use fast lossless path

	return settings.Release ();
	
	}

/*****************************************************************************/

void PreviewColorSpaceToJXLEncoding (const PreviewColorSpaceEnum colorSpace,
									 const uint32 planes,
									 dng_jxl_color_space_info &info)
	{

	#if qLogJXL
	printf ("jxl preview info color space enum: %d\n",
			int (colorSpace));
	#endif

	info.fJxlColorEncoding.Reset (new JxlColorEncoding);

	auto &encoding = *info.fJxlColorEncoding;

	memset (&encoding, 0, sizeof (encoding));

	switch (colorSpace)
		{

		case previewColorSpace_GrayGamma22:
			{
			encoding.color_space	   = JXL_COLOR_SPACE_GRAY;
			encoding.white_point	   = JXL_WHITE_POINT_D65; // unused
			encoding.primaries		   = JXL_PRIMARIES_SRGB;  // unused
			encoding.transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
			encoding.gamma			   = 1.0 / 2.2;
			break;
			}

		case previewColorSpace_sRGB:
			{
			encoding.color_space	   = JXL_COLOR_SPACE_RGB;
			encoding.white_point	   = JXL_WHITE_POINT_D65;
			encoding.primaries		   = JXL_PRIMARIES_SRGB;
			encoding.transfer_function = JXL_TRANSFER_FUNCTION_SRGB;
			break;
			}

		case previewColorSpace_AdobeRGB:
			{
			
			encoding.color_space	   = JXL_COLOR_SPACE_RGB;
			encoding.white_point	   = JXL_WHITE_POINT_D65;
			encoding.primaries		   = JXL_PRIMARIES_CUSTOM;
			encoding.transfer_function = JXL_TRANSFER_FUNCTION_GAMMA;
			encoding.gamma			   = 1.0 / 2.2;

			encoding.primaries_red_xy   [0] = 0.6400;
			encoding.primaries_red_xy   [1] = 0.3300;

			encoding.primaries_green_xy [0] = 0.2100;
			encoding.primaries_green_xy [1] = 0.7100;

			encoding.primaries_blue_xy  [0] = 0.1500;
			encoding.primaries_blue_xy  [1] = 0.0600;

			break;
			
			}

		case previewColorSpace_ProPhotoRGB:
			{

			encoding.color_space            = JXL_COLOR_SPACE_RGB;
			encoding.white_point            = JXL_WHITE_POINT_CUSTOM;
			encoding.primaries              = JXL_PRIMARIES_CUSTOM;
			encoding.transfer_function      = JXL_TRANSFER_FUNCTION_GAMMA;
			encoding.gamma                  = 1.0 / 1.8;

			encoding.primaries_red_xy   [0] = 0.734699;
			encoding.primaries_red_xy   [1] = 0.265301;

			encoding.primaries_green_xy [0] = 0.159597;
			encoding.primaries_green_xy [1] = 0.840403;

			encoding.primaries_blue_xy  [0] = 0.036598;
			encoding.primaries_blue_xy  [1] = 0.000105;

			dng_xy_coord white              = D50_xy_coord ();

			encoding.white_point_xy [0]     = white.x;
			encoding.white_point_xy [1]     = white.y;
			
			break;
			
			}

		case previewColorSpace_Unknown:
		default:
			{

			// Assume everything else is raw.

			// Raw data is linear and usually not white balanced and still in
			// native/camera RGB space. Transfer curve should be linear but
			// unclear how to indicate the primaries. For now, just pretend
			// data is linear BT 2100 / Rec 2020.

			if (planes == 1)
				{
				encoding.color_space	   = JXL_COLOR_SPACE_GRAY;
				encoding.white_point	   = JXL_WHITE_POINT_D65; // unused
				encoding.primaries		   = JXL_PRIMARIES_2100;  // unused
				encoding.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
				}
			
			else
				{
				encoding.color_space	   = JXL_COLOR_SPACE_RGB;
				encoding.white_point	   = JXL_WHITE_POINT_D65;
				encoding.primaries		   = JXL_PRIMARIES_2100;
				encoding.transfer_function = JXL_TRANSFER_FUNCTION_LINEAR;
				}

			break;
			
			}
		
		}
	
	}

/*****************************************************************************/

bool SupportsJXL (const dng_image &image)
	{
	
	// TODO(erichan): We should add an image size check. But currently JXL's
	// limits exceed our DNG SDK limits.

	// Currently ignore alpha; assume that it's a separate image.

	uint32 planes	 = image.Planes ();
	uint32 pixelType = image.PixelType ();

	return ((planes == 1 ||					 // gray
			 planes == 3)					 // RGB

			&&

			(pixelType == ttByte	  ||
			 pixelType == ttShort	  ||
			 pixelType == ttHalfFloat ||
			 pixelType == ttFloat));
	
	}

/*****************************************************************************/

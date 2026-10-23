/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegVideoDecoder.h"

#include "FFmpegLibWrapper.h"
#include "FFmpegLog.h"
#include "FFmpegUtils.h"
#include "FFmpegVideoUtils.h"
#include "ImageContainer.h"
#include "MP4Decoder.h"
#include "MediaInfo.h"
#include "VALibWrapper.h"
#include "VPXDecoder.h"
#include "VideoUtils.h"
#if LIBAVCODEC_VERSION_MAJOR >= 58
#  include "libavutil/buffer.h"
#  include "libavutil/frame.h"
#  include "libavutil/hwcontext.h"
#  include "libavutil/pixfmt.h"
#endif
#ifdef MOZ_USE_HWDECODE_VULKAN
#  include "libavutil/hwcontext_vulkan.h"
#  include "libavutil/macros.h"
#  include "libavutil/version.h"
#endif

#include <string.h>
#  include <unistd.h>

#include <algorithm>

#include "mozilla/ToString.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "nsPrintfCString.h"
#if LIBAVCODEC_VERSION_MAJOR >= 57
#  include "mozilla/layers/TextureClient.h"
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
#  include "mozilla/ProfilerMarkers.h"
#endif
#ifdef MOZ_USE_HWDECODE
#  include "H264.h"
#  include "H265.h"
#endif

#if defined(FFVPX_VERSION) || LIBAVCODEC_VERSION_MAJOR >= 59
#  define FFMPEG_AV1_DECODE 1
#  include "AOMDecoder.h"
#endif

#if LIBAVCODEC_VERSION_MAJOR < 54
#  define AV_PIX_FMT_YUV420P PIX_FMT_YUV420P
#  define AV_PIX_FMT_YUVJ420P PIX_FMT_YUVJ420P
#  define AV_PIX_FMT_YUV420P10LE PIX_FMT_YUV420P10LE
#  define AV_PIX_FMT_YUV422P PIX_FMT_YUV422P
#  define AV_PIX_FMT_YUV422P10LE PIX_FMT_YUV422P10LE
#  define AV_PIX_FMT_YUV444P PIX_FMT_YUV444P
#  define AV_PIX_FMT_YUVJ444P PIX_FMT_YUVJ444P
#  define AV_PIX_FMT_YUV444P10LE PIX_FMT_YUV444P10LE
#  define AV_PIX_FMT_GBRP PIX_FMT_GBRP
#  define AV_PIX_FMT_GBRP10LE PIX_FMT_GBRP10LE
#  define AV_PIX_FMT_NONE PIX_FMT_NONE
#  define AV_PIX_FMT_VAAPI_VLD PIX_FMT_VAAPI_VLD
#endif
#if LIBAVCODEC_VERSION_MAJOR > 58
#  define AV_PIX_FMT_VAAPI_VLD AV_PIX_FMT_VAAPI
#endif
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "nsThreadUtils.h"
#include "prsystem.h"


#ifdef MOZ_ENABLE_D3D11VA
#  include "D3D11TextureWrapper.h"
#  include "DXVA2Manager.h"
#  include "ffvpx/hwcontext_d3d11va.h"
#endif




// Use some extra HW frames for potential rendering lags.
// AV1 and VP9 can have maximum 8 frames for reference frames, so 1 base + 8
// references.
#define EXTRA_HW_FRAMES 9

#if LIBAVCODEC_VERSION_MAJOR >= 57 && LIBAVUTIL_VERSION_MAJOR >= 56
#  define CUSTOMIZED_BUFFER_ALLOCATION 1
#endif

#define AV_LOG_DEBUG 48

typedef mozilla::layers::Image Image;
typedef mozilla::layers::PlanarYCbCrImage PlanarYCbCrImage;
typedef mozilla::layers::BufferRecycleBin BufferRecycleBin;

namespace mozilla {


using media::TimeUnit;

/**
 * FFmpeg calls back to this function with a list of pixel formats it supports.
 * We choose a pixel format that we support and return it.
 * For now, we just look for YUV420P, YUVJ420P, YUV444 and YUVJ444 as
 * those are the only non-HW accelerated format supported by FFmpeg's H264 and
 * VP9 decoder.
 */
static AVPixelFormat ChoosePixelFormat(AVCodecContext* aCodecContext,
                                       const AVPixelFormat* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_YUV420P:
        FFMPEGV_LOG("Requesting pixel format YUV420P.");
        return AV_PIX_FMT_YUV420P;
      case AV_PIX_FMT_YUVJ420P:
        FFMPEGV_LOG("Requesting pixel format YUVJ420P.");
        return AV_PIX_FMT_YUVJ420P;
      case AV_PIX_FMT_YUV420P10LE:
        FFMPEGV_LOG("Requesting pixel format YUV420P10LE.");
        return AV_PIX_FMT_YUV420P10LE;
      case AV_PIX_FMT_YUV422P:
        FFMPEGV_LOG("Requesting pixel format YUV422P.");
        return AV_PIX_FMT_YUV422P;
      case AV_PIX_FMT_YUV422P10LE:
        FFMPEGV_LOG("Requesting pixel format YUV422P10LE.");
        return AV_PIX_FMT_YUV422P10LE;
      case AV_PIX_FMT_YUV444P:
        FFMPEGV_LOG("Requesting pixel format YUV444P.");
        return AV_PIX_FMT_YUV444P;
      case AV_PIX_FMT_YUVJ444P:
        FFMPEGV_LOG("Requesting pixel format YUVJ444P.");
        return AV_PIX_FMT_YUVJ444P;
      case AV_PIX_FMT_YUV444P10LE:
        FFMPEGV_LOG("Requesting pixel format YUV444P10LE.");
        return AV_PIX_FMT_YUV444P10LE;
#if LIBAVCODEC_VERSION_MAJOR >= 57
      case AV_PIX_FMT_YUV420P12LE:
        FFMPEGV_LOG("Requesting pixel format YUV420P12LE.");
        return AV_PIX_FMT_YUV420P12LE;
      case AV_PIX_FMT_YUV422P12LE:
        FFMPEGV_LOG("Requesting pixel format YUV422P12LE.");
        return AV_PIX_FMT_YUV422P12LE;
      case AV_PIX_FMT_YUV444P12LE:
        FFMPEGV_LOG("Requesting pixel format YUV444P12LE.");
        return AV_PIX_FMT_YUV444P12LE;
#endif
      case AV_PIX_FMT_GBRP:
        FFMPEGV_LOG("Requesting pixel format GBRP.");
        return AV_PIX_FMT_GBRP;
      case AV_PIX_FMT_GBRP10LE:
        FFMPEGV_LOG("Requesting pixel format GBRP10LE.");
        return AV_PIX_FMT_GBRP10LE;
      default:
        break;
    }
  }

  NS_WARNING("FFmpeg does not share any supported pixel formats.");
  return AV_PIX_FMT_NONE;
}

#ifdef MOZ_USE_HWDECODE
static AVPixelFormat ChooseVAAPIPixelFormat(AVCodecContext* aCodecContext,
                                            const AVPixelFormat* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for VA-API video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_VAAPI_VLD:
        FFMPEGV_LOG("Requesting pixel format VAAPI_VLD");
        return AV_PIX_FMT_VAAPI_VLD;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported pixel formats.");
  return AV_PIX_FMT_NONE;
}

static AVPixelFormat ChooseV4L2PixelFormat(AVCodecContext* aCodecContext,
                                           const AVPixelFormat* aFormats) {
  FFMPEGV_LOG("Choosing FFmpeg pixel format for V4L2 video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_DRM_PRIME:
        FFMPEGV_LOG("Requesting pixel format DRM PRIME");
        return AV_PIX_FMT_DRM_PRIME;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported V4L2 pixel formats.");
  return AV_PIX_FMT_NONE;
}

#  ifdef MOZ_USE_HWDECODE_VULKAN
static AVPixelFormat ChooseVulkanPixelFormat(AVCodecContext* aCodecContext,
                                             const AVPixelFormat* aFormats) {
  auto* decoder =
      static_cast<FFmpegVideoDecoder<LIBAV_VER>*>(aCodecContext->opaque);
  if (decoder) {
    return (AVPixelFormat)decoder->ChooseVulkanPixelFormatFromContext(
        aCodecContext, (const int*)aFormats);
  }
  return AV_PIX_FMT_NONE;
}
#  endif

static AVPixelFormat ChooseD3D11VAPixelFormat(AVCodecContext* aCodecContext,
                                              const AVPixelFormat* aFormats) {
#  ifdef MOZ_ENABLE_D3D11VA
  FFMPEGV_LOG("Choosing FFmpeg pixel format for D3D11VA video decoding {}. ",
              static_cast<int>(*aFormats));
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_D3D11:
        FFMPEGV_LOG("Requesting pixel format D3D11");
        return AV_PIX_FMT_D3D11;
      default:
        break;
    }
  }
  NS_WARNING("FFmpeg does not share any supported D3D11 pixel formats.");
#  endif  // MOZ_ENABLE_D3D11VA
  return AV_PIX_FMT_NONE;
}
#endif

#ifdef MOZ_USE_HWDECODE
static AVPixelFormat ChooseMediaCodecPixelFormat(
    AVCodecContext* aCodecContext, const AVPixelFormat* aFormats) {
  return AV_PIX_FMT_NONE;
}
#endif


#if LIBAVCODEC_VERSION_MAJOR < 58
FFmpegVideoDecoder<LIBAV_VER>::PtsCorrectionContext::PtsCorrectionContext()
    : mNumFaultyPts(0),
      mNumFaultyDts(0),
      mLastPts(INT64_MIN),
      mLastDts(INT64_MIN) {}

int64_t FFmpegVideoDecoder<LIBAV_VER>::PtsCorrectionContext::GuessCorrectPts(
    int64_t aPts, int64_t aDts) {
  int64_t pts = AV_NOPTS_VALUE;

  if (aDts != int64_t(AV_NOPTS_VALUE)) {
    mNumFaultyDts += aDts <= mLastDts;
    mLastDts = aDts;
  }
  if (aPts != int64_t(AV_NOPTS_VALUE)) {
    mNumFaultyPts += aPts <= mLastPts;
    mLastPts = aPts;
  }
  if ((mNumFaultyPts <= mNumFaultyDts || aDts == int64_t(AV_NOPTS_VALUE)) &&
      aPts != int64_t(AV_NOPTS_VALUE)) {
    pts = aPts;
  } else {
    pts = aDts;
  }
  return pts;
}

void FFmpegVideoDecoder<LIBAV_VER>::PtsCorrectionContext::Reset() {
  mNumFaultyPts = 0;
  mNumFaultyDts = 0;
  mLastPts = INT64_MIN;
  mLastDts = INT64_MIN;
}
#endif

#if defined(MOZ_USE_HWDECODE)
bool FFmpegVideoDecoder<LIBAV_VER>::ShouldDisableHWDecoding(
    bool aDisableHardwareDecoding) const {


  return aDisableHardwareDecoding;
}
#endif

FFmpegVideoDecoder<LIBAV_VER>::FFmpegVideoDecoder(
    const FFmpegLibWrapper* aLib, const VideoInfo& aConfig,
    KnowsCompositor* aAllocator, ImageContainer* aImageContainer,
    bool aLowLatency, bool aDisableHardwareDecoding, bool a8BitOutput,
    Maybe<TrackingId> aTrackingId, PRemoteCDMActor* aCDM)
    : FFmpegDataDecoder(aLib, GetCodecId(aConfig.mMimeType), aCDM),
      mImageAllocator(aAllocator),
      mImageContainer(aImageContainer),
      mInfo(aConfig),
#ifdef MOZ_USE_HWDECODE
      mHardwareDecodingDisabled(
          ShouldDisableHWDecoding(aDisableHardwareDecoding)),
#endif  // MOZ_USE_HWDECODE
      mLowLatency(aLowLatency),
      mTrackingId(std::move(aTrackingId)),
      // Value may be changed later when codec is known after initialization.
      m8BitOutput(a8BitOutput) {
  FFMPEG_LOG("FFmpegVideoDecoder::FFmpegVideoDecoder MIME {} Codec ID {}",
             aConfig.mMimeType.get(), static_cast<int>(mCodecID));
  // Use a new MediaByteBuffer as the object will be modified during
  // initialization.
  mExtraData = new MediaByteBuffer;
  mExtraData->AppendElements(*aConfig.mExtraData);
#ifdef MOZ_USE_HWDECODE
  InitHWDecoderIfAllowed();
#endif  // MOZ_USE_HWDECODE
}

FFmpegVideoDecoder<LIBAV_VER>::~FFmpegVideoDecoder() {
#ifdef MOZ_USE_HWDECODE_VULKAN
  VulkanDeviceHolder::Drop(mVulkanDeviceHolder);
#endif
#ifdef CUSTOMIZED_BUFFER_ALLOCATION_ASSERT_ENABLED
  // ffmpeg should have cleared all of its strong references to the decoded data
  // buffers.
  if (mImageTracker) {
    MOZ_DIAGNOSTIC_ASSERT(mImageTracker->IsEmpty(),
                          "Should release all shmem buffers before destroy!");
  }
#endif
}

#ifdef MOZ_USE_HWDECODE
void FFmpegVideoDecoder<LIBAV_VER>::InitHWDecoderIfAllowed() {
  if (mHardwareDecodingDisabled) {
    return;
  }

#  ifdef MOZ_USE_HWDECODE_VULKAN
  if (NS_SUCCEEDED(InitVulkanDecoder())) {
    return;
  }
#  endif

#  ifdef FFVPX_VERSION
  if (!StaticPrefs::media_ffvpx_hw_enabled()) {
    return;
  }
#  endif

#  ifdef MOZ_ENABLE_VAAPI
  if (NS_SUCCEEDED(InitVAAPIDecoder())) {
    return;
  }
#  endif  // MOZ_ENABLE_VAAPI

#  ifdef MOZ_ENABLE_V4L2
  // VAAPI didn't work or is disabled, so try V4L2 with DRM
  if (NS_SUCCEEDED(InitV4L2Decoder())) {
    return;
  }
#  endif  // MOZ_ENABLE_V4L2

#  ifdef MOZ_ENABLE_D3D11VA
  if (XRE_IsGPUProcess() && NS_SUCCEEDED(InitD3D11VADecoder())) {
    return;
  }
#  endif  // MOZ_ENABLE_D3D11VA

}
#endif  // MOZ_USE_HWDECODE

static bool ShouldEnable8BitConversion(const struct AVCodec* aCodec) {
  return 0 == strncmp(aCodec->name, "libdav1d", 8) ||
         0 == strncmp(aCodec->name, "vp9", 3);
}

RefPtr<MediaDataDecoder::InitPromise> FFmpegVideoDecoder<LIBAV_VER>::Init() {
  AUTO_PROFILER_LABEL("FFmpegVideoDecoder::Init", MEDIA_PLAYBACK);
  FFMPEG_LOG("FFmpegVideoDecoder, init, IsHardwareAccelerated={}\n",
             IsHardwareAccelerated());
  // We've finished the HW decoder initialization in the ctor.
  if (IsHardwareAccelerated()) {
    return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
  }
  MediaResult rv = InitSWDecoder(nullptr);
  if (NS_FAILED(rv)) {
    return InitPromise::CreateAndReject(rv, __func__);
  }
  m8BitOutput = m8BitOutput && ShouldEnable8BitConversion(mCodecContext->codec);
  if (m8BitOutput) {
    FFMPEG_LOG("Enable 8-bit output for {}", mCodecContext->codec->name);
    m8BitRecycleBin = MakeRefPtr<BufferRecycleBin>();
  }
  return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
}

static gfx::ColorRange GetColorRange(enum AVColorRange& aColorRange) {
  return aColorRange == AVCOL_RANGE_JPEG ? gfx::ColorRange::FULL
                                         : gfx::ColorRange::LIMITED;
}

static bool IsYUVFormat(const AVPixelFormat& aFormat) {
  return aFormat != AV_PIX_FMT_GBRP && aFormat != AV_PIX_FMT_GBRP10LE;
}

static gfx::YUVColorSpace TransferAVColorSpaceToColorSpace(
    const AVColorSpace aSpace, const AVPixelFormat aFormat,
    const gfx::IntSize& aSize) {
  if (!IsYUVFormat(aFormat)) {
    return gfx::YUVColorSpace::Identity;
  }
  switch (aSpace) {
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      return gfx::YUVColorSpace::BT2020;
#endif
    case AVCOL_SPC_BT709:
      return gfx::YUVColorSpace::BT709;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
      return gfx::YUVColorSpace::BT601;
    default:
      return DefaultColorSpace(aSize);
  }
}

static Maybe<gfx::ColorSpace2> TransferAVColorPrimaries(
    const AVColorPrimaries aPrimaries) {
  switch (aPrimaries) {
    case AVCOL_PRI_BT709:
      return Some(gfx::ColorSpace2::BT709);
    case AVCOL_PRI_SMPTE170M:
      return Some(gfx::ColorSpace2::BT601_525);
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AVCOL_PRI_BT2020:
      return Some(gfx::ColorSpace2::BT2020);
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
    case AVCOL_PRI_SMPTE432:
      return Some(gfx::ColorSpace2::DISPLAY_P3);
#endif
    default:
      return Nothing();
  }
}

static Maybe<gfx::TransferFunction> TransferAVTransferFunction(
    const AVColorTransferCharacteristic aTransfer) {
  switch (aTransfer) {
    case AVCOL_TRC_BT709:
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
#endif
      return Some(gfx::TransferFunction::BT709);
#if LIBAVCODEC_VERSION_MAJOR >= 55
    case AVCOL_TRC_IEC61966_2_1:
      return Some(gfx::TransferFunction::SRGB);
    case AVCOL_TRC_LINEAR:
      return Some(gfx::TransferFunction::LINEAR);
#endif
#if LIBAVCODEC_VERSION_MAJOR == 57
    case AVCOL_TRC_SMPTEST2084:
      return Some(gfx::TransferFunction::PQ);
#elif LIBAVCODEC_VERSION_MAJOR >= 58
    case AVCOL_TRC_SMPTE2084:
      return Some(gfx::TransferFunction::PQ);
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 58
    case AVCOL_TRC_ARIB_STD_B67:
      return Some(gfx::TransferFunction::HLG);
#endif
    default:
      return Nothing();
  }
}

#ifdef CUSTOMIZED_BUFFER_ALLOCATION
static int GetVideoBufferWrapper(struct AVCodecContext* aCodecContext,
                                 AVFrame* aFrame, int aFlags) {
  auto* decoder =
      static_cast<FFmpegVideoDecoder<LIBAV_VER>*>(aCodecContext->opaque);
  int rv = decoder->GetVideoBuffer(aCodecContext, aFrame, aFlags);
  return rv < 0 ? decoder->GetVideoBufferDefault(aCodecContext, aFrame, aFlags)
                : rv;
}

static void ReleaseVideoBufferWrapper(void* opaque, uint8_t* data) {
  if (opaque) {
    FFMPEGV_LOG("ReleaseVideoBufferWrapper: ImageBufferWrapper={}",
                fmt::ptr(opaque));
    RefPtr image = dont_AddRef(static_cast<ImageBufferWrapper*>(opaque));
    image->StopTracking();
  }
}

static bool IsColorFormatSupportedForUsingCustomizedBuffer(
    const AVPixelFormat& aFormat) {
  // For now, we only support for YUV420P, YUVJ420P, YUV444P and YUVJ444P which
  // are the only non-HW accelerated format supported by FFmpeg's H264 and VP9
  // decoder.
  return aFormat == AV_PIX_FMT_YUV420P || aFormat == AV_PIX_FMT_YUVJ420P ||
         aFormat == AV_PIX_FMT_YUV420P10LE ||
         aFormat == AV_PIX_FMT_YUV420P12LE || aFormat == AV_PIX_FMT_YUV444P ||
         aFormat == AV_PIX_FMT_YUVJ444P || aFormat == AV_PIX_FMT_YUV444P10LE ||
         aFormat == AV_PIX_FMT_YUV444P12LE;
}

static bool IsYUV420Sampling(const AVPixelFormat& aFormat) {
  return aFormat == AV_PIX_FMT_YUV420P || aFormat == AV_PIX_FMT_YUVJ420P ||
         aFormat == AV_PIX_FMT_YUV420P10LE || aFormat == AV_PIX_FMT_YUV420P12LE;
}


layers::TextureClient*
FFmpegVideoDecoder<LIBAV_VER>::AllocateTextureClientForImage(
    struct AVCodecContext* aCodecContext, PlanarYCbCrImage* aImage) {
  MOZ_ASSERT(
      IsColorFormatSupportedForUsingCustomizedBuffer(aCodecContext->pix_fmt));

  // FFmpeg will store images with color depth > 8 bits in 16 bits with extra
  // padding.
  const int32_t bytesPerChannel =
      GetColorDepth(aCodecContext->pix_fmt) == gfx::ColorDepth::COLOR_8 ? 1 : 2;

  // If adjusted Ysize is larger than the actual image size (coded_width *
  // coded_height), that means ffmpeg decoder needs extra padding on both width
  // and height. If that happens, the planes will need to be cropped later in
  // order to avoid visible incorrect border on the right and bottom of the
  // actual image.
  //
  // Here are examples of various sizes video in YUV420P format, the width and
  // height would need to be adjusted in order to align padding.
  //
  // Eg1. video (1920*1080)
  // plane Y
  // width 1920 height 1080 -> adjusted-width 1920 adjusted-height 1088
  // plane Cb/Cr
  // width 960  height  540 -> adjusted-width 1024 adjusted-height 544
  //
  // Eg2. video (2560*1440)
  // plane Y
  // width 2560 height 1440 -> adjusted-width 2560 adjusted-height 1440
  // plane Cb/Cr
  // width 1280 height  720 -> adjusted-width 1280 adjusted-height 736
  layers::PlanarYCbCrData data;
  const auto yDims =
      gfx::IntSize{aCodecContext->coded_width, aCodecContext->coded_height};
  auto paddedYSize = yDims;
  mLib->avcodec_align_dimensions(aCodecContext, &paddedYSize.width,
                                 &paddedYSize.height);
  data.mYStride = paddedYSize.Width() * bytesPerChannel;

  MOZ_ASSERT(
      IsColorFormatSupportedForUsingCustomizedBuffer(aCodecContext->pix_fmt));
  auto uvDims = yDims;
  if (IsYUV420Sampling(aCodecContext->pix_fmt)) {
    uvDims.width = (uvDims.width + 1) / 2;
    uvDims.height = (uvDims.height + 1) / 2;
    data.mChromaSubsampling = gfx::ChromaSubsampling::HALF_WIDTH_AND_HEIGHT;
  }
  auto paddedCbCrSize = uvDims;
  mLib->avcodec_align_dimensions(aCodecContext, &paddedCbCrSize.width,
                                 &paddedCbCrSize.height);
  data.mCbCrStride = paddedCbCrSize.Width() * bytesPerChannel;

  paddedYSize.height += GetBuffer2StrideAlign(data.mYStride);
  paddedCbCrSize.height += GetBuffer2StrideAlign(data.mCbCrStride);

  // Setting other attributes
  data.mPictureRect =
      mInfo.ScaledImageRect(aCodecContext->width, aCodecContext->height);
  data.mStereoMode = mInfo.mStereoMode;
  if (aCodecContext->colorspace != AVCOL_SPC_UNSPECIFIED) {
    data.mYUVColorSpace = TransferAVColorSpaceToColorSpace(
        aCodecContext->colorspace, aCodecContext->pix_fmt,
        data.mPictureRect.Size());
  } else {
    data.mYUVColorSpace = mInfo.mColorSpace
                              ? *mInfo.mColorSpace
                              : DefaultColorSpace(data.mPictureRect.Size());
  }
  data.mColorDepth = GetColorDepth(aCodecContext->pix_fmt);
  data.mColorRange = GetColorRange(aCodecContext->color_range);
  data.mColorPrimaries =
      TransferAVColorPrimaries(aCodecContext->color_primaries)
          .valueOr(mInfo.mColorPrimaries.valueOr(gfx::ColorSpace2::UNKNOWN));
  if (Maybe<gfx::TransferFunction> transfer =
          TransferAVTransferFunction(aCodecContext->color_trc).orElse([&] {
            return mInfo.mTransferFunction;
          })) {
    data.mTransferFunction = *transfer;
  }
  data.mHDRMetadata = mInfo.mHDRMetadata;

  FFMPEG_LOGV(
      "Created plane data, YSize=({}, {}), CbCrSize=({}, {}), "
      "Adjusted plane data={}",
      paddedYSize.Width(), paddedYSize.Height(), paddedCbCrSize.Width(),
      paddedCbCrSize.Height(), mozilla::ToString(data).c_str());

  // Allocate a shmem buffer for image.
  if (NS_FAILED(aImage->CreateEmptyBuffer(data, paddedYSize, paddedCbCrSize))) {
    return nullptr;
  }
  return aImage->GetTextureClient(mImageAllocator);
}

int FFmpegVideoDecoder<LIBAV_VER>::GetVideoBuffer(
    struct AVCodecContext* aCodecContext, AVFrame* aFrame, int aFlags) {
  FFMPEG_LOGV("GetVideoBuffer: aCodecContext={} aFrame={}",
              fmt::ptr(aCodecContext), fmt::ptr(aFrame));

  if (mIsUsingShmemBufferForDecode && !*mIsUsingShmemBufferForDecode) {
    return AVERROR(EINVAL);
  }

  // Codec doesn't support custom allocator.
  if (!(aCodecContext->codec->capabilities & AV_CODEC_CAP_DR1)) {
    return AVERROR(EINVAL);
  }

  // Pre-allocation is only for sw decoding. During decoding, ffmpeg decoder
  // will need to reference decoded frames, if those frames are on shmem buffer,
  // then it would cause a need to read CPU data from GPU, which is slow.
  if (IsHardwareAccelerated()) {
    return AVERROR(EINVAL);
  }

  if (!IsColorFormatSupportedForUsingCustomizedBuffer(aCodecContext->pix_fmt)) {
    FFMPEG_LOG("Not support color format {}",
               static_cast<int>(aCodecContext->pix_fmt));
    return AVERROR(EINVAL);
  }

  if (aCodecContext->lowres != 0) {
    FFMPEG_LOG("Not support low resolution decoding");
    return AVERROR(EINVAL);
  }

  const gfx::IntSize size(aCodecContext->width, aCodecContext->height);
  int rv = mLib->av_image_check_size(size.Width(), size.Height(), 0, nullptr);
  if (rv < 0) {
    FFMPEG_LOG("Invalid image size");
    return rv;
  }

  if (!mImageContainer) {
    FFMPEG_LOG("No Image container!");
    return AVERROR(EINVAL);
  }

  RefPtr<PlanarYCbCrImage> image = mImageContainer->CreatePlanarYCbCrImage();
  if (!image) {
    FFMPEG_LOG("Failed to create YCbCr image");
    return AVERROR(EINVAL);
  }
  image->SetColorDepth(mInfo.mColorDepth);

  RefPtr<layers::TextureClient> texture =
      AllocateTextureClientForImage(aCodecContext, image);
  if (!texture) {
    FFMPEG_LOG("Failed to allocate a texture client");
    return AVERROR(EINVAL);
  }

  if (!texture->Lock(layers::OpenMode::OPEN_WRITE)) {
    FFMPEG_LOG("Failed to lock the texture");
    return AVERROR(EINVAL);
  }
  auto autoUnlock = MakeScopeExit([&] { texture->Unlock(); });

  layers::MappedYCbCrTextureData mapped;
  if (!texture->BorrowMappedYCbCrData(mapped)) {
    FFMPEG_LOG("Failed to borrow mapped data for the texture");
    return AVERROR(EINVAL);
  }

  aFrame->data[0] = mapped.y.data;
  aFrame->data[1] = mapped.cb.data;
  aFrame->data[2] = mapped.cr.data;

  aFrame->linesize[0] = mapped.y.stride;
  aFrame->linesize[1] = mapped.cb.stride;
  aFrame->linesize[2] = mapped.cr.stride;

  aFrame->width = aCodecContext->coded_width;
  aFrame->height = aCodecContext->coded_height;
  aFrame->format = aCodecContext->pix_fmt;
  aFrame->extended_data = aFrame->data;
#  if LIBAVCODEC_VERSION_MAJOR < 61
  aFrame->reordered_opaque = aCodecContext->reordered_opaque;
#  endif
  MOZ_ASSERT(aFrame->data[0] && aFrame->data[1] && aFrame->data[2]);

#  ifdef CUSTOMIZED_BUFFER_ALLOCATION_ASSERT_ENABLED
  if (!mImageTracker) {
    mImageTracker = MakeRefPtr<ImageBufferTracker>();
  }
  auto imageWrapper =
      MakeRefPtr<ImageBufferWrapper>(std::move(image), mImageTracker);
#  else
  auto imageWrapper = MakeRefPtr<ImageBufferWrapper>(std::move(image));
#  endif

  // The buffer spans to the end of the last plane, not the packed image size.
  auto planeEnd = [](const layers::MappedYCbCrChannelData& aPlane) {
    return aPlane.data + aPlane.stride * aPlane.size.height;
  };
  const uint8_t* bufferEnd =
      std::max({planeEnd(mapped.y), planeEnd(mapped.cb), planeEnd(mapped.cr)});
  const ptrdiff_t bufferExtent = bufferEnd - aFrame->data[0];
  const CheckedInt<int> bufferSize(bufferExtent);
  if (!bufferSize.isValid() || bufferSize.value() <= 0) {
    FFMPEG_LOG("Invalid mapped buffer extent");
    return AVERROR(EINVAL);
  }

  // This will hold a strong reference to imageWrapper, and the reference will
  // be dropped when ffmpeg tells us that the buffer is no longer needed. Since
  // the wrapper keeps image alive, it will prevent recycling until ffmpeg is
  // done with the buffer.
  aFrame->buf[0] =
      mLib->av_buffer_create(aFrame->data[0], bufferSize.value(),
                             ReleaseVideoBufferWrapper, imageWrapper.get(), 0);
  if (!aFrame->buf[0]) {
    FFMPEG_LOG("Failed to allocate buffer");
    return AVERROR(EINVAL);
  }

  auto* imageWrapperPtr = imageWrapper.forget().take();
  imageWrapperPtr->StartTracking();

  FFMPEG_LOG("Created av buffer, buf={}, data={}, image={}, sz={}",
             fmt::ptr(aFrame->buf[0]), fmt::ptr(aFrame->data[0]),
             fmt::ptr(imageWrapperPtr), bufferSize.value());
  mIsUsingShmemBufferForDecode = Some(true);
  return 0;
}
#endif

void FFmpegVideoDecoder<LIBAV_VER>::InitCodecContext() {
  mCodecContext->width = mInfo.mImage.width;
  mCodecContext->height = mInfo.mImage.height;

  // We use the same logic as libvpx in determining the number of threads to use
  // so that we end up behaving in the same fashion when using ffmpeg as
  // we would otherwise cause various crashes (see bug 1236167)
  int decode_threads = 1;
  if (mInfo.mDisplay.width >= 2048) {
    decode_threads = 8;
  } else if (mInfo.mDisplay.width >= 1024) {
    decode_threads = 4;
  } else if (mInfo.mDisplay.width >= 320) {
    decode_threads = 2;
  }

  if (mLowLatency) {
    mCodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    // ffvp9 and ffvp8 at this stage do not support slice threading, but it may
    // help with the h264 decoder if there's ever one.
    mCodecContext->thread_type = FF_THREAD_SLICE;
  } else {
    decode_threads = std::min(decode_threads, PR_GetNumberOfProcessors() - 1);
    decode_threads = std::max(decode_threads, 1);
    mCodecContext->thread_count = decode_threads;
    if (decode_threads > 1) {
      mCodecContext->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
    }
  }

  // FFmpeg will call back to this to negotiate a video pixel format.
  mCodecContext->get_format = ChoosePixelFormat;
#ifdef CUSTOMIZED_BUFFER_ALLOCATION
  FFMPEG_LOG("Set get_buffer2 for customized buffer allocation");
  mCodecContext->get_buffer2 = GetVideoBufferWrapper;
  mCodecContext->opaque = this;
#  if FF_API_THREAD_SAFE_CALLBACKS
  mCodecContext->thread_safe_callbacks = 1;
#  endif
#endif
}

nsCString FFmpegVideoDecoder<LIBAV_VER>::GetCodecName() const {
#if LIBAVCODEC_VERSION_MAJOR > 53
  return nsCString(mLib->avcodec_descriptor_get(mCodecID)->name);
#else
  return nsLiteralCString("FFmpegAudioDecoder");
#endif
}

#ifdef MOZ_USE_HWDECODE
void FFmpegVideoDecoder<LIBAV_VER>::InitHWCodecContext(ContextType aType) {
  mCodecContext->width = mInfo.mImage.width;
  mCodecContext->height = mInfo.mImage.height;
  mCodecContext->thread_count = 1;
  // Mirror the SW-path cap from FFmpegDataDecoder::InitDecoder. The four
  // HW init paths (VAAPI/V4L2/D3D11VA/MediaCodec) each call this helper
  // between avcodec_alloc_context3 and avcodec_open2, so one assignment
  // here covers all of them.
  mCodecContext->max_pixels = MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT;

  switch (aType) {
    case ContextType::V4L2:
      mCodecContext->get_format = ChooseV4L2PixelFormat;
      break;
    case ContextType::VAAPI:
      mCodecContext->get_format = ChooseVAAPIPixelFormat;
      break;
#  ifdef MOZ_USE_HWDECODE_VULKAN
    case ContextType::Vulkan:
      mCodecContext->get_format = ChooseVulkanPixelFormat;
      break;
#  endif
    case ContextType::D3D11VA:
      MOZ_DIAGNOSTIC_ASSERT(aType == ContextType::D3D11VA);
      mCodecContext->get_format = ChooseD3D11VAPixelFormat;
      break;
    case ContextType::MediaCodec:
      mCodecContext->get_format = ChooseMediaCodecPixelFormat;
      break;
    default:
      break;
  }

  if (mCodecID == AV_CODEC_ID_H264) {
    mCodecContext->extra_hw_frames =
        AssertedCast<int>(H264::ComputeMaxRefFrames(mInfo.mExtraData));
  } else if (mCodecID == AV_CODEC_ID_HEVC) {
    mCodecContext->extra_hw_frames =
        AssertedCast<int>(H265::ComputeMaxRefFrames(mInfo.mExtraData));
  } else {
    mCodecContext->extra_hw_frames = EXTRA_HW_FRAMES;
  }
  if (mLowLatency) {
    mCodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
  }
}
#endif

static bool IsKeyFrame(const AVFrame* aFrame) {
#if LIBAVCODEC_VERSION_MAJOR > 61
  return !!(aFrame->flags & AV_FRAME_FLAG_KEY);
#else
  return !!aFrame->key_frame;
#endif
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::DoDecode(
    MediaRawData* aSample, uint8_t* aData, int aSize, bool* aGotFrame,
    MediaDataDecoder::DecodedData& aResults) {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
  AVPacket* packet;

#if LIBAVCODEC_VERSION_MAJOR >= 61
  packet = mLib->av_packet_alloc();
  auto raii = MakeScopeExit([&]() { mLib->av_packet_free(&packet); });
#else
  AVPacket packet_mem;
  packet = &packet_mem;
  mLib->av_init_packet(packet);
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 58
  mDecodeStats.DecodeStart();
#endif

  packet->data = aData;
  packet->size = aSize;
  packet->dts = aSample->mTimecode.ToMicroseconds();
  packet->pts = aSample->mTime.ToMicroseconds();
  packet->flags = aSample->mKeyframe ? AV_PKT_FLAG_KEY : 0;
  packet->pos = aSample->mOffset;

  mTrackingId.apply([&](const auto& aId) {
    MediaInfoFlag flag = MediaInfoFlag::None;
    flag |= (aSample->mKeyframe ? MediaInfoFlag::KeyFrame
                                : MediaInfoFlag::NonKeyFrame);
    flag |= (IsHardwareAccelerated() ? MediaInfoFlag::HardwareDecoding
                                     : MediaInfoFlag::SoftwareDecoding);
    switch (mCodecID) {
      case AV_CODEC_ID_H264:
        flag |= MediaInfoFlag::VIDEO_H264;
        break;
#if LIBAVCODEC_VERSION_MAJOR >= 54
      case AV_CODEC_ID_VP8:
        flag |= MediaInfoFlag::VIDEO_VP8;
        break;
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 55
      case AV_CODEC_ID_VP9:
        flag |= MediaInfoFlag::VIDEO_VP9;
        break;
      case AV_CODEC_ID_HEVC:
        flag |= MediaInfoFlag::VIDEO_HEVC;
        break;
#endif
#ifdef FFMPEG_AV1_DECODE
      case AV_CODEC_ID_AV1:
        flag |= MediaInfoFlag::VIDEO_AV1;
        break;
#endif
      default:
        break;
    }
    mPerformanceRecorder.Start(
        packet->dts,
        nsPrintfCString("FFmpegVideoDecoder(%d)", LIBAVCODEC_VERSION_MAJOR),
        aId, flag);
  });


#ifdef MOZ_FFMPEG_USE_INPUT_INFO_MAP
  {
    InsertInputInfo(aSample);
  }
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 58
  if (aData || !mHasSentDrainPacket) {
    packet->duration = aSample->mDuration.ToMicroseconds();
    int res = mLib->avcodec_send_packet(mCodecContext, packet);
    if (res < 0) {
      // In theory, avcodec_send_packet could sent -EAGAIN should its internal
      // buffers be full. In practice this can't happen as we only feed one
      // frame at a time, and we immediately call avcodec_receive_frame right
      // after.
      char errStr[AV_ERROR_MAX_STRING_SIZE];
      mLib->av_strerror(res, errStr, AV_ERROR_MAX_STRING_SIZE);
      FFMPEG_LOG("avcodec_send_packet error: {} (code={})", errStr, res);
      nsresult rv;
      if (res == int(AVERROR_EOF)) {
        rv = MaybeQueueDrain(aResults) ? NS_ERROR_NOT_AVAILABLE
                                       : NS_ERROR_DOM_MEDIA_END_OF_STREAM;
      } else {
        rv = NS_ERROR_DOM_MEDIA_DECODE_ERR;
      }
      return MediaResult(
          rv, RESULT_DETAIL("avcodec_send_packet error: %s", errStr));
    }
  }
  if (!aData) {
    // On some platforms (e.g. Android), there are a limited number of output
    // buffers available. When draining, we may reach this limit, so we must
    // return what we have, and allow the caller to try again. We don't need to
    // resend the null packet in that case since the codec is still in the
    // draining state.
    mHasSentDrainPacket = true;
  }
  if (aGotFrame) {
    *aGotFrame = false;
  }
  do {
    if (!PrepareFrame()) {
      NS_WARNING("FFmpeg decoder failed to allocate frame.");
      return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
    }


    int res = mLib->avcodec_receive_frame(mCodecContext, mFrame);
    int64_t fpos =
#  if LIBAVCODEC_VERSION_MAJOR > 61
        packet->pos;
#  else
        mFrame->pkt_pos;
#  endif
    if (res == int(AVERROR_EOF)) {
      if (MaybeQueueDrain(aResults)) {
        FFMPEG_LOG("  Output buffer shortage.");
        return NS_ERROR_NOT_AVAILABLE;
      }
      FFMPEG_LOG("  End of stream.");
      return NS_ERROR_DOM_MEDIA_END_OF_STREAM;
    }
    if (res == AVERROR(EAGAIN)) {
      // MediaCodec produces no frame while it has no free output buffer. When
      // draining, giving up here loses the frames it is still holding.
      if (!aData && MaybeQueueDrain(aResults)) {
        FFMPEG_LOG("  Output buffer shortage while draining.");
        return NS_ERROR_NOT_AVAILABLE;
      }
      return NS_OK;
    }
    if (res < 0) {
      char errStr[AV_ERROR_MAX_STRING_SIZE];
      mLib->av_strerror(res, errStr, AV_ERROR_MAX_STRING_SIZE);
      FFMPEG_LOG("  avcodec_receive_frame error: {}", errStr);
      return MediaResult(
          NS_ERROR_DOM_MEDIA_DECODE_ERR,
          RESULT_DETAIL("avcodec_receive_frame error: %s", errStr));
    }

    MediaResult rv;
#  ifdef MOZ_USE_HWDECODE
    if (IsHardwareAccelerated()) {
#    if   defined(MOZ_ENABLE_D3D11VA)
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      rv = CreateImageD3D11(fpos, GetFramePts(mFrame), Duration(mFrame),
                            aResults);
#    else
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                         RESULT_DETAIL("No HW decoding implementation!"));
#    endif
    } else
#  endif
    {
      mDecodeStats.UpdateDecodeTimes(Duration(mFrame));
      rv = CreateImage(fpos, GetFramePts(mFrame), Duration(mFrame), aResults);
    }
    if (NS_FAILED(rv)) {
      return rv;
    }

    RecordFrame(aSample, aResults.LastElement());
    if (aGotFrame) {
      *aGotFrame = true;
    }
  } while (true);
#else
  if (!PrepareFrame()) {
    NS_WARNING("FFmpeg decoder failed to allocate frame.");
    return MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
  }

  // Required with old version of FFmpeg/LibAV
  mFrame->reordered_opaque = AV_NOPTS_VALUE;

  int decoded;
  int bytesConsumed =
      mLib->avcodec_decode_video2(mCodecContext, mFrame, &decoded, packet);

  FFMPEG_LOG(
      "DoDecodeFrame:decode_video: rv={} decoded={} "
      "(Input: pts({}) dts({}) Output: pts({}) "
      "opaque({}) pts({}) pkt_dts({}))",
      bytesConsumed, decoded, packet->pts, packet->dts, mFrame->pts,
      mFrame->reordered_opaque, mFrame->pts, mFrame->pkt_dts);

  if (bytesConsumed < 0) {
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR,
                       RESULT_DETAIL("FFmpeg video error: %d", bytesConsumed));
  }

  if (!decoded) {
    if (aGotFrame) {
      *aGotFrame = false;
    }
    return NS_OK;
  }

  // If we've decoded a frame then we need to output it
  int64_t pts =
      mPtsContext.GuessCorrectPts(GetFramePts(mFrame), mFrame->pkt_dts);

  InputInfo info(aSample);
  TakeInputInfo(mFrame, info);

  MediaResult rv = CreateImage(aSample->mOffset, pts, info.mDuration, aResults);
  if (NS_FAILED(rv)) {
    return rv;
  }

  mTrackingId.apply(
      [&](const auto&) { RecordFrame(aSample, aResults.LastElement()); });

  if (aGotFrame) {
    *aGotFrame = true;
  }
  return rv;
#endif
}

void FFmpegVideoDecoder<LIBAV_VER>::RecordFrame(const MediaRawData* aSample,
                                                const MediaData* aData) {
  mPerformanceRecorder.Record(
      aData->mTimecode.ToMicroseconds(), [&](auto& aStage) {
        aStage.SetResolution(mFrame->width, mFrame->height);
        auto format = [&]() -> Maybe<DecodeStage::ImageFormat> {
          switch (mCodecContext->pix_fmt) {
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
            case AV_PIX_FMT_YUV420P10LE:
#if LIBAVCODEC_VERSION_MAJOR >= 57
            case AV_PIX_FMT_YUV420P12LE:
#endif
              return Some(DecodeStage::YUV420P);
            case AV_PIX_FMT_YUV422P:
            case AV_PIX_FMT_YUV422P10LE:
#if LIBAVCODEC_VERSION_MAJOR >= 57
            case AV_PIX_FMT_YUV422P12LE:
#endif
              return Some(DecodeStage::YUV422P);
            case AV_PIX_FMT_YUV444P:
            case AV_PIX_FMT_YUVJ444P:
            case AV_PIX_FMT_YUV444P10LE:
#if LIBAVCODEC_VERSION_MAJOR >= 57
            case AV_PIX_FMT_YUV444P12LE:
#endif
              return Some(DecodeStage::YUV444P);
            case AV_PIX_FMT_GBRP:
            case AV_PIX_FMT_GBRP10LE:
              return Some(DecodeStage::GBRP);
            case AV_PIX_FMT_VAAPI_VLD:
              return Some(DecodeStage::VAAPI_SURFACE);
#ifdef MOZ_ENABLE_D3D11VA
            case AV_PIX_FMT_D3D11:
              return Some(DecodeStage::D3D11_SURFACE);
#endif
            default:
              return Nothing();
          }
        }();
        format.apply([&](auto& aFmt) { aStage.SetImageFormat(aFmt); });
        aStage.SetColorDepth(GetColorDepth(mCodecContext->pix_fmt));
        aStage.SetYUVColorSpace(GetFrameColorSpace());
        aStage.SetColorRange(GetFrameColorRange());
        aStage.SetStartTimeAndEndTime(aSample->mTime.ToMicroseconds(),
                                      aSample->GetEndTime().ToMicroseconds());
      });
}


bool FFmpegVideoDecoder<LIBAV_VER>::MaybeQueueDrain(
    const MediaDataDecoder::DecodedData& aData) {
  return false;
}

gfx::ColorDepth FFmpegVideoDecoder<LIBAV_VER>::GetColorDepth(
    const AVPixelFormat& aFormat) const {
  switch (aFormat) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
      return gfx::ColorDepth::COLOR_8;
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_GBRP10LE:
      return gfx::ColorDepth::COLOR_10;
#if LIBAVCODEC_VERSION_MAJOR >= 57
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV444P12LE:
      return gfx::ColorDepth::COLOR_12;
#endif
#ifdef MOZ_ENABLE_D3D11VA
    case AV_PIX_FMT_D3D11:
#endif
    case AV_PIX_FMT_VAAPI_VLD:
      return mInfo.mColorDepth;
    default:
      MOZ_ASSERT_UNREACHABLE("Not supported format?");
      return gfx::ColorDepth::COLOR_8;
  }
}

gfx::YUVColorSpace FFmpegVideoDecoder<LIBAV_VER>::GetFrameColorSpace() const {
  AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 58
  colorSpace = mFrame->colorspace;
#else
  if (mLib->av_frame_get_colorspace) {
    colorSpace = (AVColorSpace)mLib->av_frame_get_colorspace(mFrame);
  }
#endif
  return TransferAVColorSpaceToColorSpace(
      colorSpace, (AVPixelFormat)mFrame->format,
      gfx::IntSize{mFrame->width, mFrame->height});
}

gfx::ColorSpace2 FFmpegVideoDecoder<LIBAV_VER>::GetFrameColorPrimaries() const {
  AVColorPrimaries colorPrimaries = AVCOL_PRI_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 57
  colorPrimaries = mFrame->color_primaries;
#endif
  return TransferAVColorPrimaries(colorPrimaries)
      .valueOr(mInfo.mColorPrimaries.valueOr(gfx::ColorSpace2::UNKNOWN));
}

Maybe<gfx::TransferFunction>
FFmpegVideoDecoder<LIBAV_VER>::GetFrameTransferFunction() const {
  AVColorTransferCharacteristic transfer = AVCOL_TRC_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 57
  transfer = mFrame->color_trc;
#endif
  if (Maybe<gfx::TransferFunction> transferFunction =
          TransferAVTransferFunction(transfer)) {
    return transferFunction;
  }
  return mInfo.mTransferFunction;
}

gfx::ColorRange FFmpegVideoDecoder<LIBAV_VER>::GetFrameColorRange() const {
  AVColorRange range = AVCOL_RANGE_UNSPECIFIED;
#if LIBAVCODEC_VERSION_MAJOR > 58
  range = mFrame->color_range;
#else
  if (mLib->av_frame_get_color_range) {
    range = (AVColorRange)mLib->av_frame_get_color_range(mFrame);
  }
#endif
  return GetColorRange(range);
}

gfx::SurfaceFormat FFmpegVideoDecoder<LIBAV_VER>::GetSurfaceFormat() const {
  switch (mInfo.mColorDepth) {
    case gfx::ColorDepth::COLOR_8:
      return gfx::SurfaceFormat::NV12;
    case gfx::ColorDepth::COLOR_10:
      return gfx::SurfaceFormat::P010;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected surface type");
      return gfx::SurfaceFormat::NV12;
  }
}


MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImage(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  FFMPEG_LOG("Got one frame output with pts={} dts={} duration={}", aPts,
             mFrame->pkt_dts, aDuration);

  VideoData::QuantizableBuffer b;
  b.mPlanes[0].mData = mFrame->data[0];
  b.mPlanes[1].mData = mFrame->data[1];
  b.mPlanes[2].mData = mFrame->data[2];

  b.mPlanes[0].mStride = mFrame->linesize[0];
  b.mPlanes[1].mStride = mFrame->linesize[1];
  b.mPlanes[2].mStride = mFrame->linesize[2];

  b.mPlanes[0].mSkip = 0;
  b.mPlanes[1].mSkip = 0;
  b.mPlanes[2].mSkip = 0;

  b.mPlanes[0].mWidth = mFrame->width;
  b.mPlanes[0].mHeight = mFrame->height;
  SetChromaPlaneGeometryFromAVFormat(b, static_cast<int>(mFrame->format),
                                     mFrame->width, mFrame->height);
#if LIBAVCODEC_VERSION_MAJOR >= 58
  if (!FramePlanesFitBackingBuffers(mFrame, b)) {
    FFMPEG_LOG(
        "Decoded frame plane layout does not match its buffers: format {} "
        "{}x{}, Y {}x{} stride {}, CbCr {}x{} stride {}, buffer sizes {} {} {}",
        mFrame->format, mFrame->width, mFrame->height, b.mPlanes[0].mWidth,
        b.mPlanes[0].mHeight, b.mPlanes[0].mStride, b.mPlanes[1].mWidth,
        b.mPlanes[1].mHeight, b.mPlanes[1].mStride, FrameBufferSize(mFrame, 0),
        FrameBufferSize(mFrame, 1), FrameBufferSize(mFrame, 2));
    return MediaResult(
        NS_ERROR_DOM_MEDIA_DECODE_ERR,
        RESULT_DETAIL("Decoded frame plane layout does not match its buffers"));
  }
#endif
  b.mYUVColorSpace = GetFrameColorSpace();
  b.mColorPrimaries = GetFrameColorPrimaries();
  b.mColorRange = GetFrameColorRange();

  RefPtr<VideoData> v;
#ifdef CUSTOMIZED_BUFFER_ALLOCATION
  bool requiresCopy = false;
  // Bug 1765388: macOS needs to generate a MacIOSurfaceImage in order to
  // properly display HDR video. The later call to ::CreateAndCopyData does
  // that. If this shared memory buffer path also generated a
  // MacIOSurfaceImage, then we could use it for HDR.
  requiresCopy = (b.mColorDepth != gfx::ColorDepth::COLOR_8);
  if (mIsUsingShmemBufferForDecode && *mIsUsingShmemBufferForDecode &&
      !requiresCopy) {
    auto* wrapper = static_cast<ImageBufferWrapper*>(
        mLib->av_buffer_get_opaque(mFrame->buf[0]));
    MOZ_ASSERT(wrapper);
    FFMPEG_LOGV("Create a video data from a shmem image={}", fmt::ptr(wrapper));
    v = VideoData::CreateFromImage(
        mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
        TimeUnit::FromMicroseconds(aDuration), wrapper->AsImage(),
        IsKeyFrame(mFrame), TimeUnit::FromMicroseconds(-1));
  }
#endif
  if (!v) {
    if (m8BitOutput && b.mColorDepth != gfx::ColorDepth::COLOR_8) {
      MediaResult ret = b.To8BitPerChannel(m8BitRecycleBin);
      if (NS_FAILED(ret.Code())) {
        FFMPEG_LOG("{}: {}", __func__, ret.Message().get());
        return ret;
      }
    }
    VideoInfo info = mInfo;
    info.mTransferFunction = GetFrameTransferFunction();
    Result<already_AddRefed<VideoData>, MediaResult> r =
        VideoData::CreateAndCopyData(
            info, mImageContainer, aOffset, TimeUnit::FromMicroseconds(aPts),
            TimeUnit::FromMicroseconds(aDuration), b, IsKeyFrame(mFrame),
            TimeUnit::FromMicroseconds(mFrame->pkt_dts),
            info.ScaledImageRect(mFrame->width, mFrame->height),
            mImageAllocator);
    if (r.isErr()) {
      return r.unwrapErr();
    }
    v = r.unwrap();
  }
  MOZ_ASSERT(v);
  aResults.AppendElement(std::move(v));
  return NS_OK;
}


RefPtr<MediaDataDecoder::FlushPromise>
FFmpegVideoDecoder<LIBAV_VER>::ProcessFlush() {
  FFMPEG_LOG("ProcessFlush()");
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());
#if LIBAVCODEC_VERSION_MAJOR >= 58
  mHasSentDrainPacket = false;
#endif
#if LIBAVCODEC_VERSION_MAJOR < 58
  mPtsContext.Reset();
#endif
#ifdef MOZ_FFMPEG_USE_DURATION_MAP
  mDurationMap.Clear();
#endif
  mPerformanceRecorder.Record(std::numeric_limits<int64_t>::max());
  return FFmpegDataDecoder::ProcessFlush();
}



AVCodecID FFmpegVideoDecoder<LIBAV_VER>::GetCodecId(
    const nsACString& aMimeType) {
  if (MP4Decoder::IsH264(aMimeType)) {
    return AV_CODEC_ID_H264;
  }

#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (MP4Decoder::IsHEVC(aMimeType)) {
    return AV_CODEC_ID_HEVC;
  }
#endif

  if (aMimeType.EqualsLiteral("video/x-vnd.on2.vp6")) {
    return AV_CODEC_ID_VP6F;
  }

#if LIBAVCODEC_VERSION_MAJOR >= 54
  if (VPXDecoder::IsVP8(aMimeType)) {
    return AV_CODEC_ID_VP8;
  }
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 55
  if (VPXDecoder::IsVP9(aMimeType)) {
    return AV_CODEC_ID_VP9;
  }
#endif

#if defined(FFMPEG_AV1_DECODE)
  if (AOMDecoder::IsAV1(aMimeType)) {
    return AV_CODEC_ID_AV1;
  }
#endif

  return AV_CODEC_ID_NONE;
}

void FFmpegVideoDecoder<LIBAV_VER>::ProcessShutdown() {
  MOZ_ASSERT(mTaskQueue->IsOnCurrentThread());

  // Shutdown order for Vulkan hw decode:
  // 1. avcodec_free_context() via ProcessShutdown() — FFmpeg must finish
  //    ff_vk_uninit before we touch Firefox-owned Vulkan objects.
  // 2. mVulkanDecoder.Cleanup() — uses mDeviceWaitIdle and destroys our
  //    command pools/fences; must run while mVulkanDeviceContext still keeps
  //    the AVHWDeviceContext alive.
  // 3. av_buffer_unref(&mVulkanDeviceContext) — extra refs from this decoder.
  // 4. VulkanDeviceHolder::Drop — always (no-op if already nullptr); last
  //    process-wide client may vkDestroyDevice under sDeviceHolders.
  FFmpegDataDecoder<LIBAV_VER>::ProcessShutdown();
#ifdef MOZ_ENABLE_D3D11VA
  if (IsHardwareAccelerated()) {
    AVHWDeviceContext* hwctx =
        reinterpret_cast<AVHWDeviceContext*>(mD3D11VADeviceContext->data);
    AVD3D11VADeviceContext* d3d11vactx =
        reinterpret_cast<AVD3D11VADeviceContext*>(hwctx->hwctx);
    d3d11vactx->device = nullptr;
    mLib->av_buffer_unref(&mD3D11VADeviceContext);
    mD3D11VADeviceContext = nullptr;
  }
#endif
}

bool FFmpegVideoDecoder<LIBAV_VER>::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
#if   defined(MOZ_ENABLE_D3D11VA)
  return !!mD3D11VADeviceContext;
#else
  return false;
#endif
}


#ifdef MOZ_ENABLE_D3D11VA
MediaResult FFmpegVideoDecoder<LIBAV_VER>::InitD3D11VADecoder() {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsGPUProcess());
  FFMPEG_LOG("Initialising D3D11VA FFmpeg decoder");
  StaticMutexAutoLock mon(sMutex);

  if (!mImageAllocator || !mImageAllocator->SupportsD3D11()) {
    FFMPEG_LOG("  no KnowsCompositor or it doesn't support D3D11");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  if (mInfo.mColorDepth > gfx::ColorDepth::COLOR_10) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("not supported color depth"));
  }

  AVCodec* codec = FindVideoHardwareAVCodec(mLib, mCodecID);
  if (!codec) {
    FFMPEG_LOG("  couldn't find d3d11va decoder for {}",
               AVCodecToString(mCodecID));
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       RESULT_DETAIL("unable to find codec"));
  }
  FFMPEG_LOG("  codec {} : {}", codec->name, codec->long_name);

  if (!(mCodecContext = mLib->avcodec_alloc_context3(codec))) {
    FFMPEG_LOG("  couldn't init d3d11va ffmpeg context");
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mCodecContext->opaque = this;
  InitHWCodecContext(ContextType::D3D11VA);

  // MOZ_REQUIRES isn't recognized in MakeScopeExit, but InitD3D11VADecoder
  // already locks sMutex at the start, so just escape thread analysis.
  auto releaseResources = MakeScopeExit([&]() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    ReleaseCodecContext();
    if (mD3D11VADeviceContext) {
      AVHWDeviceContext* hwctx =
          reinterpret_cast<AVHWDeviceContext*>(mD3D11VADeviceContext->data);
      AVD3D11VADeviceContext* d3d11vactx =
          reinterpret_cast<AVD3D11VADeviceContext*>(hwctx->hwctx);
      d3d11vactx->device = nullptr;
      mLib->av_buffer_unref(&mD3D11VADeviceContext);
      mD3D11VADeviceContext = nullptr;
    }
    mDXVA2Manager.reset();
  });

  FFMPEG_LOG("  creating device context");
  mD3D11VADeviceContext = mLib->av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
  if (!mD3D11VADeviceContext) {
    FFMPEG_LOG("  av_hwdevice_ctx_alloc failed.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  nsAutoCString failureReason;
  mDXVA2Manager.reset(
      DXVA2Manager::CreateD3D11DXVA(mImageAllocator, failureReason));
  if (!mDXVA2Manager) {
    FFMPEG_LOG("  failed to create dxva manager.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  ID3D11Device* device = mDXVA2Manager->GetD3D11Device();
  if (!device) {
    FFMPEG_LOG("  failed to get D3D11 device.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  AVHWDeviceContext* hwctx = (AVHWDeviceContext*)mD3D11VADeviceContext->data;
  AVD3D11VADeviceContext* d3d11vactx = (AVD3D11VADeviceContext*)hwctx->hwctx;
  d3d11vactx->device = device;

  if (mLib->av_hwdevice_ctx_init(mD3D11VADeviceContext) < 0) {
    FFMPEG_LOG("  av_hwdevice_ctx_init failed.");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  mCodecContext->hw_device_ctx = mLib->av_buffer_ref(mD3D11VADeviceContext);
  MediaResult ret = AllocateExtraData();
  if (NS_FAILED(ret)) {
    FFMPEG_LOG("  failed to allocate extradata.");
    return ret;
  }

  if (mLib->avcodec_open2(mCodecContext, codec, nullptr) < 0) {
    FFMPEG_LOG("  avcodec_open2 failed for d3d11va decoder");
    return NS_ERROR_DOM_MEDIA_FATAL_ERR;
  }

  FFMPEG_LOG("  D3D11VA FFmpeg init successful");
  releaseResources.release();
  return NS_OK;
}

MediaResult FFmpegVideoDecoder<LIBAV_VER>::CreateImageD3D11(
    int64_t aOffset, int64_t aPts, int64_t aDuration,
    MediaDataDecoder::DecodedData& aResults) {
  MOZ_DIAGNOSTIC_ASSERT(mFrame);
  MOZ_DIAGNOSTIC_ASSERT(mDXVA2Manager);

  gfx::TransferFunction transferFunction =
      mInfo.mTransferFunction.refOr(gfx::TransferFunction::BT709);
  bool isHDR = transferFunction == gfx::TransferFunction::PQ ||
               transferFunction == gfx::TransferFunction::HLG;
  HRESULT hr = mDXVA2Manager->ConfigureForSize(
      GetSurfaceFormat(), GetFrameColorSpace(), GetFrameColorRange(),
      mInfo.mColorDepth,
      mInfo.mTransferFunction.refOr(gfx::TransferFunction::BT709),
      mInfo.mHDRMetadata, mFrame->width, mFrame->height);
  if (FAILED(hr)) {
    nsPrintfCString msg("Failed to configure DXVA2Manager, hr=%lx", hr);
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }

  if (!mFrame->data[0]) {
    nsPrintfCString msg("Frame data shouldn't be null!");
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }

  ID3D11Resource* resource = reinterpret_cast<ID3D11Resource*>(mFrame->data[0]);
  RefPtr<ID3D11Texture2D> texture;
  hr = resource->QueryInterface(
      static_cast<ID3D11Texture2D**>(getter_AddRefs(texture)));
  if (FAILED(hr)) {
    nsPrintfCString msg("Failed to get ID3D11Texture2D, hr=%lx", hr);
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }

  D3D11_TEXTURE2D_DESC desc;
  texture->GetDesc(&desc);

  auto format = [&]() {
    if (desc.Format == DXGI_FORMAT_P010) {
      return gfx::SurfaceFormat::P010;
    }
    if (desc.Format == DXGI_FORMAT_P016) {
      return gfx::SurfaceFormat::P016;
    }
    if (isHDR) {
      return gfx::SurfaceFormat::P010;
    }
    MOZ_ASSERT(desc.Format == DXGI_FORMAT_NV12);
    return gfx::SurfaceFormat::NV12;
  }();

  RefPtr<Image> image;
  gfx::IntRect pictureRegion =
      mInfo.ScaledImageRect(mFrame->width, mFrame->height);
  UINT index = (uintptr_t)mFrame->data[1];

  // TODO(https://bugzilla.mozilla.org/show_bug.cgi?id=2008886)
  // Currently the zero-copy path supports NV12 but not P010 so it can't do HDR
  // yet, this can be implemented in future.
  if (format == gfx::SurfaceFormat::NV12 && CanUseZeroCopyVideoFrame()) {
    mNumOfHWTexturesInUse++;
    FFMPEGV_LOG("CreateImageD3D11, zero copy, index={} (texInUse={}), isHDR={}",
                index, mNumOfHWTexturesInUse.load(), (unsigned int)isHDR);
    hr = mDXVA2Manager->WrapTextureWithImage(
        new D3D11TextureWrapper(
            mFrame, mLib, texture, format, index,
            [self = RefPtr<FFmpegVideoDecoder>(this), this]() {
              MOZ_ASSERT(mNumOfHWTexturesInUse > 0);
              mNumOfHWTexturesInUse--;
            }),
        pictureRegion, getter_AddRefs(image));
  } else {
    FFMPEGV_LOG("CreateImageD3D11, copy output to a shared texture, isHDR={}",
                (unsigned int)isHDR);
    hr = mDXVA2Manager->CopyToImage(texture, index, pictureRegion,
                                    getter_AddRefs(image));
  }
  if (FAILED(hr)) {
    nsPrintfCString msg("Failed to create a D3D image");
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }
  MOZ_ASSERT(image);

  RefPtr<VideoData> v = VideoData::CreateFromImage(
      mInfo.mDisplay, aOffset, TimeUnit::FromMicroseconds(aPts),
      TimeUnit::FromMicroseconds(aDuration), image, IsKeyFrame(mFrame),
      TimeUnit::FromMicroseconds(mFrame->pkt_dts));
  if (!v) {
    nsPrintfCString msg("D3D image allocation error");
    FFMPEG_LOG("{}", msg.get());
    return MediaResult(NS_ERROR_DOM_MEDIA_DECODE_ERR, msg);
  }
  aResults.AppendElement(std::move(v));
  return NS_OK;
}

bool FFmpegVideoDecoder<LIBAV_VER>::CanUseZeroCopyVideoFrame() const {
  // When zero-copy is available, we use a hybrid approach that combines
  // zero-copy and texture copying. This prevents scenarios where all
  // zero-copy frames remain unreleased, which could block ffmpeg from
  // allocating new textures for subsequent frames. Zero-copy should only be
  // used when there is sufficient space available in the texture pool.
  return gfx::gfxVars::HwDecodedVideoZeroCopy() && mImageAllocator &&
         mImageAllocator->UsingHardwareWebRender() && mDXVA2Manager &&
         mDXVA2Manager->SupportsZeroCopyNV12Texture() &&
         mNumOfHWTexturesInUse <= EXTRA_HW_FRAMES / 2;
}
#endif


#if MOZ_USE_HWDECODE
/* static */ AVCodec* FFmpegVideoDecoder<LIBAV_VER>::FindVideoHardwareAVCodec(
    const FFmpegLibWrapper* aLib, AVCodecID aCodec,
    AVHWDeviceType aDeviceType) {
  return FindHardwareAVCodec(aLib, aCodec, aDeviceType);
}
#endif

}  // namespace mozilla

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegEncoderModule.h"

#include "EncoderConfig.h"
#include "FFmpegAudioEncoder.h"
#include "FFmpegLog.h"
#include "FFmpegUtils.h"
#include "FFmpegVideoEncoder.h"

#ifdef DEBUG
#  include "mozilla/AppShutdown.h"
#endif

#include "mozilla/StaticPrefs_media.h"
#include "mozilla/gfx/gfxVars.h"
#include "prenv.h"

// This must be the last header included
#include "FFmpegLibs.h"

using mozilla::media::EncodeSupport;
using mozilla::media::EncodeSupportSet;

namespace mozilla {

template <int V>
/* static */ void FFmpegEncoderModule<V>::Init(const FFmpegLibWrapper* aLib) {
#if (defined(XP_WIN) || defined(MOZ_WIDGET_GTK) ||                \
     defined(MOZ_WIDGET_ANDROID)) &&                              \
    defined(MOZ_USE_HWDECODE) && !defined(MOZ_FFVPX_AUDIOONLY) && \
    LIBAVCODEC_VERSION_MAJOR >= 58
  if (!XRE_IsRDDProcess() &&
      !(XRE_IsParentProcess() && PR_GetEnv("MOZ_RUN_GTEST")))
  {
    MOZ_LOG_FMT(sPEMLog, LogLevel::Debug, "No support in {} process",
                XRE_GetProcessTypeString());
    return;
  }

  if (!gfx::gfxVars::IsInitialized()) {
    MOZ_ASSERT(AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown));
    return;
  }

  struct CodecEntry {
    AVCodecID mId;
    bool mHwAllowed;
  };

  const CodecEntry kCodecIDs[] = {
  // The following open video codecs can be encoded via hardware by using the
  // system ffmpeg or ffvpx.
#  if LIBAVCODEC_VERSION_MAJOR >= 59
      {AV_CODEC_ID_AV1, gfx::gfxVars::UseAV1HwEncode()},
#  endif
      {AV_CODEC_ID_VP9, gfx::gfxVars::UseVP9HwEncode()},

  };

  // Reset the list of supported hardware codecs and reevaluate them.
  auto hwCodecs = sSupportedHWCodecs.Lock();
  hwCodecs->Clear();
  for (const auto& entry : kCodecIDs) {
    if (!entry.mHwAllowed) {
      MOZ_LOG_FMT(sPEMLog, LogLevel::Debug,
                  "Hw codec disabled by gfxVars for {}",
                  AVCodecToString(entry.mId));
      continue;
    }

    const auto* codec =
        FFmpegDataEncoder<V>::FindHardwareEncoder(aLib, entry.mId);
    if (!codec) {
      MOZ_LOG_FMT(sPEMLog, LogLevel::Debug, "No hw codec or encoder for {}",
                  AVCodecToString(entry.mId));
      continue;
    }

    hwCodecs->AppendElement(entry.mId);
    MOZ_LOG_FMT(sPEMLog, LogLevel::Debug, "Support {} for hw encoding",
                AVCodecToString(entry.mId));
  }
#endif  // (XP_WIN || MOZ_WIDGET_GTK || MOZ_WIDGET_ANDROID) && MOZ_USE_HWDECODE
        // && !MOZ_FFVPX_AUDIOONLY && LIBAVCODEC_VERSION_MAJOR >= 58
}  // namespace mozilla

template <int V>
EncodeSupportSet FFmpegEncoderModule<V>::Supports(
    const EncoderConfig& aConfig) const {
  if (!CanLikelyEncode(aConfig)) {
    return EncodeSupportSet{};
  }
  // We only support L1T2 and L1T3 ScalabilityMode in VPX and AV1 encoders via
  // libvpx and libaom for now.
  if ((aConfig.mScalabilityMode != ScalabilityMode::None)) {
    if (aConfig.mCodec == CodecType::AV1) {
      // libaom only supports SVC in CBR mode.
      if (aConfig.mBitrateMode != BitrateMode::Constant) {
        return EncodeSupportSet{};
      }
    } else if (aConfig.mCodec != CodecType::VP8 &&
               aConfig.mCodec != CodecType::VP9) {
      return EncodeSupportSet{};
    }
  }
  auto support = SupportsCodec(aConfig.mCodec);
  if (aConfig.mHardwarePreference == HardwarePreference::RequireHardware &&
      !support.contains(EncodeSupport::HardwareEncode)) {
    return {};
  }
  if (aConfig.mHardwarePreference == HardwarePreference::RequireSoftware &&
      !support.contains(EncodeSupport::SoftwareEncode)) {
    return {};
  }
  return support;
}

template <int V>
EncodeSupportSet FFmpegEncoderModule<V>::SupportsCodec(CodecType aCodec) const {
  AVCodecID id = GetFFmpegEncoderCodecId<V>(aCodec);
  if (id == AV_CODEC_ID_NONE) {
    return EncodeSupportSet{};
  }
#if LIBAVCODEC_VERSION_MAJOR >= 58
  if (id == AV_CODEC_ID_HEVC && !StaticPrefs::media_hevc_enabled()) {
    return EncodeSupportSet{};
  }
#endif
  EncodeSupportSet supports;
#ifdef MOZ_USE_HWDECODE
  if (StaticPrefs::media_ffvpx_hw_enabled()) {
    // We don't need to check the gfxVars again because we check them when we
    // populated sSupportedHWCodecs.
    auto hwCodecs = sSupportedHWCodecs.Lock();
    if (hwCodecs->Contains(static_cast<uint32_t>(id))) {
      supports += EncodeSupport::HardwareEncode;
    }
  }
#endif
  if ((XRE_IsParentProcess() || XRE_IsContentProcess() ||
       StaticPrefs::media_use_remote_encoder_video_software()) &&
      FFmpegDataEncoder<V>::FindSoftwareEncoder(mLib, id)) {
    supports += EncodeSupport::SoftwareEncode;
  }
  return supports;
}

template <int V>
already_AddRefed<MediaDataEncoder> FFmpegEncoderModule<V>::CreateVideoEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  AVCodecID codecId = GetFFmpegEncoderCodecId<V>(aConfig.mCodec);
  if (codecId == AV_CODEC_ID_NONE) {
    FFMPEGV_LOG("No ffmpeg encoder for {}", EnumValueToString(aConfig.mCodec));
    return nullptr;
  }

  RefPtr<MediaDataEncoder> encoder =
      new FFmpegVideoEncoder<V>(mLib, codecId, aTaskQueue, aConfig);
  FFMPEGV_LOG("ffmpeg {} encoder: {} has been created",
              EnumValueToString(aConfig.mCodec),
              encoder->GetDescriptionName().get());
  return encoder.forget();
}

template <int V>
already_AddRefed<MediaDataEncoder> FFmpegEncoderModule<V>::CreateAudioEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  AVCodecID codecId = GetFFmpegEncoderCodecId<V>(aConfig.mCodec);
  if (codecId == AV_CODEC_ID_NONE) {
    FFMPEGV_LOG("No ffmpeg encoder for {}", EnumValueToString(aConfig.mCodec));
    return nullptr;
  }

  RefPtr<MediaDataEncoder> encoder =
      new FFmpegAudioEncoder<V>(mLib, codecId, aTaskQueue, aConfig);
  FFMPEGA_LOG("ffmpeg {} encoder: {} has been created",
              EnumValueToString(aConfig.mCodec),
              encoder->GetDescriptionName().get());
  return encoder.forget();
}

template class FFmpegEncoderModule<LIBAV_VER>;

}  // namespace mozilla

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedTexture.h"

#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/webgpu/WebGPUParent.h"



#  include "mozilla/webgpu/SharedTextureMacIOSurface.h"

namespace mozilla::webgpu {

// static
UniquePtr<SharedTexture> SharedTexture::Create(
    WebGPUParent* aParent, const ffi::WGPUDeviceId aDeviceId,
    const uint32_t aWidth, const uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    const ffi::WGPUTextureUsages aUsage) {
  MOZ_ASSERT(aParent);

  UniquePtr<SharedTexture> texture;
  texture = SharedTextureMacIOSurface::Create(aParent, aDeviceId, aWidth,
                                              aHeight, aFormat, aUsage);
  return texture;
}

SharedTexture::SharedTexture(const uint32_t aWidth, const uint32_t aHeight,
                             const struct ffi::WGPUTextureFormat aFormat,
                             const ffi::WGPUTextureUsages aUsage)
    : mWidth(aWidth), mHeight(aHeight), mFormat(aFormat), mUsage(aUsage) {}

SharedTexture::~SharedTexture() {
  MOZ_ASSERT_IF(mTextureHost,
                gfx::CanvasRenderThread::IsInCanvasRenderThread());
}

RefPtr<layers::TextureHost> SharedTexture::GetTextureHost() {
  return mTextureHost;
}

void SharedTexture::SetTextureHost(layers::TextureHost* aTextureHost) {
  MOZ_ASSERT(aTextureHost);
  MOZ_ASSERT(!mTextureHost);

  mTextureHost = aTextureHost;
}

void SharedTexture::ClearTextureHost() { mTextureHost = nullptr; }

void SharedTexture::SetSubmissionIndex(uint64_t aSubmissionIndex) {
  MOZ_ASSERT(aSubmissionIndex != 0);

  mSubmissionIndex = aSubmissionIndex;
}

UniquePtr<SharedTextureReadBackPresent> SharedTextureReadBackPresent::Create(
    const uint32_t aWidth, const uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    const ffi::WGPUTextureUsages aUsage) {
  return MakeUnique<SharedTextureReadBackPresent>(aWidth, aHeight, aFormat,
                                                  aUsage);
}

SharedTextureReadBackPresent::SharedTextureReadBackPresent(
    const uint32_t aWidth, const uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    const ffi::WGPUTextureUsages aUsage)
    : SharedTexture(aWidth, aHeight, aFormat, aUsage) {}

SharedTextureReadBackPresent::~SharedTextureReadBackPresent() = default;

}  // namespace mozilla::webgpu

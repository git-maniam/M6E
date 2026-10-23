/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurface.h"

#include "../2d/2D.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLReadTexImageHelper.h"
#include "GLScreenBuffer.h"
#include "ScopedGLHelpers.h"
#include "SharedSurfaceEGL.h"
#include "SharedSurfaceGL.h"
#include "VRManagerChild.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/layers/TextureHost.h"
#include "nsThreadUtils.h"


#  include "SharedSurfaceIO.h"



namespace mozilla {
namespace gl {

////////////////////////////////////////////////////////////////////////
// SharedSurface

SharedSurface::SharedSurface(const SharedSurfaceDesc& desc,
                             UniquePtr<MozFramebuffer> fb)
    : mDesc(desc), mFb(std::move(fb)) {}

SharedSurface::~SharedSurface() {
  MOZ_ASSERT_IF(mTextureHost,
                gfx::CanvasRenderThread::IsInCanvasRenderThread());
}

RefPtr<layers::TextureHost> SharedSurface::GetTextureHost() {
  return mTextureHost;
}

void SharedSurface::SetTextureHost(layers::TextureHost* aTextureHost) {
  MOZ_ASSERT(aTextureHost);
  MOZ_ASSERT(!mTextureHost);

  mTextureHost = aTextureHost;
}

void SharedSurface::ClearTextureHost() { mTextureHost = nullptr; }

void SharedSurface::LockProd() {
  MOZ_ASSERT(!mIsLocked);

  LockProdImpl();

  mDesc.gl->LockSurface(this);
  mIsLocked = true;
}

void SharedSurface::UnlockProd() {
  if (!mIsLocked) return;

  UnlockProdImpl();

  mDesc.gl->UnlockSurface(this);
  mIsLocked = false;
}

////////////////////////////////////////////////////////////////////////
// SurfaceFactory

/* static */
UniquePtr<SurfaceFactory> SurfaceFactory::Create(
    GLContext* const pGl, const layers::TextureType consumerType) {
  auto& gl = *pGl;

  switch (consumerType) {
    case layers::TextureType::D3D11:
      break;

    case layers::TextureType::MacIOSurface:
      return MakeUnique<SurfaceFactory_IOSurface>(gl);

    case layers::TextureType::DMABUF:
      break;

    case layers::TextureType::AndroidHardwareBuffer:
      break;

    case layers::TextureType::EGLImage:
      break;

    case layers::TextureType::Unknown:
    case layers::TextureType::Last:
      break;
  }

  // Silence a warning.
  (void)gl;

  return nullptr;
}

SurfaceFactory::SurfaceFactory(const PartialSharedSurfaceDesc& partialDesc)
    : mDesc(partialDesc), mMutex("SurfaceFactor::mMutex") {}

SurfaceFactory::~SurfaceFactory() = default;

}  // namespace gl
}  // namespace mozilla

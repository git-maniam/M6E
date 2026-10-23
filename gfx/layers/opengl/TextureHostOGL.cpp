/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureHostOGL.h"

#include "GLContextEGL.h"  // for GLContext, etc
#include "GLLibraryEGL.h"  // for GLLibraryEGL
#include "GLReadTexImageHelper.h"
#include "GLUploadHelpers.h"
#include "GeckoProfiler.h"
#include "GfxTexturesReporter.h"   // for GfxTexturesReporter
#include "gfx2DGlue.h"             // for ContentForFormat, etc
#include "mozilla/gfx/2D.h"        // for DataSourceSurface
#include "mozilla/gfx/BaseSize.h"  // for BaseSize
#include "mozilla/gfx/Logging.h"   // for gfxCriticalError
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/layers/ISurfaceAllocator.h"
#include "mozilla/webrender/RenderEGLImageTextureHost.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsRegion.h"  // for nsIntRegion

#  include "mozilla/layers/MacIOSurfaceTextureHostOGL.h"



using namespace mozilla::gl;
using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

class Compositor;

void ApplySamplingFilterToBoundTexture(gl::GLContext* aGL,
                                       gfx::SamplingFilter aSamplingFilter,
                                       GLuint aTarget) {
  GLenum filter =
      (aSamplingFilter == gfx::SamplingFilter::POINT ? LOCAL_GL_NEAREST
                                                     : LOCAL_GL_LINEAR);

  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MIN_FILTER, filter);
  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MAG_FILTER, filter);
}

already_AddRefed<TextureHost> CreateTextureHostOGL(
    const SurfaceDescriptor& aDesc, ISurfaceAllocator* aDeallocator,
    LayersBackend aBackend, TextureFlags aFlags) {
  RefPtr<TextureHost> result;
  switch (aDesc.type()) {

    case SurfaceDescriptor::TEGLImageDescriptor: {
      if (aDeallocator && !aDeallocator->IsSameProcess()) {
        gfxCriticalError()
            << "EGLImageDescriptor must only be used in same process";
        return nullptr;
      }
      const EGLImageDescriptor& desc = aDesc.get_EGLImageDescriptor();
      result = new EGLImageTextureHost(aFlags, (EGLImage)desc.image(),
                                       (EGLSync)desc.fence(), desc.size(),
                                       desc.hasAlpha());
      break;
    }


    case SurfaceDescriptor::TSurfaceDescriptorMacIOSurface: {
      const SurfaceDescriptorMacIOSurface& desc =
          aDesc.get_SurfaceDescriptorMacIOSurface();
      result = new MacIOSurfaceTextureHostOGL(aFlags, desc);
      break;
    }

    case SurfaceDescriptor::TSurfaceDescriptorSharedGLTexture: {
      if (aDeallocator && !aDeallocator->IsSameProcess()) {
        gfxCriticalError() << "SurfaceDescriptorSharedGLTexture must only be "
                              "used in same process";
        return nullptr;
      }
      const auto& desc = aDesc.get_SurfaceDescriptorSharedGLTexture();
      result =
          new GLTextureHost(aFlags, desc.texture(), desc.target(),
                            (GLsync)desc.fence(), desc.size(), desc.hasAlpha());
      break;
    }
    default: {
      MOZ_ASSERT_UNREACHABLE("Unsupported SurfaceDescriptor type");
      break;
    }
  }
  return result.forget();
}

static gl::TextureImage::Flags FlagsToGLFlags(TextureFlags aFlags) {
  uint32_t result = TextureImage::NoFlags;

  if (aFlags & TextureFlags::USE_NEAREST_FILTER)
    result |= TextureImage::UseNearestFilter;
  if (aFlags & TextureFlags::ORIGIN_BOTTOM_LEFT)
    result |= TextureImage::OriginBottomLeft;
  if (aFlags & TextureFlags::DISALLOW_BIGIMAGE)
    result |= TextureImage::DisallowBigImage;

  return static_cast<gl::TextureImage::Flags>(result);
}

TextureImageTextureSourceOGL::TextureImageTextureSourceOGL(
    CompositorOGL* aCompositor, TextureFlags aFlags)
    : mGL(aCompositor->gl()),
      mCompositor(aCompositor),
      mFlags(aFlags),
      mIterating(false) {
  if (mCompositor) {
    mCompositor->RegisterTextureSource(this);
  }
}

TextureImageTextureSourceOGL::~TextureImageTextureSourceOGL() {
  DeallocateDeviceData();
}

void TextureImageTextureSourceOGL::DeallocateDeviceData() {
  mTexImage = nullptr;
  mGL = nullptr;
  if (mCompositor) {
    mCompositor->UnregisterTextureSource(this);
  }
  SetUpdateSerial(0);
}

bool TextureImageTextureSourceOGL::Update(gfx::DataSourceSurface* aSurface,
                                          nsIntRegion* aDestRegion,
                                          gfx::IntPoint* aSrcOffset,
                                          gfx::IntPoint* aDstOffset) {
  GLContext* gl = mGL;
  MOZ_ASSERT(gl);
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING(
        "trying to update TextureImageTextureSourceOGL without a GLContext");
    return false;
  }
  if (!aSurface) {
    gfxCriticalError() << "Invalid surface for OGL update";
    return false;
  }
  MOZ_ASSERT(aSurface);

  IntSize size = aSurface->GetSize();
  if (!mTexImage || (mTexImage->GetSize() != size && !aSrcOffset) ||
      mTexImage->GetContentType() !=
          gfx::ContentForFormat(aSurface->GetFormat())) {
    if (mFlags & TextureFlags::DISALLOW_BIGIMAGE) {
      GLint maxTextureSize;
      gl->fGetIntegerv(LOCAL_GL_MAX_TEXTURE_SIZE, &maxTextureSize);
      if (size.width > maxTextureSize || size.height > maxTextureSize) {
        NS_WARNING("Texture exceeds maximum texture size, refusing upload");
        return false;
      }
      // Explicitly use CreateBasicTextureImage instead of CreateTextureImage,
      // because CreateTextureImage might still choose to create a tiled
      // texture image.
      mTexImage = CreateBasicTextureImage(
          gl, size, gfx::ContentForFormat(aSurface->GetFormat()),
          LOCAL_GL_CLAMP_TO_EDGE, FlagsToGLFlags(mFlags));
    } else {
      // XXX - clarify which size we want to use. IncrementalContentHost will
      // require the size of the destination surface to be different from
      // the size of aSurface.
      // See bug 893300 (tracks the implementation of ContentHost for new
      // textures).
      mTexImage = CreateTextureImage(
          gl, size, gfx::ContentForFormat(aSurface->GetFormat()),
          LOCAL_GL_CLAMP_TO_EDGE, FlagsToGLFlags(mFlags),
          SurfaceFormatToImageFormat(aSurface->GetFormat()));
    }
    ClearCachedFilter();

    if (aDestRegion && !aSrcOffset &&
        !aDestRegion->IsEqual(gfx::IntRect(0, 0, size.width, size.height))) {
      // UpdateFromDataSource will ignore our specified aDestRegion since the
      // texture hasn't been allocated with glTexImage2D yet. Call Resize() to
      // force the allocation (full size, but no upload), and then we'll only
      // upload the pixels we care about below.
      mTexImage->Resize(size);
    }
  }

  return mTexImage->UpdateFromDataSource(aSurface, aDestRegion, aSrcOffset,
                                         aDstOffset);
}

void TextureImageTextureSourceOGL::EnsureBuffer(const IntSize& aSize,
                                                gfxContentType aContentType) {
  if (!mTexImage || mTexImage->GetSize() != aSize ||
      mTexImage->GetContentType() != aContentType) {
    mTexImage =
        CreateTextureImage(mGL, aSize, aContentType, LOCAL_GL_CLAMP_TO_EDGE,
                           FlagsToGLFlags(mFlags));
  }
  mTexImage->Resize(aSize);
}

gfx::IntSize TextureImageTextureSourceOGL::GetSize() const {
  if (mTexImage) {
    if (mIterating) {
      return mTexImage->GetTileRect().Size();
    }
    return mTexImage->GetSize();
  }
  NS_WARNING("Trying to query the size of an empty TextureSource.");
  return gfx::IntSize(0, 0);
}

gfx::SurfaceFormat TextureImageTextureSourceOGL::GetFormat() const {
  if (mTexImage) {
    return mTexImage->GetTextureFormat();
  }
  NS_WARNING("Trying to query the format of an empty TextureSource.");
  return gfx::SurfaceFormat::UNKNOWN;
}

gfx::IntRect TextureImageTextureSourceOGL::GetTileRect() {
  return mTexImage->GetTileRect();
}

void TextureImageTextureSourceOGL::BindTexture(
    GLenum aTextureUnit, gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mTexImage,
             "Trying to bind a TextureSource that does not have an underlying "
             "GL texture.");
  mTexImage->BindTexture(aTextureUnit);
  SetSamplingFilter(mGL, aSamplingFilter);
}

////////////////////////////////////////////////////////////////////////
// GLTextureSource

GLTextureSource::GLTextureSource(TextureSourceProvider* aProvider,
                                 GLuint aTextureHandle, GLenum aTarget,
                                 gfx::IntSize aSize, gfx::SurfaceFormat aFormat)
    : GLTextureSource(aProvider->GetGLContext(), aTextureHandle, aTarget, aSize,
                      aFormat) {}

GLTextureSource::GLTextureSource(GLContext* aGL, GLuint aTextureHandle,
                                 GLenum aTarget, gfx::IntSize aSize,
                                 gfx::SurfaceFormat aFormat)
    : mGL(aGL),
      mTextureHandle(aTextureHandle),
      mTextureTarget(aTarget),
      mSize(aSize),
      mFormat(aFormat) {
  MOZ_COUNT_CTOR(GLTextureSource);
}

GLTextureSource::~GLTextureSource() {
  MOZ_COUNT_DTOR(GLTextureSource);
  DeleteTextureHandle();
}

void GLTextureSource::DeallocateDeviceData() { DeleteTextureHandle(); }

void GLTextureSource::DeleteTextureHandle() {
  GLContext* gl = this->gl();
  if (mTextureHandle != 0 && gl && gl->MakeCurrent()) {
    gl->fDeleteTextures(1, &mTextureHandle);
  }
  mTextureHandle = 0;
}

void GLTextureSource::BindTexture(GLenum aTextureUnit,
                                  gfx::SamplingFilter aSamplingFilter) {
  MOZ_ASSERT(mTextureHandle != 0);
  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    return;
  }
  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, mTextureHandle);
  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool GLTextureSource::IsValid() const { return !!gl() && mTextureHandle != 0; }

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// SurfaceTextureHost


////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// EGLImage

EGLImageTextureSource::EGLImageTextureSource(TextureSourceProvider* aProvider,
                                             EGLImage aImage,
                                             gfx::SurfaceFormat aFormat,
                                             GLenum aTarget, GLenum aWrapMode,
                                             gfx::IntSize aSize)
    : mGL(aProvider->GetGLContext()),
      mCompositor(aProvider->AsCompositorOGL()),
      mImage(aImage),
      mFormat(aFormat),
      mTextureTarget(aTarget),
      mWrapMode(aWrapMode),
      mSize(aSize) {
  MOZ_ASSERT(mTextureTarget == LOCAL_GL_TEXTURE_2D ||
             mTextureTarget == LOCAL_GL_TEXTURE_EXTERNAL);
}

void EGLImageTextureSource::BindTexture(GLenum aTextureUnit,
                                        gfx::SamplingFilter aSamplingFilter) {
  GLContext* gl = this->gl();
  if (!gl || !gl->MakeCurrent()) {
    NS_WARNING("Trying to bind a texture without a GLContext");
    return;
  }

#ifdef DEBUG
  const bool supportsEglImage = [&]() {
    const auto& gle = GLContextEGL::Cast(gl);
    const auto& egl = gle->mEgl;

    return egl->HasKHRImageBase() &&
           egl->IsExtensionSupported(EGLExtension::KHR_gl_texture_2D_image) &&
           gl->IsExtensionSupported(GLContext::OES_EGL_image);
  }();
  MOZ_ASSERT(supportsEglImage, "EGLImage not supported or disabled in runtime");
#endif

  GLuint tex = mCompositor->GetTemporaryTexture(mTextureTarget, aTextureUnit);

  gl->fActiveTexture(aTextureUnit);
  gl->fBindTexture(mTextureTarget, tex);

  gl->fEGLImageTargetTexture2D(mTextureTarget, mImage);

  ApplySamplingFilterToBoundTexture(gl, aSamplingFilter, mTextureTarget);
}

bool EGLImageTextureSource::IsValid() const { return !!gl(); }

gfx::Matrix4x4 EGLImageTextureSource::GetTextureTransform() {
  gfx::Matrix4x4 ret;
  return ret;
}

////////////////////////////////////////////////////////////////////////

EGLImageTextureHost::EGLImageTextureHost(TextureFlags aFlags, EGLImage aImage,
                                         EGLSync aSync, gfx::IntSize aSize,
                                         bool hasAlpha)
    : TextureHost(TextureHostType::EGLImage, aFlags),
      mImage(aImage),
      mSync(aSync),
      mSize(aSize),
      mHasAlpha(hasAlpha) {}

EGLImageTextureHost::~EGLImageTextureHost() = default;

gl::GLContext* EGLImageTextureHost::gl() const { return nullptr; }

gfx::SurfaceFormat EGLImageTextureHost::GetFormat() const {
  return mHasAlpha ? gfx::SurfaceFormat::R8G8B8A8
                   : gfx::SurfaceFormat::R8G8B8X8;
}

void EGLImageTextureHost::CreateRenderTexture(
    const wr::ExternalImageId& aExternalImageId) {
  MOZ_ASSERT(mExternalImageId.isSome());

  RefPtr texture = MakeRefPtr<wr::RenderEGLImageTextureHost>(
      mImage, mSync, mSize, GetFormat());
  wr::RenderThread::Get()->RegisterExternalImage(aExternalImageId,
                                                 texture.forget());
}

void EGLImageTextureHost::PushResourceUpdates(
    wr::TransactionBuilder& aResources, ResourceUpdateOp aOp,
    const Range<wr::ImageKey>& aImageKeys, const wr::ExternalImageId& aExtID) {
  auto method = aOp == TextureHost::ADD_IMAGE
                    ? &wr::TransactionBuilder::AddExternalImage
                    : &wr::TransactionBuilder::UpdateExternalImage;

  // Prefer TextureExternal unless the backend requires TextureRect.
  TextureHost::NativeTexturePolicy policy =
      TextureHost::BackendNativeTexturePolicy(
          aResources.GetCapabilities().mBackendType, GetSize());
  auto imageType = policy == TextureHost::NativeTexturePolicy::REQUIRE
                       ? wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureRect)
                       : wr::ExternalImageType::TextureHandle(
                             wr::ImageBufferKind::TextureExternal);

  gfx::SurfaceFormat format = GetFormat();

  if (aImageKeys.length() != 1) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  // XXX Add RGBA handling. Temporary hack to avoid crash
  // With BGRA format setting, rendering works without problem.
  wr::ImageDescriptor descriptor(GetSize(), wr::ImageFormat::BGRA8,
                                 format == gfx::SurfaceFormat::R8G8B8A8
                                     ? wr::OpacityType::HasAlphaChannel
                                     : wr::OpacityType::Opaque);
  (aResources.*method)(aImageKeys[0], descriptor, aExtID, imageType, 0,
                       /* aNormalizedUvs */ false);
}

void EGLImageTextureHost::PushDisplayItems(
    wr::DisplayListBuilder& aBuilder, const wr::LayoutRect& aBounds,
    const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
    const Range<wr::ImageKey>& aImageKeys, PushDisplayItemFlagSet aFlags) {
  bool preferCompositorSurface =
      aFlags.contains(PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE);
  bool supportsExternalCompositing =
      SupportsExternalCompositing(aBuilder.GetBackendType());

  if (aImageKeys.length() != 1) {
    MOZ_ASSERT_UNREACHABLE("unexpected key length");
    return;
  }
  aBuilder.PushImage(aBounds, aClip, true, false, aFilter, aImageKeys[0],
                     !(mFlags & TextureFlags::NON_PREMULTIPLIED),
                     wr::ColorF{1.0f, 1.0f, 1.0f, 1.0f},
                     preferCompositorSurface, supportsExternalCompositing);
}

bool EGLImageTextureHost::SupportsExternalCompositing(
    WebRenderBackend aBackend) {
  return aBackend == WebRenderBackend::SOFTWARE;
}

//

GLTextureHost::GLTextureHost(TextureFlags aFlags, GLuint aTextureHandle,
                             GLenum aTarget, GLsync aSync, gfx::IntSize aSize,
                             bool aHasAlpha)
    : TextureHost(TextureHostType::GLTexture, aFlags),
      mTexture(aTextureHandle),
      mTarget(aTarget),
      mSync(aSync),
      mSize(aSize),
      mHasAlpha(aHasAlpha) {}

GLTextureHost::~GLTextureHost() = default;

gl::GLContext* GLTextureHost::gl() const { return nullptr; }

gfx::SurfaceFormat GLTextureHost::GetFormat() const {
  MOZ_ASSERT(mTextureSource);
  return mTextureSource ? mTextureSource->GetFormat()
                        : gfx::SurfaceFormat::UNKNOWN;
}

}  // namespace layers
}  // namespace mozilla

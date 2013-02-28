/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureClient.h"
#include "ImageClient.h"
#include "CanvasClient.h"
#include "ContentClient.h"
#include "mozilla/layers/ShadowLayers.h"
#include "SharedTextureImage.h"
#include "GLContext.h"
#include "mozilla/layers/TextureChild.h"
#include "BasicLayers.h" // for PaintContext
#include "ShmemYCbCrImage.h"
#include "gfxReusableSurfaceWrapper.h"
#include "gfxPlatform.h"
#ifdef XP_WIN
#include "mozilla/layers/TextureD3D11.h"
#endif
 
using namespace mozilla::gl;

namespace mozilla {
namespace layers {


TextureClient::TextureClient(CompositableForwarder* aForwarder,
                             CompositableType aCompositableType)
  : mLayerForwarder(aForwarder)
  , mTextureChild(nullptr)
{
  mTextureInfo.compositableType = aCompositableType;
}

TextureClient::~TextureClient()
{
  MOZ_ASSERT(mDescriptor.type() == SurfaceDescriptor::T__None, "Need to release surface!");
}

void
TextureClient::Updated()
{
  if (mDescriptor.type() != SurfaceDescriptor::T__None &&
      mDescriptor.type() != SurfaceDescriptor::Tnull_t) {
    mLayerForwarder->UpdateTexture(this, SurfaceDescriptor(mDescriptor));
    mDescriptor = SurfaceDescriptor();
  } else {
    NS_WARNING("Trying to send a null SurfaceDescriptor.");
  }
}

void
TextureClient::Destroyed()
{
  // The owning layer must be locked at some point in the chain of callers
  // by calling Hold.
  mLayerForwarder->DestroyedThebesBuffer(mDescriptor);
}

void
TextureClient::UpdatedRegion(const nsIntRegion& aUpdatedRegion,
                             const nsIntRect& aBufferRect,
                             const nsIntPoint& aBufferRotation)
{
  mLayerForwarder->UpdateTextureRegion(this,
                                       ThebesBuffer(mDescriptor,
                                                    aBufferRect,
                                                    aBufferRotation),
                                       aUpdatedRegion);
}

void
TextureClient::SetIPDLActor(PTextureChild* aChild) {
  mTextureChild = aChild;
}


TextureClientShmem::TextureClientShmem(CompositableForwarder* aForwarder, CompositableType aCompositableType)
  : TextureClient(aForwarder, aCompositableType)
  , mSurface(nullptr)
  , mSurfaceAsImage(nullptr)
{
}

void
TextureClientShmem::ReleaseResources()
{
  if (mSurface) {
    mSurface = nullptr;
    ShadowLayerForwarder::CloseDescriptor(mDescriptor);
  }
  if (IsSurfaceDescriptorValid(mDescriptor)) {
    mLayerForwarder->DestroySharedSurface(&mDescriptor);
  }
}

void
TextureClientShmem::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aContentType)
{
  if (aSize != mSize ||
      aContentType != mContentType ||
      !IsSurfaceDescriptorValid(mDescriptor)) {
    if (IsSurfaceDescriptorValid(mDescriptor)) {
      mLayerForwarder->DestroySharedSurface(&mDescriptor);
    }
    mContentType = aContentType;
    mSize = aSize;

    if (!mLayerForwarder->AllocSurfaceDescriptor(gfxIntSize(mSize.width, mSize.height), mContentType, &mDescriptor)) {
      NS_RUNTIMEABORT("creating SurfaceDescriptor failed!");
    }
  }
}


// --------- Autolock


bool AutoLockShmemClient::EnsureTextureClient(nsIntSize aSize,
                                              gfxASurface* surface,
                                              gfxASurface::gfxContentType contentType)
{
  if (!surface) {
    return false;
  }
  if (aSize != surface->GetSize() ||
      contentType != surface->GetContentType() ||
      !IsSurfaceDescriptorValid(*mDescriptor)) {
    if (IsSurfaceDescriptorValid(*mDescriptor)) {
      mTextureClient->GetForwarder()->DestroySharedSurface(mDescriptor);
    }
    if (!mTextureClient->GetForwarder()->AllocSurfaceDescriptor(aSize,
                                                                     surface->GetContentType(),
                                                                     mDescriptor)) {
      NS_WARNING("creating SurfaceDescriptor failed!");
      return false;
    }
    MOZ_ASSERT(mDescriptor->type() != SurfaceDescriptor::Tnull_t);
    MOZ_ASSERT(mDescriptor->type() != SurfaceDescriptor::T__None);
  }
  MOZ_ASSERT(mDescriptor->type() != SurfaceDescriptor::Tnull_t);
  MOZ_ASSERT(mDescriptor->type() != SurfaceDescriptor::T__None);
  return true;
}

bool AutoLockShmemClient::Update(Image* aImage, uint32_t aContentFlags, gfxPattern* pat)
{
  nsRefPtr<gfxASurface> surface = pat->GetSurface();
  CompositableType type = CompositingFactory::TypeForImage(aImage);
  if (type != BUFFER_IMAGE_SINGLE) {
    return type == BUFFER_UNKNOWN;
  }

  nsRefPtr<gfxPattern> pattern;
  pattern =  pat ? pat : new gfxPattern(surface);

  gfxIntSize size = aImage->GetSize();

  gfxASurface::gfxContentType contentType = gfxASurface::CONTENT_COLOR_ALPHA;
  bool isOpaque = (aContentFlags & Layer::CONTENT_OPAQUE);
  if (surface) {
    contentType = surface->GetContentType();
  }
  if (contentType != gfxASurface::CONTENT_ALPHA &&
      isOpaque) {
    contentType = gfxASurface::CONTENT_COLOR;
  }
  if (!EnsureTextureClient(size, surface, contentType)) {
    return false;
  }

  nsRefPtr<gfxASurface> tmpASurface =
    ShadowLayerForwarder::OpenDescriptor(OPEN_READ_WRITE,
                                         *mTextureClient->LockSurfaceDescriptor());
  nsRefPtr<gfxContext> tmpCtx = new gfxContext(tmpASurface.get());
  tmpCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
  PaintContext(pat,
               nsIntRegion(nsIntRect(0, 0, size.width, size.height)),
               1.0, tmpCtx, nullptr);

  return true;
}


bool
AutoLockYCbCrClient::Update(PlanarYCbCrImage* aImage)
{
  MOZ_ASSERT(aImage);
  MOZ_ASSERT(mDescriptor);

  const PlanarYCbCrImage::Data *data = aImage->GetData();
  NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
  if (!data) {
    return false;
  }

  if (!EnsureTextureClient(aImage)) {
    NS_RUNTIMEABORT("DARNIT!");
    return false;
  }

  ipc::Shmem& shmem = mDescriptor->get_YCbCrImage().data();

  ShmemYCbCrImage shmemImage(shmem);
  if (!shmemImage.CopyData(data->mYChannel, data->mCbChannel, data->mCrChannel,
                           data->mYSize, data->mYStride,
                           data->mCbCrSize, data->mCbCrStride,
                           data->mYSkip, data->mCbSkip)) {
    NS_WARNING("Failed to copy image data!");
    return false;
  }
  return true;
}

bool AutoLockYCbCrClient::EnsureTextureClient(PlanarYCbCrImage* aImage) {
  MOZ_ASSERT(aImage);
  if (!aImage) {
    return false;
  }

  const PlanarYCbCrImage::Data *data = aImage->GetData();
  NS_ASSERTION(data, "Must be able to retrieve yuv data from image!");
  if (!data) {
    return false;
  }

  bool needsAllocation = false;
  if (mDescriptor->type() != SurfaceDescriptor::TYCbCrImage) {
    needsAllocation = true;
  } else {
    ipc::Shmem& shmem = mDescriptor->get_YCbCrImage().data();
    ShmemYCbCrImage shmemImage(shmem);
    if (shmemImage.GetYSize() != data->mYSize ||
        shmemImage.GetCbCrSize() != data->mCbCrSize) {
      needsAllocation = true;
    }
  }

  if (!needsAllocation) {
    return true;
  }

  mTextureClient->ReleaseResources();

  ipc::SharedMemory::SharedMemoryType shmType = OptimalShmemType();
  size_t size = ShmemYCbCrImage::ComputeMinBufferSize(data->mYSize,
                                                      data->mCbCrSize);
  ipc::Shmem shmem;
  if (!mTextureClient->GetForwarder()->AllocUnsafeShmem(size, shmType, &shmem)) {
    return false;
  }

  ShmemYCbCrImage::InitializeBufferInfo(shmem.get<uint8_t>(),
                                        data->mYSize,
                                        data->mCbCrSize);


  *mDescriptor = YCbCrImage(shmem, 0);

  return true;
}

AutoLockHandleClient::AutoLockHandleClient(TextureClient* aClient,
                                           gl::GLContext* aGL,
                                           gfx::IntSize aSize,
                                           gl::GLContext::SharedTextureShareType aFlags)
: AutoLockTextureClient(aClient), mHandle(0)
{
  if (mDescriptor->type() == SurfaceDescriptor::TSharedTextureDescriptor) {
    mHandle = mDescriptor->get_SharedTextureDescriptor().handle();
  } else {
    mHandle = aGL->CreateSharedHandle(aFlags);
    *mDescriptor = SharedTextureDescriptor(aFlags,
                                          mHandle,
                                          nsIntSize(aSize.width,
                                                    aSize.height),
                                          false);
  }
}

already_AddRefed<gfxContext>
TextureClientShmem::LockContext()
{
  nsRefPtr<gfxContext> result = new gfxContext(GetSurface());
  return result.forget();
}

gfxASurface*
TextureClientShmem::GetSurface()
{
  if (!mSurface) {
    if (mDescriptor.type() == SurfaceDescriptor::T__None) {
      return nullptr;
    }
    mSurface = ShadowLayerForwarder::OpenDescriptor(OPEN_READ_WRITE, mDescriptor);
  }

  return mSurface.get();
}

void
TextureClientShmem::Unlock()
{
  mSurface = nullptr;
  mSurfaceAsImage = nullptr;

  ShadowLayerForwarder::CloseDescriptor(mDescriptor);
}

gfxImageSurface*
TextureClientShmem::LockImageSurface()
{
  if (!mSurfaceAsImage) {
    mSurfaceAsImage = GetSurface()->GetAsImageSurface();
  }

  return mSurfaceAsImage.get();
}

void
TextureClientShmemYCbCr::ReleaseResources()
{
  GetForwarder()->DestroySharedSurface(&mDescriptor);
}

void
TextureClientShmemYCbCr::SetDescriptor(const SurfaceDescriptor& aDescriptor)
{
  MOZ_ASSERT(aDescriptor.type() == SurfaceDescriptor::TYCbCrImage);

  if (IsSurfaceDescriptorValid(mDescriptor)) {
    GetForwarder()->DestroySharedSurface(&mDescriptor);
  }
  mDescriptor = aDescriptor;
  MOZ_ASSERT(IsSurfaceDescriptorValid(mDescriptor));
}


void
TextureClientShmemYCbCr::EnsureTextureClient(gfx::IntSize aSize,
                                             gfxASurface::gfxContentType aType)
{
  NS_RUNTIMEABORT("not enough arguments to do this (need both Y and CbCr sizes)");
}

TextureClientSharedGL::TextureClientSharedGL(CompositableForwarder* aForwarder,
                                             CompositableType aCompositableType)
  : TextureClient(aForwarder, aCompositableType)
  , mGL(nullptr)
{
}

void
TextureClientSharedGL::ReleaseResources()
{
  if (!IsSurfaceDescriptorValid(mDescriptor) ||
      mDescriptor.type() != SurfaceDescriptor::TSharedTextureDescriptor) {
    return;
  }
  SharedTextureDescriptor handle = mDescriptor.get_SharedTextureDescriptor();
  if (mGL && handle.handle()) {
    mGL->ReleaseSharedHandle(handle.shareType(), handle.handle());
  }
}

void
TextureClientSharedGL::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aContentType)
{
  mSize = aSize;
}

TextureClientBridge::TextureClientBridge(CompositableForwarder* aForwarder,
                                         CompositableType aCompositableType)
  : TextureClient(aForwarder, aCompositableType)
{
}

/* static */ CompositableType
CompositingFactory::TypeForImage(Image* aImage) {
  if (!aImage) {
    return BUFFER_UNKNOWN;
  }

  return BUFFER_IMAGE_SINGLE;
}

/* static */ TemporaryRef<ImageClient>
CompositingFactory::CreateImageClient(LayersBackend aParentBackend,
                                      CompositableType aCompositableHostType,
                                      CompositableForwarder* aForwarder,
                                      TextureFlags aFlags)
{
  RefPtr<ImageClient> result = nullptr;
  switch (aCompositableHostType) {
  case BUFFER_IMAGE_SINGLE:
  case BUFFER_IMAGE_BUFFERED:
    if (aParentBackend == LAYERS_OPENGL || aParentBackend == LAYERS_D3D11) {
      result = new ImageClientTexture(aForwarder, aFlags);
    }
    break;
  case BUFFER_BRIDGE:
    if (aParentBackend == LAYERS_OPENGL) {
      result = new ImageClientBridge(aForwarder, aFlags);
    }
    break;
  case BUFFER_UNKNOWN:
    result = nullptr;
    break;
  default:
    MOZ_NOT_REACHED("unhandled program type");
  }

  NS_ASSERTION(result, "Failed to create ImageClient");

  return result.forget();
}

/* static */ TemporaryRef<CanvasClient>
CompositingFactory::CreateCanvasClient(LayersBackend aParentBackend,
                                       CompositableType aCompositableHostType,
                                       CompositableForwarder* aForwarder,
                                       TextureFlags aFlags)
{
  if (aCompositableHostType == BUFFER_IMAGE_SINGLE) {
    return new CanvasClient2D(aForwarder, aFlags);
  }
  if (aCompositableHostType == BUFFER_IMAGE_BUFFERED) {
    if (aParentBackend == LAYERS_OPENGL) {
      return new CanvasClientWebGL(aForwarder, aFlags);
    }
    return new CanvasClient2D(aForwarder, aFlags);
  }
  return nullptr;
}

/* static */ TemporaryRef<ContentClient>
CompositingFactory::CreateContentClient(LayersBackend aParentBackend,
                                        CompositableType aCompositableHostType,
                                        CompositableForwarder* aForwarder)
{
  if (aParentBackend != LAYERS_OPENGL && aParentBackend != LAYERS_D3D11) {
    return nullptr;
  }
  if (aCompositableHostType == BUFFER_CONTENT) {
    return new ContentClientTexture(aForwarder);
  }
  if (aCompositableHostType == BUFFER_CONTENT_DIRECT) {
    if (ShadowLayerManager::SupportsDirectTexturing()) {
      return new ContentClientDirect(aForwarder);
    }
    return new ContentClientTexture(aForwarder);
  }
  if (aCompositableHostType == BUFFER_TILED) {
    MOZ_NOT_REACHED("No CompositableClient for tiled layers");
  }
  return nullptr;
}

void
TextureClientTile::EnsureTextureClient(gfx::IntSize aSize, gfxASurface::gfxContentType aType)
{
  if (!mSurface ||
      mSurface->Format() != gfxPlatform::GetPlatform()->OptimalFormatForContent(aType)) {
    gfxImageSurface* tmpTile = new gfxImageSurface(gfxIntSize(aSize.width, aSize.height),
                                                   gfxPlatform::GetPlatform()->OptimalFormatForContent(aType),
                                                   aType != gfxASurface::CONTENT_COLOR);
    mSurface = new gfxReusableSurfaceWrapper(tmpTile);
  }
}

gfxImageSurface*
TextureClientTile::LockImageSurface()
{
  // Use the gfxReusableSurfaceWrapper, which will reuse the surface
  // if the compositor no longer has a read lock, otherwise the surface
  // will be copied into a new writable surface.
  gfxImageSurface* writableSurface = nullptr;
  mSurface = mSurface->GetWritable(&writableSurface);
  return writableSurface;
}

}
}

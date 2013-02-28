/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_SHAREDIMAGEUTILS_H
#define MOZILLA_LAYERS_SHAREDIMAGEUTILS_H

#include "gfxSharedImageSurface.h"
#include "gfxPlatform.h"
#include "ShadowLayers.h"
 
namespace mozilla {
namespace layers {

template<typename Deallocator>
void DeallocSurfaceDescriptorData(Deallocator* protocol, const SurfaceDescriptor& aImage)
{
  if (aImage.type() == SurfaceDescriptor::TYCbCrImage) {
    protocol->DeallocShmem(aImage.get_YCbCrImage().data());
  } else if (aImage.type() == SurfaceDescriptor::TShmem) {
    protocol->DeallocShmem(aImage.get_Shmem());
  }
}

} // namespace
} // namespace

#endif


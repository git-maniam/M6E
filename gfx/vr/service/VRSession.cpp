/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRSession.h"

#include "moz_external_vr.h"
#include "mozilla/ipc/FileDescriptor.h"


#if defined(MOZILLA_INTERNAL_API)
#else
#  define NS_WARNING(s)
#endif

using namespace mozilla::gfx;

VRSession::VRSession()
    : mShouldQuit(false)
{
}



bool VRSession::SubmitFrame(
    const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer) {

  if (aLayer.textureType == VRLayerTextureType::LayerTextureType_MacIOSurface) {
    return SubmitFrame(aLayer, aLayer.textureHandle);
  }


  return false;
}

void VRSession::UpdateTrigger(VRControllerState& aState, uint32_t aButtonIndex,
                              float aValue, float aThreshold) {
  // For OpenVR, the threshold value of ButtonPressed and ButtonTouched is 0.55.
  // We prefer to let developers to set their own threshold for the adjustment.
  // Therefore, we don't check ButtonPressed and ButtonTouched with ButtonMask
  // here. we just check the button value is larger than the threshold value or
  // not.
  uint64_t mask = (1ULL << aButtonIndex);
  aState.triggerValue[aButtonIndex] = aValue;
  if (aValue > aThreshold) {
    aState.buttonPressed |= mask;
    aState.buttonTouched |= mask;
  } else {
    aState.buttonPressed &= ~mask;
    aState.buttonTouched &= ~mask;
  }
}

void VRSession::SetControllerSelectionAndSqueezeFrameId(
    VRControllerState& controllerState, uint64_t aFrameId) {
  // The 1st button, trigger, is its selection action.
  const bool selectionPressed = controllerState.buttonPressed & 1ULL;
  if (selectionPressed && controllerState.selectActionStopFrameId >=
                              controllerState.selectActionStartFrameId) {
    controllerState.selectActionStartFrameId = aFrameId;
  } else if (!selectionPressed && controllerState.selectActionStartFrameId >
                                      controllerState.selectActionStopFrameId) {
    controllerState.selectActionStopFrameId = aFrameId;
  }
  // The 2nd button, squeeze, is its squeeze action.
  const bool squeezePressed = controllerState.buttonPressed & (1ULL << 1);
  if (squeezePressed && controllerState.squeezeActionStopFrameId >=
                            controllerState.squeezeActionStartFrameId) {
    controllerState.squeezeActionStartFrameId = aFrameId;
  } else if (!squeezePressed && controllerState.squeezeActionStartFrameId >
                                    controllerState.squeezeActionStopFrameId) {
    controllerState.squeezeActionStopFrameId = aFrameId;
  }
}

bool VRSession::ShouldQuit() const { return mShouldQuit; }

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PuppetSession.h"

#include "VRPuppetCommandBuffer.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsString.h"

#  include "mozilla/gfx/MacIOSurface.h"

using namespace mozilla::gfx;

namespace mozilla::gfx {

PuppetSession::PuppetSession() = default;

PuppetSession::~PuppetSession() { Shutdown(); }

bool PuppetSession::Initialize(mozilla::gfx::VRSystemState& aSystemState,
                               bool aDetectRuntimesOnly) {
  if (!StaticPrefs::dom_vr_enabled() || !StaticPrefs::dom_vr_puppet_enabled()) {
    return false;
  }
  if (!VRPuppetCommandBuffer::IsCreated()) {
    // We only want to initialize VRPuppetCommandBuffer on the main thread.
    // We can assume if it is not initialized, that the puppet display
    // would not be enumerated.
    return false;
  }
  if (aDetectRuntimesOnly) {
    aSystemState.displayState.capabilityFlags |=
        VRDisplayCapabilityFlags::Cap_ImmersiveVR;
    return false;
  }
  VRPuppetCommandBuffer::Get().Run(aSystemState);
  if (!aSystemState.displayState.isConnected) {
    return false;
  }

  // Succeeded
  return true;
}


void PuppetSession::Shutdown() {}

void PuppetSession::StartFrame(mozilla::gfx::VRSystemState& aSystemState) {
  VRPuppetCommandBuffer::Get().Run(aSystemState);
}

void PuppetSession::ProcessEvents(mozilla::gfx::VRSystemState& aSystemState) {
  VRPuppetCommandBuffer& puppet = VRPuppetCommandBuffer::Get();
  puppet.Run(aSystemState);
  if (!aSystemState.displayState.isConnected) {
    mShouldQuit = true;
  }
}

bool PuppetSession::SubmitFrame(
    const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer,
    const VRLayerTextureHandle& aTexture) {
  return VRPuppetCommandBuffer::Get().SubmitFrame();
}

void PuppetSession::StopPresentation() {
  VRPuppetCommandBuffer::Get().StopPresentation();
}

bool PuppetSession::StartPresentation() {
  VRPuppetCommandBuffer::Get().StartPresentation();
  return true;
}

void PuppetSession::VibrateHaptic(uint32_t aControllerIdx,
                                  uint32_t aHapticIndex, float aIntensity,
                                  float aDuration) {
  VRPuppetCommandBuffer::Get().VibrateHaptic(aControllerIdx, aHapticIndex,
                                             aIntensity, aDuration);
}

void PuppetSession::StopVibrateHaptic(uint32_t aControllerIdx) {
  VRPuppetCommandBuffer::Get().StopVibrateHaptic(aControllerIdx);
}

void PuppetSession::StopAllHaptics() {
  VRPuppetCommandBuffer::Get().StopAllHaptics();
}

}  // namespace mozilla::gfx

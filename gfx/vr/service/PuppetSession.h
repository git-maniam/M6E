/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_VR_SERVICE_PUPPETSESSION_H
#define GFX_VR_SERVICE_PUPPETSESSION_H

#include "VRSession.h"
#include "moz_external_vr.h"
#include "mozilla/TimeStamp.h"

class nsITimer;

namespace mozilla {
namespace gfx {

class PuppetSession : public VRSession {
 public:
  PuppetSession();
  virtual ~PuppetSession();

  bool Initialize(mozilla::gfx::VRSystemState& aSystemState,
                  bool aDetectRuntimesOnly) override;
  void Shutdown() override;
  void ProcessEvents(mozilla::gfx::VRSystemState& aSystemState) override;
  void StartFrame(mozilla::gfx::VRSystemState& aSystemState) override;
  bool StartPresentation() override;
  void StopPresentation() override;
  void VibrateHaptic(uint32_t aControllerIdx, uint32_t aHapticIndex,
                     float aIntensity, float aDuration) override;
  void StopVibrateHaptic(uint32_t aControllerIdx) override;
  void StopAllHaptics() override;

 protected:
  bool SubmitFrame(const mozilla::gfx::VRLayer_Stereo_Immersive& aLayer,
                   const VRLayerTextureHandle& aTexture) override;

 private:
};

}  // namespace gfx
}  // namespace mozilla

#endif  // GFX_VR_SERVICE_PUPPETSESSION_H

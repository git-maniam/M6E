/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRGPUParent.h"

#include "VRPuppetCommandBuffer.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"

namespace mozilla {
namespace gfx {

using namespace ipc;

VRGPUParent::VRGPUParent(EndpointProcInfo aChildProcess) : mClosed(false) {
  MOZ_ASSERT(NS_IsMainThread());

  SetOtherEndpointProcInfo(aChildProcess);
}

VRGPUParent::~VRGPUParent() = default;

void VRGPUParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (mVRService) {
    mVRService->Stop();
    mVRService = nullptr;
  }

  mClosed = true;
}

/* static */
RefPtr<VRGPUParent> VRGPUParent::CreateForGPU(
    Endpoint<PVRGPUParent>&& aEndpoint) {
  if (!StaticPrefs::dom_vr_enabled() && !StaticPrefs::dom_vr_webxr_enabled()) {
    return nullptr;
  }

  RefPtr<VRGPUParent> vcp = new VRGPUParent(aEndpoint.OtherEndpointProcInfo());
  GetCurrentSerialEventTarget()->Dispatch(
      NewRunnableMethod<Endpoint<PVRGPUParent>&&>("gfx::VRGPUParent::Bind", vcp,
                                                  &VRGPUParent::Bind,
                                                  std::move(aEndpoint)));

  return vcp;
}

void VRGPUParent::Bind(Endpoint<PVRGPUParent>&& aEndpoint) {
  if (!aEndpoint.Bind(this)) {
    return;
  }
}

mozilla::ipc::IPCResult VRGPUParent::RecvStartVRService() {
  mVRService = VRService::Create();
  MOZ_ASSERT(mVRService);

  mVRService->Start();

  return IPC_OK();
}

mozilla::ipc::IPCResult VRGPUParent::RecvStopVRService() {
  if (mVRService) {
    mVRService->Stop();
    mVRService = nullptr;
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult VRGPUParent::RecvPuppetReset() {
  VRPuppetCommandBuffer::Get().Reset();
  return IPC_OK();
}

mozilla::ipc::IPCResult VRGPUParent::RecvPuppetSubmit(
    const nsTArray<uint64_t>& aBuffer) {
  VRPuppetCommandBuffer::Get().Submit(aBuffer);
  return IPC_OK();
}

mozilla::ipc::IPCResult VRGPUParent::RecvPuppetCheckForCompletion() {
  if (VRPuppetCommandBuffer::Get().HasEnded()) {
    (void)SendNotifyPuppetComplete();
  }
  return IPC_OK();
}

bool VRGPUParent::IsClosed() { return mClosed; }

}  // namespace gfx
}  // namespace mozilla

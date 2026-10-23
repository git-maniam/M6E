/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRShMem.h"

#ifdef MOZILLA_INTERNAL_API
#  include "nsString.h"
#  include "nsXULAppAPI.h"
#endif

#include "gfxVRMutex.h"

#  include <fcntl.h> /* For O_* constants */
#  include <sys/mman.h>
#  include <sys/stat.h> /* For mode constants */

#  include <unistd.h>  // for ::sleep

using namespace mozilla::gfx;

static const char* kShmemName = "/moz.gecko.vr_ext." SHMEM_VERSION;

namespace {
void YieldThread() {
  ::sleep(0);
}
}  // anonymous namespace

VRShMem::VRShMem(volatile VRExternalShmem* aShmem, bool aRequiresMutex)
    : mExternalShmem(aShmem),
      mIsSharedExternalShmem(aShmem != nullptr)
      ,
      mShmemFD(0)
{
  // Regarding input parameters,
  // - aShmem is null for VRManager or for VRService in multi-proc
  // - aShmem is !null for VRService in-proc (i.e., no VR proc)
}

// Note: This function should only be called for in-proc scenarios, where the
// shared memory is only shared within the same proc (rather than across
// processes). Also, this local heap memory's lifetime is tied to the class.
// Callers to this must ensure that its reference doesn't outlive the owning
// VRShMem instance.
volatile VRExternalShmem* VRShMem::GetExternalShmem() const {
  MOZ_ASSERT(mShmemFD == 0);
  return mExternalShmem;
}

bool VRShMem::IsDisplayStateShutdown() const {
  // adapted from VRService::Refresh
  // Does this need the mutex for getting .shutdown?
  return mExternalShmem != nullptr &&
         mExternalShmem->state.displayState.shutdown;
}

// This method returns true when there is a Shmem struct allocated and
// when there is a shmem handle from the OS. This implies that the struct
// is mapped to shared memory rather than being allocated on the heap by
// this process.
bool VRShMem::IsCreatedOnSharedMemory() const {
  return HasExternalShmem() && (mShmemFD != 0);
}

// CreateShMem allocates system shared memory for mExternalShmem and
// synchronization primitives to protect it.
// Callers/Processes to CreateShMem should followup with CloseShMem
void VRShMem::CreateShMem(bool aCreateOnSharedMemory) {
  if (HasExternalShmem()) {
    MOZ_ASSERT(mIsSharedExternalShmem && !IsCreatedOnSharedMemory());
    return;
  }
  // The VR Service accesses all hardware from a separate process
  // and replaces the other VRManager when enabled.
  // If the VR process is not enabled, create an in-process VRService.
  if (!aCreateOnSharedMemory) {
    MOZ_ASSERT(mExternalShmem == nullptr);
    // If the VR process is disabled, attempt to create a
    // VR service within the current process on the heap
    mExternalShmem = new VRExternalShmem();
    ClearShMem();
    return;
  }

  MOZ_ASSERT(aCreateOnSharedMemory);

  if (mShmemFD == 0) {
    mShmemFD =
        shm_open(kShmemName, O_RDWR, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
  }
  if (mShmemFD <= 0) {
    mShmemFD = 0;
    return;
  }

  struct stat sb;
  fstat(mShmemFD, &sb);
  off_t length = sb.st_size;
  if (length < (off_t)sizeof(VRExternalShmem)) {
    // TODO - Implement logging (Bug 1558912)
    CloseShMem();
    return;
  }

  mExternalShmem = (VRExternalShmem*)mmap(
      nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, mShmemFD, 0);
  if (mExternalShmem == MAP_FAILED) {
    // TODO - Implement logging (Bug 1558912)
    mExternalShmem = nullptr;
    CloseShMem();
    return;
  }

}

// This function sets mExternalShmem in the Android/GeckoView
// scenarios where the host creates it in-memory and VRShMem
// accesses it via GeckVRManager.
void VRShMem::CreateShMemForAndroid() {
}

void VRShMem::ClearShMem() {
  if (mExternalShmem != nullptr) {
#ifdef MOZILLA_INTERNAL_API
    // VRExternalShmem is asserted to be POD
    mExternalShmem->Clear();
#else
    memset((void*)mExternalShmem, 0, sizeof(VRExternalShmem));
#endif
  }
}

// The cleanup corresponding to CreateShMem
void VRShMem::CloseShMem() {
  if (!IsCreatedOnSharedMemory()) {
    MOZ_ASSERT(!mIsSharedExternalShmem);
    if (mExternalShmem) {
      delete mExternalShmem;
      mExternalShmem = nullptr;
    }
    return;
  }
  if (mExternalShmem) {
    munmap((void*)mExternalShmem, sizeof(VRExternalShmem));
    mExternalShmem = nullptr;
  }
  if (mShmemFD) {
    close(mShmemFD);
    mShmemFD = 0;
  }

}

// Called to use an existing shmem instance created by another process
// Callers to JoinShMem should call LeaveShMem for cleanup
bool VRShMem::JoinShMem() {

  if (HasExternalShmem()) {
    // An ExternalShmem is already set. No need to override and rejoin
    return true;
  }

  // TODO: Implement shmem for other platforms.
  //
  // TODO: ** Does this mean that ShMem only works in Windows for now? If so,
  // let's delete the code from other platforms (Bug 1563234)
  MOZ_ASSERT(false, "JoinShMem not implemented");
  return true;
}

// The cleanup corresponding to JoinShMem
void VRShMem::LeaveShMem() {

  if (mExternalShmem != nullptr) {
    mExternalShmem = nullptr;
  }
}

void VRShMem::PushBrowserState(VRBrowserState& aBrowserState,
                               bool aNotifyCond) {
  if (!mExternalShmem) {
    return;
  }
  bool status = true;


  if (status) {
    mExternalShmem->geckoGenerationA = mExternalShmem->geckoGenerationA + 1;
    memcpy((void*)&(mExternalShmem->geckoState), (void*)&aBrowserState,
           sizeof(VRBrowserState));
    mExternalShmem->geckoGenerationB = mExternalShmem->geckoGenerationB + 1;
  }
}

void VRShMem::PullBrowserState(mozilla::gfx::VRBrowserState& aState) {
  if (!mExternalShmem) {
    return;
  }
  // Copying the browser state from the shmem is non-blocking
  // on x86/x64 architectures.  Arm requires a mutex that is
  // locked for the duration of the memcpy to and from shmem on
  // both sides.
  // On x86/x64 It is fallable -- If a dirty copy is detected by
  // a mismatch of geckoGenerationA and geckoGenerationB,
  // the copy is discarded and will not replace the last known
  // browser state.

  bool status = true;
  if (status) {
    VRExternalShmem tmp;
    if (mExternalShmem->geckoGenerationA != mBrowserGeneration) {
      // TODO - (void *) cast removes volatile semantics.
      // The memcpy is not likely to be optimized out, but is theoretically
      // possible.  Suggest refactoring to either explicitly enforce memory
      // order or to use locks.
      memcpy(&tmp, (void*)mExternalShmem, sizeof(VRExternalShmem));
      if (tmp.geckoGenerationA == tmp.geckoGenerationB &&
          tmp.geckoGenerationA != 0) {
        memcpy(&aState, &tmp.geckoState, sizeof(VRBrowserState));
        mBrowserGeneration = tmp.geckoGenerationA;
      }
    }
  }
}

void VRShMem::PushSystemState(const mozilla::gfx::VRSystemState& aState) {
  if (!mExternalShmem) {
    return;
  }
  // Copying the VR service state to the shmem is atomic, infallable,
  // and non-blocking on x86/x64 architectures.  Arm requires a mutex
  // that is locked for the duration of the memcpy to and from shmem on
  // both sides.

  bool lockState = true;


  if (lockState) {
    mExternalShmem->generationA = mExternalShmem->generationA + 1;
    memcpy((void*)&mExternalShmem->state, &aState, sizeof(VRSystemState));
    mExternalShmem->generationB = mExternalShmem->generationB + 1;
  }
}

void VRShMem::PullSystemState(
    VRDisplayState& aDisplayState, VRHMDSensorState& aSensorState,
    std::array<VRControllerState, kVRControllerMaxCount>* const
        aControllerState,
    bool& aEnumerationCompleted,
    const std::function<bool()>& aWaitCondition /* = nullptr */) {
  MOZ_ASSERT(mExternalShmem);
  if (!mExternalShmem) {
    return;
  }
  while (true) {
    {  // Scope for WaitForMutex
        VRExternalShmem tmp;
        memcpy(&tmp, (void*)mExternalShmem, sizeof(VRExternalShmem));
        bool isCleanCopy =
            tmp.generationA == tmp.generationB && tmp.generationA != 0;
        if (isCleanCopy) {
          memcpy(&aDisplayState, &tmp.state.displayState,
                 sizeof(VRDisplayState));
          memcpy(&aSensorState, &tmp.state.sensorState,
                 sizeof(VRHMDSensorState));
          *aControllerState = tmp.state.controllerState;
          aEnumerationCompleted = tmp.state.enumerationCompleted;
          // Check for wait condition
          if (!aWaitCondition || aWaitCondition()) {
            return;
          }
        } else if (!aWaitCondition) {
          // We did not get a clean copy, and we are not waiting for a condition
          // to exit from PullState call.
          return;
        }
        // Yield the thread while polling
        YieldThread();
    }  // End: Scope for WaitForMutex
    // Yield the thread while polling
    YieldThread();
  }  // while (!true)
}

void VRShMem::PushWindowState(VRWindowState& aState) {
}

void VRShMem::PullWindowState(VRWindowState& aState) {
}

void VRShMem::PushTelemetryState(VRTelemetryState& aState) {
}
void VRShMem::PullTelemetryState(VRTelemetryState& aState) {
}

void VRShMem::SendEvent(uint64_t aWindowID,
                        mozilla::gfx::VRFxEventType aEventType,
                        mozilla::gfx::VRFxEventState aEventState) {
  MOZ_ASSERT(!HasExternalShmem());
  if (JoinShMem()) {
    mozilla::gfx::VRWindowState windowState = {0};
    PullWindowState(windowState);
    windowState.windowID = aWindowID;
    windowState.eventType = aEventType;
    windowState.eventState = aEventState;
    PushWindowState(windowState);
    LeaveShMem();

  }
}

void VRShMem::SendIMEState(uint64_t aWindowID,
                           mozilla::gfx::VRFxEventState aEventState) {
  SendEvent(aWindowID, mozilla::gfx::VRFxEventType::IME, aEventState);
}

void VRShMem::SendFullscreenState(uint64_t aWindowID, bool aFullscreen) {
  SendEvent(aWindowID, mozilla::gfx::VRFxEventType::FULLSCREEN,
            aFullscreen ? mozilla::gfx::VRFxEventState::FULLSCREEN_ENTER
                        : mozilla::gfx::VRFxEventState::FULLSCREEN_EXIT);
}

// Note: this should be called from the VRShMem instance that created
// the external shmem rather than joined it.
void VRShMem::SendShutdowmState(uint64_t aWindowID) {
  MOZ_ASSERT(HasExternalShmem());

  mozilla::gfx::VRWindowState windowState = {0};
  PullWindowState(windowState);
  windowState.windowID = aWindowID;
  windowState.eventType = mozilla::gfx::VRFxEventType::SHUTDOWN;
  PushWindowState(windowState);

}

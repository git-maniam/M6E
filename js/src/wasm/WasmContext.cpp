/*
 * Copyright 2025 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmContext.h"

#include "jit/JitRuntime.h"
#include "js/friend/StackLimits.h"
#include "js/TracingAPI.h"
#include "vm/JSContext.h"
#include "wasm/WasmPI.h"
#include "wasm/WasmStacks.h"


using namespace js::wasm;

Context::Context()
    : triedToInstallSignalHandlers(false),
      haveSignalHandlers(false),
      stackLimit(JS::NativeStackLimitMin)
#ifdef ENABLE_WASM_JSPI
      ,
      mainStackTarget_(),
      currentStack_(nullptr),
      baseHandlers_(nullptr)
#endif
{
#ifdef ENABLE_WASM_JSPI
  MOZ_ASSERT(mainStackTarget_.isMainStack());
#endif
}

Context::~Context() {
#ifdef ENABLE_WASM_JSPI
  MOZ_ASSERT(currentStack_ == nullptr);
  MOZ_ASSERT(baseHandlers_ == nullptr);
#endif  // ENABLE_WASM_JSPI
}

void Context::initStackLimit(JSContext* cx) {
  // The wasm stack limit is the same as the jit stack limit. We also don't
  // use the stack limit for triggering interrupts.
  stackLimit = cx->jitStackLimitNoInterrupt;

#ifdef ENABLE_WASM_JSPI
  // Fill in the main stack target
  mainStackTarget_.stack = nullptr;
  mainStackTarget_.jitLimit = stackLimit;
  MOZ_ASSERT(!mainStackTarget_.stack);

  // See the comment on wasm::Context for why we do this.
#endif    // ENABLE_WASM_JSPI
}

#ifdef ENABLE_WASM_JSPI
#endif    // ENABLE_WASM_JSPI

#ifdef ENABLE_WASM_JSPI
ContStack* Context::findStackForAddress(JSContext* cx, uintptr_t stackAddress) {
  if (cx->stackContainsAddress(stackAddress,
                               JS::StackKind::StackForSystemCode)) {
    return nullptr;
  }

  ContStack* stack = contStacks_.findForAddress(stackAddress);
  if (stack && stack->hasStackAddress(stackAddress)) {
    return stack;
  }

  // We have an address that's not on the main stack, but also not in a
  // continuation stack. This can happen sometimes in stack overflow situations
  // and is fine.
  return nullptr;
}
#endif

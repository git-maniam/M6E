/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_gfx_ipc_D3DMessageUtils_h_
#define _include_gfx_ipc_D3DMessageUtils_h_

#include "chrome/common/ipc_message_utils.h"
#include "ipc/IPCMessageUtils.h"

// Can't include dxgi.h, since it leaks random identifiers and #defines, and
// IPDL causes this file to be #included all over.
typedef struct DXGI_ADAPTER_DESC DXGI_ADAPTER_DESC;

struct DxgiAdapterDesc {

  bool operator==(const DxgiAdapterDesc& aOther) const;
};

namespace IPC {

DEFINE_IPC_SERIALIZER_WITHOUT_FIELDS(DxgiAdapterDesc);

}  // namespace IPC

#endif  // _include_gfx_ipc_D3DMessageUtils_h_

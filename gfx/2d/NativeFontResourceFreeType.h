/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_gfx_NativeFontResourceFreeType_h
#define mozilla_gfx_NativeFontResourceFreeType_h

#include <cairo-ft.h>

#include "2D.h"

namespace mozilla {
namespace gfx {

class NativeFontResourceFreeType : public NativeFontResource {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(NativeFontResourceFreeType, override)


  ~NativeFontResourceFreeType();

  already_AddRefed<SharedFTFace> CloneFace(int aFaceIndex = 0);

 protected:
  NativeFontResourceFreeType(const uint8_t*&& aFontData, uint32_t aDataLength);

  template <class T>
  static already_AddRefed<T> CreateInternal(const uint8_t* aFontData,
                                            uint32_t aDataLength);

  RefPtr<FontData> mFontData;
};


}  // namespace gfx
}  // namespace mozilla

#endif  // mozilla_gfx_NativeFontResourceFreeType_h

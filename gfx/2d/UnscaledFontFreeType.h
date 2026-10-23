/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_UNSCALEDFONTFREETYPE_H_
#define MOZILLA_GFX_UNSCALEDFONTFREETYPE_H_

#include <cairo-ft.h>

#include "2D.h"

namespace mozilla {
namespace gfx {

class ScaledFontFreeType;
class ScaledFontFontconfig;

class UnscaledFontFreeType : public UnscaledFont {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(UnscaledFontFreeType, override)
  explicit UnscaledFontFreeType(const RefPtr<SharedFTFace>& aFace)
      : mFace(aFace) {
    if (mFace) {
      if (mFace->GetFilename()) {
        mFile = mFace->GetFilename();
      }
      mIndex = mFace->GetFaceIndex();
    }
  }
  explicit UnscaledFontFreeType(const char* aFile, uint32_t aIndex = 0,
                                RefPtr<SharedFTFace> aFace = nullptr)
      : mFace(std::move(aFace)), mFile(aFile), mIndex(aIndex) {}
  explicit UnscaledFontFreeType(std::string&& aFile, uint32_t aIndex = 0,
                                RefPtr<SharedFTFace> aFace = nullptr)
      : mFace(std::move(aFace)), mFile(std::move(aFile)), mIndex(aIndex) {}

  FontType GetType() const override { return FontType::FREETYPE; }

  const RefPtr<SharedFTFace>& GetFace() const { return mFace; }
  const std::string& GetFile() const { return mFile; }
  uint32_t GetIndex() const { return mIndex; }

  bool GetFontFileData(FontFileDataOutput aDataCallback, void* aBaton) override;

  bool GetFontDescriptor(FontDescriptorOutput aCb, void* aBaton) override;

  RefPtr<SharedFTFace> InitFace();


 protected:
  RefPtr<SharedFTFace> mFace;
  std::string mFile;
  uint32_t mIndex = 0;

  friend class ScaledFontFreeType;
  friend class ScaledFontFontconfig;

  static void GetVariationSettingsFromFace(
      std::vector<wr::FontVariation>* aVariations, SharedFTFace* aFace);

  static void ApplyVariationsToFace(const wr::FontVariation* aVariations,
                                    uint32_t aNumVariations, FT_Face aFace);
};


}  // namespace gfx
}  // namespace mozilla

#endif /* MOZILLA_GFX_UNSCALEDFONTFREETYPE_H_ */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_headless_HeadlessCompositorWidgetParent_h
#define widget_headless_HeadlessCompositorWidgetParent_h

#include "mozilla/VsyncDispatcher.h"
#include "mozilla/widget/HeadlessCompositorWidget.h"
#include "mozilla/widget/PCompositorWidgetParent.h"

namespace mozilla {
namespace widget {

class HeadlessCompositorWidgetInitData;

class HeadlessCompositorWidgetParent final : public PCompositorWidgetParent,
                                             public HeadlessCompositorWidget {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(HeadlessCompositorWidgetParent,
                                       CompositorWidget)

  HeadlessCompositorWidgetParent(
      const HeadlessCompositorWidgetInitData& aInitData,
      const layers::CompositorOptions& aOptions);

  // CompositorWidget overrides.
  void ObserveVsync(VsyncObserver* aObserver) override;
  RefPtr<VsyncObserver> GetVsyncObserver() const override;

  // Common headless size synchronization.
  mozilla::ipc::IPCResult RecvNotifyClientSizeChanged(
      const LayoutDeviceIntSize& aClientSize) override;



 private:
  ~HeadlessCompositorWidgetParent() override;

  RefPtr<VsyncObserver> mVsyncObserver;
};

}  // namespace widget
}  // namespace mozilla

#endif  // widget_headless_HeadlessCompositorWidgetParent_h

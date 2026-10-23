/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeAppSupportBase.h"
#include "nsComponentManagerUtils.h"
#include "nsCOMPtr.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIObserverService.h"
#include "nsIAppStartup.h"
#include "nsServiceManagerUtils.h"
#include "prlink.h"
#include "nsXREDirProvider.h"
#include "nsReadableUtils.h"

#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsPIDOMWindow.h"
#include "nsIWidget.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/XREAppData.h"

#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>




class nsNativeAppSupportUnix : public nsNativeAppSupportBase {
 public:
  NS_IMETHOD Start(bool* aRetVal) override;
  NS_IMETHOD Enable() override;

 private:
};


static void RemoveArg(char** argv) {
  do {
    *argv = *(argv + 1);
    ++argv;
  } while (*argv);

  --gArgc;
}

NS_IMETHODIMP
nsNativeAppSupportUnix::Start(bool* aRetVal) {
  NS_ASSERTION(gAppData, "gAppData must not be null.");

// The dbus library is used by both nsWifiScannerDBus and BluetoothDBusService,
// from diffrent threads. This could lead to race conditions if the dbus is not
// initialized before making any other library calls.

  *aRetVal = true;


  return NS_OK;
}

NS_IMETHODIMP
nsNativeAppSupportUnix::Enable() { return NS_OK; }

nsresult NS_CreateNativeAppSupport(nsINativeAppSupport** aResult) {
  nsNativeAppSupportBase* native = new nsNativeAppSupportUnix();
  if (!native) return NS_ERROR_OUT_OF_MEMORY;

  *aResult = native;
  NS_ADDREF(*aResult);

  return NS_OK;
}

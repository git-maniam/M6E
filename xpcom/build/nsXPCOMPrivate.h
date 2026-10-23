/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXPCOMPrivate_h_
#define nsXPCOMPrivate_h_

#include "mozilla/Attributes.h"
#include "nsXPCOM.h"
#include "nscore.h"

/**
 * During this shutdown notification all threads which run XPCOM code must
 * be joined.
 */
#define NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID "xpcom-shutdown-threads"

// PUBLIC
namespace mozilla {

/**
 * Shutdown XPCOM. You must call this method after you are finished
 * using xpcom.
 *
 * @param aServMgr          The service manager which was returned by
 * NS_InitXPCOM. This will release servMgr.  You may pass null.
 *
 * @return NS_OK for success;
 *         other error codes indicate a failure during shutdown
 *
 */
MOZ_CAN_RUN_SCRIPT
nsresult ShutdownXPCOM(nsIServiceManager* aServMgr);

void SetICUMemoryFunctions();

/**
 * C++ namespaced version of NS_LogTerm.
 */
void LogTerm();

}  // namespace mozilla

/* XPCOM Specific Defines
 *
 * XPCOM_DLL              - name of the loadable xpcom library on disk.
 * XUL_DLL                - name of the loadable XUL library on disk
 * XPCOM_SEARCH_KEY       - name of the environment variable that can be
 *                          modified to include additional search paths.
 * GRE_CONF_NAME          - Name of the GRE Configuration file
 */

#  include <limits.h>  // for PATH_MAX

#  define XPCOM_DLL XUL_DLL

// you have to love apple..
#    define XPCOM_SEARCH_KEY "DYLD_LIBRARY_PATH"
#    define GRE_FRAMEWORK_NAME "XUL.framework"
#    define XUL_DLL "XUL"

#  define GRE_CONF_NAME ".gre.config"
#  define GRE_CONF_PATH "/etc/gre.conf"
#  define GRE_CONF_DIR "/etc/gre.d"
#  define GRE_USER_CONF_DIR ".gre.d"

#  define XPCOM_FILE_PATH_SEPARATOR "/"
#  define XPCOM_ENV_PATH_SEPARATOR ":"

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  elif defined(_MAX_PATH)
#    define MAXPATHLEN _MAX_PATH
#  elif defined(CCHMAXPATH)
#    define MAXPATHLEN CCHMAXPATH
#  else
#    define MAXPATHLEN 1024
#  endif
#endif

// Needed by the IPC layer from off the main thread
extern char16_t* gGREBinPath;

namespace mozilla {
namespace services {

/**
 * Clears service cache, sets gXPCOMShuttingDown
 */
void Shutdown();

}  // namespace services
}  // namespace mozilla

#endif

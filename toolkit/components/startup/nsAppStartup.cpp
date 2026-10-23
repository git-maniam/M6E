/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAppStartup.h"

#include "nsComponentManagerUtils.h"
#include "nsIAppShellService.h"
#include "nsPIDOMWindow.h"
#include "nsIInterfaceRequestor.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIProcess.h"
#include "nsIToolkitProfile.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWindowMediator.h"
#include "nsIXULRuntime.h"
#include "nsIAppWindow.h"
#include "nsNativeCharsetUtils.h"
#include "nsThreadUtils.h"
#include "nsString.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/Try.h"
#include "nsThread.h"

#include "GeckoProfiler.h"
#include "prprf.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsWidgetsCID.h"
#include "nsAppRunner.h"
#include "nsAppShellCID.h"
#include "nsXPCOMCIDInternal.h"
#include "mozilla/Services.h"
#include "jsapi.h"
#include "js/Date.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "prenv.h"
#include "nsAppDirectoryServiceDefs.h"

#  include <mach/mach_time.h>

#include "mozilla/IOInterposer.h"
#include "mozilla/Telemetry.h"
#include "mozilla/StartupTimeline.h"

static NS_DEFINE_CID(kAppShellCID, NS_APPSHELL_CID);

#define kPrefLastSuccess "toolkit.startup.last_success"
#define kPrefMaxResumedCrashes "toolkit.startup.max_resumed_crashes"
#define kPrefRecentCrashes "toolkit.startup.recent_crashes"
#define kPrefAlwaysUseSafeMode "toolkit.startup.always_use_safe_mode"

#define kNanosecondsPerSecond 1000000000.0


using namespace mozilla;

class nsAppExitEvent : public mozilla::Runnable {
 private:
  RefPtr<nsAppStartup> mService;

 public:
  explicit nsAppExitEvent(nsAppStartup* service)
      : mozilla::Runnable("nsAppExitEvent"), mService(service) {}

  NS_IMETHOD Run() override {
    // Tell the appshell to exit
    mService->mAppShell->Exit();

    mService->mRunning = false;
    return NS_OK;
  }
};

/**
 * Computes an approximation of the absolute time represented by @a stamp
 * which is comparable to those obtained via PR_Now(). If the current absolute
 * time varies a lot (e.g. DST adjustments) since the first call then the
 * resulting times may be inconsistent.
 *
 * @param stamp The timestamp to be converted
 * @returns The converted timestamp
 */
static uint64_t ComputeAbsoluteTimestamp(TimeStamp stamp) {
  static PRTime sAbsoluteNow = PR_Now();
  static TimeStamp sMonotonicNow = TimeStamp::Now();

  return sAbsoluteNow - (sMonotonicNow - stamp).ToMicroseconds();
}

//
// nsAppStartup
//

nsAppStartup::nsAppStartup()
    : mConsiderQuitStopper(0),
      mRunning(false),
      mShuttingDown(false),
      mStartingUp(true),
      mAttemptingQuit(false),
      mIsSafeModeNecessary(false),
      mStartupCrashAndHangTrackingEnded(false) {
  char* mozAppSilentStart = PR_GetEnv("MOZ_APP_SILENT_START");

  /* When calling PR_SetEnv() with an empty value the existing variable may
   * be unset or set to the empty string depending on the underlying platform
   * thus we have to check if the variable is present and not empty. */
  mWasSilentlyStarted =
      mozAppSilentStart && (strcmp(mozAppSilentStart, "") != 0);

  // Make sure to clear this in case we restart again non-silently.
  PR_SetEnv("MOZ_APP_SILENT_START=");

}

nsAppStartup::~nsAppStartup() {
  BackgroundHangMonitor::UnregisterAnnotator(*this);
}

nsresult nsAppStartup::Init() {
  nsresult rv;

  // Create widget application shell
  mAppShell = do_GetService(kAppShellCID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) return NS_ERROR_FAILURE;

  os->AddObserver(this, "quit-application", true);
  os->AddObserver(this, "quit-application-forced", true);
  os->AddObserver(this, "sessionstore-init-started", true);
  os->AddObserver(this, "sessionstore-windows-restored", true);
  os->AddObserver(this, "profile-change-teardown", true);
  os->AddObserver(this, "xul-window-registered", true);
  os->AddObserver(this, "xul-window-destroyed", true);
  os->AddObserver(this, "profile-before-change", true);
  os->AddObserver(this, "xpcom-shutdown", true);

  BackgroundHangMonitor::RegisterAnnotator(*this);


  return NS_OK;
}

void nsAppStartup::AnnotateHang(BackgroundHangAnnotations& aAnnotations) {
  if (!mStartupCrashAndHangTrackingEnded) {
    aAnnotations.AddAnnotation(u"BeforeStartupCrashAndHangTrackingEnded"_ns,
                               true);
  }

  if (AppShutdown::IsShutdownImpending()) {
    aAnnotations.AddAnnotation(u"ShutdownImpending"_ns, true);
  }
}

//
// nsAppStartup->nsISupports
//

NS_IMPL_ISUPPORTS(nsAppStartup, nsIAppStartup, nsIWindowCreator, nsIObserver,
                  nsISupportsWeakReference)

//
// nsAppStartup->nsIAppStartup
//

NS_IMETHODIMP
nsAppStartup::CreateHiddenWindow() {
  nsCOMPtr<nsIAppShellService> appShellService(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  NS_ENSURE_TRUE(appShellService, NS_ERROR_FAILURE);

  return appShellService->CreateHiddenWindow();
}

NS_IMETHODIMP
nsAppStartup::DestroyHiddenWindow() {
  nsCOMPtr<nsIAppShellService> appShellService(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  NS_ENSURE_TRUE(appShellService, NS_ERROR_FAILURE);

  return appShellService->DestroyHiddenWindow();
}

NS_IMETHODIMP
nsAppStartup::Run(void) {
  NS_ASSERTION(!mRunning, "Reentrant appstartup->Run()");

  // If we have no windows open and no explicit calls to
  // enterLastWindowClosingSurvivalArea, or somebody has explicitly called
  // quit, don't bother running the event loop which would probably leave us
  // with a zombie process.

  if (!mShuttingDown && mConsiderQuitStopper != 0) {
    EnterLastWindowClosingSurvivalArea();

    mRunning = true;

    nsresult rv = mAppShell->Run();
    if (NS_FAILED(rv)) return rv;
  }

  // Make sure that the appropriate quit notifications have been dispatched
  // regardless of whether the event loop has spun or not. Note that this call
  // is a no-op if Quit has already been called previously.
  Quit(eForceQuit, 0);

  nsresult retval = NS_OK;
  if (mozilla::AppShutdown::IsRestarting()) {
    retval = NS_SUCCESS_RESTART_APP;
  }

  return retval;
}

NS_IMETHODIMP
nsAppStartup::Quit(uint32_t aMode, int aExitCode) {
  if ((aMode & eSilently) != 0 && (aMode & eRestart) == 0) {
    // eSilently is only valid when combined with eRestart.
    return NS_ERROR_INVALID_ARG;
  }

  uint32_t ferocity = (aMode & 0xF);

  // Quit the application. We will asynchronously call the appshell's
  // Exit() method via nsAppExitEvent to allow one last pass
  // through any events in the queue. This guarantees a tidy cleanup.
  nsresult rv = NS_OK;
  bool postedExitEvent = false;

  if (mShuttingDown) return NS_OK;

  // If we're considering quitting, we will only do so if:
  if (ferocity == eConsiderQuit) {
    nsCOMPtr<nsIAppShellService> appShell(
        do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
    int32_t suspiciousCount = 1;

    if (mConsiderQuitStopper == 0) {
      // there are no windows...
      ferocity = eAttemptQuit;
    }
    else if (mConsiderQuitStopper == suspiciousCount) {
      // Failure shouldn't be fatal, but will abort quit attempt:
      if (!appShell) return NS_OK;
      bool hasHiddenWindow = false;
      appShell->GetHasHiddenWindow(&hasHiddenWindow);
      // If there's a hidden window, we won't quit:
      if (hasHiddenWindow) {
        return NS_OK;
      }

      ferocity = eAttemptQuit;
    }
  }

  nsCOMPtr<nsIObserverService> obsService;
  if (ferocity == eAttemptQuit || ferocity == eForceQuit) {
    nsCOMPtr<nsISimpleEnumerator> windowEnumerator;
    nsCOMPtr<nsIWindowMediator> mediator(
        do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
    if (mediator) {
      mediator->GetEnumerator(nullptr, getter_AddRefs(windowEnumerator));
      if (windowEnumerator) {
        bool more;
        windowEnumerator->HasMoreElements(&more);
        // If we reported no windows, we definitely shouldn't be
        // iterating any here.
        MOZ_ASSERT_IF(!mConsiderQuitStopper, !more);

        while (more) {
          nsCOMPtr<nsISupports> window;
          windowEnumerator->GetNext(getter_AddRefs(window));
          nsCOMPtr<nsPIDOMWindowOuter> domWindow(do_QueryInterface(window));
          if (domWindow) {
            if (!domWindow->CanClose()) {
              return NS_OK;
            }
          }
          windowEnumerator->HasMoreElements(&more);
        }
      }
    }

    // We passed the last line of a shutdown to maybe not happen for legit
    // reasons after checking CanClose() for all windows above. Now we are
    // supposed to go away (or restart), for sure.

    PROFILER_MARKER_UNTYPED("Shutdown start", OTHER);
    mozilla::RecordShutdownStartTimeStamp();

    mShuttingDown = true;
    auto shutdownMode = ((aMode & eRestart) != 0)
                            ? mozilla::AppShutdownMode::Restart
                            : mozilla::AppShutdownMode::Normal;
    // TODO: Add (or pass in) more reasons here for Mac and Linux, see
    // bug 1827643 and bug 1827644.
    // See as example the Windows WM_ENDSESSION handling.
    auto shutdownReason = ((aMode & eRestart) != 0)
                              ? mozilla::AppShutdownReason::AppRestart
                              : mozilla::AppShutdownReason::AppClose;
    mozilla::AppShutdown::Init(shutdownMode, aExitCode, shutdownReason);

    if (mozilla::AppShutdown::IsRestarting()) {
      // Mark the next startup as a restart.
      PR_SetEnv("MOZ_APP_RESTART=1");

      /* Firefox-restarts reuse the process so regular process start-time isn't
         a useful indicator of startup time anymore. */
      TimeStamp::RecordProcessRestart();
    }

    if ((aMode & eSilently) != 0) {
      // Mark the next startup as a silent restart.
      // See the eSilently definition for details.
      PR_SetEnv("MOZ_APP_SILENT_START=1");
    }

    obsService = mozilla::services::GetObserverService();

    if (!mAttemptingQuit) {
      mAttemptingQuit = true;
      // now even the Mac wants to quit when the last window is closed
      ExitLastWindowClosingSurvivalArea();
      if (obsService)
        obsService->NotifyObservers(nullptr, "quit-application-granted",
                                    nullptr);
    }

    // Enumerate through each open window and close it.
    CloseAllWindows();

    if (mediator) {
      if (ferocity == eAttemptQuit) {
        ferocity = eForceQuit;  // assume success

        // TODO: The above CloseAllWindows() does a window->ForceClose() which
        // seems to have no way of saying that we refuse to close. It dispatches
        // events and sends notifications which might race with the below check.
        // However the flag we check below with domWindow->Closed() should have
        // always been set synchronously above. In other words: most likely this
        // entire check can go away and maybe CloseAllWindows could be renamed
        // to ForceCloseAllWindows.

        /* Were we able to immediately close all windows? if not, eAttemptQuit
           failed. This could happen for a variety of reasons; in fact it's
           very likely. Perhaps we're being called from JS and the window->Close
           method hasn't had a chance to wrap itself up yet. So give up.
           We'll return (with eConsiderQuit) as the remaining windows are
           closed. */
        mediator->GetEnumerator(nullptr, getter_AddRefs(windowEnumerator));
        if (windowEnumerator) {
          bool more;
          while (NS_SUCCEEDED(windowEnumerator->HasMoreElements(&more)) &&
                 more) {
            /* we can't quit immediately. we'll try again as the last window
               finally closes. */
            ferocity = eAttemptQuit;
            nsCOMPtr<nsISupports> window;
            windowEnumerator->GetNext(getter_AddRefs(window));
            nsCOMPtr<nsPIDOMWindowOuter> domWindow = do_QueryInterface(window);
            if (domWindow) {
              if (!domWindow->Closed()) {
                MOZ_DIAGNOSTIC_ASSERT(false,
                                      "CloseAllWindows() did not succeed.");
                rv = NS_ERROR_FAILURE;
                break;
              }
            }
          }
        }
      }
    }
  }

  if (ferocity == eForceQuit) {
    // do it!
    mozilla::AppShutdown::OnShutdownConfirmed();

    // No chance of the shutdown being cancelled from here on; tell people
    // we're shutting down for sure while all services are still available.
    bool isRestarting = mozilla::AppShutdown::IsRestarting();
    mozilla::AppShutdown::AdvanceShutdownPhase(
        mozilla::ShutdownPhase::AppShutdownConfirmed,
        isRestarting ? u"restart" : u"shutdown");

    if (!mRunning) {
      postedExitEvent = true;
    } else {
      // no matter what, make sure we send the exit event.  If
      // worst comes to worst, we'll do a leaky shutdown but we WILL
      // shut down. Well, assuming that all *this* stuff works ;-).
      nsCOMPtr<nsIRunnable> event = new nsAppExitEvent(this);
      rv = NS_DispatchToCurrentThread(event);
      if (NS_SUCCEEDED(rv)) {
        postedExitEvent = true;
      } else {
        NS_WARNING("failed to dispatch nsAppExitEvent");
      }
    }
  }

  // turn off the reentrancy check flag, but not if we have
  // more asynchronous work to do still.
  if (!postedExitEvent) {
    mShuttingDown = false;
  }
  return rv;
}

// Ensure ShutdownPhase.h and nsIAppStartup.idl are in sync.
static_assert(int(nsIAppStartup::SHUTDOWN_PHASE_NOTINSHUTDOWN) ==
                      int(mozilla::ShutdownPhase::NotInShutdown) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED) ==
                      int(mozilla::ShutdownPhase::AppShutdownConfirmed) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_APPSHUTDOWNNETTEARDOWN) ==
                      int(mozilla::ShutdownPhase::AppShutdownNetTeardown) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_APPSHUTDOWNTEARDOWN) ==
                      int(mozilla::ShutdownPhase::AppShutdownTeardown) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_APPSHUTDOWN) ==
                      int(mozilla::ShutdownPhase::AppShutdown) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_APPSHUTDOWNQM) ==
                      int(mozilla::ShutdownPhase::AppShutdownQM) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_APPSHUTDOWNTELEMETRY) ==
                      int(mozilla::ShutdownPhase::AppShutdownTelemetry) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_XPCOMWILLSHUTDOWN) ==
                      int(mozilla::ShutdownPhase::XPCOMWillShutdown) &&
                  int(nsIAppStartup::SHUTDOWN_PHASE_XPCOMSHUTDOWN) ==
                      int(mozilla::ShutdownPhase::XPCOMShutdown),
              "IDLShutdownPhase values are as expected");

// Helper for safe conversion to native shutdown phases.
Result<ShutdownPhase, nsresult> IDLShutdownPhaseToNative(
    nsAppStartup::IDLShutdownPhase aPhase) {
  if (uint8_t(aPhase) <= nsIAppStartup::SHUTDOWN_PHASE_XPCOMSHUTDOWN) {
    return ShutdownPhase(aPhase);
  }
  return Err(NS_ERROR_ILLEGAL_VALUE);
}

NS_IMETHODIMP
nsAppStartup::AdvanceShutdownPhase(IDLShutdownPhase aPhase) {
  ShutdownPhase nativePhase = MOZ_TRY(IDLShutdownPhaseToNative(aPhase));
  AppShutdown::AdvanceShutdownPhase(nativePhase);
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::IsInOrBeyondShutdownPhase(IDLShutdownPhase aPhase,
                                        bool* aIsInOrBeyond) {
  ShutdownPhase nativePhase = MOZ_TRY(IDLShutdownPhaseToNative(aPhase));
  *aIsInOrBeyond = AppShutdown::IsInOrBeyond(nativePhase);
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::SetImpendingShutdown() {
  AppShutdown::SetImpendingShutdown();
  return NS_OK;
}

namespace mozilla {

void CollectShutdownHangAnnotations() {
#ifdef NS_THREAD_SHUTDOWN_ANNOTATIONS_ENABLED
  nsThread::CollectShutdownHangAnnotation();
#endif
}

}  // namespace mozilla

NS_IMETHODIMP
nsAppStartup::CollectShutdownHangAnnotations() {
  mozilla::CollectShutdownHangAnnotations();
  return NS_OK;
}

void nsAppStartup::CloseAllWindows() {
  nsCOMPtr<nsIWindowMediator> mediator(
      do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));

  nsCOMPtr<nsISimpleEnumerator> windowEnumerator;

  mediator->GetEnumerator(nullptr, getter_AddRefs(windowEnumerator));

  if (!windowEnumerator) return;

  bool more;
  while (NS_SUCCEEDED(windowEnumerator->HasMoreElements(&more)) && more) {
    nsCOMPtr<nsISupports> isupports;
    if (NS_FAILED(windowEnumerator->GetNext(getter_AddRefs(isupports)))) break;

    nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(isupports);
    NS_ASSERTION(window, "not an nsPIDOMWindow");
    if (window) {
      window->ForceClose();
    }
  }
}

NS_IMETHODIMP
nsAppStartup::EnterLastWindowClosingSurvivalArea(void) {
  ++mConsiderQuitStopper;
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::ExitLastWindowClosingSurvivalArea(void) {
  NS_ASSERTION(mConsiderQuitStopper > 0, "consider quit stopper out of bounds");
  --mConsiderQuitStopper;

  if (mRunning) {
    // A previous call to Quit may have told all windows to close and then
    // bailed out waiting for that to happen. This is how we get back into Quit
    // after each window closes so the exit process can continue when ready.
    // Make sure to pass along the exit code that was initially passed to Quit.
    Quit(eConsiderQuit, mozilla::AppShutdown::GetExitCode());
  }

  return NS_OK;
}

//
// nsAppStartup->nsIAppStartup2
//

NS_IMETHODIMP
nsAppStartup::GetShuttingDown(bool* aResult) {
  *aResult = AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed);
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetAttemptingQuit(bool* aResult) {
  *aResult = mAttemptingQuit;
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetStartingUp(bool* aResult) {
  *aResult = mStartingUp;
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::DoneStartingUp() {
  // This must be called once at most
  MOZ_ASSERT(mStartingUp);

  mStartingUp = false;
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetRestarting(bool* aResult) {
  *aResult = mozilla::AppShutdown::IsRestarting();
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetWasRestarted(bool* aResult) {
  char* mozAppRestart = PR_GetEnv("MOZ_APP_RESTART");

  /* When calling PR_SetEnv() with an empty value the existing variable may
   * be unset or set to the empty string depending on the underlying platform
   * thus we have to check if the variable is present and not empty. */
  *aResult = mozAppRestart && (strcmp(mozAppRestart, "") != 0);

  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetWasSilentlyStarted(bool* aResult) {
  *aResult = mWasSilentlyStarted;
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetSecondsSinceLastOSRestart(int64_t* aResult) {
  uint64_t absTime = mach_absolute_time();
  mach_timebase_info_data_t timebaseInfo;
  mach_timebase_info(&timebaseInfo);
  double toNanoseconds =
      double(timebaseInfo.numer) / double(timebaseInfo.denom);
  *aResult =
      std::llround(double(absTime) * toNanoseconds / kNanosecondsPerSecond);
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetShowedPreXULSkeletonUI(bool* aResult) {
  *aResult = false;
  return NS_OK;
}

//
// nsAppStartup->nsIWindowCreator
//

NS_IMETHODIMP
nsAppStartup::CreateChromeWindow(nsIWebBrowserChrome* aParent,
                                 uint32_t aChromeFlags,
                                 nsIOpenWindowInfo* aOpenWindowInfo,
                                 bool* aCancel, nsIWebBrowserChrome** _retval) {
  NS_ENSURE_ARG_POINTER(aCancel);
  NS_ENSURE_ARG_POINTER(_retval);
  *aCancel = false;
  *_retval = nullptr;

  // Non-modal windows cannot be opened if we are attempting to quit
  if (mAttemptingQuit &&
      (aChromeFlags & nsIWebBrowserChrome::CHROME_MODAL) == 0)
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;

  // Fission windows must also be marked as remote
  if ((aChromeFlags & nsIWebBrowserChrome::CHROME_FISSION_WINDOW) &&
      !(aChromeFlags & nsIWebBrowserChrome::CHROME_REMOTE_WINDOW)) {
    NS_WARNING("Cannot create non-remote fission window!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIAppWindow> newWindow;

  if (aParent) {
    nsCOMPtr<nsIAppWindow> appParent(do_GetInterface(aParent));
    NS_ASSERTION(appParent,
                 "window created using non-app parent. that's unexpected, but "
                 "may work.");

    if (appParent)
      appParent->CreateNewWindow(aChromeFlags, aOpenWindowInfo,
                                 getter_AddRefs(newWindow));
    // And if it fails, don't try again without a parent. It could fail
    // intentionally (bug 115969).
  } else {  // try using basic methods:
    MOZ_RELEASE_ASSERT(!aOpenWindowInfo,
                       "Unexpected aOpenWindowInfo, we shouldn't ever have an "
                       "nsIOpenWindowInfo without a parent");

    /* You really shouldn't be making dependent windows without a parent.
      But unparented modal (and therefore dependent) windows happen
      in our codebase, so we allow it after some bellyaching: */
    if (aChromeFlags & nsIWebBrowserChrome::CHROME_DEPENDENT)
      NS_WARNING("dependent window created without a parent");

    nsCOMPtr<nsIAppShellService> appShell(
        do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
    if (!appShell) return NS_ERROR_FAILURE;

    appShell->CreateTopLevelWindow(
        nullptr, nullptr, aChromeFlags, nsIAppShellService::SIZE_TO_CONTENT,
        nsIAppShellService::SIZE_TO_CONTENT, getter_AddRefs(newWindow));
  }

  // if anybody gave us anything to work with, use it
  if (newWindow) {
    nsCOMPtr<nsIInterfaceRequestor> thing(do_QueryInterface(newWindow));
    if (thing) CallGetInterface(thing.get(), _retval);
  }

  return *_retval ? NS_OK : NS_ERROR_FAILURE;
}

//
// nsAppStartup->nsIObserver
//

NS_IMETHODIMP
nsAppStartup::Observe(nsISupports* aSubject, const char* aTopic,
                      const char16_t* aData) {
  NS_ASSERTION(mAppShell, "appshell service notified before appshell built");
  if (!strcmp(aTopic, "quit-application-forced")) {
    mShuttingDown = true;
  } else if (!strcmp(aTopic, "profile-change-teardown")) {
    if (!mShuttingDown) {
      EnterLastWindowClosingSurvivalArea();
      CloseAllWindows();
      ExitLastWindowClosingSurvivalArea();
    }
  } else if (!strcmp(aTopic, "xul-window-registered")) {
    EnterLastWindowClosingSurvivalArea();
  } else if (!strcmp(aTopic, "xul-window-destroyed")) {
    ExitLastWindowClosingSurvivalArea();
  } else if (!strcmp(aTopic, "sessionstore-windows-restored")) {
    StartupTimeline::Record(StartupTimeline::SESSION_RESTORED);
    IOInterposer::EnteringNextStage();
  } else if (!strcmp(aTopic, "sessionstore-init-started")) {
    StartupTimeline::Record(StartupTimeline::SESSION_RESTORE_INIT);
  } else if (!strcmp(aTopic, "xpcom-shutdown")) {
    IOInterposer::EnteringNextStage();
  } else if (!strcmp(aTopic, "quit-application")) {
    StartupTimeline::Record(StartupTimeline::QUIT_APPLICATION);
  } else if (!strcmp(aTopic, "profile-before-change")) {
    StartupTimeline::Record(StartupTimeline::PROFILE_BEFORE_CHANGE);
  } else {
    NS_ERROR("Unexpected observer topic.");
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetStartupInfo(JSContext* aCx,
                             JS::MutableHandle<JS::Value> aRetval) {
  JS::Rooted<JSObject*> obj(aCx, JS_NewPlainObject(aCx));

  aRetval.setObject(*obj);

  TimeStamp procTime = StartupTimeline::Get(StartupTimeline::PROCESS_CREATION);

  if (procTime.IsNull()) {
    procTime = TimeStamp::ProcessCreation();

    StartupTimeline::Record(StartupTimeline::PROCESS_CREATION, procTime);
  }

  for (int i = StartupTimeline::PROCESS_CREATION;
       i < StartupTimeline::MAX_EVENT_ID; ++i) {
    StartupTimeline::Event ev = static_cast<StartupTimeline::Event>(i);
    TimeStamp stamp = StartupTimeline::Get(ev);

    if (stamp.IsNull() && (ev == StartupTimeline::MAIN)) {
      // Always define main to aid with bug 689256.
      stamp = procTime;
      MOZ_ASSERT(!stamp.IsNull());
    }

    if (!stamp.IsNull()) {
      if (stamp >= procTime) {
        PRTime prStamp = ComputeAbsoluteTimestamp(stamp) / PR_USEC_PER_MSEC;
        JS::Rooted<JSObject*> date(
            aCx, JS::NewDateObject(aCx, JS::TimeClip(prStamp)));
        JS_DefineProperty(aCx, obj, StartupTimeline::Describe(ev), date,
                          JSPROP_ENUMERATE);
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::GetAutomaticSafeModeNecessary(bool* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);

  bool alwaysSafe = false;
  Preferences::GetBool(kPrefAlwaysUseSafeMode, &alwaysSafe);

  if (!alwaysSafe) {
#if DEBUG
    mIsSafeModeNecessary = false;
#else
    mIsSafeModeNecessary &= !PR_GetEnv("MOZ_DISABLE_AUTO_SAFE_MODE");
#endif
  }

  *_retval = mIsSafeModeNecessary;
  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::TrackStartupCrashBegin(bool* aIsSafeModeNecessary) {
  const int32_t MAX_TIME_SINCE_STARTUP = 6 * 60 * 60 * 1000;
  const int32_t MAX_STARTUP_BUFFER = 10;
  nsresult rv;

  mStartupCrashAndHangTrackingEnded = false;

  StartupTimeline::Record(StartupTimeline::STARTUP_CRASH_DETECTION_BEGIN);

  bool hasLastSuccess = Preferences::HasUserValue(kPrefLastSuccess);
  if (!hasLastSuccess) {
    // Clear so we don't get stuck with SafeModeNecessary returning true if we
    // have had too many recent crashes and the last success pref is missing.
    Preferences::ClearUser(kPrefRecentCrashes);
    return NS_ERROR_NOT_AVAILABLE;
  }

  bool inSafeMode = false;
  nsCOMPtr<nsIXULRuntime> xr = do_GetService(XULRUNTIME_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(xr, NS_ERROR_FAILURE);

  xr->GetInSafeMode(&inSafeMode);

  PRTime replacedLockTime;
  rv = xr->GetReplacedLockTime(&replacedLockTime);

  if (NS_FAILED(rv) || !replacedLockTime) {
    if (!inSafeMode) Preferences::ClearUser(kPrefRecentCrashes);
    GetAutomaticSafeModeNecessary(aIsSafeModeNecessary);
    return NS_OK;
  }

  // check whether safe mode is necessary
  int32_t maxResumedCrashes = -1;
  rv = Preferences::GetInt(kPrefMaxResumedCrashes, &maxResumedCrashes);
  NS_ENSURE_SUCCESS(rv, NS_OK);

  int32_t recentCrashes = 0;
  Preferences::GetInt(kPrefRecentCrashes, &recentCrashes);
  mIsSafeModeNecessary =
      (recentCrashes > maxResumedCrashes && maxResumedCrashes != -1);

  // Bug 731613 - Don't check if the last startup was a crash if
  // XRE_PROFILE_PATH is set.  After profile manager, the profile lock's mod.
  // time has been changed so can't be used on this startup. After a restart,
  // it's safe to assume the last startup was successful.
  // Startup clears XRE_PROFILE_PATH by setting it empty, which leaves it in the
  // environment on Unix for processes this one launches.
  char* xreProfilePath = PR_GetEnv("XRE_PROFILE_PATH");
  if (xreProfilePath && *xreProfilePath) {
    GetAutomaticSafeModeNecessary(aIsSafeModeNecessary);
    return NS_ERROR_NOT_AVAILABLE;
  }

  // time of last successful startup
  int32_t lastSuccessfulStartup;
  rv = Preferences::GetInt(kPrefLastSuccess, &lastSuccessfulStartup);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t lockSeconds = (int32_t)(replacedLockTime / PR_MSEC_PER_SEC);

  // started close enough to good startup so call it good
  if (lockSeconds <= lastSuccessfulStartup + MAX_STARTUP_BUFFER &&
      lockSeconds >= lastSuccessfulStartup - MAX_STARTUP_BUFFER) {
    GetAutomaticSafeModeNecessary(aIsSafeModeNecessary);
    return NS_OK;
  }

  // sanity check that the pref set at last success is not greater than the
  // current time
  if (PR_Now() / PR_USEC_PER_SEC <= lastSuccessfulStartup)
    return NS_ERROR_FAILURE;

  if (inSafeMode) {
    GetAutomaticSafeModeNecessary(aIsSafeModeNecessary);
    return NS_OK;
  }

  PRTime now = (PR_Now() / PR_USEC_PER_MSEC);
  // if the last startup attempt which crashed was in the last 6 hours
  if (replacedLockTime >= now - MAX_TIME_SINCE_STARTUP) {
    NS_WARNING("Last startup was detected as a crash.");
    recentCrashes++;
    rv = Preferences::SetInt(kPrefRecentCrashes, recentCrashes);
  } else {
    // Otherwise ignore that crash and all previous since it may not be
    // applicable anymore and we don't want someone to get stuck in safe mode if
    // their prefs are read-only.
    rv = Preferences::ClearUser(kPrefRecentCrashes);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // recalculate since recent crashes count may have changed above
  mIsSafeModeNecessary =
      (recentCrashes > maxResumedCrashes && maxResumedCrashes != -1);

  nsCOMPtr<nsIPrefService> prefs = Preferences::GetService();
  rv = static_cast<Preferences*>(prefs.get())
           ->SavePrefFileBlocking();  // flush prefs to disk since we are
                                      // tracking crashes
  NS_ENSURE_SUCCESS(rv, rv);

  GetAutomaticSafeModeNecessary(aIsSafeModeNecessary);
  return rv;
}

static nsresult RemoveIncompleteStartupFile() {
  nsCOMPtr<nsIFile> file;
  MOZ_TRY(NS_GetSpecialDirectory(NS_APP_USER_PROFILE_LOCAL_50_DIR,
                                 getter_AddRefs(file)));

  return NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "RemoveIncompleteStartupFile", [file = std::move(file)] {
        auto incompleteStartup =
            mozilla::startup::GetIncompleteStartupFile(file);
        if (NS_WARN_IF(incompleteStartup.isErr())) {
          return;
        }
        (void)NS_WARN_IF(NS_FAILED(incompleteStartup.unwrap()->Remove(false)));
      }));
}

NS_IMETHODIMP
nsAppStartup::TrackStartupCrashEnd() {
  bool inSafeMode = false;
  nsCOMPtr<nsIXULRuntime> xr = do_GetService(XULRUNTIME_SERVICE_CONTRACTID);
  if (xr) xr->GetInSafeMode(&inSafeMode);

  // return if we already ended or we're restarting into safe mode
  if (mStartupCrashAndHangTrackingEnded ||
      (mIsSafeModeNecessary && !inSafeMode))
    return NS_OK;
  mStartupCrashAndHangTrackingEnded = true;

  StartupTimeline::Record(StartupTimeline::STARTUP_CRASH_DETECTION_END);

  // Remove the incomplete startup canary file, so the next startup doesn't
  // detect a recent startup crash.
  (void)NS_WARN_IF(NS_FAILED(RemoveIncompleteStartupFile()));

  // Use the timestamp of XRE_main as an approximation for the lock file
  // timestamp. See MAX_STARTUP_BUFFER for the buffer time period.
  TimeStamp mainTime = StartupTimeline::Get(StartupTimeline::MAIN);
  nsresult rv;

  if (mainTime.IsNull()) {
    NS_WARNING("Could not get StartupTimeline::MAIN time.");
  } else {
    uint64_t lockFileTime = ComputeAbsoluteTimestamp(mainTime);

    rv = Preferences::SetInt(kPrefLastSuccess,
                             (int32_t)(lockFileTime / PR_USEC_PER_SEC));

    if (NS_FAILED(rv))
      NS_WARNING("Could not set startup crash detection pref.");
  }

  if (inSafeMode && mIsSafeModeNecessary) {
    // On a successful startup in automatic safe mode, allow the user one more
    // crash in regular mode before returning to safe mode.
    int32_t maxResumedCrashes = 0;
    nsIPrefBranch::PreferenceType prefType;
    rv = Preferences::GetRootBranch(PrefValueKind::Default)
             ->GetPrefType(kPrefMaxResumedCrashes, &prefType);
    NS_ENSURE_SUCCESS(rv, rv);
    if (prefType == nsIPrefBranch::PREF_INT) {
      rv = Preferences::GetInt(kPrefMaxResumedCrashes, &maxResumedCrashes);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    rv = Preferences::SetInt(kPrefRecentCrashes, maxResumedCrashes);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (!inSafeMode) {
    // clear the count of recent crashes after a succesful startup when not in
    // safe mode
    rv = Preferences::ClearUser(kPrefRecentCrashes);
    if (NS_FAILED(rv)) NS_WARNING("Could not clear startup crash count.");
  }
  nsCOMPtr<nsIPrefService> prefs = Preferences::GetService();
  // save prefs to disk since we are tracking crashes.  This may be
  // asynchronous, so a crash could sneak in that we would mistake for
  // a start up crash. See bug 789945 and bug 1361262.
  rv = prefs->SavePrefFile(nullptr);

  return rv;
}

NS_IMETHODIMP
nsAppStartup::RestartInSafeMode(uint32_t aQuitMode) {
  PR_SetEnv("MOZ_SAFE_MODE_RESTART=1");
  this->Quit(aQuitMode | nsIAppStartup::eRestart, 0);

  return NS_OK;
}

NS_IMETHODIMP
nsAppStartup::CreateInstanceWithProfile(nsIToolkitProfile* aProfile,
                                        const nsTArray<nsString>& aArgs) {
  if (NS_WARN_IF(!aProfile)) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(gAbsoluteArgv0Path.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIFile> execPath;
  nsresult rv = NS_NewLocalFile(gAbsoluteArgv0Path, getter_AddRefs(execPath));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIProcess> process = do_CreateInstance(NS_PROCESS_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = process->Init(execPath);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsAutoCString profileName;
  rv = aProfile->GetName(profileName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  NS_ConvertUTF8toUTF16 wideName(profileName);

  // Build argument list: -P <profile_name> followed by any additional args
  AutoTArray<const char16_t*, 2> args = {u"-P", wideName.get()};

  // Add optional arguments if provided
  for (const auto& arg : aArgs) {
    args.AppendElement(arg.get());
  }

  rv = process->Runw(false, args.Elements(), args.Length());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

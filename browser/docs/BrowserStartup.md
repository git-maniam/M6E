# Browser Startup

## Invoking your code on browser startup

The first rule of running code during startup is: don't.

We take performance very seriously and ideally your component/feature should
initialize only when needed.

If you have established that you really must run code during startup, register
a category manager entry in `browser/components/BrowserComponents.manifest`.
See [the category manager indirection docs](./CategoryManagerIndirection.md) for
more details on the mechanism.

### Available category manager entries (in order)

Categories come in two types: per-application (fired once at startup) and
per-window (fired for each browser window). The following categories
are listed in the order they fire.

#### Per-application categories (invoked once, not per-window)

| Category | Notes |
|---|---|
| `browser-before-ui-startup` | Please don't do it unless absolutely necessary. This will run code before the first browser window appears on the screen and make Firefox seem slow. |
| `browser-first-window-ready` | After the first browser window's `browser-window-delayed-startup` call. |
| `browser-idle-startup` | Preferred entrypoint for most startup code. Runs during a user-idle period after startup. |
| `browser-best-effort-idle-startup` | Like `browser-idle-startup`, but your code **may not run at all** if the browser is shut down quickly. |

#### Per-window categories (invoked for each browser window)

| Category | Notes |
|---|---|
| `browser-window-before-initial-xul-layout-document-preparation` | Before XUL layout; before the tab bar visibility update. |
| `browser-window-before-initial-xul-layout` | Before XUL layout; after the tab bar visibility update and document icon are set. |
| `browser-window-domcontentloaded-before-tabbrowser` | During `DOMContentLoaded`, before the `Tabbrowser` is created. |
| `browser-window-domcontentloaded-tabbrowser` | During `DOMContentLoaded`, creates the `Tabbrowser`. **Internal category - do not add consumers here.** |
| `browser-window-domcontentloaded` | During `DOMContentLoaded`, after the `Tabbrowser` is created. Preferred entry point for `DOMContentLoaded` initialization. |
| `browser-window-load-before-sessionstore-init` | During the window `load` event, before SessionStore initializes the window. For components that must be ready before session restore runs. |
| `browser-window-load` | During the window `load` event, after SessionStore has initialized the window. |
| `browser-window-delayed-startup` | After the window's first paint, for code that can run after the window is visible. |
| `browser-window-sessionstore-initialized` | Fired from the window's delayed startup once SessionStore is initialized. |
| `browser-window-idle-tasks` | Invoked once all windows have been restored, for per-window work that can wait until the browser is idle. Each consumer runs in its own idle callback, and is dropped if the window closes before it runs. |
| `browser-window-location-change` | Fires on every `XULBrowserWindow.onLocationChange` for the window. |
| `browser-window-unload-begin` | At the very start of window unload, before checking whether the window finished loading. Runs even if the window never fully loaded. |
| `browser-window-unload` | During window unload, before `Tabbrowser` is destroyed. Preferred entry point for window unload cleanup. |
| `browser-window-unload-delayed-startup` | During window unload, before `Tabbrowser` is destroyed. Teardown counterpart to `browser-window-delayed-startup`; only fires if the window's delayed startup actually ran. |
| `browser-window-unload-tabbrowser` | During window unload, destroys the `Tabbrowser`. **Internal category - do not add consumers here.** |
| `browser-window-final-unload` | During window unload, after `Tabbrowser` is destroyed. |

Other useful points in startup are:

- `BrowserGlue`'s `_onWindowsRestored`, and/or the observer service's
  `sessionstore-windows-restored` topic, and/or a category manager call that
  should replace the `BrowserGlue` list of direct calls. This fires after
  session restore has completed restoring all windows (but before all pages
  that may have been restored have necessarily loaded). Note that this is
  guaranteed to fire even if automatic session restore is not enabled.

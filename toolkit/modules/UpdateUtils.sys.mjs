/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  WindowsRegistry: "resource://gre/modules/WindowsRegistry.sys.mjs",
  ctypes: "resource://gre/modules/ctypes.sys.mjs",
});

const FILE_DEFAULT_LOCALE = "default.locale";
const PREF_APP_DISTRIBUTION = "distribution.id";
const PREF_APP_DISTRIBUTION_VERSION = "distribution.version";

export var UpdateUtils = {
  _locale: undefined,

  /**
   * Read the update channel from defaults only.  We do this to ensure that
   * the channel is tightly coupled with the application and does not apply
   * to other instances of the application that may use the same profile.
   *
   * @param [optional] aIncludePartners
   *        Whether or not to include the partner bits. Default: true.
   */
  getUpdateChannel(aIncludePartners = true) {
    let defaults = Services.prefs.getDefaultBranch(null);
    let channel = defaults.getCharPref(
      "app.update.channel",
      AppConstants.MOZ_UPDATE_CHANNEL
    );

    if (aIncludePartners) {
      try {
        let partners = Services.prefs.getChildList("app.partner.").sort();
        if (partners.length) {
          channel += "-cck";
          partners.forEach(function (prefName) {
            channel += "-" + Services.prefs.getCharPref(prefName);
          });
        }
      } catch (e) {
        console.error(e);
      }
    }

    return channel;
  },

  get UpdateChannel() {
    return this.getUpdateChannel();
  },

  /**
   * Formats a URL by replacing %...% values with OS, build and locale specific
   * values.
   *
   * @param  url
   *         The URL to format.
   * @return The formatted URL.
   */
  async formatUpdateURL(url) {
    const locale = await this.getLocale();

    return url.replace(/%(\w+)%/g, (match, name) => {
      let replacement = match;
      switch (name) {
        case "PRODUCT":
          replacement = Services.appinfo.name;
          break;
        case "VERSION":
          replacement = Services.appinfo.version;
          break;
        case "BUILD_ID":
          replacement = Services.appinfo.appBuildID;
          break;
        case "BUILD_TARGET":
          replacement = Services.appinfo.OS + "_" + this.ABI;
          break;
        case "OS_VERSION":
          replacement = this.OSVersion;
          break;
        case "LOCALE":
          replacement = locale;
          break;
        case "CHANNEL":
          replacement = this.UpdateChannel;
          break;
        case "PLATFORM_VERSION":
          replacement = Services.appinfo.platformVersion;
          break;
        case "SYSTEM_CAPABILITIES":
          replacement = getSystemCapabilities();
          break;
        case "DISTRIBUTION":
          replacement = getDistributionPrefValue(PREF_APP_DISTRIBUTION);
          break;
        case "DISTRIBUTION_VERSION":
          replacement = getDistributionPrefValue(PREF_APP_DISTRIBUTION_VERSION);
          break;
      }
      return encodeURIComponent(replacement);
    });
  },

  /**
   * Gets the locale from the default.locale file for replacing %LOCALE% in the
   * update url. The default.locale file can be located in the application
   * directory or the GRE directory with preference given to it being located in
   * the application directory.
   */
  async getLocale() {
    if (this._locale !== undefined) {
      return this._locale;
    }

    for (let res of ["app", "gre"]) {
      const url = `resource://${res}/${FILE_DEFAULT_LOCALE}`;
      let data;
      try {
        data = await fetch(url);
      } catch (e) {
        continue;
      }
      const locale = await data.text();
      if (locale) {
        return (this._locale = locale.trim());
      }
    }
    console.error(
      FILE_DEFAULT_LOCALE,
      " file doesn't exist in either the application or GRE directories"
    );

    return (this._locale = null);
  },
};

/* Get the distribution pref values, from defaults only */
function getDistributionPrefValue(aPrefName) {
  let value = Services.prefs
    .getDefaultBranch(null)
    .getCharPref(aPrefName, "default");
  if (!value) {
    value = "default";
  }
  return value;
}

function getSystemCapabilities() {
  return "ISET:" + lazy.gInstructionSet + ",MEM:" + getMemoryMB();
}

/**
 * Gets the RAM size in megabytes. This will round the value because sysinfo
 * doesn't always provide RAM in multiples of 1024.
 */
function getMemoryMB() {
  let memoryMB = "unknown";
  try {
    memoryMB = Services.sysinfo.getProperty("memsize");
    if (memoryMB) {
      memoryMB = Math.round(memoryMB / 1024 / 1024);
    }
  } catch (e) {
    console.error("Error getting system info memsize property. Exception: ", e);
  }
  return memoryMB;
}

/**
 * Gets the supported CPU instruction set.
 */
ChromeUtils.defineLazyGetter(lazy, "gInstructionSet", function aus_gIS() {
  const CPU_EXTENSIONS = [
    "hasSSE4_2",
    "hasSSE4_1",
    "hasSSE4A",
    "hasSSSE3",
    "hasSSE3",
    "hasSSE2",
    "hasSSE",
    "hasMMX",
    "hasNEON",
    "hasARMv7",
    "hasARMv6",
  ];
  for (let ext of CPU_EXTENSIONS) {
    if (Services.sysinfo.getProperty(ext)) {
      return ext.substring(3);
    }
  }

  return "unknown";
});

/* Windows only getter that returns the processor architecture. */
ChromeUtils.defineLazyGetter(lazy, "gWinCPUArch", function aus_gWinCPUArch() {
  // Get processor architecture
  let arch = "unknown";

  const WORD = lazy.ctypes.uint16_t;
  const DWORD = lazy.ctypes.uint32_t;

  // This structure is described at:
  // http://msdn.microsoft.com/en-us/library/ms724958%28v=vs.85%29.aspx
  const SYSTEM_INFO = new lazy.ctypes.StructType("SYSTEM_INFO", [
    { wProcessorArchitecture: WORD },
    { wReserved: WORD },
    { dwPageSize: DWORD },
    { lpMinimumApplicationAddress: lazy.ctypes.voidptr_t },
    { lpMaximumApplicationAddress: lazy.ctypes.voidptr_t },
    { dwActiveProcessorMask: DWORD.ptr },
    { dwNumberOfProcessors: DWORD },
    { dwProcessorType: DWORD },
    { dwAllocationGranularity: DWORD },
    { wProcessorLevel: WORD },
    { wProcessorRevision: WORD },
  ]);

  let kernel32 = false;
  try {
    kernel32 = lazy.ctypes.open("Kernel32");
  } catch (e) {
    console.error("Unable to open kernel32! Exception: ", e);
  }

  if (kernel32) {
    try {
      let GetNativeSystemInfo = kernel32.declare(
        "GetNativeSystemInfo",
        lazy.ctypes.winapi_abi,
        lazy.ctypes.void_t,
        SYSTEM_INFO.ptr
      );
      let winSystemInfo = SYSTEM_INFO();
      // Default to unknown
      winSystemInfo.wProcessorArchitecture = 0xffff;

      GetNativeSystemInfo(winSystemInfo.address());
      switch (winSystemInfo.wProcessorArchitecture) {
        case 12:
          arch = "aarch64";
          break;
        case 9:
          arch = "x64";
          break;
        case 6:
          arch = "IA64";
          break;
        case 0:
          arch = "x86";
          break;
      }
    } catch (e) {
      console.error("Error getting processor architecture. Exception: ", e);
    } finally {
      kernel32.close();
    }
  }

  return arch;
});

ChromeUtils.defineLazyGetter(UpdateUtils, "ABI", function () {
  let abi = null;
  try {
    abi = Services.appinfo.XPCOMABI;
  } catch (e) {
    console.error("XPCOM ABI unknown");
  }

  if (AppConstants.platform == "win") {
    // Windows build should report the CPU architecture that it's running on.
    abi += "-" + lazy.gWinCPUArch;
  }

  if (AppConstants.ASAN) {
    // Allow ASan builds to receive their own updates
    abi += "-asan";
  }

  return abi;
});

ChromeUtils.defineLazyGetter(UpdateUtils, "OSVersion", function () {
  let osVersion;
  try {
    osVersion =
      Services.sysinfo.getProperty("name") +
      " " +
      Services.sysinfo.getProperty("version");
  } catch (e) {
    console.error("OS Version unknown.");
  }

  if (osVersion) {
    if (AppConstants.platform == "win") {
      const CURRENT_VERSION_PATH =
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

      // 'CurrentBuild' is a REG_SZ for some reason, so coerce it to a number
      // first.
      let build = Number(
        lazy.WindowsRegistry.readRegKey(
          Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
          CURRENT_VERSION_PATH,
          "CurrentBuild",
          Ci.nsIWindowsRegKey.WOW64_64
        )
      );
      if (Number.isInteger(build)) {
        osVersion += `.0.0.${build}`;
      } else {
        osVersion += ".unknown";
      }

      // The value of UBR is already a REG_DWORD, so don't coerce it.
      let ubr = lazy.WindowsRegistry.readRegKey(
        Ci.nsIWindowsRegKey.ROOT_KEY_LOCAL_MACHINE,
        CURRENT_VERSION_PATH,
        "UBR",
        Ci.nsIWindowsRegKey.WOW64_64
      );
      if (Number.isInteger(ubr)) {
        osVersion += `.${ubr}`;
      } else {
        osVersion += ".unknown";
      }

      // Add processor architecture
      osVersion += " (" + lazy.gWinCPUArch + ")";
    }

    try {
      osVersion +=
        " (" + Services.sysinfo.getProperty("secondaryLibrary") + ")";
    } catch (e) {
      // Not all platforms have a secondary widget library, so an error is nothing to worry about.
    }
    osVersion = encodeURIComponent(osVersion);
  }
  return osVersion;
});

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Checks a signed M6E build: Ion JIT in the parent and a content process, the
// process types and the content sandbox. Run with
//   ./mach python tools/m6e/chrome_eval.py --binary <M6E.app>/Contents/MacOS/m6e --script tools/m6e/package/check-signed.js [--headless]
// The GPU process only starts without --headless. Every entry of `checks`
// must be true (hasGpu only without --headless). inIon() is true only while
// the calling code runs in Ion, so a build that interprets slowly cannot pass.
// WASM is sum() from: int sum(int n) { int s = 0; for (int i = 0; i < n; i++) s += (i ^ (i >> 3)) * 3; return s; }
const WASM = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 6, 1, 96, 1, 127, 1, 127, 3, 2, 1, 0, 5, 3, 1, 0, 1, 7, 16, 2, 6, 109, 101, 109, 111, 114, 121, 2, 0, 3, 115, 117, 109, 0, 0, 10, 178, 1, 1, 175, 1, 1, 4, 127, 2, 64, 32, 0, 65, 1, 78, 13, 0, 65, 0, 15, 11, 32, 0, 65, 3, 113, 33, 1, 65, 0, 33, 2, 65, 0, 33, 3, 2, 64, 2, 64, 32, 0, 65, 4, 73, 13, 0, 32, 0, 65, 252, 255, 255, 255, 7, 113, 33, 4, 65, 0, 33, 2, 65, 0, 33, 3, 3, 64, 32, 2, 65, 3, 118, 34, 0, 32, 2, 65, 3, 106, 115, 65, 3, 108, 32, 0, 32, 2, 65, 2, 106, 115, 65, 3, 108, 32, 0, 32, 2, 65, 1, 106, 115, 65, 3, 108, 32, 0, 32, 2, 115, 65, 3, 108, 32, 3, 106, 106, 106, 106, 33, 3, 32, 2, 65, 4, 106, 34, 2, 32, 4, 71, 13, 0, 11, 32, 1, 69, 13, 1, 11, 3, 64, 32, 2, 65, 3, 118, 32, 2, 115, 65, 3, 108, 32, 3, 106, 33, 3, 32, 2, 65, 1, 106, 33, 2, 32, 1, 65, 127, 106, 34, 1, 13, 0, 11, 11, 32, 3, 11, 0, 24, 4, 110, 97, 109, 101, 0, 9, 8, 115, 117, 109, 46, 119, 97, 115, 109, 1, 6, 1, 0, 3, 115, 117, 109, 0, 47, 9, 112, 114, 111, 100, 117, 99, 101, 114, 115, 1, 12, 112, 114, 111, 99, 101, 115, 115, 101, 100, 45, 98, 121, 1, 14, 72, 111, 109, 101, 98, 114, 101, 119, 32, 99, 108, 97, 110, 103, 6, 50, 51, 46, 49, 46, 52, 0, 148, 1, 15, 116, 97, 114, 103, 101, 116, 95, 102, 101, 97, 116, 117, 114, 101, 115, 8, 43, 11, 98, 117, 108, 107, 45, 109, 101, 109, 111, 114, 121, 43, 15, 98, 117, 108, 107, 45, 109, 101, 109, 111, 114, 121, 45, 111, 112, 116, 43, 22, 99, 97, 108, 108, 45, 105, 110, 100, 105, 114, 101, 99, 116, 45, 111, 118, 101, 114, 108, 111, 110, 103, 43, 10, 109, 117, 108, 116, 105, 118, 97, 108, 117, 101, 43, 15, 109, 117, 116, 97, 98, 108, 101, 45, 103, 108, 111, 98, 97, 108, 115, 43, 19, 110, 111, 110, 116, 114, 97, 112, 112, 105, 110, 103, 45, 102, 112, 116, 111, 105, 110, 116, 43, 15, 114, 101, 102, 101, 114, 101, 110, 99, 101, 45, 116, 121, 112, 101, 115, 43, 8, 115, 105, 103, 110, 45, 101, 120, 116]);
const out = {};

const testing = Cu.getJSTestingFunctions();

function hot(n) {
  let s = 0;
  let ionAt = -1;
  for (let i = 0; i < n; i++) {
    s = (s + ((i ^ (i >>> 3)) * 3)) | 0;
    if (ionAt < 0 && (i & 1023) === 0 && testing.inIon() === true) {
      ionAt = i;
    }
  }
  return { s, ionAt };
}

const t0 = performance.now();
const parentLoop = hot(5e7);
out.parent = {
  pid: Services.appinfo.processID,
  hotLoopMs: Math.round(performance.now() - t0),
  hotLoopResult: parentLoop.s,
  ionAt: parentLoop.ionAt,
};

const sandboxSettings = Cc["@mozilla.org/sandbox/sandbox-settings;1"].getService(Ci.mozISandboxSettings);
out.sandbox = {
  contentLevelPref: Services.prefs.getIntPref("security.sandbox.content.level"),
  effectiveContentLevel: sandboxSettings.effectiveContentSandboxLevel,
  gpuLevelPref: Services.prefs.getIntPref("security.sandbox.gpu.level"),
};

const home = Services.dirsvc.get("Home", Ci.nsIFile).path;
let target = null;
for (const c of await IOUtils.getChildren(home)) {
  if ((await IOUtils.stat(c)).type == "regular" && !c.endsWith(".CFUserTextEncoding")) {
    target = c;
    break;
  }
}
out.parentReads = {
  homeFile: target,
  homeFileBytes: target ? (await IOUtils.read(target, { maxBytes: 16 })).length : null,
  etcHostsBytes: (await IOUtils.read("/etc/hosts", { maxBytes: 16 })).length,
};

const win = Services.wm.getMostRecentWindow("navigator:browser");
const tab = win.gBrowser.addTab("about:blank", {
  triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
});
win.gBrowser.selectedTab = tab;
await sleep(1500);
out.contentRemoteType = tab.linkedBrowser.remoteType;

const frame = `
(function () {
  const out = { pid: Services.appinfo.processID, processType: Services.appinfo.processType };
  const testing = Cu.getJSTestingFunctions();
  const hot = n => {
    let s = 0;
    let ionAt = -1;
    for (let i = 0; i < n; i++) {
      s = (s + ((i ^ (i >>> 3)) * 3)) | 0;
      if (ionAt < 0 && (i & 1023) === 0 && testing.inIon() === true) {
        ionAt = i;
      }
    }
    return ionAt;
  };
  out.frameIonAt = hot(5e7);
  const w = Cu.waiveXrays(content);
  Cu.exportFunction(testing.inIon, w, { defineAs: "__m6eInIon" });
  let t = Date.now();
  out.pageHotLoopResult = w.eval("(function(n){let s=0;let ionAt=-1;for(let i=0;i<n;i++){s=(s+((i^(i>>>3))*3))|0;if(ionAt<0&&(i&1023)===0&&__m6eInIon()===true)ionAt=i;}__m6eIonAt=ionAt;return s;})(5e7)");
  out.pageHotLoopMs = Date.now() - t;
  out.pageIonAt = w.__m6eIonAt;
  try {
    t = Date.now();
    w.__bytes = Cu.cloneInto(new Uint8Array(${JSON.stringify(Array.from(WASM))}), content);
    out.pageWasmResult = w.eval("new WebAssembly.Instance(new WebAssembly.Module(__bytes)).exports.sum(2e8)");
    out.pageWasmMs = Date.now() - t;
  } catch (e) {
    out.pageWasmError = String(e);
  }
  const file = path => {
    const f = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    f.initWithPath(path);
    return f;
  };
  const probe = (label, fn) => {
    try {
      out[label] = fn();
    } catch (e) {
      out[label] = "denied: " + (e.name || e);
    }
  };
  probe("readHomeFile", () => {
    const s = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(Ci.nsIFileInputStream);
    s.init(file(${JSON.stringify(target)}), 0x01, 0, 0);
    s.close();
    return "readable";
  });
  probe("readEtcHosts", () => {
    const s = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(Ci.nsIFileInputStream);
    s.init(file("/etc/hosts"), 0x01, 0, 0);
    s.close();
    return "readable";
  });
  probe("writeTmp", () => {
    const f = file("/tmp/m6e-sandbox-probe");
    const s = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(Ci.nsIFileOutputStream);
    s.init(f, 0x02 | 0x08 | 0x20, 0o600, 0);
    s.close();
    f.remove(false);
    return "written";
  });
  probe("spawnProcess", () => {
    const p = Cc["@mozilla.org/process/util;1"].createInstance(Ci.nsIProcess);
    p.init(file("/bin/echo"));
    p.run(true, ["x"], 1);
    return "spawned";
  });
  try {
    for (const contentType of ['video/mp4; codecs="avc1.64001F"', 'video/webm; codecs="vp09.00.10.08"']) {
      content.navigator.mediaCapabilities.decodingInfo({
        type: "file",
        video: { contentType, width: 1280, height: 720, bitrate: 2e6, framerate: 30 },
      });
    }
  } catch (e) {
    out.mediaError = String(e);
  }
  sendAsyncMessage("m6e-sign", out);
})();
`;

out.content = await new Promise((resolve, reject) => {
  const timer = setTimeout(() => reject(new Error("frame script timeout")), 90000);
  tab.linkedBrowser.messageManager.addMessageListener(
    "m6e-sign",
    m => {
      clearTimeout(timer);
      resolve(m.data);
    },
    { once: true }
  );
  tab.linkedBrowser.messageManager.loadFrameScript("data:," + encodeURIComponent(frame), false);
});

await sleep(3000);
const procs = await ChromeUtils.requestProcInfo();
out.processTypes = [...new Set(procs.children.map(c => c.type))].sort();
out.processCount = procs.children.length;
win.gBrowser.removeTab(tab);

const c = out.content;
out.checks = {
  jitParent: out.parent.ionAt >= 0,
  jitContentFrame: c.frameIonAt >= 0,
  jitContentPage: c.pageIonAt >= 0,
  wasmContent: c.pageWasmResult === 2121555200 && c.pageWasmMs < 5000,
  contentIsWebProcess: c.processType === 2 && out.contentRemoteType === "web",
  contentSandboxLevel3: out.sandbox.effectiveContentLevel >= 3,
  contentCannotReadHome: String(c.readHomeFile).startsWith("denied") && out.parentReads.homeFileBytes > 0,
  contentCannotReadEtc: String(c.readEtcHosts).startsWith("denied") && out.parentReads.etcHostsBytes > 0,
  contentCannotWrite: String(c.writeTmp).startsWith("denied"),
  contentCannotSpawn: String(c.spawnProcess).startsWith("denied"),
  hasSocket: out.processTypes.includes("socket"),
  hasRdd: out.processTypes.includes("rdd"),
  hasGpu: out.processTypes.includes("gpu"),
};
return out;
